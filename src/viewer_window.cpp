// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewer_window.h"

#ifdef FLICK_ENABLE_TEST_HARNESS
#include "viewer_window_test_control.h"
#endif

#include "image_information.h"
#include "image_loading.h"
#include "platform_services.h"
#include "settings_editor.h"
#include "viewing_surface.h"

#include "browsing_sequence.h"
#include <QAccessible>
#include <QAccessibleWidget>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QColor>
#include <QColorSpace>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QCursor>
#include <QDialog>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGraphicsOpacityEffect>
#ifdef FLICK_ENABLE_TEST_HARNESS
#include <QHash>
#endif
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSettings>
#include <QStackedLayout>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QTransform>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidget>
#include <QWindow>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>

namespace {
constexpr int KeyboardPanStep = 40;
constexpr qint64 LargeImageAllocationLimit = 1024LL * 1024 * 1024;

enum class AnimationPlayback
{
    Playing,
    Paused,
    Finished
};

enum class ZoomPolicy
{
    Auto,
    Fit,
    Fixed
};

using ImageLoading::LoadedImage;
using Settings::WheelAction;

QAccessibleInterface *flickAccessibleInterface(const QString &, QObject *object)
{
    auto *widget = qobject_cast<QWidget *>(object);
    if (widget && widget->objectName() == QStringLiteral("imageLabel")) {
        return new QAccessibleWidget(widget, QAccessible::Graphic);
    }
    return nullptr;
}

class DisplayedImage final : public QLabel
{
  public:
    void showImage(const QImage &image, const QSize &displayedSize, const QSize &viewportSize)
    {
        image_ = image;
        displayedSize_ = displayedSize;
        setFixedSize(displayedSize + viewportSize);
        update();
    }

    void clearImage()
    {
        image_ = {};
        displayedSize_ = {};
        setFixedSize(QSize());
        update();
    }

  protected:
    void paintEvent(QPaintEvent *event) override
    {
        if (image_.isNull()) {
            return;
        }
        QPainter painter(this);
        painter.setClipRect(event->rect());
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const QRect target((width() - displayedSize_.width()) / 2,
                           (height() - displayedSize_.height()) / 2, displayedSize_.width(),
                           displayedSize_.height());
        const QRect visibleTarget = target.intersected(event->rect());
        if (visibleTarget.isEmpty()) {
            return;
        }
        const double sourceScaleX = double(image_.width()) / target.width();
        const double sourceScaleY = double(image_.height()) / target.height();
        const QRectF source((visibleTarget.left() - target.left()) * sourceScaleX,
                            (visibleTarget.top() - target.top()) * sourceScaleY,
                            visibleTarget.width() * sourceScaleX,
                            visibleTarget.height() * sourceScaleY);
        painter.drawImage(QRectF(visibleTarget), image_, source);
    }

  private:
    QImage image_;
    QSize displayedSize_;
};

class ViewerWindowImplementation final : public QWidget
{
  public:
    ViewerWindowImplementation(const QString &imagePath, std::unique_ptr<PlatformServices> platformServices,
                               ViewerWindowConfiguration configuration)
        : configuration_(std::move(configuration)), platformServices_(std::move(platformServices)), imageLoader_(this)
    {
        setWindowTitle(QStringLiteral("Flick"));
        setObjectName(QStringLiteral("viewingSurfaceFocus"));
        setMinimumSize(480, 320);
        setAcceptDrops(true);

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);

        imageLabel_ = new DisplayedImage;
        imageLabel_->setObjectName(QStringLiteral("imageLabel"));
        imageLabel_->setAlignment(Qt::AlignCenter);
        imageLabel_->setAccessibleName(tr("Viewing surface"));
        imageLabel_->setAccessibleDescription(
            tr("Displays the current image; use the application actions to "
               "navigate and zoom."));
        imageLabel_->setMouseTracking(true);
        imageLabel_->installEventFilter(this);

        viewport_ = new QScrollArea;
        viewport_->setAlignment(Qt::AlignCenter);
        viewport_->setBackgroundRole(QPalette::Dark);
        viewport_->setFocusPolicy(Qt::NoFocus);
        viewport_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        viewport_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        viewport_->setWidget(imageLabel_);
        viewport_->viewport()->installEventFilter(this);
        viewport_->viewport()->setMouseTracking(true);
        viewport_->setContextMenuPolicy(Qt::CustomContextMenu);
        ViewingSurface::Configuration surfaceConfiguration;
#ifdef FLICK_ENABLE_TEST_HARNESS
        surfaceConfiguration.loadingIndicatorDelayMilliseconds =
            configuration_.loadingIndicatorDelayMilliseconds;
        surfaceConfiguration.reducedMotion = configuration_.reducedMotion;
#endif
        surface_ = new ViewingSurface(
            viewport_,
            {.chooseFile = [this] { openFromFilePicker(); },
             .retry = [this] { retryCurrentImage(); },
             .approveLargeImage = [this] { approveLargeImage(); },
             .rejectLargeImage = [this] { rejectLargeImage(); },
             .currentImageIsLoading =
                 [this] { return imageLoader_.isLoading(browsingSequence_.selectedPath()); },
             .hasCurrentImage = [this] { return browsingSequence_.selectedIndex() >= 0; },
             .statusContext = [this] {
                 return ViewingSurface::StatusContext{
                     QFileInfo(browsingSequence_.selectedPath()).fileName(),
                     browsingSequence_.selectedIndex() + 1,
                     static_cast<int>(browsingSequence_.paths().size()), qRound(zoom_ * 100)};
             },
             .isFullscreen = [this] { return isFullScreen(); },
             .hidePointer = [this] { viewport_->viewport()->setCursor(Qt::BlankCursor); }},
            this, surfaceConfiguration);
        surface_->installEventFilter(this);
        layout->addWidget(surface_);
        auto *wheelActionGroup = new QActionGroup(this);
        wheelActionGroup->setExclusive(true);
        auto *navigateWithWheel = new QAction(tr("Wheel navigates images"), wheelActionGroup);
        navigateWithWheel->setObjectName(QStringLiteral("wheelNavigateAction"));
        navigateWithWheel->setCheckable(true);
        auto *zoomWithWheel = new QAction(tr("Wheel zooms image"), wheelActionGroup);
        zoomWithWheel->setObjectName(QStringLiteral("wheelZoomAction"));
        zoomWithWheel->setCheckable(true);
        viewport_->addAction(navigateWithWheel);
        viewport_->addAction(zoomWithWheel);
        navigateWithWheel->setChecked(wheelAction_ == WheelAction::Navigate);
        zoomWithWheel->setChecked(wheelAction_ == WheelAction::Zoom);
        QObject::connect(navigateWithWheel, &QAction::triggered, this,
                         [this] { setWheelAction(WheelAction::Navigate); });
        QObject::connect(zoomWithWheel, &QAction::triggered, this,
                         [this] { setWheelAction(WheelAction::Zoom); });
        addViewerActions();
        addImageActions();

        QTimer::singleShot(0, this, [this] {
            if (windowHandle()) {
                QObject::connect(windowHandle(), &QWindow::screenChanged, this,
                                 [this] { displayConfigurationChanged(); });
            }
            displayConfigurationChanged();
        });
        setFocusPolicy(Qt::StrongFocus);

        animationTimer_ = new QTimer(this);
        animationTimer_->setSingleShot(true);
        QObject::connect(animationTimer_, &QTimer::timeout, this, [this] { advanceAnimation(); });
        directoryWatcher_ = new QFileSystemWatcher(this);
        QObject::connect(directoryWatcher_, &QFileSystemWatcher::directoryChanged, this,
                         [this] { refreshDirectorySequence(); });

        settingsEditor_ = std::make_unique<Settings::Editor>(*this);
        informationDialog_ = std::make_unique<ImageInformation::Dialog>(
            *this, [this] { restoreViewingFocus(); });
        loadSettings();

        imageLoader_.setOutcomeHandler([this](ImageLoading::DecodeOutcome outcome) {
            if (const auto *confirmation =
                    std::get_if<ImageLoading::ConfirmationRequired>(&outcome)) {
                showLargeImageWarning(*confirmation);
                return;
            }
            if (const auto *loaded = std::get_if<LoadedImage>(&outcome)) {
                present(loaded->path, *loaded);
                prefetchNeighbors();
                return;
            }
            showDecodeError(std::get<ImageLoading::DecodeFailure>(outcome));
        });

        showEmptyState();

        auto *settingsAction = new QAction(tr("Settings"), this);
        settingsAction->setObjectName(QStringLiteral("settingsAction"));
        settingsAction->setShortcut(QKeySequence::Preferences);
        settingsAction->setShortcutContext(Qt::WindowShortcut);
        QObject::connect(settingsAction, &QAction::triggered, this, [this] { showSettings(); });
        viewport_->addAction(settingsAction);
        addAction(settingsAction);

        addCommandSurfaces(settingsAction, layout);

        if (!imagePath.isEmpty()) {
            openDirectoryBacked(imagePath);
        }
    }

    bool isLoading() const
    {
        return imageLoader_.isLoading(browsingSequence_.selectedPath());
    }

    void displayConfigurationChanged()
    {
        refreshDisplayColorSpace();
    }

    void persistWindowGeometry()
    {
        const QByteArray geometry = isFullScreen() && !windowedGeometry_.isEmpty()
                                        ? windowedGeometry_
                                        : saveGeometry();
        Settings::Editor::persistWindowGeometry(geometry, restoreWindowGeometry_);
    }

    void openExternalFile(const QString &path)
    {
        openDirectoryBacked(path);
        showNormal();
        applicationMenuBar_->show();
        raise();
        activateWindow();
    }

#ifdef FLICK_ENABLE_TEST_HARNESS
    ViewerWindowTestSnapshot testSnapshot(const QString &decodePath) const
    {
        ViewerWindowTestSnapshot snapshot;
        snapshot.performance.loading = isLoading();
        snapshot.performance.cacheBytes = imageLoader_.cacheBytes();
        snapshot.performance.decodesInFlight = imageLoader_.requestsInFlight();
        snapshot.performance.requestedPathDecodeCount =
            decodeCounts_.value(QFileInfo(decodePath).canonicalFilePath());
        snapshot.view.zoom = zoom_;
        snapshot.view.scrollPosition = {viewport_->horizontalScrollBar()->value(),
                                   viewport_->verticalScrollBar()->value()};
        snapshot.view.viewportSize = viewport_->viewport()->size();
        snapshot.view.imageOrigin = imageOrigin();
        snapshot.view.fullScreen = isFullScreen();
        snapshot.view.statusVisible = surface_->statusVisible();
        snapshot.view.pointerHidden = viewport_->viewport()->cursor().shape() == Qt::BlankCursor;
        snapshot.view.statusText = surface_->statusText();
        snapshot.view.windowSize = size();
        snapshot.presentation.informationText = informationDialog_->text();
        snapshot.presentation.informationDialog = informationDialog_->state();
        snapshot.presentation.presentation = surface_->presentationSnapshot();
        snapshot.presentation.pendingLargeImageSize = pendingLargeImageSize_;

        const std::function<TestActionSnapshot(const QAction *)> actionSnapshot =
            [&actionSnapshot](const QAction *action) {
                TestActionSnapshot result{action->text(),
                                          action->shortcut().toString(QKeySequence::NativeText),
                                          action->isEnabled(), action->isSeparator(), {}};
                if (action->menu() != nullptr) {
                    for (const QAction *child : action->menu()->actions()) {
                        result.children.append(actionSnapshot(child));
                    }
                }
                return result;
            };
        for (const QAction *action : viewport_->actions()) {
            if (action->objectName().startsWith(QStringLiteral("image"))) {
                snapshot.commands.imageActions.append(actionSnapshot(action));
            }
            if (action->objectName().startsWith(QStringLiteral("viewer")) ||
                action->objectName() == QStringLiteral("settingsAction")) {
                snapshot.accessibility.accessibleActions.append(actionSnapshot(action));
            }
        }
        for (const QAction *action : contextMenu_->actions()) {
            snapshot.commands.contextMenu.append(actionSnapshot(action));
        }
        for (const QAction *action : applicationMenuBar_->actions()) {
            snapshot.commands.applicationMenu.append(actionSnapshot(action));
        }
        snapshot.commands.contextMenuGeometry = contextMenu_->frameGeometry();
        QScreen *screen = contextMenu_->screen();
        snapshot.commands.contextMenuScreenGeometry =
            screen != nullptr ? screen->availableGeometry() : QRect{};
        snapshot.commands.applicationMenuVisible = applicationMenuBar_->isVisible();

        QAction *quitAction = commandAction("applicationQuitAction");
        bool fileMenuContainsAction = false;
        for (const QAction *menuAction : applicationMenuBar_->actions()) {
            if (menuAction->text() == tr("File") && menuAction->menu() != nullptr) {
                fileMenuContainsAction = menuAction->menu()->actions().contains(quitAction);
                break;
            }
        }
        snapshot.commands.quitActionShared =
            contextMenu_->actions().contains(quitAction) && fileMenuContainsAction;
        snapshot.commands.quitActionHasStandardRole = quitAction->menuRole() == QAction::QuitRole;
        snapshot.commands.quitActionHasStandardShortcut = quitAction->shortcut() == QKeySequence::Quit;
        if (QApplication::activePopupWidget() != nullptr) {
            snapshot.accessibility.focusOwner = TestFocusOwner::Menu;
        } else if (QApplication::activeModalWidget() != nullptr ||
                   qobject_cast<QDialog *>(QApplication::activeWindow()) != nullptr) {
            snapshot.accessibility.focusOwner = TestFocusOwner::Dialog;
        } else {
            QWidget *focused = QApplication::focusWidget();
            snapshot.accessibility.focusOwner =
                (focused == this || (focused != nullptr && isAncestorOf(focused)))
                    ? TestFocusOwner::ViewingSurface
                    : TestFocusOwner::Other;
        }
        const QAccessibleInterface *interface = QAccessible::queryAccessibleInterface(imageLabel_);
        snapshot.accessibility.accessibleImageName = imageLabel_->accessibleName();
        snapshot.accessibility.accessibleImageHasGraphicRole =
            interface != nullptr && interface->role() == QAccessible::Graphic;
        snapshot.accessibility.accessibleImageDescription = imageLabel_->accessibleDescription();
        snapshot.settings.settings = currentSettings();
        snapshot.settings.settingsDialog = settingsEditor_->testSnapshot();
        return snapshot;
    }

    void applyAcceptedSettingsForTest(const Settings::Values &values)
    {
        applySettings(values);
    }

    void setSettingsDialogValuesForTest(const Settings::Values &values)
    {
        settingsEditor_->setTestValues(values);
    }

    void performTestOperation(const ViewerWindowTestOperation operation)
    {
        switch (operation) {
        case ViewerWindowTestOperation::OpenSettings: commandAction("settingsAction")->trigger(); break;
        case ViewerWindowTestOperation::QuitApplication: commandAction("applicationQuitAction")->trigger(); break;
        case ViewerWindowTestOperation::SelectWheelZoom: commandAction("wheelZoomAction")->trigger(); break;
        case ViewerWindowTestOperation::FocusViewingSurface: activateWindow(); setFocus(Qt::OtherFocusReason); break;
        case ViewerWindowTestOperation::PersistWindowGeometry: persistWindowGeometry(); break;
        case ViewerWindowTestOperation::ResetSettings: settingsEditor_->resetForTest(); break;
        case ViewerWindowTestOperation::ApplySettingsDialog: settingsEditor_->finishForTest(true); break;
        case ViewerWindowTestOperation::CancelSettingsDialog: settingsEditor_->finishForTest(false); break;
        case ViewerWindowTestOperation::DisplayConfigurationChanged: displayConfigurationChanged(); break;
        case ViewerWindowTestOperation::FailExternalActions: failExternalActionsForTest_ = true; break;
        }
    }

    void activatePresentationActionForTest(const ViewingSurface::ActionRole role)
    {
        surface_->activateActionForTest(role);
    }

    void focusPresentationActionForTest(const ViewingSurface::ActionRole role)
    {
        surface_->focusActionForTest(role);
    }

#endif

  protected:
    void closeEvent(QCloseEvent *event) override
    {
        persistWindowGeometry();
        if (isFullScreen()) {
            showNormal();
            applicationMenuBar_->show();
        }
        QWidget::closeEvent(event);
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == viewport_->viewport() && event->type() == QEvent::Resize) {
            scheduleViewportRefit();
        }
        if ((watched == viewport_->viewport() || watched == imageLabel_) &&
            event->type() == QEvent::MouseMove) {
            showStatus(true);
        }
        if (event->type() == QEvent::ContextMenu) {
            surface_->markBrowsingTeachingComplete();
            const auto *contextEvent = static_cast<QContextMenuEvent *>(event);
            contextMenu_->popup(contextEvent->globalPos());
            return true;
        }
        if ((watched == viewport_->viewport() || watched == imageLabel_) &&
            event->type() == QEvent::MouseButtonDblClick) {
            toggleFullscreen();
            return true;
        }
        if (watched == viewport_->viewport() && event->type() == QEvent::Wheel) {
            const auto *wheel = static_cast<QWheelEvent *>(event);
            const int wheelDelta = wheel->angleDelta().y() != 0 ? wheel->angleDelta().y()
                                                                : wheel->pixelDelta().y() * 8;
            const bool alternate = wheel->modifiers().testFlag(Qt::ControlModifier);
            const bool zoom = (wheelAction_ == WheelAction::Zoom) != alternate;
            if (zoom) {
                zoomAt(wheel->position(), wheelDelta);
            } else if (wheelDelta != 0) {
                navigate(wheelDelta > 0 ? -1 : 1);
            }
            return true;
        }
        if (watched == viewport_->viewport() && event->type() == QEvent::MouseButtonPress) {
            const auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton) {
                dragging_ = true;
                lastDragPosition_ = mouse->position();
                viewport_->viewport()->grabMouse();
                return true;
            }
        }
        if (watched == viewport_->viewport() && event->type() == QEvent::MouseMove && dragging_) {
            const auto *mouse = static_cast<QMouseEvent *>(event);
            if (!mouse->buttons().testFlag(Qt::LeftButton)) {
                dragging_ = false;
                viewport_->viewport()->releaseMouse();
                return true;
            }
            const QPointF movement = mouse->position() - lastDragPosition_;
            panBy(-qRound(movement.x()), -qRound(movement.y()));
            lastDragPosition_ = mouse->position();
            return true;
        }
        if (watched == viewport_->viewport() && event->type() == QEvent::MouseButtonRelease) {
            const auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton && dragging_) {
                dragging_ = false;
                viewport_->viewport()->releaseMouse();
                return true;
            }
        }
        return QWidget::eventFilter(watched, event);
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_F11) {
            toggleFullscreen();
            return;
        }
        if (event->key() == Qt::Key_F5) {
            retryCurrentImage();
            return;
        }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            if (auto *focusedAction =
                    qobject_cast<QAbstractButton *>(QApplication::focusWidget())) {
                focusedAction->click();
                return;
            }
            if (surface_->activatePrimaryAction()) {
                return;
            }
        }
        if (event->key() == Qt::Key_Escape &&
            surface_->state() == ViewingSurface::State::LargeImageConfirmation) {
            rejectLargeImage();
            return;
        }
        if (event->key() == Qt::Key_Escape && isFullScreen()) {
            leaveFullscreen();
            return;
        }
        if (event->matches(QKeySequence::Open)) {
            openFromFilePicker();
            return;
        }
        if (event->matches(QKeySequence::ZoomIn)) {
            setFixedZoomCentered(zoom_ * 1.25);
            return;
        }
        if (event->matches(QKeySequence::ZoomOut)) {
            setFixedZoomCentered(zoom_ / 1.25);
            return;
        }
        if (event->key() == Qt::Key_1) {
            setFixedZoomCentered(1.0);
            return;
        }
        if (event->key() == Qt::Key_F) {
            fitToViewport();
            return;
        }
        if (event->key() == Qt::Key_L) {
            rotateView(-1);
            return;
        }
        if (event->key() == Qt::Key_R) {
            rotateView(1);
            return;
        }
        if (event->modifiers().testFlag(Qt::ShiftModifier)) {
            if (event->key() == Qt::Key_Left) {
                panBy(-KeyboardPanStep, 0);
                return;
            }
            if (event->key() == Qt::Key_Right) {
                panBy(KeyboardPanStep, 0);
                return;
            }
            if (event->key() == Qt::Key_Up) {
                panBy(0, -KeyboardPanStep);
                return;
            }
            if (event->key() == Qt::Key_Down) {
                panBy(0, KeyboardPanStep);
                return;
            }
        }
        if (event->key() == Qt::Key_Left) {
            navigate(-1);
            return;
        }
        if (event->key() == Qt::Key_Right) {
            navigate(1);
            return;
        }
        if (event->key() == Qt::Key_Space) {
            toggleAnimation();
            return;
        }
        QWidget::keyPressEvent(event);
    }

    void dragEnterEvent(QDragEnterEvent *event) override
    {
        int supportedCount = 0;
        for (const QUrl &url : event->mimeData()->urls()) {
            if (url.isLocalFile() && BrowsingSequence::supports(url.toLocalFile())) {
                ++supportedCount;
            }
        }
        if (supportedCount == 0) {
            if (event->mimeData()->hasUrls()) {
                event->acceptProposedAction();
            }
            return;
        }
        surface_->showDropTarget(supportedCount);
        event->acceptProposedAction();
    }

    void dragLeaveEvent(QDragLeaveEvent *event) override
    {
        surface_->hideDropTarget();
        event->accept();
    }

    void dropEvent(QDropEvent *event) override
    {
        QStringList paths;
        for (const QUrl &url : event->mimeData()->urls()) {
            if (url.isLocalFile()) {
                paths.append(url.toLocalFile());
            }
        }
        surface_->hideDropTarget();
        openDroppedPaths(paths);
        event->acceptProposedAction();
    }

  private:
    QAction *commandAction(const char *objectName) const
    {
        QAction *action = findChild<QAction *>(QString::fromLatin1(objectName));
        Q_ASSERT(action != nullptr);
        return action;
    }

    void restoreViewingFocus()
    {
        activateWindow();
        setFocus(Qt::OtherFocusReason);
    }

    void addCommandSurfaces(QAction *settingsAction, QVBoxLayout *windowLayout)
    {
        auto *quitAction = new QAction(tr("Quit"), this);
        quitAction->setObjectName(QStringLiteral("applicationQuitAction"));
        quitAction->setMenuRole(QAction::QuitRole);
        quitAction->setShortcut(QKeySequence::Quit);
        QObject::connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);

        const QList<QAction *> viewCommands{
            commandAction("viewerFitAction"), commandAction("viewerActualSizeAction"),
            commandAction("viewerZoomInAction"), commandAction("viewerZoomOutAction"),
            commandAction("viewerFullscreenAction")};
        const QList<QAction *> imageCommands{
            commandAction("viewerRotateLeftAction"), commandAction("viewerRotateRightAction"),
            commandAction("viewerAnimationAction"), commandAction("imageInformationAction")};
        const QList<QAction *> copyAndRevealCommands{commandAction("imageCopyAction"),
                                                     commandAction("imageCopyPathAction"),
                                                     commandAction("imageRevealAction")};
        contextMenu_ = new QMenu(this);
        contextMenu_->addAction(commandAction("viewerOpenAction"));
        contextMenu_->addSeparator();
        for (QAction *action : viewCommands) {
            contextMenu_->addAction(action);
        }
        contextMenu_->addSeparator();
        for (QAction *action : imageCommands) {
            contextMenu_->addAction(action);
        }
        contextMenu_->addSeparator();
        for (QAction *action : copyAndRevealCommands) {
            contextMenu_->addAction(action);
        }
        contextMenu_->addSeparator();
        contextMenu_->addAction(settingsAction);
        contextMenu_->addSeparator();
        contextMenu_->addAction(quitAction);
        QObject::connect(contextMenu_, &QMenu::aboutToHide, this, [this] {
            QTimer::singleShot(0, this, [this] { restoreViewingFocus(); });
        });

        applicationMenuBar_ = new QMenuBar;
#if defined(Q_OS_MACOS)
        applicationMenuBar_->setNativeMenuBar(true);
#else
        applicationMenuBar_->setNativeMenuBar(false);
#endif
        windowLayout->setMenuBar(applicationMenuBar_);
        QMenu *fileMenu = applicationMenuBar_->addMenu(tr("File"));
        fileMenu->addAction(commandAction("viewerOpenAction"));
        fileMenu->addAction(settingsAction);
        fileMenu->addAction(quitAction);
#if defined(Q_OS_MACOS)
        settingsAction->setMenuRole(QAction::PreferencesRole);
#endif
        QMenu *viewMenu = applicationMenuBar_->addMenu(tr("View"));
        for (QAction *action : viewCommands) {
            viewMenu->addAction(action);
        }
        QMenu *imageMenu = applicationMenuBar_->addMenu(tr("Image"));
        for (QAction *action : imageCommands) {
            imageMenu->addAction(action);
        }
        QMenu *helpMenu = applicationMenuBar_->addMenu(tr("Help"));
        auto *aboutAction = helpMenu->addAction(tr("About Flick"));
        aboutAction->setMenuRole(QAction::AboutRole);
        QObject::connect(aboutAction, &QAction::triggered, this, [this] {
            QMessageBox::about(this, tr("About Flick"),
                               tr("Flick %1\nA color-managed image viewer.")
                                   .arg(QCoreApplication::applicationVersion()));
            setFocus();
        });
    }

    void loadSettings()
    {
        Settings::Values values = Settings::Editor::readAccepted();
#ifdef FLICK_ENABLE_TEST_HARNESS
        if (configuration_.cacheBudgetBytes.has_value()) {
            values.cacheBudgetBytes = *configuration_.cacheBudgetBytes;
        }
#endif
        if (values.cacheBudgetBytes <= 0) {
            values.cacheBudgetBytes = Settings::Editor::defaults().cacheBudgetBytes;
        }
        wheelAction_ = values.wheelAction;
        viewportBackground_ = values.background;
        surface_->setStatusVisible(values.statusVisible);
        imageLoader_.setCacheBudget(values.cacheBudgetBytes);
        restoreWindowGeometry_ = values.restoreWindowGeometry;
        findChild<QAction *>(QStringLiteral("wheelNavigateAction"))
            ->setChecked(wheelAction_ == WheelAction::Navigate);
        findChild<QAction *>(QStringLiteral("wheelZoomAction"))
            ->setChecked(wheelAction_ == WheelAction::Zoom);
        applyViewportBackground();
        if (restoreWindowGeometry_) {
            restoreGeometry(Settings::Editor::readWindowGeometry());
        }
    }

    void applyViewportBackground()
    {
        QPalette palette = viewport_->viewport()->palette();
        palette.setColor(QPalette::Window, viewportBackground_);
        palette.setColor(QPalette::Base, viewportBackground_);
        palette.setColor(QPalette::Dark, viewportBackground_);
        viewport_->viewport()->setAutoFillBackground(true);
        viewport_->viewport()->setPalette(palette);
    }

    void applySettings(const Settings::Values &values)
    {
        previewSettings(values);
        Settings::Editor::persistAccepted(currentSettings());
    }

    Settings::Values currentSettings() const
    {
        return {wheelAction_, viewportBackground_, surface_->statusEnabled(), imageLoader_.cacheBudget(),
                restoreWindowGeometry_};
    }

    void previewSettings(const Settings::Values &values)
    {
        setWheelAction(values.wheelAction, false);
        viewportBackground_ =
            values.background.isValid() ? values.background : Settings::Editor::defaults().background;
        surface_->setStatusVisible(values.statusVisible);
        imageLoader_.setCacheBudget(std::max<qsizetype>(1024 * 1024, values.cacheBudgetBytes));
        restoreWindowGeometry_ = values.restoreWindowGeometry;
        if (values.statusVisible) {
            showStatus(false);
        }
        applyViewportBackground();
        if (auto *navigate = findChild<QAction *>(QStringLiteral("wheelNavigateAction"))) {
            navigate->setChecked(wheelAction_ == WheelAction::Navigate);
        }
        if (auto *zoom = findChild<QAction *>(QStringLiteral("wheelZoomAction"))) {
            zoom->setChecked(wheelAction_ == WheelAction::Zoom);
        }
    }

    void showSettings()
    {
        settingsEditor_->open(currentSettings(), Settings::Editor::defaults(),
                              [this](const Settings::Values &values) { previewSettings(values); });
    }

    void addImageActions()
    {
        const auto addImageAction = [this](const QString &text, const QString &objectName,
                                           const QKeySequence &shortcut, auto operation) {
            QAction *action = addViewportAction(text, objectName, shortcut, std::move(operation));
            action->setEnabled(false);
            imageActions_.append(action);
        };
        addImageAction(tr("Information"), QStringLiteral("imageInformationAction"),
                       QKeySequence(Qt::Key_I), [this] { showInformation(); });
        addImageAction(tr("Copy Image"), QStringLiteral("imageCopyAction"), QKeySequence::Copy,
                       [this] { copyRenderedImage(); });
        addImageAction(tr("Copy Path"), QStringLiteral("imageCopyPathAction"),
                       QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C),
                       [this] { copyCurrentPath(); });
        addImageAction(tr("Show in File Manager"), QStringLiteral("imageRevealAction"),
                       QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R),
                       [this] { revealCurrentFile(); });
    }

    void addViewerActions()
    {
        addViewportAction(tr("Open Image"), QStringLiteral("viewerOpenAction"), QKeySequence::Open,
                          [this] { openFromFilePicker(); });
        addViewportAction(tr("Previous Image"), QStringLiteral("viewerPreviousAction"),
                          QKeySequence(Qt::Key_Left), [this] { navigate(-1); });
        addViewportAction(tr("Next Image"), QStringLiteral("viewerNextAction"),
                          QKeySequence(Qt::Key_Right), [this] { navigate(1); });
        addViewportAction(tr("Pan Left"), QStringLiteral("viewerPanLeftAction"),
                          QKeySequence(Qt::SHIFT | Qt::Key_Left),
                          [this] { panBy(-KeyboardPanStep, 0); });
        addViewportAction(tr("Pan Right"), QStringLiteral("viewerPanRightAction"),
                          QKeySequence(Qt::SHIFT | Qt::Key_Right),
                          [this] { panBy(KeyboardPanStep, 0); });
        addViewportAction(tr("Pan Up"), QStringLiteral("viewerPanUpAction"),
                          QKeySequence(Qt::SHIFT | Qt::Key_Up),
                          [this] { panBy(0, -KeyboardPanStep); });
        addViewportAction(tr("Pan Down"), QStringLiteral("viewerPanDownAction"),
                          QKeySequence(Qt::SHIFT | Qt::Key_Down),
                          [this] { panBy(0, KeyboardPanStep); });
        const auto addImageViewerAction = [this](const QString &text, const QString &objectName,
                                                 const QKeySequence &shortcut, auto operation) {
            QAction *action = addViewportAction(text, objectName, shortcut, std::move(operation));
            action->setEnabled(false);
            imageActions_.append(action);
        };
        addImageViewerAction(tr("Zoom In"), QStringLiteral("viewerZoomInAction"),
                             QKeySequence::ZoomIn,
                             [this] { setFixedZoomCentered(zoom_ * 1.25); });
        addImageViewerAction(tr("Zoom Out"), QStringLiteral("viewerZoomOutAction"),
                             QKeySequence::ZoomOut,
                             [this] { setFixedZoomCentered(zoom_ / 1.25); });
        addImageViewerAction(tr("Actual Size"), QStringLiteral("viewerActualSizeAction"),
                             QKeySequence(Qt::Key_1), [this] { setFixedZoomCentered(1.0); });
        addImageViewerAction(tr("Fit to Window"), QStringLiteral("viewerFitAction"),
                             QKeySequence(Qt::Key_F), [this] { fitToViewport(); });
        addImageViewerAction(tr("Rotate Left"), QStringLiteral("viewerRotateLeftAction"),
                             QKeySequence(Qt::Key_L), [this] { rotateView(-1); });
        addImageViewerAction(tr("Rotate Right"), QStringLiteral("viewerRotateRightAction"),
                             QKeySequence(Qt::Key_R), [this] { rotateView(1); });
        addImageViewerAction(tr("Pause or Resume Animation"),
                             QStringLiteral("viewerAnimationAction"), QKeySequence(Qt::Key_Space),
                             [this] { toggleAnimation(); });
        addViewportAction(tr("Toggle Fullscreen"), QStringLiteral("viewerFullscreenAction"),
                          QKeySequence(Qt::Key_F11), [this] { toggleFullscreen(); });
        addViewportAction(tr("Retry"), QStringLiteral("viewerRetryAction"),
                          QKeySequence(Qt::Key_F5), [this] { retryCurrentImage(); });
    }

    QAction *addViewportAction(const QString &text, const QString &objectName,
                               const QKeySequence &shortcut, std::function<void()> operation)
    {
        auto *action = new QAction(text, this);
        action->setObjectName(objectName);
        action->setShortcut(shortcut);
        action->setShortcutContext(Qt::WindowShortcut);
        QObject::connect(action, &QAction::triggered, this, std::move(operation));
        viewport_->addAction(action);
        return action;
    }

    ImageInformation::Snapshot imageInformationSnapshot() const
    {
        const QString selectedPath = browsingSequence_.selectedPath();
        const QFileInfo file(selectedPath);
        QString format = file.suffix().toUpper();
        if (format == QStringLiteral("JPG")) {
            format = QStringLiteral("JPEG");
        }
        const bool displayed = !image_.isNull() && currentImage_.path == selectedPath;
        const auto availability = displayed
                                      ? ImageInformation::DecodedAvailability::Available
                                  : surface_->state() == ViewingSurface::State::Error
                                      ? ImageInformation::DecodedAvailability::Unavailable
                                      : ImageInformation::DecodedAvailability::Loading;
        ImageInformation::AnimationState animation = ImageInformation::AnimationState::Unavailable;
        if (displayed && currentImage_.frames.size() < 2) {
            animation = ImageInformation::AnimationState::Static;
        } else if (displayed && animationPlayback_ == AnimationPlayback::Paused) {
            animation = ImageInformation::AnimationState::Paused;
        } else if (displayed && animationPlayback_ == AnimationPlayback::Finished) {
            animation = ImageInformation::AnimationState::Finished;
        } else if (displayed) {
            animation = ImageInformation::AnimationState::Playing;
        }
        return {.path = selectedPath.isEmpty() ? QString{} : file.absoluteFilePath(),
                .format = format,
                .fileSize = file.size(),
                .modified = file.lastModified(),
                .decodedAvailability = availability,
                .dimensions = displayed ? image_.size() : QSize{},
                .zoomPercent = qRound(zoom_ * 100),
                .rotationDegrees = rotationQuarterTurns_ * 90,
                .animationState = animation,
                .position = browsingSequence_.selectedIndex() + 1,
                .sequenceSize = static_cast<int>(browsingSequence_.paths().size())};
    }

    void updateInformation()
    {
        informationDialog_->update(imageInformationSnapshot());
    }

    void showInformation()
    {
        if (browsingSequence_.selectedIndex() < 0 || image_.isNull()) {
            return;
        }
        informationDialog_->open(imageInformationSnapshot());
    }

    bool externalActionCanRun(const QString &failureMessage)
    {
#ifdef FLICK_ENABLE_TEST_HARNESS
        if (failExternalActionsForTest_) {
            showFeedback(failureMessage);
            return false;
        }
#else
        Q_UNUSED(failureMessage);
#endif
        return true;
    }

    void copyCurrentPath()
    {
        if (browsingSequence_.selectedIndex() < 0 ||
            !externalActionCanRun(tr("Could not copy the current file path"))) {
            return;
        }
        QClipboard *clipboard = QApplication::clipboard();
        if (clipboard == nullptr) {
            showFeedback(tr("Could not copy the current file path"));
            return;
        }
        const QString path = QFileInfo(currentImage_.path).absoluteFilePath();
        clipboard->setText(path);
        if (clipboard->text() != path) {
            showFeedback(tr("Could not copy the current file path"));
        } else {
            showFeedback(tr("File path copied"));
        }
    }

    void copyRenderedImage()
    {
        if (image_.isNull() || !externalActionCanRun(tr("Could not copy the current image"))) {
            return;
        }
        QClipboard *clipboard = QApplication::clipboard();
        if (clipboard == nullptr) {
            showFeedback(tr("Could not copy the current image"));
            return;
        }
        const QImage content = rotatedImage();
        clipboard->setImage(content);
        showFeedback(tr("Image copied"));
    }

    void revealCurrentFile()
    {
        if (browsingSequence_.selectedIndex() < 0 ||
            !externalActionCanRun(tr("Could not show the current file in the file manager"))) {
            return;
        }
        if (!platformServices_->revealFile(QFileInfo(currentImage_.path).absoluteFilePath())) {
            showFeedback(tr("Could not show the current file in the file manager"));
        }
    }

    void setWheelAction(const WheelAction action, const bool persist = true)
    {
        wheelAction_ = action;
        if (persist) {
            Settings::Editor::persistWheelAction(action);
        }
    }

    void toggleFullscreen()
    {
        if (isFullScreen()) {
            leaveFullscreen();
        } else {
            windowedGeometry_ = saveGeometry();
            applicationMenuBar_->hide();
            showFullScreen();
            surface_->enteredFullscreen();
        }
    }

    void leaveFullscreen()
    {
        showNormal();
        applicationMenuBar_->show();
        viewport_->viewport()->unsetCursor();
        showStatus(false);
    }

    void showStatus(const bool revealPointer)
    {
        if (browsingSequence_.selectedIndex() < 0) {
            return;
        }
        if (revealPointer) {
            viewport_->viewport()->unsetCursor();
        }
        surface_->showStatus();
    }

    void openDirectoryBacked(const QString &path)
    {
        const BrowsingSequence browsingSequence = BrowsingSequence::directoryBacked(path);
        if (browsingSequence.selectedIndex() < 0) {
            showFeedback(tr("Unsupported dropped content"));
            return;
        }
        const QString canonicalPath = browsingSequence.selectedPath();
        const QString directoryPath = QFileInfo(canonicalPath).absolutePath();
        directoryWatcher_->removePaths(directoryWatcher_->directories());
        directoryWatcher_->addPath(directoryPath);
        browsingSequence_ = browsingSequence;
        displaySelectedImage();
    }

    void openExplicitList(const QStringList &paths)
    {
        directoryWatcher_->removePaths(directoryWatcher_->directories());
        const BrowsingSequence browsingSequence = BrowsingSequence::explicitList(paths);
        browsingSequence_ = browsingSequence;
        if (browsingSequence_.paths().isEmpty()) {
            showFeedback(tr("No supported images in drop"));
            return;
        }
        displaySelectedImage();
    }

    void openDroppedPaths(const QStringList &paths)
    {
        if (paths.size() == 1 && BrowsingSequence::supports(paths.first())) {
            openDirectoryBacked(paths.first());
        } else {
            openExplicitList(paths);
        }
    }

    void openFromFilePicker()
    {
        QSettings settings;
        const QString initialDirectory =
            settings.value(QStringLiteral("filePicker/lastDirectory"), QDir::homePath()).toString();
#ifdef FLICK_ENABLE_TEST_HARNESS
        if (configuration_.filePickerSelection.has_value()) {
            const QString testSelection = *configuration_.filePickerSelection;
            if (testSelection.isEmpty()) {
                return;
            }
            QTimer::singleShot(0, this, [testSelection] {
                if (auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
                    dialog->selectFile(testSelection);
                    static_cast<QDialog *>(dialog)->accept();
                }
            });
        }
#endif
        const QString selectedPath =
            QFileDialog::getOpenFileName(this, tr("Open Image"), initialDirectory,
                                         tr("Images (*.jpg *.jpeg *.png *.webp *.gif *.bmp)"));
        restoreViewingFocus();
        if (selectedPath.isEmpty()) {
            return;
        }
        const BrowsingSequence browsingSequence = BrowsingSequence::directoryBacked(selectedPath);
        if (browsingSequence.selectedIndex() < 0) {
            showFeedback(tr("Unsupported image"));
            return;
        }
        pendingFilePickerPath_ = QFileInfo(selectedPath).canonicalFilePath();
        openDirectoryBacked(selectedPath);
    }

    void displaySelectedImage()
    {
        dismissLargeImageWarning();
        rotationQuarterTurns_ = 0;
        const QString currentPath = browsingSequence_.selectedPath();
        imageLoader_.setCurrentPath(currentPath);
        for (QAction *action : imageActions_) {
            action->setEnabled(false);
        }
        currentImage_ = {};
        image_ = {};
        imageLabel_->clearImage();
        surface_->hideStatus();
        beginCurrentImageLoad(currentPath, false);
    }

    void retryCurrentImage()
    {
        const QString currentPath = browsingSequence_.selectedPath();
        if (currentPath.isEmpty()) {
            return;
        }
        dismissLargeImageWarning();
        beginCurrentImageLoad(currentPath, true);
    }

    void approveLargeImage()
    {
        const QString approvedPath = pendingLargeImagePath_;
        dismissLargeImageWarning();
        if (!approvedPath.isEmpty() && browsingSequence_.selectedPath() == approvedPath) {
            beginCurrentImageLoad(approvedPath, true, true);
        }
    }

    void beginCurrentImageLoad(const QString &path, const bool retry,
                               const bool approvedLargeImage = false)
    {
        surface_->beginLoading(QFileInfo(path).fileName());
        updateInformation();
        if (retry) {
            retryDecode(path, approvedLargeImage);
        } else {
            requestDecode(path);
        }
    }

    void rejectLargeImage()
    {
        const bool rejectingCurrent = !pendingLargeImagePath_.isEmpty() &&
                                      browsingSequence_.selectedPath() == pendingLargeImagePath_;
        dismissLargeImageWarning();
        if (rejectingCurrent) {
            const BrowsingSequence::AdjacentPaths adjacent = browsingSequence_.adjacentPaths();
            if (!adjacent.next.isEmpty()) {
                navigate(1);
            } else if (!adjacent.previous.isEmpty()) {
                navigate(-1);
            } else {
                browsingSequence_ = BrowsingSequence::explicitList({});
                imageLoader_.setCurrentPath({});
                showEmptyState();
            }
            showFeedback(tr("Large image skipped"));
        }
    }

    void dismissLargeImageWarning()
    {
        surface_->dismissLargeImageConfirmation();
        pendingLargeImagePath_.clear();
    }

    void present(const QString &path, const LoadedImage &decoded)
    {
        if (decoded.frames.isEmpty() || browsingSequence_.selectedPath() != path) {
            return;
        }
        animationTimer_->stop();
        currentImage_ = decoded;
        currentFrame_ = 0;
        completedLoops_ = 0;
        animationPlayback_ = AnimationPlayback::Playing;
        pausedDelayMilliseconds_ = 0;
        rotationQuarterTurns_ = 0;
        image_ = currentImage_.frames.at(0);
        zoomPolicy_ = ZoomPolicy::Auto;
        surface_->showDisplayed();
        applyInitialZoom();
        showFrame(currentFrame_);
        showStatus(false);
        scheduleCenterView();
        if (currentImage_.frames.size() > 1) {
            animationTimer_->start(std::max(1, currentImage_.frameDelays.at(currentFrame_)));
        }
        if (path == pendingFilePickerPath_) {
            QSettings settings;
            settings.setValue(QStringLiteral("filePicker/lastDirectory"),
                              QFileInfo(path).absolutePath());
            settings.sync();
            pendingFilePickerPath_.clear();
        }
        for (QAction *action : imageActions_) {
            action->setEnabled(true);
        }
        commandAction("viewerAnimationAction")->setEnabled(currentImage_.frames.size() > 1);
        updateInformation();
        if (!informationDialog_->isOpen()) {
            restoreViewingFocus();
        }
        setWindowTitle(tr("Flick — %1").arg(QFileInfo(path).fileName()));
        surface_->currentImageDisplayed();
    }

    void refreshDirectorySequence()
    {
        if (directoryWatcher_->directories().isEmpty()) {
            return;
        }
        const BrowsingSequence::ReconcileOutcome outcome = browsingSequence_.reconcileDirectory();
        if (outcome == BrowsingSequence::ReconcileOutcome::Unchanged) {
            return;
        }
        if (outcome == BrowsingSequence::ReconcileOutcome::SelectionPreserved) {
            showStatus(false);
            updateInformation();
            prefetchNeighbors();
            return;
        }

        if (outcome == BrowsingSequence::ReconcileOutcome::Empty) {
            imageLoader_.setCurrentPath({});
            currentImage_ = {};
            image_ = {};
            animationTimer_->stop();
            setWindowTitle(QStringLiteral("Flick"));
            showEmptyState();
            showFeedback(tr("Current image is no longer available"));
            return;
        }

        surface_->queueFeedback(tr("Current image is no longer available"));
        displaySelectedImage();
    }

    void showFrame(const int index)
    {
        const QImage &source = currentImage_.frames.at(index);
        const QColorSpace target =
            displayColorSpace_.isValid() ? displayColorSpace_ : QColorSpace(QColorSpace::SRgb);
        image_ = source.colorSpace() == target ? source : source.convertedToColorSpace(target);
        image_.setColorSpace({});
        renderImage();
    }

    void refreshDisplayColorSpace()
    {
        QScreen *activeScreen = windowHandle() ? windowHandle()->screen() : screen();
        applyDisplayColorSpace(platformServices_->displayColorSpace(activeScreen));
    }

    void applyDisplayColorSpace(const QColorSpace &exposed)
    {
        const QColorSpace next = exposed.isValid() ? exposed : QColorSpace(QColorSpace::SRgb);
        if (displayColorSpace_ == next) {
            return;
        }
        displayColorSpace_ = next;
        if (!currentImage_.frames.isEmpty()) {
            showFrame(currentFrame_);
        }
    }

    double fitZoom() const
    {
        if (image_.isNull()) {
            return 1.0;
        }
        const QSize available = viewport_->viewport()->size();
        const QSize imageSize = rotatedImageSize();
        return std::min(double(available.width()) / imageSize.width(),
                        double(available.height()) / imageSize.height());
    }

    void applyInitialZoom()
    {
        zoom_ = std::min(1.0, fitZoom());
    }

    void fitToViewport()
    {
        if (!image_.isNull()) {
            zoomPolicy_ = ZoomPolicy::Fit;
            setZoomCentered(fitZoom());
        }
    }

    void setFixedZoomCentered(const double zoom)
    {
        zoomPolicy_ = ZoomPolicy::Fixed;
        setZoomCentered(zoom);
    }

    void scheduleViewportRefit()
    {
        if (viewportRefitPending_) {
            return;
        }
        viewportRefitPending_ = true;
        QTimer::singleShot(0, this, [this] {
            viewportRefitPending_ = false;
            if (image_.isNull()) {
                return;
            }
            if (zoomPolicy_ == ZoomPolicy::Auto) {
                setViewportZoom(std::min(1.0, fitZoom()));
            } else if (zoomPolicy_ == ZoomPolicy::Fit) {
                setViewportZoom(fitZoom());
            } else {
                renderImage();
            }
        });
    }

    void setViewportZoom(const double zoom)
    {
        applyZoom(zoom);
        scheduleCenterView();
    }

    void setZoom(const double zoom)
    {
        if (image_.isNull()) {
            return;
        }
        applyZoom(zoom);
        showStatus(false);
    }

    void applyZoom(const double zoom)
    {
        zoom_ = std::clamp(zoom, 0.01, 64.0);
        renderImage();
        updateInformation();
    }

    void setZoomCentered(const double zoom)
    {
        setZoom(zoom);
        scheduleCenterView();
    }

    void zoomAt(const QPointF &viewportPosition, const int angleDelta)
    {
        if (image_.isNull() || angleDelta == 0) {
            return;
        }
        auto *horizontal = viewport_->horizontalScrollBar();
        auto *vertical = viewport_->verticalScrollBar();
        const QPoint originBefore = imageOrigin();
        const QPointF imagePoint(
            (horizontal->value() + viewportPosition.x() - originBefore.x()) / zoom_,
            (vertical->value() + viewportPosition.y() - originBefore.y()) / zoom_);
        const double steps = angleDelta / 120.0;
        zoomPolicy_ = ZoomPolicy::Fixed;
        setZoom(zoom_ * std::pow(1.25, steps));
        const QPoint originAfter = imageOrigin();
        horizontal->setValue(
            qRound(originAfter.x() + imagePoint.x() * zoom_ - viewportPosition.x()));
        vertical->setValue(qRound(originAfter.y() + imagePoint.y() * zoom_ - viewportPosition.y()));
    }

    void panBy(const int horizontalDistance, const int verticalDistance)
    {
        auto *horizontal = viewport_->horizontalScrollBar();
        auto *vertical = viewport_->verticalScrollBar();
        horizontal->setValue(horizontal->value() + horizontalDistance);
        vertical->setValue(vertical->value() + verticalDistance);
    }

    void rotateView(const int quarterTurns)
    {
        if (image_.isNull()) {
            return;
        }
        rotationQuarterTurns_ = (rotationQuarterTurns_ + quarterTurns) % 4;
        if (rotationQuarterTurns_ < 0) {
            rotationQuarterTurns_ += 4;
        }
        renderImage();
        updateInformation();
        scheduleCenterView();
        showStatus(false);
    }

    void renderImage()
    {
        const QSize displayedSize = displayedImageSize();
        imageLabel_->showImage(rotatedImage(), displayedSize, viewport_->viewport()->size());
    }

    QImage rotatedImage() const
    {
        if (rotationQuarterTurns_ == 0) {
            return image_;
        }
        return image_.transformed(QTransform().rotate(rotationQuarterTurns_ * 90));
    }

    QSize rotatedImageSize() const
    {
        return rotationQuarterTurns_ % 2 == 0 ? image_.size()
                                              : QSize(image_.height(), image_.width());
    }

    QSize displayedImageSize() const
    {
        const QSize imageSize = rotatedImageSize();
        return QSize(qMax(1, qRound(imageSize.width() * zoom_)),
                     qMax(1, qRound(imageSize.height() * zoom_)));
    }

    QPoint imageOrigin() const
    {
        const QSize displayedSize = displayedImageSize();
        return QPoint((imageLabel_->width() - displayedSize.width()) / 2,
                      (imageLabel_->height() - displayedSize.height()) / 2);
    }

    void centerView()
    {
        viewport_->horizontalScrollBar()->setValue(viewport_->horizontalScrollBar()->maximum() / 2);
        viewport_->verticalScrollBar()->setValue(viewport_->verticalScrollBar()->maximum() / 2);
    }

    void scheduleCenterView()
    {
        QTimer::singleShot(0, this, [this] { centerView(); });
    }

    void advanceAnimation()
    {
        if (currentImage_.frames.size() < 2 ||
            animationPlayback_ == AnimationPlayback::Paused) {
            return;
        }
        if (currentFrame_ + 1 < currentImage_.frames.size()) {
            ++currentFrame_;
        } else if (currentImage_.loopCount < 0 || completedLoops_ < currentImage_.loopCount) {
            currentFrame_ = 0;
            ++completedLoops_;
        } else {
            animationPlayback_ = AnimationPlayback::Finished;
            updateInformation();
            return;
        }
        showFrame(currentFrame_);
        animationTimer_->start(std::max(1, currentImage_.frameDelays.at(currentFrame_)));
    }

    void toggleAnimation()
    {
        if (currentImage_.frames.size() < 2 ||
            animationPlayback_ == AnimationPlayback::Finished) {
            return;
        }
        if (animationPlayback_ == AnimationPlayback::Paused) {
            animationPlayback_ = AnimationPlayback::Playing;
            animationTimer_->start(std::max(1, pausedDelayMilliseconds_));
        } else {
            pausedDelayMilliseconds_ = std::max(1, animationTimer_->remainingTime());
            animationPlayback_ = AnimationPlayback::Paused;
            animationTimer_->stop();
        }
        updateInformation();
    }

    ImageLoading::DecodeRequest decodeRequest(const QString &path,
                                              const bool approvedLargeImage = false) const
    {
#ifdef FLICK_ENABLE_TEST_HARNESS
        const qint64 allocationLimit = configuration_.largeImageAllocationLimitBytes.value_or(
            LargeImageAllocationLimit);
        const int delay = configuration_.delayedDecodePath.isEmpty() ||
                                  configuration_.delayedDecodePath == path
                              ? configuration_.decodeDelayMilliseconds
                              : 0;
        return {path, approvedLargeImage, allocationLimit, delay};
#else
        return {path, approvedLargeImage, LargeImageAllocationLimit};
#endif
    }

    void recordScheduledDecode(const QString &path, const bool scheduled)
    {
#ifdef FLICK_ENABLE_TEST_HARNESS
        if (scheduled) {
            ++decodeCounts_[path];
        }
#else
        Q_UNUSED(path)
        Q_UNUSED(scheduled)
#endif
    }

    void requestDecode(const QString &path)
    {
        if (!path.isEmpty()) {
            recordScheduledDecode(path, imageLoader_.request(decodeRequest(path)));
        }
    }

    void retryDecode(const QString &path, const bool approvedLargeImage = false)
    {
        if (!path.isEmpty()) {
            recordScheduledDecode(path,
                                  imageLoader_.retry(decodeRequest(path, approvedLargeImage)));
        }
    }

    void prefetchNeighbors()
    {
        const auto requestFor =
            [this](const QString &path) -> std::optional<ImageLoading::DecodeRequest> {
            return path.isEmpty() ? std::nullopt
                                  : std::optional<ImageLoading::DecodeRequest>(decodeRequest(path));
        };
        const BrowsingSequence::AdjacentPaths adjacent = browsingSequence_.adjacentPaths();
        const QList<QString> scheduled =
            imageLoader_.prefetchAdjacent(requestFor(adjacent.previous), requestFor(adjacent.next));
        for (const QString &path : scheduled) {
            recordScheduledDecode(path, true);
        }
    }

    void navigate(const int offset)
    {
        const BrowsingSequence::MoveOutcome outcome =
            offset < 0 ? browsingSequence_.movePrevious() : browsingSequence_.moveNext();
        if (outcome != BrowsingSequence::MoveOutcome::Selected) {
            showFeedback(outcome == BrowsingSequence::MoveOutcome::Beginning
                             ? tr("Beginning of folder")
                             : tr("End of folder"));
            return;
        }
        surface_->markBrowsingTeachingComplete();
        displaySelectedImage();
    }

    void showFeedback(const QString &message)
    {
        surface_->showFeedback(message);
    }

    void showEmptyState()
    {
        surface_->showEmpty();
        updateInformation();
    }

    void showDecodeError(const ImageLoading::DecodeFailure &failure)
    {
        animationTimer_->stop();
        surface_->showError(QFileInfo(failure.path).fileName(), failure.details);
        updateInformation();
        setWindowTitle(tr("Flick — Error"));
    }

    void showLargeImageWarning(const ImageLoading::ConfirmationRequired &confirmation)
    {
        pendingLargeImageSize_ = confirmation.declaredSize;
        pendingLargeImagePath_ = confirmation.path;
        surface_->showLargeImageConfirmation(
            tr("%1 declares %2 × %3 pixels and may require about %4 MB when "
               "decoded. "
               "Decode it anyway?")
                .arg(QFileInfo(confirmation.path).fileName())
                .arg(confirmation.declaredSize.width())
                .arg(confirmation.declaredSize.height())
                .arg(confirmation.estimatedAllocationBytes / (1024 * 1024)));
        updateInformation();
    }

    ViewerWindowConfiguration configuration_;
    QImage image_;
    std::unique_ptr<PlatformServices> platformServices_;
    ImageLoading::Loader imageLoader_;
    DisplayedImage *imageLabel_ = nullptr;
    ViewingSurface *surface_ = nullptr;
    QScrollArea *viewport_ = nullptr;
    QTimer *animationTimer_ = nullptr;
    QFileSystemWatcher *directoryWatcher_ = nullptr;
    QSize pendingLargeImageSize_;
    QString pendingLargeImagePath_;
    BrowsingSequence browsingSequence_ = BrowsingSequence::explicitList({});
    QString pendingFilePickerPath_;
    LoadedImage currentImage_;
    int currentFrame_ = 0;
    int completedLoops_ = 0;
    int pausedDelayMilliseconds_ = 0;
    AnimationPlayback animationPlayback_ = AnimationPlayback::Finished;
    WheelAction wheelAction_ = WheelAction::Navigate;
    double zoom_ = 1.0;
    ZoomPolicy zoomPolicy_ = ZoomPolicy::Auto;
    bool viewportRefitPending_ = false;
    int rotationQuarterTurns_ = 0;
    bool dragging_ = false;
    QPointF lastDragPosition_;
    QColor viewportBackground_{QStringLiteral("#181A1B")};
    bool restoreWindowGeometry_ = false;
    QByteArray windowedGeometry_;
    QColorSpace displayColorSpace_{QColorSpace::SRgb};
    QList<QAction *> imageActions_;
    std::unique_ptr<ImageInformation::Dialog> informationDialog_;
    QMenuBar *applicationMenuBar_ = nullptr;
    QMenu *contextMenu_ = nullptr;
    std::unique_ptr<Settings::Editor> settingsEditor_;
#ifdef FLICK_ENABLE_TEST_HARNESS
    QHash<QString, int> decodeCounts_;
    bool failExternalActionsForTest_ = false;
#endif
};

} // namespace

struct ViewerWindow
{
    ViewerWindow(const QString &initialPath, std::unique_ptr<PlatformServices> platformServices,
                 ViewerWindowConfiguration configuration)
        : implementation(initialPath, std::move(platformServices), std::move(configuration))
    {
    }

    ViewerWindowImplementation implementation;
};

#ifdef FLICK_ENABLE_TEST_HARNESS
ViewerWindowTestControl::ViewerWindowTestControl(ViewerWindow &window) : window_(window) {}

namespace {
QWidget *testTarget(ViewerWindowImplementation &window, const ViewerWindowTestRegion target)
{
    if (target == ViewerWindowTestRegion::ViewingSurface) {
        return window.findChild<QWidget *>(QStringLiteral("viewingSurface"));
    }
    if (target == ViewerWindowTestRegion::ImageViewport) {
        return window.findChild<QScrollArea *>()->viewport();
    }
    return &window;
}
} // namespace

void ViewerWindowTestControl::capture(const QString &path) const
{
    window_.implementation.grab().save(path, "PNG");
}

ViewerWindowTestSnapshot ViewerWindowTestControl::snapshot(const QString &decodePath) const
{
    return window_.implementation.testSnapshot(decodePath);
}

void ViewerWindowTestControl::perform(const ViewerWindowTestOperation operation)
{
    window_.implementation.performTestOperation(operation);
}

void ViewerWindowTestControl::activatePresentationAction(const ViewingSurface::ActionRole role)
{
    window_.implementation.activatePresentationActionForTest(role);
}

void ViewerWindowTestControl::focusPresentationAction(const ViewingSurface::ActionRole role)
{
    window_.implementation.focusPresentationActionForTest(role);
}

QRect ViewerWindowTestControl::rect(const ViewerWindowTestRegion target) const
{
    return testTarget(window_.implementation, target)->rect();
}

QRect ViewerWindowTestControl::availableScreenGeometry(const ViewerWindowTestRegion target) const
{
    return testTarget(window_.implementation, target)->screen()->availableGeometry();
}

QPoint ViewerWindowTestControl::mapToGlobal(const ViewerWindowTestRegion target,
                                            const QPoint &point) const
{
    return testTarget(window_.implementation, target)->mapToGlobal(point);
}

void ViewerWindowTestControl::sendEvent(const ViewerWindowTestRegion target, QEvent &event) const
{
    QApplication::sendEvent(testTarget(window_.implementation, target), &event);
}

void ViewerWindowTestControl::sendKeyEvent(QKeyEvent &event, const bool forceWindow) const
{
    QWidget *target = forceWindow ? &window_.implementation : QApplication::activePopupWidget();
    if (target == nullptr) {
        target = QApplication::focusWidget();
    }
    QApplication::sendEvent(target != nullptr ? target : &window_.implementation, &event);
}

void ViewerWindowTestControl::resize(const int width, const int height)
{
    window_.implementation.resize(width, height);
}

void ViewerWindowTestControl::close() { window_.implementation.close(); }

void ViewerWindowTestControl::applySettings(const Settings::Values &values)
{
    window_.implementation.applyAcceptedSettingsForTest(values);
}

void ViewerWindowTestControl::setSettingsDialogValues(const Settings::Values &values)
{
    window_.implementation.setSettingsDialogValuesForTest(values);
}

#endif

void ViewerWindowDeleter::operator()(ViewerWindow *window) const
{
    delete window;
}

ViewerWindowPtr createViewerWindow(const QString &initialPath,
                                   std::unique_ptr<PlatformServices> platformServices,
                                   ViewerWindowConfiguration configuration)
{
    return ViewerWindowPtr(
        new ViewerWindow(initialPath, std::move(platformServices), std::move(configuration)));
}

void installViewerWindowAccessibility()
{
    QAccessible::installFactory(flickAccessibleInterface);
}

void openViewerWindowFile(ViewerWindow &window, const QString &path)
{
    window.implementation.openExternalFile(path);
}

void showViewerWindow(ViewerWindow &window)
{
    window.implementation.show();
    window.implementation.setFocus();
}
