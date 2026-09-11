// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewer_window.h"

#include "flick_application.h"
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
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHash>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
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

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>

#ifdef FLICK_ENABLE_TEST_HARNESS
#include <QPixmap>
#include <QSocketNotifier>
#include <QUrl>
#include <unistd.h>
#endif

namespace {
constexpr int KeyboardPanStep = 40;
constexpr qint64 LargeImageAllocationLimit = 1024LL * 1024 * 1024;

enum class AnimationPlayback
{
    Playing,
    Paused,
    Finished
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

class ImageCanvas final : public QLabel
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
    ViewerWindowImplementation(const QString &imagePath, std::unique_ptr<PlatformServices> platformServices)
        : platformServices_(std::move(platformServices)), imageLoader_(this)
    {
        setWindowTitle(QStringLiteral("Flick"));
        setObjectName(QStringLiteral("viewingSurfaceFocus"));
        setMinimumSize(480, 320);
        setAcceptDrops(true);

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);

        imageLabel_ = new ImageCanvas;
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
             .hidePointer = [this] { viewport_->viewport()->setCursor(Qt::BlankCursor); }});
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
        Settings::Editor::persistWindowGeometry(saveGeometry(), restoreWindowGeometry_);
    }

    void openExternalFile(const QString &path)
    {
        openDirectoryBacked(path);
        showNormal();
        raise();
        activateWindow();
    }

#ifdef FLICK_ENABLE_TEST_HARNESS
    qsizetype cacheBytes() const
    {
        return imageLoader_.cacheBytes();
    }

    qsizetype decodesInFlight() const
    {
        return imageLoader_.requestsInFlight();
    }

    int decodeCount(const QString &path) const
    {
        return decodeCounts_.value(QFileInfo(path).canonicalFilePath());
    }

    QByteArray viewState() const
    {
        return QByteArray::number(zoom_, 'f', 6) + ',' +
               QByteArray::number(viewport_->horizontalScrollBar()->value()) + ',' +
               QByteArray::number(viewport_->verticalScrollBar()->value()) + ',' +
               QByteArray::number(viewport_->viewport()->width()) + ',' +
               QByteArray::number(viewport_->viewport()->height()) + ',' +
               QByteArray::number(imageOrigin().x()) + ',' + QByteArray::number(imageOrigin().y());
    }

    QByteArray uiState() const
    {
        return QByteArray(isFullScreen() ? "fullscreen" : "windowed") + '|' +
               (surface_->statusVisible() ? "status-visible" : "status-hidden") + '|' +
               (viewport_->viewport()->cursor().shape() == Qt::BlankCursor ? "pointer-hidden"
                                                                           : "pointer-visible") +
               '|' + surface_->statusText().toUtf8();
    }

    QByteArray informationState() const
    {
        return informationText_.toUtf8();
    }

    QByteArray informationDialogState() const
    {
        return informationDialog_ == nullptr
                   ? QByteArrayLiteral("closed")
                   : QByteArrayLiteral("open|") + QByteArray::number(informationDialog_->width()) +
                         'x' + QByteArray::number(informationDialog_->height());
    }

    void focusViewingSurfaceForTest()
    {
        activateWindow();
        setFocus(Qt::OtherFocusReason);
    }

    QByteArray feedbackState() const
    {
        return surface_->statusText().toUtf8();
    }

    QByteArray errorState() const
    {
        return surface_->errorDescription();
    }

    QByteArray primaryActionState() const
    {
        return surface_->primaryActionDescription();
    }

#ifdef FLICK_ENABLE_TEST_HARNESS
    QByteArray presentationMotionContract() const
    {
        return surface_->motionContractDescription();
    }

    QByteArray activePresentationTransition() const
    {
        return surface_->activeTransitionDescription();
    }

    QByteArray backgroundPickerTitle() const
    {
        return Settings::Editor::backgroundPickerTitle();
    }
#endif

    QByteArray largeImageState() const
    {
        return QByteArray(surface_->isLargeImageConfirmationVisible() ? "visible" : "hidden") +
               '|' + QByteArray::number(pendingLargeImageSize_.width()) + 'x' +
               QByteArray::number(pendingLargeImageSize_.height());
    }

    QByteArray contextActions() const
    {
        QStringList descriptions;
        for (const QAction *action : viewport_->actions()) {
            if (!action->objectName().startsWith(QStringLiteral("image"))) {
                continue;
            }
            descriptions.append(action->text() + QStringLiteral(" [") +
                                action->shortcut().toString(QKeySequence::NativeText) +
                                QStringLiteral("]"));
        }
        return descriptions.join(QLatin1Char('|')).toUtf8();
    }

    QByteArray contextMenuStructure() const
    {
        return menuStructure(contextMenu_).toUtf8();
    }

    QByteArray contextMenuGeometry() const
    {
        const auto encode = [](const QRect &rect) {
            return QByteArray::number(rect.x()) + ',' + QByteArray::number(rect.y()) + ',' +
                   QByteArray::number(rect.width()) + ',' + QByteArray::number(rect.height());
        };
        QScreen *screen = contextMenu_->screen();
        return encode(contextMenu_->frameGeometry()) + '|' +
               encode(screen != nullptr ? screen->availableGeometry() : QRect{});
    }

    QByteArray applicationMenuStructure() const
    {
        return menuStructure(applicationMenuBar_).toUtf8();
    }

    QByteArray commandAvailability() const
    {
        QStringList entries;
        for (const QAction *action : contextMenu_->actions()) {
            if (!action->isSeparator()) {
                entries.append(
                    action->text() + QLatin1Char('=') +
                    (action->isEnabled() ? QStringLiteral("enabled") : QStringLiteral("disabled")));
            }
        }
        return entries.join(QLatin1Char('|')).toUtf8();
    }

    QByteArray quitActionState() const
    {
        QAction *quitAction = commandAction("applicationQuitAction");
        bool fileMenuContainsAction = false;
        for (const QAction *menuAction : applicationMenuBar_->actions()) {
            if (menuAction->text() == tr("File") && menuAction->menu() != nullptr) {
                fileMenuContainsAction = menuAction->menu()->actions().contains(quitAction);
                break;
            }
        }
        const bool shared = contextMenu_->actions().contains(quitAction) && fileMenuContainsAction;
        return QByteArray(shared ? "shared" : "duplicated") + '|' +
               (quitAction->menuRole() == QAction::QuitRole ? "standard-role" : "custom-role") +
               '|' + (quitAction->shortcut() == QKeySequence::Quit ? "standard-shortcut"
                                                                   : "custom-shortcut");
    }

    QByteArray focusState() const
    {
        if (QApplication::activePopupWidget() != nullptr) {
            return QByteArrayLiteral("menu");
        }
        if (QApplication::activeModalWidget() != nullptr ||
            qobject_cast<QDialog *>(QApplication::activeWindow()) != nullptr) {
            return QByteArrayLiteral("dialog");
        }
        QWidget *focused = QApplication::focusWidget();
        return (focused == this || (focused != nullptr && isAncestorOf(focused)))
                   ? QByteArrayLiteral("viewing-surface")
                   : QByteArrayLiteral("other");
    }

    QByteArray settingsState() const
    {
        return Settings::Editor::describe(currentSettings());
    }

    QByteArray storedSettingsState() const
    {
        return Settings::Editor::describe(Settings::Editor::readAccepted());
    }

    QByteArray settingsFileName() const
    {
        return Settings::Editor::settingsFileName();
    }

    QByteArray settingsDialogStructure() const
    {
        return settingsEditor_->dialogStructure();
    }

    QByteArray settingsDialogGeometry() const
    {
        return settingsEditor_->dialogGeometry();
    }

    QByteArray settingsDialogFocusOrder() const
    {
        return settingsEditor_->dialogFocusOrder();
    }

    QByteArray accessibilityState() const
    {
        const QAccessibleInterface *interface = QAccessible::queryAccessibleInterface(imageLabel_);
        const QString role = interface && interface->role() == QAccessible::Graphic
                                 ? QStringLiteral("Graphic")
                                 : QStringLiteral("Unknown");
        QStringList descriptions{imageLabel_->accessibleName() +
                                 QStringLiteral("|AccessibleRole=") + role + QLatin1Char('|') +
                                 imageLabel_->accessibleDescription()};
        for (const QAction *action : viewport_->actions()) {
            if (!action->objectName().startsWith(QStringLiteral("viewer")) &&
                action->objectName() != QStringLiteral("settingsAction")) {
                continue;
            }
            descriptions.append(action->text() + QLatin1Char('|') +
                                action->shortcut().toString(QKeySequence::NativeText));
        }
        return descriptions.join(QLatin1Char('\n')).toUtf8();
    }

    QByteArray windowGeometryState() const
    {
        return QByteArray::number(width()) + 'x' + QByteArray::number(height());
    }

    QByteArray presentationState() const
    {
        return surface_->presentationDescription();
    }

    void applyTestSettings(const QStringList &values)
    {
        if (const auto settings = Settings::Editor::parseTestValues(values)) {
            applySettings(settings->wheelAction, settings->background, settings->statusVisible,
                          settings->cacheBudgetBytes, settings->restoreWindowGeometry);
        }
    }

    void previewTestSettings(const QStringList &values)
    {
        if (const auto settings = Settings::Editor::parseTestValues(values)) {
            settingsEditor_->setTestValues(*settings);
        }
    }

    void resetTestSettings()
    {
        settingsEditor_->resetForTest();
    }

    void finishTestSettings(const bool apply)
    {
        settingsEditor_->finishForTest(apply);
    }

    void failExternalActionsForTest()
    {
        failExternalActionsForTest_ = true;
    }

#endif

  protected:
    void closeEvent(QCloseEvent *event) override
    {
        persistWindowGeometry();
        QWidget::closeEvent(event);
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
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
        if (event->key() == Qt::Key_Escape && informationDialog_ != nullptr) {
            informationDialog_->close();
            return;
        }
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
            setZoomCentered(zoom_ * 1.25);
            return;
        }
        if (event->matches(QKeySequence::ZoomOut)) {
            setZoomCentered(zoom_ / 1.25);
            return;
        }
        if (event->key() == Qt::Key_1) {
            setZoomCentered(1.0);
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
    template <typename MenuContainer> static QString menuStructure(const MenuContainer *container)
    {
        if (container == nullptr) {
            return {};
        }
        QStringList entries;
        for (const QAction *action : container->actions()) {
            if (action->isSeparator()) {
                entries.append(QStringLiteral("---"));
            } else if (action->menu() != nullptr) {
                entries.append(action->text() + QLatin1Char('[') + menuStructure(action->menu()) +
                               QLatin1Char(']'));
            } else {
                entries.append(action->text());
            }
        }
        return entries.join(QLatin1Char('|'));
    }

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
        auto *quitAction = new QAction(tr("Quit Flick"), this);
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
        const qint64 testBudget = qEnvironmentVariableIntValue("FLICK_TEST_CACHE_BUDGET_BYTES");
        if (testBudget > 0) {
            values.cacheBudgetBytes = testBudget;
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

    void applySettings(const WheelAction wheelAction, const QColor &background,
                       const bool statusVisible, const qsizetype cacheBudgetBytes,
                       const bool restoreWindowGeometry)
    {
        previewSettings(
            {wheelAction, background, statusVisible, cacheBudgetBytes, restoreWindowGeometry});
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
                             QKeySequence::ZoomIn, [this] { setZoomCentered(zoom_ * 1.25); });
        addImageViewerAction(tr("Zoom Out"), QStringLiteral("viewerZoomOutAction"),
                             QKeySequence::ZoomOut, [this] { setZoomCentered(zoom_ / 1.25); });
        addImageViewerAction(tr("Actual Size"), QStringLiteral("viewerActualSizeAction"),
                             QKeySequence(Qt::Key_1), [this] { setZoomCentered(1.0); });
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

    QString imageInformation() const
    {
        const QString selectedPath = browsingSequence_.selectedPath();
        if (selectedPath.isEmpty()) {
            return tr("No current image");
        }
        const QFileInfo file(selectedPath);
        QString format = file.suffix().toUpper();
        if (format == QStringLiteral("JPG")) {
            format = QStringLiteral("JPEG");
        }
        const bool displayed = !image_.isNull() && currentImage_.path == selectedPath;
        const QString dimensions =
            displayed ? tr("%1 × %2").arg(image_.width()).arg(image_.height())
            : surface_->state() == ViewingSurface::State::Error ? tr("Unavailable")
                                                                : tr("Loading…");
        const QString animation = !displayed                        ? tr("Unavailable")
                                  : currentImage_.frames.size() < 2 ? tr("Static image")
                                  : animationPlayback_ == AnimationPlayback::Paused
                                      ? tr("Paused")
                                  : animationPlayback_ == AnimationPlayback::Finished
                                      ? tr("Finished")
                                                                    : tr("Playing");
        return tr("Path: %1\nFormat: %2\nDimensions: %3\nSize: %4 bytes\n"
                  "Modified: %5\nZoom: %6%\nRotation: %7°\nAnimation: "
                  "%8\nPosition: %9 / %10")
            .arg(file.absoluteFilePath(), format)
            .arg(dimensions)
            .arg(file.size())
            .arg(QLocale().toString(file.lastModified(), QLocale::ShortFormat))
            .arg(qRound(zoom_ * 100))
            .arg(rotationQuarterTurns_ * 90)
            .arg(animation)
            .arg(browsingSequence_.selectedIndex() + 1)
            .arg(browsingSequence_.paths().size());
    }

    void updateInformation()
    {
        if (informationDialog_ == nullptr) {
            return;
        }
        informationText_ = imageInformation();
        informationFacts_->setText(informationText_);
        informationDialog_->adjustSize();
    }

    void showInformation()
    {
        if (browsingSequence_.selectedIndex() < 0 || image_.isNull()) {
            return;
        }
        if (informationDialog_ != nullptr) {
            informationDialog_->raise();
            informationDialog_->activateWindow();
            return;
        }
        informationText_ = imageInformation();
        informationDialog_ = new QDialog(this, Qt::Tool);
        informationDialog_->setAttribute(Qt::WA_DeleteOnClose);
        informationDialog_->setModal(false);
        informationDialog_->setWindowTitle(tr("Image Information"));
        informationDialog_->setMaximumSize(480, 320);
        auto *layout = new QVBoxLayout(informationDialog_);
        informationFacts_ = new QLabel(informationText_, informationDialog_);
        informationFacts_->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                                   Qt::TextSelectableByKeyboard);
        informationFacts_->setWordWrap(true);
        informationFacts_->setAccessibleName(tr("Current image information"));
        auto *factsViewport = new QScrollArea(informationDialog_);
        factsViewport->setWidgetResizable(true);
        factsViewport->setFrameShape(QFrame::NoFrame);
        factsViewport->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        factsViewport->setWidget(informationFacts_);
        factsViewport->setMinimumSize(360, 180);
        layout->addWidget(factsViewport);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, informationDialog_);
        QObject::connect(buttons, &QDialogButtonBox::rejected, informationDialog_, &QDialog::close);
        layout->addWidget(buttons);
        informationDialog_->show();
        QObject::connect(informationDialog_, &QDialog::finished, this, [this] {
            informationDialog_ = nullptr;
            informationFacts_ = nullptr;
            QTimer::singleShot(0, this, [this] { restoreViewingFocus(); });
        });
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
        if (clipboard->image() != content) {
            showFeedback(tr("Could not copy the current image"));
        } else {
            showFeedback(tr("Image copied"));
        }
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
            showFullScreen();
            surface_->enteredFullscreen();
        }
    }

    void leaveFullscreen()
    {
        showNormal();
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
        if (qEnvironmentVariableIsSet("FLICK_TEST_FILE_PICKER_SELECTION")) {
            const QString testSelection = qEnvironmentVariable("FLICK_TEST_FILE_PICKER_SELECTION");
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
        applyInitialZoom();
        showFrame(currentFrame_);
        showStatus(false);
        scheduleCenterView();
        if (currentImage_.frames.size() > 1) {
            animationTimer_->start(std::max(1, currentImage_.frameDelays.at(currentFrame_)));
        }
        surface_->showDisplayed();
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
        if (informationDialog_ == nullptr) {
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
        image_ = currentImage_.frames.at(0);
        zoom_ = std::min(1.0, fitZoom());
    }

    void fitToViewport()
    {
        if (!image_.isNull()) {
            setZoomCentered(fitZoom());
        }
    }

    void setZoom(const double zoom)
    {
        if (image_.isNull()) {
            return;
        }
        zoom_ = std::clamp(zoom, 0.01, 64.0);
        renderImage();
        updateInformation();
        showStatus(false);
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
        const qint64 configuredAllocationLimit =
            qEnvironmentVariableIntValue("FLICK_TEST_LARGE_ALLOCATION_LIMIT_BYTES");
        const qint64 allocationLimit =
            configuredAllocationLimit > 0 ? configuredAllocationLimit : LargeImageAllocationLimit;
#else
        constexpr qint64 allocationLimit = LargeImageAllocationLimit;
#endif
        return {path, approvedLargeImage, allocationLimit};
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

    QImage image_;
    std::unique_ptr<PlatformServices> platformServices_;
    ImageLoading::Loader imageLoader_;
    ImageCanvas *imageLabel_ = nullptr;
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
    int rotationQuarterTurns_ = 0;
    bool dragging_ = false;
    QPointF lastDragPosition_;
    QColor viewportBackground_{QStringLiteral("#181A1B")};
    bool restoreWindowGeometry_ = false;
    QColorSpace displayColorSpace_{QColorSpace::SRgb};
    QList<QAction *> imageActions_;
    QString informationText_;
    QDialog *informationDialog_ = nullptr;
    QLabel *informationFacts_ = nullptr;
    QMenuBar *applicationMenuBar_ = nullptr;
    QMenu *contextMenu_ = nullptr;
    std::unique_ptr<Settings::Editor> settingsEditor_;
#ifdef FLICK_ENABLE_TEST_HARNESS
    QHash<QString, int> decodeCounts_;
    bool failExternalActionsForTest_ = false;
#endif
};

#ifdef FLICK_ENABLE_TEST_HARNESS
void captureVisibleWindow(ViewerWindowImplementation &window)
{
    const QString screenshotPath = qEnvironmentVariable("FLICK_TEST_SCREENSHOT_FILE");
    if (screenshotPath.isEmpty()) {
        return;
    }
    window.grab().save(screenshotPath, "PNG");
}

void scheduleCapture(ViewerWindowImplementation &window, QObject &context, const bool waitUntilReady)
{
    auto capture = std::make_shared<std::function<void()>>();
    auto readyForCapture = std::make_shared<bool>(false);
    *capture = [&window, &context, waitUntilReady, capture, readyForCapture] {
        if (waitUntilReady && window.isLoading()) {
            QTimer::singleShot(10, &context, *capture);
            return;
        }
        if (waitUntilReady && !*readyForCapture) {
            *readyForCapture = true;
            QTimer::singleShot(0, &context, *capture);
            return;
        }
        captureVisibleWindow(window);
    };
    QTimer::singleShot(0, &context, *capture);
}
#endif
} // namespace

#ifdef FLICK_ENABLE_TEST_HARNESS
void installViewerWindowTestProtocolImplementation(ViewerWindowImplementation &window,
                                                    FlickApplication &application,
                                                    TestPlatformServices &platformServices)
{
    scheduleCapture(window, application, true);
    auto *testCommands = new QSocketNotifier(STDIN_FILENO, QSocketNotifier::Read, &application);
    QObject::connect(
        testCommands, &QSocketNotifier::activated, &application,
        [&application, &window, &platformServices] {
            char command[4096] = {};
            const auto bytesRead = ::read(STDIN_FILENO, command, sizeof(command));
            if (bytesRead <= 0) {
                return;
            }
            const QByteArray input(command, bytesRead);
            const bool captureImmediately = input.startsWith("Capture");
            if (input.startsWith("CacheBytes")) {
                fprintf(stdout, "%lld\n", static_cast<long long>(window.cacheBytes()));
                fflush(stdout);
                return;
            } else if (input.startsWith("UiResponsiveness")) {
                fprintf(stdout, "%d|%s\n", window.decodesInFlight() > 0 ? 1 : 0,
                        window.uiState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("SettingsState")) {
                fprintf(stdout, "%s\n", window.settingsState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("StoredSettingsState")) {
                fprintf(stdout, "%s\n", window.storedSettingsState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("SettingsFileName")) {
                fprintf(stdout, "%s\n", window.settingsFileName().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("SettingsDialogStructure")) {
                fprintf(stdout, "%s\n", window.settingsDialogStructure().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("SettingsDialogGeometry")) {
                fprintf(stdout, "%s\n", window.settingsDialogGeometry().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("SettingsDialogFocusOrder")) {
                fprintf(stdout, "%s\n", window.settingsDialogFocusOrder().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("LastPickerDirectory")) {
                fprintf(stdout, "%s\n",
                        QSettings()
                            .value(QStringLiteral("filePicker/lastDirectory"))
                            .toString()
                            .toUtf8()
                            .constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("WindowGeometry")) {
                fprintf(stdout, "%s\n", window.windowGeometryState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("PresentationState")) {
                fprintf(stdout, "%s\n", window.presentationState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("PresentationMotionContract")) {
                fprintf(stdout, "%s\n", window.presentationMotionContract().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("ActivePresentationTransition")) {
                fprintf(stdout, "%s\n", window.activePresentationTransition().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("BackgroundPickerTitle")) {
                fprintf(stdout, "%s\n", window.backgroundPickerTitle().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("SaveWindowGeometry")) {
                window.persistWindowGeometry();
                fprintf(stdout, "saved\n");
                fflush(stdout);
                return;
            } else if (input.startsWith("ApplySettings:")) {
                window.applyTestSettings(
                    QString::fromUtf8(input.mid(14).trimmed()).split(QLatin1Char(':')));
                return;
            } else if (input.startsWith("OpenSettings")) {
                if (QAction *settings =
                        window.findChild<QAction *>(QStringLiteral("settingsAction"))) {
                    settings->trigger();
                }
                return;
            } else if (input.startsWith("PreviewSettings:")) {
                window.previewTestSettings(
                    QString::fromUtf8(input.mid(16).trimmed()).split(QLatin1Char(':')));
                return;
            } else if (input.startsWith("ResetSettings")) {
                window.resetTestSettings();
                return;
            } else if (input.startsWith("ApplySettingsDialog")) {
                window.finishTestSettings(true);
                return;
            } else if (input.startsWith("CancelSettings")) {
                window.finishTestSettings(false);
                return;
            } else if (input.startsWith("DisplayProfileChanged:")) {
                platformServices.setDisplayIccProfile(
                    QString::fromUtf8(input.mid(22).trimmed()));
                window.displayConfigurationChanged();
            } else if (input.startsWith("Resize:")) {
                const QList<QByteArray> size = input.mid(7).trimmed().split(':');
                if (size.size() == 2) {
                    window.resize(size.at(0).toInt(), size.at(1).toInt());
                }
                return;
            } else if (input.startsWith("Close")) {
                window.close();
                application.quit();
                return;
            } else if (input.startsWith("ViewState")) {
                fprintf(stdout, "%s\n", window.viewState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("UiState")) {
                fprintf(stdout, "%s\n", window.uiState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("FocusViewingSurface")) {
                window.focusViewingSurfaceForTest();
                return;
            } else if (input.startsWith("InformationState")) {
                fprintf(stdout, "%s\n", window.informationState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("InformationDialogState")) {
                fprintf(stdout, "%s\n", window.informationDialogState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("ClipboardText")) {
                fprintf(stdout, "%s\n", QApplication::clipboard()->text().toUtf8().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("ClipboardImageSize")) {
                const QSize size = QApplication::clipboard()->image().size();
                fprintf(stdout, "%dx%d\n", size.width(), size.height());
                fflush(stdout);
                return;
            } else if (input.startsWith("ContextActions")) {
                fprintf(stdout, "%s\n", window.contextActions().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("ContextMenuStructure")) {
                fprintf(stdout, "%s\n", window.contextMenuStructure().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("ContextMenuGeometry")) {
                fprintf(stdout, "%s\n", window.contextMenuGeometry().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("ApplicationMenuStructure")) {
                fprintf(stdout, "%s\n", window.applicationMenuStructure().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("CommandAvailability")) {
                fprintf(stdout, "%s\n", window.commandAvailability().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("QuitActionState")) {
                fprintf(stdout, "%s\n", window.quitActionState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("TriggerQuit")) {
                window.findChild<QAction *>(QStringLiteral("applicationQuitAction"))->trigger();
                return;
            } else if (input.startsWith("FocusState")) {
                fprintf(stdout, "%s\n", window.focusState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("AccessibilityState")) {
                fprintf(stdout, "%s\n", window.accessibilityState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("RevealedPath")) {
                fprintf(stdout, "%s\n", platformServices.revealedPath().toUtf8().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("Feedback")) {
                fprintf(stdout, "%s\n", window.feedbackState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("ErrorState")) {
                fprintf(stdout, "%s\n", window.errorState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("LargeImageState")) {
                fprintf(stdout, "%s\n", window.largeImageState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("PrimaryActionState")) {
                fprintf(stdout, "%s\n", window.primaryActionState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("FocusDetails")) {
                window.findChild<QToolButton *>()->setFocus(Qt::OtherFocusReason);
                return;
            } else if (input.startsWith("FocusSkip")) {
                window.findChild<QPushButton *>(QStringLiteral("rejectLargeImage"))
                    ->setFocus(Qt::OtherFocusReason);
                return;
            } else if (input.startsWith("ToggleDetails")) {
                if (auto *button = window.findChild<QToolButton *>()) {
                    button->toggle();
                }
            } else if (input.startsWith("ApproveLarge")) {
                window.findChild<QPushButton *>(QStringLiteral("approveLargeImage"))->click();
            } else if (input.startsWith("RejectLarge")) {
                window.findChild<QPushButton *>(QStringLiteral("rejectLargeImage"))->click();
            } else if (input.startsWith("FailExternalActions")) {
                window.failExternalActionsForTest();
                return;
            } else if (input.startsWith("DecodeCount:")) {
                const QString path = QString::fromUtf8(input.mid(12).trimmed());
                fprintf(stdout, "%d\n", window.decodeCount(path));
                fflush(stdout);
                return;
            } else if (input.startsWith("Drop:")) {
                const QList<QByteArray> encodedPaths = input.mid(5).trimmed().split('|');
                auto *mimeData = new QMimeData;
                QList<QUrl> urls;
                for (const QByteArray &encodedPath : encodedPaths) {
                    urls.append(QUrl::fromLocalFile(QString::fromUtf8(encodedPath)));
                }
                mimeData->setUrls(urls);
                QDragEnterEvent dragEvent(window.rect().center(), Qt::CopyAction, mimeData,
                                          Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(&window, &dragEvent);
                QDropEvent event(QPointF(window.rect().center()), Qt::CopyAction, mimeData,
                                 Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(&window, &event);
                delete mimeData;
            } else if (input.startsWith("BeginDrag:")) {
                auto *mimeData = new QMimeData;
                mimeData->setUrls(
                    {QUrl::fromLocalFile(QString::fromUtf8(input.mid(10).trimmed()))});
                QDragEnterEvent event(window.rect().center(), Qt::CopyAction, mimeData,
                                      Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(&window, &event);
                delete mimeData;
            } else if (input.startsWith("LeaveDrag")) {
                QDragLeaveEvent event;
                QApplication::sendEvent(&window, &event);
            } else if (input.startsWith("SelectZoomWheelAction")) {
                if (auto *action = window.findChild<QAction *>(QStringLiteral("wheelZoomAction"))) {
                    action->trigger();
                }
            } else if (input.startsWith("ContextMenuAtScreenEdge:")) {
                QWidget *target = window.findChild<QWidget *>(QStringLiteral("viewingSurface"));
                const QRect available = target->screen()->availableGeometry();
                const QByteArray corner = input.mid(24).trimmed();
                const QPoint globalPosition = corner == "TopRight"      ? available.topRight()
                                              : corner == "BottomLeft"  ? available.bottomLeft()
                                              : corner == "BottomRight" ? available.bottomRight()
                                                                        : available.topLeft();
                QContextMenuEvent event(QContextMenuEvent::Mouse, target->rect().center(),
                                        globalPosition);
                QApplication::sendEvent(target, &event);
            } else if (input.startsWith("ContextMenu:")) {
                const QList<QByteArray> parts = input.trimmed().split(':');
                if (parts.size() == 3) {
                    QWidget *target = window.findChild<QWidget *>(QStringLiteral("viewingSurface"));
                    const QPoint position(parts.at(1).toInt(), parts.at(2).toInt());
                    QContextMenuEvent event(QContextMenuEvent::Mouse, position,
                                            target->mapToGlobal(position));
                    QApplication::sendEvent(target, &event);
                }
            } else if (input.startsWith("MenuDown") || input.startsWith("MenuEnter")) {
                if (QWidget *menu = QApplication::activePopupWidget()) {
                    const int key = input.startsWith("MenuDown") ? Qt::Key_Down : Qt::Key_Return;
                    QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
                    QApplication::sendEvent(menu, &event);
                }
            } else if (input.startsWith("Wheel:") || input.startsWith("CtrlWheel:")) {
                const QList<QByteArray> parts = input.trimmed().split(':');
                if (parts.size() == 4) {
                    const QPointF position(parts.at(1).toInt(), parts.at(2).toInt());
                    const Qt::KeyboardModifiers modifiers =
                        input.startsWith("CtrlWheel:") ? Qt::ControlModifier : Qt::NoModifier;
                    QWheelEvent event(position, position, QPoint(), QPoint(0, parts.at(3).toInt()),
                                      Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
                    QApplication::sendEvent(window.findChild<QScrollArea *>()->viewport(), &event);
                }
            } else if (input.startsWith("Drag:")) {
                const QList<QByteArray> parts = input.trimmed().split(':');
                if (parts.size() == 5) {
                    QWidget *target = window.findChild<QScrollArea *>()->viewport();
                    const QPointF start(parts.at(1).toInt(), parts.at(2).toInt());
                    const QPointF end(parts.at(3).toInt(), parts.at(4).toInt());
                    QMouseEvent press(QEvent::MouseButtonPress, start, start, start, Qt::LeftButton,
                                      Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(target, &press);
                    QMouseEvent move(QEvent::MouseMove, end, end, end, Qt::NoButton, Qt::LeftButton,
                                     Qt::NoModifier);
                    QApplication::sendEvent(target, &move);
                    QMouseEvent release(QEvent::MouseButtonRelease, end, end, end, Qt::LeftButton,
                                        Qt::NoButton, Qt::NoModifier);
                    QApplication::sendEvent(target, &release);
                }
            } else if (input.startsWith("Move:") || input.startsWith("DoubleClick:")) {
                const QList<QByteArray> parts = input.trimmed().split(':');
                if (parts.size() == 3) {
                    QWidget *target = window.findChild<QScrollArea *>()->viewport();
                    const QPointF position(parts.at(1).toInt(), parts.at(2).toInt());
                    const QEvent::Type type = input.startsWith("DoubleClick:")
                                                  ? QEvent::MouseButtonDblClick
                                                  : QEvent::MouseMove;
                    const Qt::MouseButton button =
                        type == QEvent::MouseButtonDblClick ? Qt::LeftButton : Qt::NoButton;
                    QMouseEvent event(type, position, position, position, button, button,
                                      Qt::NoModifier);
                    QApplication::sendEvent(target, &event);
                }
            } else if (!input.startsWith("Capture")) {
                const bool ctrlO = input.startsWith("CtrlO");
                const bool copyImage = input.startsWith("CopyImage");
                const bool copyPath = input.startsWith("CopyPath");
                const bool reveal = input.startsWith("Reveal");
                const bool ctrlPlus = input.startsWith("CtrlPlus");
                const bool ctrlMinus = input.startsWith("CtrlMinus");
                const bool shift = input.startsWith("Shift");
                const int qtKey = ctrlO                             ? Qt::Key_O
                                  : copyImage                       ? Qt::Key_C
                                  : copyPath                        ? Qt::Key_C
                                  : reveal                          ? Qt::Key_R
                                  : ctrlPlus                        ? Qt::Key_Plus
                                  : ctrlMinus                       ? Qt::Key_Minus
                                  : input.startsWith("Information") ? Qt::Key_I
                                  : input.startsWith("Fit")         ? Qt::Key_F
                                  : input.startsWith("ActualSize")  ? Qt::Key_1
                                  : input.startsWith("ShiftLeft")   ? Qt::Key_Left
                                  : input.startsWith("ShiftRight")  ? Qt::Key_Right
                                  : input.startsWith("ShiftUp")     ? Qt::Key_Up
                                  : input.startsWith("ShiftDown")   ? Qt::Key_Down
                                  : input.startsWith("RotateLeft")  ? Qt::Key_L
                                  : input.startsWith("RotateRight") ? Qt::Key_R
                                  : input.startsWith("F11")         ? Qt::Key_F11
                                  : input.startsWith("Refresh")     ? Qt::Key_F5
                                  : input.startsWith("Enter")       ? Qt::Key_Return
                                  : input.startsWith("Escape")      ? Qt::Key_Escape
                                  : input.startsWith("Left")        ? Qt::Key_Left
                                  : input.startsWith("Space")       ? Qt::Key_Space
                                                                    : Qt::Key_Right;
                QKeyEvent event(QEvent::KeyPress, qtKey,
                                (ctrlO || copyImage || ctrlPlus || ctrlMinus) ? Qt::ControlModifier
                                : (copyPath || reveal) ? Qt::ControlModifier | Qt::ShiftModifier
                                : shift                ? Qt::ShiftModifier
                                                       : Qt::NoModifier);
                QWidget *target = QApplication::activePopupWidget();
                if (input.startsWith("Enter")) {
                    target = &window;
                } else if (target == nullptr) {
                    target = QApplication::focusWidget();
                }
                QApplication::sendEvent(target != nullptr ? target : &window, &event);
            }
            scheduleCapture(window, application, !captureImmediately);
        });
}
#endif

struct ViewerWindow
{
    ViewerWindow(const QString &initialPath, std::unique_ptr<PlatformServices> platformServices)
        : implementation(initialPath, std::move(platformServices))
    {
    }

    ViewerWindowImplementation implementation;
};

void ViewerWindowDeleter::operator()(ViewerWindow *window) const
{
    delete window;
}

ViewerWindowPtr createViewerWindow(const QString &initialPath,
                                   std::unique_ptr<PlatformServices> platformServices)
{
    return ViewerWindowPtr(new ViewerWindow(initialPath, std::move(platformServices)));
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

#ifdef FLICK_ENABLE_TEST_HARNESS
void installViewerWindowTestProtocol(ViewerWindow &window, FlickApplication &application,
                                     TestPlatformServices &platformServices)
{
    installViewerWindowTestProtocolImplementation(window.implementation, application,
                                                   platformServices);
}
#endif
