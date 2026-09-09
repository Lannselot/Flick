// SPDX-License-Identifier: GPL-3.0-or-later

#include "flick_application.h"
#include "image_loading.h"
#include "platform_services.h"

#include <QApplication>
#include <QAccessible>
#include <QAccessibleWidget>
#include <QAction>
#include <QActionGroup>
#include <QClipboard>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QColorSpace>
#include <QColorDialog>
#include "browsing_sequence.h"
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QCursor>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHash>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFormLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMimeData>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QScreen>
#include <QSettings>
#include <QSet>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QTransform>
#include <QVBoxLayout>
#include <QWidget>
#include <QWheelEvent>
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
constexpr qsizetype DefaultCacheBudgetBytes = 512LL * 1024 * 1024;
constexpr int StatusVisibilityMilliseconds = 2000;
constexpr int KeyboardPanStep = 40;
constexpr qint64 LargeImageAllocationLimit = 1024LL * 1024 * 1024;

enum class WheelAction
{
    Navigate,
    Zoom
};

using ImageLoading::LoadedImage;

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
                           (height() - displayedSize_.height()) / 2,
                           displayedSize_.width(), displayedSize_.height());
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

class ViewerWindow final : public QWidget
{
public:
    ViewerWindow(const QString &imagePath, std::unique_ptr<PlatformServices> platformServices)
        : platformServices_(std::move(platformServices))
        , imageLoader_(this)
    {
        setWindowTitle(QStringLiteral("Flick"));
        setMinimumSize(480, 320);
        setAcceptDrops(true);

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);

        imageLabel_ = new ImageCanvas;
        imageLabel_->setObjectName(QStringLiteral("imageLabel"));
        imageLabel_->setAlignment(Qt::AlignCenter);
        imageLabel_->setAccessibleName(tr("Image viewport"));
        imageLabel_->setAccessibleDescription(
            tr("Displays the current image; use the application actions to navigate and zoom."));
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
        viewport_->setContextMenuPolicy(Qt::ActionsContextMenu);
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
        const QString storedWheelAction =
            QSettings().value(QStringLiteral("view/wheelAction"), QStringLiteral("navigate"))
                .toString();
        wheelAction_ =
            storedWheelAction == QStringLiteral("zoom") ? WheelAction::Zoom : WheelAction::Navigate;
        navigateWithWheel->setChecked(wheelAction_ == WheelAction::Navigate);
        zoomWithWheel->setChecked(wheelAction_ == WheelAction::Zoom);
        QObject::connect(navigateWithWheel, &QAction::triggered, this, [this] {
            setWheelAction(WheelAction::Navigate);
        });
        QObject::connect(zoomWithWheel, &QAction::triggered, this, [this] {
            setWheelAction(WheelAction::Zoom);
        });
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

        boundaryMessage_ = new QLabel;
        boundaryMessage_->setAlignment(Qt::AlignCenter);
        boundaryMessage_->setAccessibleName(tr("Sequence boundary message"));
        boundaryMessage_->hide();
        boundaryTimer_ = new QTimer(this);
        boundaryTimer_->setSingleShot(true);
        QObject::connect(boundaryTimer_, &QTimer::timeout, boundaryMessage_, &QWidget::hide);
        animationTimer_ = new QTimer(this);
        animationTimer_->setSingleShot(true);
        QObject::connect(animationTimer_, &QTimer::timeout, this, [this] {
            advanceAnimation();
        });
        directoryWatcher_ = new QFileSystemWatcher(this);
        QObject::connect(directoryWatcher_, &QFileSystemWatcher::directoryChanged, this,
                         [this] {
                             refreshDirectorySequence();
                         });

        statusDisplay_ = new QLabel(viewport_->viewport());
        statusDisplay_->setAlignment(Qt::AlignCenter);
        statusDisplay_->setAccessibleName(tr("Image status"));
        statusDisplay_->setAccessibleDescription(
            tr("Current filename, sequence position, and zoom level."));
        statusDisplay_->setAttribute(Qt::WA_TransparentForMouseEvents);
        statusDisplay_->setAutoFillBackground(true);
        statusDisplay_->setBackgroundRole(QPalette::ToolTipBase);
        statusDisplay_->setForegroundRole(QPalette::ToolTipText);
        statusDisplay_->setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
        statusDisplay_->setMargin(6);
        statusDisplay_->hide();
        statusTimer_ = new QTimer(this);
        statusTimer_->setSingleShot(true);
        QObject::connect(statusTimer_, &QTimer::timeout, this, [this] {
            statusDisplay_->hide();
            if (isFullScreen()) {
                viewport_->viewport()->setCursor(Qt::BlankCursor);
            }
        });
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

        emptyState_ = new QLabel(tr("No image open"));
        emptyState_->setAlignment(Qt::AlignCenter);
        emptyState_->setAccessibleName(tr("No image open"));

        errorState_ = new QWidget;
        auto *errorLayout = new QVBoxLayout(errorState_);
        errorLayout->setAlignment(Qt::AlignCenter);
        errorExplanation_ = new QLabel;
        errorExplanation_->setAlignment(Qt::AlignCenter);
        errorExplanation_->setWordWrap(true);
        errorExplanation_->setAccessibleName(tr("Image error"));
        errorDetailsButton_ = new QToolButton;
        errorDetailsButton_->setText(tr("Technical details"));
        errorDetailsButton_->setAccessibleDescription(
            tr("Shows or hides technical decoder details."));
        errorDetailsButton_->setCheckable(true);
        errorDetails_ = new QLabel;
        errorDetails_->setAlignment(Qt::AlignCenter);
        errorDetails_->setWordWrap(true);
        errorDetails_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        errorDetails_->setTextInteractionFlags(errorDetails_->textInteractionFlags() |
                                               Qt::TextSelectableByKeyboard);
        errorDetails_->hide();
        QObject::connect(errorDetailsButton_, &QToolButton::toggled, errorDetails_,
                         &QWidget::setVisible);
        errorLayout->addWidget(errorExplanation_);
        errorLayout->addWidget(errorDetailsButton_, 0, Qt::AlignHCenter);
        errorLayout->addWidget(errorDetails_);
        errorState_->hide();

        largeImageWarning_ = new QWidget;
        auto *warningLayout = new QVBoxLayout(largeImageWarning_);
        warningLayout->setAlignment(Qt::AlignCenter);
        largeImageExplanation_ = new QLabel;
        largeImageExplanation_->setAlignment(Qt::AlignCenter);
        largeImageExplanation_->setWordWrap(true);
        largeImageExplanation_->setAccessibleName(tr("Large image warning"));
        auto *warningButtons = new QWidget;
        auto *warningButtonLayout = new QHBoxLayout(warningButtons);
        auto *approveLarge = new QPushButton(tr("Decode anyway"));
        approveLarge->setObjectName(QStringLiteral("approveLargeImage"));
        approveLarge->setAccessibleDescription(
            tr("Allows decoding of the current exceptionally large image."));
        auto *rejectLarge = new QPushButton(tr("Cancel"));
        rejectLarge->setObjectName(QStringLiteral("rejectLargeImage"));
        rejectLarge->setAccessibleDescription(
            tr("Cancels decoding of the current exceptionally large image."));
        warningButtonLayout->addWidget(approveLarge);
        warningButtonLayout->addWidget(rejectLarge);
        QObject::connect(approveLarge, &QPushButton::clicked, this, [this] {
            approveLargeImage();
        });
        QObject::connect(rejectLarge, &QPushButton::clicked, this, [this] {
            rejectLargeImage();
        });
        warningLayout->addWidget(largeImageExplanation_);
        warningLayout->addWidget(warningButtons, 0, Qt::AlignHCenter);
        largeImageWarning_->hide();

        layout->addWidget(viewport_);
        layout->addWidget(emptyState_);
        layout->addWidget(errorState_);
        layout->addWidget(largeImageWarning_);
        layout->addWidget(boundaryMessage_);
        showEmptyState();

        auto *settingsAction = new QAction(tr("Settings"), this);
        settingsAction->setObjectName(QStringLiteral("settingsAction"));
        settingsAction->setShortcut(QKeySequence::Preferences);
        settingsAction->setShortcutContext(Qt::WindowShortcut);
        QObject::connect(settingsAction, &QAction::triggered, this, [this] {
            showSettings();
        });
        viewport_->addAction(settingsAction);
        addAction(settingsAction);

#if defined(Q_OS_MACOS)
        addMacApplicationMenu(settingsAction);
#endif

        if (!imagePath.isEmpty()) {
            openDirectoryBacked(imagePath);
        }
    }

    bool isLoading() const
    {
        return imageLoader_.isLoading(requestedPath_);
    }

    void displayConfigurationChanged()
    {
        refreshDisplayColorSpace();
    }

    void persistWindowGeometry()
    {
        QSettings settings;
        if (restoreWindowGeometry_) {
            settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
        } else {
            settings.remove(QStringLiteral("window/geometry"));
        }
        settings.sync();
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
               QByteArray::number(imageOrigin().x()) + ',' +
               QByteArray::number(imageOrigin().y());
    }

    QByteArray uiState() const
    {
        return QByteArray(isFullScreen() ? "fullscreen" : "windowed") + '|' +
               (statusDisplay_->isVisible() ? "status-visible" : "status-hidden") + '|' +
               (viewport_->viewport()->cursor().shape() == Qt::BlankCursor ? "pointer-hidden"
                                                                           : "pointer-visible") +
               '|' + statusDisplay_->text().toUtf8();
    }

    QByteArray informationState() const
    {
        return informationText_.toUtf8();
    }

    QByteArray feedbackState() const
    {
        return boundaryMessage_->text().toUtf8();
    }

    QByteArray errorState() const
    {
        return QByteArray(errorState_->isVisible() ? "visible" : "hidden") + '|' +
               errorExplanation_->text().toUtf8() + '|' + errorDetails_->text().toUtf8() + '|' +
               (errorDetails_->isVisible() ? "details-visible" : "details-hidden");
    }

    QByteArray largeImageState() const
    {
        return QByteArray(largeImageWarning_->isVisible() ? "visible" : "hidden") + '|' +
               QByteArray::number(pendingLargeImageSize_.width()) + 'x' +
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

    QByteArray settingsState() const
    {
        return QByteArray(wheelAction_ == WheelAction::Zoom ? "zoom" : "navigate") + '|' +
               viewportBackground_.name().toUtf8() + '|' +
               (statusVisible_ ? "visible" : "hidden") + '|' +
               QByteArray::number(imageLoader_.cacheBudget()) + '|' +
               (restoreWindowGeometry_ ? "restore" : "forget");
    }

    QByteArray accessibilityState() const
    {
        const QAccessibleInterface *interface =
            QAccessible::queryAccessibleInterface(imageLabel_);
        const QString role =
            interface && interface->role() == QAccessible::Graphic ? QStringLiteral("Graphic")
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

    void applyTestSettings(const QStringList &values)
    {
        if (values.size() != 5) {
            return;
        }
        applySettings(values.at(0) == QStringLiteral("zoom") ? WheelAction::Zoom
                                                              : WheelAction::Navigate,
                      QColor(values.at(1)), values.at(2).toInt() != 0,
                      values.at(3).toLongLong() * 1024 * 1024, values.at(4).toInt() != 0);
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

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == viewport_->viewport() && event->type() == QEvent::Resize) {
            positionStatusDisplay();
        }
        if ((watched == viewport_->viewport() || watched == imageLabel_) &&
            event->type() == QEvent::MouseMove) {
            showStatus(true);
        }
        if ((watched == viewport_->viewport() || watched == imageLabel_) &&
            event->type() == QEvent::MouseButtonDblClick) {
            toggleFullscreen();
            return true;
        }
        if (watched == viewport_->viewport() && event->type() == QEvent::Wheel) {
            const auto *wheel = static_cast<QWheelEvent *>(event);
            const int wheelDelta = wheel->angleDelta().y() != 0
                                       ? wheel->angleDelta().y()
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
        if (event->mimeData()->hasUrls()) {
            event->acceptProposedAction();
        }
    }

    void dropEvent(QDropEvent *event) override
    {
        QStringList paths;
        for (const QUrl &url : event->mimeData()->urls()) {
            if (url.isLocalFile()) {
                paths.append(url.toLocalFile());
            }
        }
        openDroppedPaths(paths);
        event->acceptProposedAction();
    }

private:
#if defined(Q_OS_MACOS)
    void addMacApplicationMenu(QAction *settingsAction)
    {
        applicationMenuBar_ = new QMenuBar(this);
        applicationMenuBar_->setNativeMenuBar(true);
        QMenu *fileMenu = applicationMenuBar_->addMenu(tr("File"));
        auto *openAction = fileMenu->addAction(tr("Open…"));
        openAction->setShortcut(QKeySequence::Open);
        QObject::connect(openAction, &QAction::triggered, this, [this] {
            openFromFilePicker();
        });
        fileMenu->addAction(settingsAction);
        settingsAction->setMenuRole(QAction::PreferencesRole);

        QMenu *applicationMenu = applicationMenuBar_->addMenu(tr("Flick"));
        auto *aboutAction = applicationMenu->addAction(tr("About Flick"));
        aboutAction->setMenuRole(QAction::AboutRole);
        QObject::connect(aboutAction, &QAction::triggered, this, [this] {
            QMessageBox::about(this, tr("About Flick"),
                               tr("Flick %1\nA color-managed image viewer.")
                                   .arg(QCoreApplication::applicationVersion()));
        });
        auto *quitAction = applicationMenu->addAction(tr("Quit Flick"));
        quitAction->setMenuRole(QAction::QuitRole);
        quitAction->setShortcut(QKeySequence::Quit);
        QObject::connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);
    }
#endif

    void loadSettings()
    {
        QSettings settings;
        viewportBackground_ =
            QColor(settings.value(QStringLiteral("view/background"), QStringLiteral("#202020"))
                       .toString());
        if (!viewportBackground_.isValid()) {
            viewportBackground_ = QColor(QStringLiteral("#202020"));
        }
        statusVisible_ = settings.value(QStringLiteral("view/statusVisible"), true).toBool();
        qsizetype cacheBudgetBytes =
            settings.value(QStringLiteral("cache/budgetBytes"), DefaultCacheBudgetBytes)
                .toLongLong();
#ifdef FLICK_ENABLE_TEST_HARNESS
        const qint64 testBudget = qEnvironmentVariableIntValue("FLICK_TEST_CACHE_BUDGET_BYTES");
        if (testBudget > 0) {
            cacheBudgetBytes = testBudget;
        }
#endif
        if (cacheBudgetBytes <= 0) {
            cacheBudgetBytes = DefaultCacheBudgetBytes;
        }
        imageLoader_.setCacheBudget(cacheBudgetBytes);
        restoreWindowGeometry_ =
            settings.value(QStringLiteral("window/restoreGeometry"), false).toBool();
        applyViewportBackground();
        if (restoreWindowGeometry_) {
            restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
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
        setWheelAction(wheelAction);
        viewportBackground_ = background.isValid() ? background : QColor(QStringLiteral("#202020"));
        statusVisible_ = statusVisible;
        imageLoader_.setCacheBudget(std::max<qsizetype>(1024 * 1024, cacheBudgetBytes));
        restoreWindowGeometry_ = restoreWindowGeometry;
        if (!statusVisible_) {
            statusTimer_->stop();
            statusDisplay_->hide();
        } else {
            showStatus(false);
        }
        applyViewportBackground();
        QSettings settings;
        settings.setValue(QStringLiteral("view/background"), viewportBackground_.name());
        settings.setValue(QStringLiteral("view/statusVisible"), statusVisible_);
        settings.setValue(QStringLiteral("cache/budgetBytes"), imageLoader_.cacheBudget());
        settings.setValue(QStringLiteral("window/restoreGeometry"), restoreWindowGeometry_);
        if (!restoreWindowGeometry_) {
            settings.remove(QStringLiteral("window/geometry"));
        }
        settings.sync();
        if (auto *navigate = findChild<QAction *>(QStringLiteral("wheelNavigateAction"))) {
            navigate->setChecked(wheelAction_ == WheelAction::Navigate);
        }
        if (auto *zoom = findChild<QAction *>(QStringLiteral("wheelZoomAction"))) {
            zoom->setChecked(wheelAction_ == WheelAction::Zoom);
        }
    }

    void showSettings()
    {
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Settings"));
        QFormLayout layout(&dialog);
        QComboBox wheel;
        wheel.addItem(tr("Navigate images"), QStringLiteral("navigate"));
        wheel.addItem(tr("Zoom image"), QStringLiteral("zoom"));
        wheel.setCurrentIndex(wheelAction_ == WheelAction::Zoom ? 1 : 0);
        QPushButton background(viewportBackground_.name());
        QColor selectedBackground = viewportBackground_;
        QObject::connect(&background, &QPushButton::clicked, &dialog, [&] {
            const QColor selected = QColorDialog::getColor(selectedBackground, &dialog,
                                                            tr("Viewport Background"));
            if (selected.isValid()) {
                selectedBackground = selected;
                background.setText(selected.name());
            }
        });
        QCheckBox status(tr("Show transient image status"));
        status.setChecked(statusVisible_);
        QSpinBox cache;
        cache.setRange(1, 16384);
        cache.setSuffix(tr(" MB"));
        cache.setValue(static_cast<int>(imageLoader_.cacheBudget() / (1024 * 1024)));
        QCheckBox geometry(tr("Restore window size and position"));
        geometry.setChecked(restoreWindowGeometry_);
        layout.addRow(tr("Mouse wheel:"), &wheel);
        layout.addRow(tr("Background:"), &background);
        layout.addRow(QString{}, &status);
        layout.addRow(tr("Cache budget:"), &cache);
        layout.addRow(QString{}, &geometry);
        QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        QObject::connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        QObject::connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout.addRow(&buttons);
        if (dialog.exec() == QDialog::Accepted) {
            applySettings(wheel.currentData() == QStringLiteral("zoom") ? WheelAction::Zoom
                                                                         : WheelAction::Navigate,
                          selectedBackground, status.isChecked(),
                          static_cast<qsizetype>(cache.value()) * 1024 * 1024,
                          geometry.isChecked());
        }
    }

    void addImageActions()
    {
        const auto addImageAction = [this](const QString &text, const QString &objectName,
                                           const QKeySequence &shortcut, auto operation) {
            QAction *action =
                addViewportAction(text, objectName, shortcut, std::move(operation));
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
        addViewportAction(tr("Open Image"), QStringLiteral("viewerOpenAction"),
                          QKeySequence::Open, [this] { openFromFilePicker(); });
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
        addViewportAction(tr("Zoom In"), QStringLiteral("viewerZoomInAction"),
                          QKeySequence::ZoomIn, [this] { setZoomCentered(zoom_ * 1.25); });
        addViewportAction(tr("Zoom Out"), QStringLiteral("viewerZoomOutAction"),
                          QKeySequence::ZoomOut, [this] { setZoomCentered(zoom_ / 1.25); });
        addViewportAction(tr("Actual Size"), QStringLiteral("viewerActualSizeAction"),
                          QKeySequence(Qt::Key_1), [this] { setZoomCentered(1.0); });
        addViewportAction(tr("Fit to Window"), QStringLiteral("viewerFitAction"),
                          QKeySequence(Qt::Key_F), [this] { fitToViewport(); });
        addViewportAction(tr("Rotate Left"), QStringLiteral("viewerRotateLeftAction"),
                          QKeySequence(Qt::Key_L), [this] { rotateView(-1); });
        addViewportAction(tr("Rotate Right"), QStringLiteral("viewerRotateRightAction"),
                          QKeySequence(Qt::Key_R), [this] { rotateView(1); });
        addViewportAction(tr("Pause or Resume Animation"),
                          QStringLiteral("viewerAnimationAction"), QKeySequence(Qt::Key_Space),
                          [this] { toggleAnimation(); });
        addViewportAction(tr("Toggle Fullscreen"), QStringLiteral("viewerFullscreenAction"),
                          QKeySequence(Qt::Key_F11), [this] { toggleFullscreen(); });
        addViewportAction(tr("Retry"), QStringLiteral("viewerRetryAction"),
                          QKeySequence(Qt::Key_F5), [this] { retryCurrentImage(); });
    }

    QAction *addViewportAction(const QString &text, const QString &objectName,
                               const QKeySequence &shortcut,
                               std::function<void()> operation)
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
        const QFileInfo file(currentImage_.path);
        QString format = file.suffix().toUpper();
        if (format == QStringLiteral("JPG")) {
            format = QStringLiteral("JPEG");
        }
        return tr("Path: %1\nFormat: %2\nDimensions: %3 × %4\nSize: %5 bytes\n"
                  "Modified: %6\nZoom: %7%\nPosition: %8 / %9")
            .arg(file.absoluteFilePath(), format)
            .arg(image_.width())
            .arg(image_.height())
            .arg(file.size())
            .arg(QLocale().toString(file.lastModified(), QLocale::ShortFormat))
            .arg(qRound(zoom_ * 100))
            .arg(sequence_.indexOf(currentImage_.path) + 1)
            .arg(sequence_.size());
    }

    void showInformation()
    {
        if (currentIndex_ < 0 || image_.isNull()) {
            return;
        }
        informationText_ = imageInformation();
        auto *dialog = new QDialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowTitle(tr("Image Information"));
        auto *layout = new QVBoxLayout(dialog);
        auto *facts = new QLabel(informationText_, dialog);
        facts->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        layout->addWidget(facts);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
        QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
        layout->addWidget(buttons);
        dialog->show();
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
        if (currentIndex_ < 0 ||
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
        }
    }

    void revealCurrentFile()
    {
        if (currentIndex_ < 0 ||
            !externalActionCanRun(tr("Could not show the current file in the file manager"))) {
            return;
        }
        if (!platformServices_->revealFile(
                QFileInfo(currentImage_.path).absoluteFilePath())) {
            showFeedback(tr("Could not show the current file in the file manager"));
        }
    }

    void setWheelAction(const WheelAction action)
    {
        wheelAction_ = action;
        QSettings().setValue(QStringLiteral("view/wheelAction"),
                             action == WheelAction::Zoom ? QStringLiteral("zoom")
                                                         : QStringLiteral("navigate"));
    }

    void toggleFullscreen()
    {
        if (isFullScreen()) {
            leaveFullscreen();
        } else {
            showFullScreen();
            showStatus(true);
        }
    }

    void leaveFullscreen()
    {
        showNormal();
        viewport_->viewport()->unsetCursor();
        showStatus(false);
    }

    void updateStatusText()
    {
        if (currentIndex_ < 0 || requestedPath_.isEmpty()) {
            return;
        }
        statusDisplay_->setText(
            tr("%1 — %2 / %3 — %4%")
                .arg(QFileInfo(requestedPath_).fileName())
                .arg(currentIndex_ + 1)
                .arg(sequence_.size())
                .arg(qRound(zoom_ * 100)));
        statusDisplay_->adjustSize();
        positionStatusDisplay();
    }

    void positionStatusDisplay()
    {
        if (statusDisplay_ == nullptr || viewport_ == nullptr) {
            return;
        }
        constexpr int BottomMargin = 12;
        const QSize viewportSize = viewport_->viewport()->size();
        statusDisplay_->move((viewportSize.width() - statusDisplay_->width()) / 2,
                             viewportSize.height() - statusDisplay_->height() - BottomMargin);
        statusDisplay_->raise();
    }

    void showStatus(const bool revealPointer)
    {
        if (currentIndex_ < 0 || !statusVisible_) {
            return;
        }
        updateStatusText();
        statusDisplay_->show();
        statusDisplay_->raise();
        if (revealPointer) {
            viewport_->viewport()->unsetCursor();
        }
        statusTimer_->start(StatusVisibilityMilliseconds);
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
        sequence_ = browsingSequence.paths();
        displayImage(browsingSequence.selectedIndex());
    }

    void openExplicitList(const QStringList &paths)
    {
        directoryWatcher_->removePaths(directoryWatcher_->directories());
        const BrowsingSequence browsingSequence = BrowsingSequence::explicitList(paths);
        sequence_ = browsingSequence.paths();
        if (sequence_.isEmpty()) {
            showFeedback(tr("No supported images in drop"));
            return;
        }
        displayImage(browsingSequence.selectedIndex());
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
        const QString selectedPath = QFileDialog::getOpenFileName(
            this, tr("Open Image"), initialDirectory,
            tr("Images (*.jpg *.jpeg *.png *.webp *.gif *.bmp)"));
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

    void displayImage(const int index)
    {
        dismissLargeImageWarning();
        rotationQuarterTurns_ = 0;
        currentIndex_ = index;
        requestedPath_ = sequence_.at(index);
        imageLoader_.setCurrentPath(requestedPath_);
        for (QAction *action : imageActions_) {
            action->setEnabled(false);
        }
        requestDecode(requestedPath_);
    }

    void retryCurrentImage()
    {
        if (currentIndex_ < 0 || requestedPath_.isEmpty()) {
            return;
        }
        errorState_->hide();
        dismissLargeImageWarning();
        retryDecode(requestedPath_);
    }

    void approveLargeImage()
    {
        const QString approvedPath = pendingLargeImagePath_;
        dismissLargeImageWarning();
        if (!approvedPath.isEmpty() && requestedPath_ == approvedPath) {
            retryDecode(approvedPath, true);
        }
    }

    void rejectLargeImage()
    {
        const bool rejectingCurrent =
            !pendingLargeImagePath_.isEmpty() && requestedPath_ == pendingLargeImagePath_;
        dismissLargeImageWarning();
        if (rejectingCurrent) {
            showEmptyState();
            showFeedback(tr("Large image decode cancelled"));
        }
    }

    void dismissLargeImageWarning()
    {
        largeImageWarning_->hide();
        pendingLargeImagePath_.clear();
    }

    void present(const QString &path, const LoadedImage &decoded)
    {
        if (decoded.frames.isEmpty() || requestedPath_ != path) {
            return;
        }
        animationTimer_->stop();
        currentImage_ = decoded;
        currentFrame_ = 0;
        completedLoops_ = 0;
        animationPaused_ = false;
        pausedDelayMilliseconds_ = 0;
        rotationQuarterTurns_ = 0;
        applyInitialZoom();
        showFrame(currentFrame_);
        showStatus(false);
        scheduleCenterView();
        if (currentImage_.frames.size() > 1) {
            animationTimer_->start(std::max(1, currentImage_.frameDelays.at(currentFrame_)));
        }
        emptyState_->hide();
        errorState_->hide();
        largeImageWarning_->hide();
        viewport_->show();
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
        setWindowTitle(tr("Flick — %1").arg(QFileInfo(path).fileName()));
        boundaryTimer_->stop();
        boundaryMessage_->hide();
        if (!pendingFeedback_.isEmpty()) {
            const QString feedback = pendingFeedback_;
            pendingFeedback_.clear();
            showFeedback(feedback);
        }
    }

    void refreshDirectorySequence()
    {
        if (directoryWatcher_->directories().isEmpty()) {
            return;
        }
        const int previousIndex = currentIndex_;
        const QString previousPath = requestedPath_;
        QStringList refreshed = BrowsingSequence::directoryBacked(
                                    directoryWatcher_->directories().constFirst(), previousPath)
                                    .paths();
        const int preservedIndex = refreshed.indexOf(previousPath);
        sequence_ = std::move(refreshed);
        if (preservedIndex >= 0) {
            currentIndex_ = preservedIndex;
            updateStatusText();
            prefetchNeighbors();
            return;
        }

        currentIndex_ = -1;
        requestedPath_.clear();
        imageLoader_.setCurrentPath({});
        if (sequence_.isEmpty()) {
            currentImage_ = {};
            image_ = {};
            animationTimer_->stop();
            setWindowTitle(QStringLiteral("Flick"));
            showEmptyState();
            showFeedback(tr("Current image is no longer available"));
            return;
        }

        pendingFeedback_ = tr("Current image is no longer available");
        displayImage(std::clamp(previousIndex, 0, int(sequence_.size()) - 1));
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
        vertical->setValue(
            qRound(originAfter.y() + imagePoint.y() * zoom_ - viewportPosition.y()));
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
        scheduleCenterView();
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
        viewport_->horizontalScrollBar()->setValue(
            viewport_->horizontalScrollBar()->maximum() / 2);
        viewport_->verticalScrollBar()->setValue(viewport_->verticalScrollBar()->maximum() / 2);
    }

    void scheduleCenterView()
    {
        QTimer::singleShot(0, this, [this] {
            centerView();
        });
    }

    void advanceAnimation()
    {
        if (currentImage_.frames.size() < 2 || animationPaused_) {
            return;
        }
        if (currentFrame_ + 1 < currentImage_.frames.size()) {
            ++currentFrame_;
        } else if (currentImage_.loopCount < 0 || completedLoops_ < currentImage_.loopCount) {
            currentFrame_ = 0;
            ++completedLoops_;
        } else {
            return;
        }
        showFrame(currentFrame_);
        animationTimer_->start(std::max(1, currentImage_.frameDelays.at(currentFrame_)));
    }

    void toggleAnimation()
    {
        if (currentImage_.frames.size() < 2) {
            return;
        }
        if (animationPaused_) {
            animationPaused_ = false;
            animationTimer_->start(std::max(1, pausedDelayMilliseconds_));
        } else {
            pausedDelayMilliseconds_ = std::max(1, animationTimer_->remainingTime());
            animationPaused_ = true;
            animationTimer_->stop();
        }
    }

    ImageLoading::DecodeRequest decodeRequest(const QString &path,
                                              const bool approvedLargeImage = false) const
    {
#ifdef FLICK_ENABLE_TEST_HARNESS
        const qint64 configuredAllocationLimit =
            qEnvironmentVariableIntValue("FLICK_TEST_LARGE_ALLOCATION_LIMIT_BYTES");
        const qint64 allocationLimit = configuredAllocationLimit > 0
                                           ? configuredAllocationLimit
                                           : LargeImageAllocationLimit;
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
            recordScheduledDecode(
                path, imageLoader_.retry(decodeRequest(path, approvedLargeImage)));
        }
    }

    void prefetchNeighbors()
    {
        const auto requestAt = [this](const int index)
            -> std::optional<ImageLoading::DecodeRequest> {
            if (index < 0 || index >= sequence_.size()) {
                return std::nullopt;
            }
            return decodeRequest(sequence_.at(index));
        };
        const QList<QString> scheduled = imageLoader_.prefetchAdjacent(
            requestAt(currentIndex_ - 1), requestAt(currentIndex_ + 1));
        for (const QString &path : scheduled) {
            recordScheduledDecode(path, true);
        }
    }

    void navigate(const int offset)
    {
        const int requestedIndex = currentIndex_ + offset;
        if (requestedIndex < 0 || requestedIndex >= sequence_.size()) {
            boundaryMessage_->setText(offset < 0 ? tr("Beginning of folder") : tr("End of folder"));
            boundaryMessage_->show();
            boundaryTimer_->start(1500);
            return;
        }
        displayImage(requestedIndex);
    }

    void showFeedback(const QString &message)
    {
        boundaryMessage_->setText(message);
        boundaryMessage_->show();
        boundaryTimer_->start(1500);
    }

    void showEmptyState()
    {
        viewport_->hide();
        errorState_->hide();
        largeImageWarning_->hide();
        emptyState_->show();
    }

    void showDecodeError(const ImageLoading::DecodeFailure &failure)
    {
        animationTimer_->stop();
        viewport_->hide();
        emptyState_->hide();
        errorExplanation_->setText(
            tr("%1 could not be displayed. You can retry or browse to another image.")
                .arg(QFileInfo(failure.path).fileName()));
        errorDetails_->setText(
            failure.details.isEmpty() ? tr("The image decoder returned no pixels.")
                                      : failure.details);
        errorDetailsButton_->setChecked(false);
        errorState_->show();
        setWindowTitle(tr("Flick — Error"));
    }

    void showLargeImageWarning(const ImageLoading::ConfirmationRequired &confirmation)
    {
        pendingLargeImageSize_ = confirmation.declaredSize;
        pendingLargeImagePath_ = confirmation.path;
        viewport_->hide();
        emptyState_->hide();
        errorState_->hide();
        largeImageExplanation_->setText(
            tr("%1 declares %2 × %3 pixels and may require about %4 MB when decoded. "
               "Decode it anyway?")
                .arg(QFileInfo(confirmation.path).fileName())
                .arg(confirmation.declaredSize.width())
                .arg(confirmation.declaredSize.height())
                .arg(confirmation.estimatedAllocationBytes / (1024 * 1024)));
        largeImageWarning_->show();
    }

    QImage image_;
    std::unique_ptr<PlatformServices> platformServices_;
    ImageLoading::Loader imageLoader_;
    ImageCanvas *imageLabel_ = nullptr;
    QScrollArea *viewport_ = nullptr;
    QLabel *boundaryMessage_ = nullptr;
    QTimer *boundaryTimer_ = nullptr;
    QTimer *animationTimer_ = nullptr;
    QFileSystemWatcher *directoryWatcher_ = nullptr;
    QLabel *emptyState_ = nullptr;
    QWidget *errorState_ = nullptr;
    QLabel *errorExplanation_ = nullptr;
    QLabel *errorDetails_ = nullptr;
    QToolButton *errorDetailsButton_ = nullptr;
    QWidget *largeImageWarning_ = nullptr;
    QLabel *largeImageExplanation_ = nullptr;
    QSize pendingLargeImageSize_;
    QString pendingLargeImagePath_;
    QLabel *statusDisplay_ = nullptr;
    QTimer *statusTimer_ = nullptr;
    QStringList sequence_;
    int currentIndex_ = -1;
    QString requestedPath_;
    QString pendingFilePickerPath_;
    QString pendingFeedback_;
    LoadedImage currentImage_;
    int currentFrame_ = 0;
    int completedLoops_ = 0;
    int pausedDelayMilliseconds_ = 0;
    bool animationPaused_ = false;
    WheelAction wheelAction_ = WheelAction::Navigate;
    double zoom_ = 1.0;
    int rotationQuarterTurns_ = 0;
    bool dragging_ = false;
    QPointF lastDragPosition_;
    QColor viewportBackground_{QStringLiteral("#202020")};
    bool statusVisible_ = true;
    bool restoreWindowGeometry_ = false;
    QColorSpace displayColorSpace_{QColorSpace::SRgb};
    QList<QAction *> imageActions_;
    QString informationText_;
#if defined(Q_OS_MACOS)
    QMenuBar *applicationMenuBar_ = nullptr;
#endif
#ifdef FLICK_ENABLE_TEST_HARNESS
    QHash<QString, int> decodeCounts_;
    bool failExternalActionsForTest_ = false;
#endif
};

#ifdef FLICK_ENABLE_TEST_HARNESS
void captureVisibleWindow(ViewerWindow &window)
{
    const QString screenshotPath = qEnvironmentVariable("FLICK_TEST_SCREENSHOT_FILE");
    if (screenshotPath.isEmpty()) {
        return;
    }
    window.grab().save(screenshotPath, "PNG");
}

void scheduleCapture(ViewerWindow &window, QObject &context, const bool waitUntilReady)
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

int main(int argc, char *argv[])
{
    FlickApplication application(argc, argv);
    QAccessible::installFactory(flickAccessibleInterface);
    QApplication::setApplicationName(QStringLiteral("Flick"));
    QApplication::setApplicationDisplayName(QStringLiteral("Flick"));
    QApplication::setApplicationVersion(QStringLiteral(FLICK_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("org.flick.Flick"));
    QApplication::setOrganizationName(QStringLiteral("Flick"));

    const QStringList arguments = application.arguments();
    const QString imagePath = arguments.size() > 1 ? arguments.at(1) : QString{};
#ifdef FLICK_ENABLE_TEST_HARNESS
    auto platformServices = std::make_unique<TestPlatformServices>();
    TestPlatformServices *testPlatformServices = platformServices.get();
#else
    auto platformServices = createPlatformServices();
#endif
    ViewerWindow window(imagePath, std::move(platformServices));
    application.setFileOpenHandler([&window](const QString &path) {
        window.openExternalFile(path);
    });
    window.show();
    window.setFocus();
#ifdef FLICK_ENABLE_TEST_HARNESS
    scheduleCapture(window, application, true);
    QSocketNotifier testCommands(STDIN_FILENO, QSocketNotifier::Read, &application);
    QObject::connect(
        &testCommands, &QSocketNotifier::activated, &application,
        [&application, &window, testPlatformServices] {
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
            } else if (input.startsWith("LastPickerDirectory")) {
                fprintf(stdout, "%s\n",
                        QSettings().value(QStringLiteral("filePicker/lastDirectory"))
                            .toString().toUtf8().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("WindowGeometry")) {
                fprintf(stdout, "%s\n", window.windowGeometryState().constData());
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
            } else if (input.startsWith("DisplayProfileChanged:")) {
                testPlatformServices->setDisplayIccProfile(
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
            } else if (input.startsWith("InformationState")) {
                fprintf(stdout, "%s\n", window.informationState().constData());
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
            } else if (input.startsWith("AccessibilityState")) {
                fprintf(stdout, "%s\n", window.accessibilityState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("RevealedPath")) {
                fprintf(stdout, "%s\n",
                        testPlatformServices->revealedPath().toUtf8().constData());
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
            } else if (input.startsWith("SelectZoomWheelAction")) {
                if (auto *action =
                        window.findChild<QAction *>(QStringLiteral("wheelZoomAction"))) {
                    action->trigger();
                }
            } else if (input.startsWith("ContextMenu:")) {
                const QList<QByteArray> parts = input.trimmed().split(':');
                if (parts.size() == 3) {
                    QWidget *target =
                        window.findChild<QLabel *>(QStringLiteral("imageLabel"));
                    const QPoint position(parts.at(1).toInt(), parts.at(2).toInt());
                    QContextMenuEvent event(QContextMenuEvent::Mouse, position,
                                            target->mapToGlobal(position));
                    QApplication::sendEvent(target, &event);
                }
            } else if (input.startsWith("MenuDown") || input.startsWith("MenuEnter")) {
                if (QWidget *menu = QApplication::activePopupWidget()) {
                    const int key =
                        input.startsWith("MenuDown") ? Qt::Key_Down : Qt::Key_Return;
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
                    QMouseEvent press(QEvent::MouseButtonPress, start, start, start,
                                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(target, &press);
                    QMouseEvent move(QEvent::MouseMove, end, end, end, Qt::NoButton,
                                     Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(target, &move);
                    QMouseEvent release(QEvent::MouseButtonRelease, end, end, end,
                                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
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
                const int qtKey = ctrlO                         ? Qt::Key_O
                                  : copyImage                   ? Qt::Key_C
                                  : copyPath                    ? Qt::Key_C
                                  : reveal                      ? Qt::Key_R
                                  : ctrlPlus                    ? Qt::Key_Plus
                                  : ctrlMinus                   ? Qt::Key_Minus
                                  : input.startsWith("Information") ? Qt::Key_I
                                  : input.startsWith("Fit")     ? Qt::Key_F
                                  : input.startsWith("ActualSize") ? Qt::Key_1
                                  : input.startsWith("ShiftLeft") ? Qt::Key_Left
                                  : input.startsWith("ShiftRight") ? Qt::Key_Right
                                  : input.startsWith("ShiftUp") ? Qt::Key_Up
                                  : input.startsWith("ShiftDown") ? Qt::Key_Down
                                  : input.startsWith("RotateLeft") ? Qt::Key_L
                                  : input.startsWith("RotateRight") ? Qt::Key_R
                                  : input.startsWith("F11")     ? Qt::Key_F11
                                  : input.startsWith("Refresh") ? Qt::Key_F5
                                  : input.startsWith("Escape")  ? Qt::Key_Escape
                                  : input.startsWith("Left")    ? Qt::Key_Left
                                  : input.startsWith("Space")   ? Qt::Key_Space
                                                                : Qt::Key_Right;
                QKeyEvent event(QEvent::KeyPress, qtKey,
                                (ctrlO || copyImage || ctrlPlus || ctrlMinus)
                                    ? Qt::ControlModifier
                                : (copyPath || reveal)
                                    ? Qt::ControlModifier | Qt::ShiftModifier
                                : shift ? Qt::ShiftModifier
                                        : Qt::NoModifier);
                QWidget *target = QApplication::focusWidget();
                QApplication::sendEvent(target != nullptr ? target : &window, &event);
            }
            scheduleCapture(window, application, !captureImmediately);
        });
#endif

    return application.exec();
}
