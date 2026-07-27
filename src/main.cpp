// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform_services.h"

#include <QApplication>
#include <QAction>
#include <QActionGroup>
#include <QClipboard>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QColorSpace>
#include <QColorDialog>
#include <QCollator>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFutureWatcher>
#include <QHash>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFormLayout>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMimeData>
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
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTransform>
#include <QVBoxLayout>
#include <QWidget>
#include <QWheelEvent>
#include <QWindow>
#include <QtEndian>
#include <QtConcurrentRun>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <memory>

#ifdef FLICK_ENABLE_TEST_HARNESS
#include <QPixmap>
#include <QSocketNotifier>
#include <QUrl>
#include <unistd.h>
#endif

namespace {
constexpr auto EmptyStateText = "No image open";
constexpr qsizetype DefaultCacheBudgetBytes = 512LL * 1024 * 1024;
constexpr int StatusVisibilityMilliseconds = 2000;
constexpr qint64 LargeImagePixelLimit = 100'000'000;
constexpr qint64 LargeImageAllocationLimit = 1024LL * 1024 * 1024;

enum class WheelAction
{
    Navigate,
    Zoom
};

struct DecodedImage
{
    QString path;
    QString errorDetails;
    QSize declaredSize;
    qint64 estimatedAllocationBytes = 0;
    bool confirmationRequired = false;
    QList<QImage> frames;
    QList<int> frameDelays;
    int loopCount = 0;

    bool isNull() const
    {
        return frames.isEmpty() || frames.first().isNull();
    }

    qsizetype sizeInBytes() const
    {
        qsizetype bytes = 0;
        for (const QImage &frame : frames) {
            bytes += frame.sizeInBytes();
        }
        return bytes;
    }
};

struct AnimationMetadata
{
    QList<int> frameDelays;
    int repetitions = 0;
};

AnimationMetadata gifAnimationMetadata(const QByteArray &data)
{
    AnimationMetadata metadata;
    if (data.size() < 13) {
        return metadata;
    }
    const auto byteAt = [&data](const qsizetype index) {
        return static_cast<uchar>(data.at(index));
    };
    const auto skipSubBlocks = [&data, &byteAt](qsizetype &offset) {
        while (offset < data.size()) {
            const qsizetype blockSize = byteAt(offset++);
            if (blockSize == 0) {
                return true;
            }
            if (offset + blockSize > data.size()) {
                return false;
            }
            offset += blockSize;
        }
        return false;
    };

    qsizetype offset = 13;
    const uchar logicalScreenFlags = byteAt(10);
    if (logicalScreenFlags & 0x80) {
        offset += 3 * (1 << ((logicalScreenFlags & 0x07) + 1));
    }
    int pendingFrameDelay = 100;
    while (offset < data.size()) {
        const uchar blockType = byteAt(offset++);
        if (blockType == 0x3b) {
            break;
        }
        if (blockType == 0x2c) {
            if (offset + 9 > data.size()) {
                break;
            }
            const uchar imageFlags = byteAt(offset + 8);
            offset += 9;
            if (imageFlags & 0x80) {
                offset += 3 * (1 << ((imageFlags & 0x07) + 1));
            }
            if (offset >= data.size()) {
                break;
            }
            ++offset;
            if (!skipSubBlocks(offset)) {
                break;
            }
            metadata.frameDelays.append(pendingFrameDelay);
            pendingFrameDelay = 100;
            continue;
        }
        if (blockType != 0x21 || offset >= data.size()) {
            break;
        }
        const uchar extensionType = byteAt(offset++);
        if (extensionType == 0xf9) {
            if (offset + 6 > data.size() || byteAt(offset) != 4) {
                break;
            }
            const auto *delayBytes =
                reinterpret_cast<const uchar *>(data.constData() + offset + 2);
            pendingFrameDelay = 10 * qFromLittleEndian<quint16>(delayBytes);
            offset += 6;
            continue;
        }
        if (offset >= data.size()) {
            break;
        }
        const qsizetype headerSize = byteAt(offset++);
        if (offset + headerSize > data.size()) {
            break;
        }
        const QByteArray applicationIdentifier = data.mid(offset, headerSize);
        offset += headerSize;
        if (extensionType == 0xff && applicationIdentifier == QByteArrayLiteral("NETSCAPE2.0") &&
            offset + 5 <= data.size() && byteAt(offset) == 3 && byteAt(offset + 1) == 1) {
            const auto *loopBytes =
                reinterpret_cast<const uchar *>(data.constData() + offset + 2);
            const quint16 loopCount = qFromLittleEndian<quint16>(loopBytes);
            metadata.repetitions = loopCount == 0 ? -1 : loopCount;
        }
        if (!skipSubBlocks(offset)) {
            break;
        }
    }
    return metadata;
}

quint32 littleEndian24(const uchar *bytes)
{
    return quint32(bytes[0]) | (quint32(bytes[1]) << 8) | (quint32(bytes[2]) << 16);
}

AnimationMetadata webpAnimationMetadata(const QByteArray &data)
{
    AnimationMetadata metadata;
    qsizetype offset = 12;
    while (offset + 8 <= data.size()) {
        const QByteArray chunkName = data.mid(offset, 4);
        const auto *chunk = reinterpret_cast<const uchar *>(data.constData() + offset + 8);
        const quint32 chunkSize =
            qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(data.constData() + offset + 4));
        if (offset + 8 + chunkSize > static_cast<quint32>(data.size())) {
            break;
        }
        if (chunkName == QByteArrayLiteral("ANIM") && chunkSize >= 6) {
            const quint16 playCount = qFromLittleEndian<quint16>(chunk + 4);
            metadata.repetitions = playCount == 0 ? -1 : std::max(0, int(playCount) - 1);
        } else if (chunkName == QByteArrayLiteral("ANMF") && chunkSize >= 16) {
            metadata.frameDelays.append(int(littleEndian24(chunk + 12)));
        }
        offset += 8 + chunkSize + (chunkSize & 1U);
    }
    return metadata;
}

AnimationMetadata animationMetadata(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray data = file.readAll();
    if (data.startsWith("GIF8")) {
        return gifAnimationMetadata(data);
    }
    if (data.startsWith("RIFF") && data.mid(8, 4) == QByteArrayLiteral("WEBP")) {
        return webpAnimationMetadata(data);
    }
    return {};
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
    {
        setWindowTitle(QStringLiteral("Flick"));
        setMinimumSize(480, 320);
        setAcceptDrops(true);

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);

        imageLabel_ = new ImageCanvas;
        imageLabel_->setObjectName(QStringLiteral("imageLabel"));
        imageLabel_->setAlignment(Qt::AlignCenter);
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
        statusDisplay_->setAttribute(Qt::WA_TransparentForMouseEvents);
        statusDisplay_->setStyleSheet(QStringLiteral(
            "QLabel { color: white; background-color: rgba(0, 0, 0, 180);"
            " padding: 6px 10px; border-radius: 4px; }"));
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

        emptyState_ = new QLabel(tr(EmptyStateText));
        emptyState_->setAlignment(Qt::AlignCenter);
        emptyState_->setAccessibleName(tr(EmptyStateText));

        errorState_ = new QWidget;
        auto *errorLayout = new QVBoxLayout(errorState_);
        errorLayout->setAlignment(Qt::AlignCenter);
        errorExplanation_ = new QLabel;
        errorExplanation_->setAlignment(Qt::AlignCenter);
        errorExplanation_->setWordWrap(true);
        errorExplanation_->setAccessibleName(tr("Image error"));
        errorDetailsButton_ = new QToolButton;
        errorDetailsButton_->setText(tr("Technical details"));
        errorDetailsButton_->setCheckable(true);
        errorDetails_ = new QLabel;
        errorDetails_->setAlignment(Qt::AlignCenter);
        errorDetails_->setWordWrap(true);
        errorDetails_->setTextInteractionFlags(Qt::TextSelectableByMouse);
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
        auto *rejectLarge = new QPushButton(tr("Cancel"));
        rejectLarge->setObjectName(QStringLiteral("rejectLargeImage"));
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

        if (!imagePath.isEmpty()) {
            openDirectoryBacked(imagePath);
        }
    }

    bool isLoading() const
    {
        return decodesInFlight_.contains(requestedPath_);
    }

    void displayConfigurationChanged()
    {
        refreshDisplayColorSpace();
    }

#ifdef FLICK_ENABLE_TEST_HARNESS
    qsizetype cacheBytes() const
    {
        return cachedBytes_;
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
               QByteArray::number(cacheBudgetBytes_) + '|' +
               (restoreWindowGeometry_ ? "restore" : "forget");
    }

    QByteArray windowGeometryState() const
    {
        return QByteArray::number(width()) + 'x' + QByteArray::number(height());
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
            constexpr int KeyboardPanStep = 40;
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
        cacheBudgetBytes_ =
            settings.value(QStringLiteral("cache/budgetBytes"), DefaultCacheBudgetBytes)
                .toLongLong();
#ifdef FLICK_ENABLE_TEST_HARNESS
        const qint64 testBudget = qEnvironmentVariableIntValue("FLICK_TEST_CACHE_BUDGET_BYTES");
        if (testBudget > 0) {
            cacheBudgetBytes_ = testBudget;
        }
#endif
        if (cacheBudgetBytes_ <= 0) {
            cacheBudgetBytes_ = DefaultCacheBudgetBytes;
        }
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

    bool evictLeastRecentlyUsed()
    {
        QString oldestPath;
        quint64 oldestUse = std::numeric_limits<quint64>::max();
        for (auto it = cache_.cbegin(); it != cache_.cend(); ++it) {
            if (it.value().lastUse < oldestUse && it.key() != requestedPath_) {
                oldestPath = it.key();
                oldestUse = it.value().lastUse;
            }
        }
        if (oldestPath.isEmpty()) {
            return false;
        }
        cachedBytes_ -= cache_.value(oldestPath).decoded.sizeInBytes();
        cache_.remove(oldestPath);
        return true;
    }

    void trimCacheToBudget()
    {
        while (cachedBytes_ > cacheBudgetBytes_ && evictLeastRecentlyUsed()) {
        }
    }

    void applySettings(const WheelAction wheelAction, const QColor &background,
                       const bool statusVisible, const qsizetype cacheBudgetBytes,
                       const bool restoreWindowGeometry)
    {
        setWheelAction(wheelAction);
        viewportBackground_ = background.isValid() ? background : QColor(QStringLiteral("#202020"));
        statusVisible_ = statusVisible;
        cacheBudgetBytes_ = std::max<qsizetype>(1024 * 1024, cacheBudgetBytes);
        restoreWindowGeometry_ = restoreWindowGeometry;
        if (!statusVisible_) {
            statusTimer_->stop();
            statusDisplay_->hide();
        } else {
            showStatus(false);
        }
        applyViewportBackground();
        trimCacheToBudget();
        QSettings settings;
        settings.setValue(QStringLiteral("view/background"), viewportBackground_.name());
        settings.setValue(QStringLiteral("view/statusVisible"), statusVisible_);
        settings.setValue(QStringLiteral("cache/budgetBytes"), cacheBudgetBytes_);
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
        cache.setValue(static_cast<int>(cacheBudgetBytes_ / (1024 * 1024)));
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
        const auto addAction = [this](const QString &text, const QString &objectName,
                                      const QKeySequence &shortcut, auto operation) {
            auto *action = new QAction(text, this);
            action->setObjectName(objectName);
            action->setShortcut(shortcut);
            action->setShortcutContext(Qt::WindowShortcut);
            action->setEnabled(false);
            QObject::connect(action, &QAction::triggered, this, operation);
            viewport_->addAction(action);
            imageActions_.append(action);
        };
        addAction(tr("Information"), QStringLiteral("imageInformationAction"),
                  QKeySequence(Qt::Key_I), [this] { showInformation(); });
        addAction(tr("Copy Image"), QStringLiteral("imageCopyAction"), QKeySequence::Copy,
                  [this] { copyRenderedImage(); });
        addAction(tr("Copy Path"), QStringLiteral("imageCopyPathAction"),
                  QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C),
                  [this] { copyCurrentPath(); });
        addAction(tr("Show in File Manager"), QStringLiteral("imageRevealAction"),
                  QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R),
                  [this] { revealCurrentFile(); });
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

    static bool isSupportedImage(const QString &path)
    {
        static const QStringList supportedSuffixes = {QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                                      QStringLiteral("png"), QStringLiteral("webp"),
                                                      QStringLiteral("gif"), QStringLiteral("bmp")};
        const QFileInfo file(path);
        return file.isFile() && supportedSuffixes.contains(file.suffix(), Qt::CaseInsensitive);
    }

    static void sortNaturally(QStringList &paths)
    {
        QCollator collator(QLocale::English);
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        collator.setNumericMode(true);
        std::sort(paths.begin(), paths.end(), [&collator](const QString &left, const QString &right) {
            const QString leftName = QFileInfo(left).fileName();
            const QString rightName = QFileInfo(right).fileName();
            const int naturalOrder = collator.compare(leftName, rightName);
            return naturalOrder == 0 ? leftName < rightName : naturalOrder < 0;
        });
    }

    static QStringList directorySequenceForDirectory(const QString &directoryPath)
    {
        const QDir directory(directoryPath);
        QStringList paths;
        for (const QFileInfo &entry : directory.entryInfoList(QDir::Files)) {
            if (isSupportedImage(entry.filePath())) {
                paths.append(entry.canonicalFilePath());
            }
        }
        sortNaturally(paths);
        return paths;
    }

    static QStringList directorySequence(const QString &imagePath)
    {
        return directorySequenceForDirectory(QFileInfo(imagePath).absolutePath());
    }

    void openDirectoryBacked(const QString &path)
    {
        if (!isSupportedImage(path)) {
            showFeedback(tr("Unsupported dropped content"));
            return;
        }
        const QString canonicalPath = QFileInfo(path).canonicalFilePath();
        const QString directoryPath = QFileInfo(canonicalPath).absolutePath();
        directoryWatcher_->removePaths(directoryWatcher_->directories());
        directoryWatcher_->addPath(directoryPath);
        sequence_ = directorySequence(canonicalPath);
        const int openedIndex = sequence_.indexOf(canonicalPath);
        if (openedIndex < 0) {
            showEmptyState();
            return;
        }
        displayImage(openedIndex);
    }

    void openExplicitList(const QStringList &paths)
    {
        directoryWatcher_->removePaths(directoryWatcher_->directories());
        sequence_.clear();
        for (const QString &path : paths) {
            if (isSupportedImage(path)) {
                const QString canonicalPath = QFileInfo(path).canonicalFilePath();
                if (!sequence_.contains(canonicalPath)) {
                    sequence_.append(canonicalPath);
                }
            }
        }
        sortNaturally(sequence_);
        if (sequence_.isEmpty()) {
            showFeedback(tr("No supported images in drop"));
            return;
        }
        displayImage(0);
    }

    void openDroppedPaths(const QStringList &paths)
    {
        if (paths.size() == 1 && isSupportedImage(paths.first())) {
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
        if (!isSupportedImage(selectedPath)) {
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
        for (QAction *action : imageActions_) {
            action->setEnabled(false);
        }
        if (cache_.contains(requestedPath_)) {
            touch(requestedPath_);
            present(requestedPath_, cache_.value(requestedPath_).decoded);
            prefetchNeighbors();
            return;
        }
        decode(requestedPath_);
    }

    void retryCurrentImage()
    {
        if (currentIndex_ < 0 || requestedPath_.isEmpty()) {
            return;
        }
        if (cache_.contains(requestedPath_)) {
            cachedBytes_ -= cache_.value(requestedPath_).decoded.sizeInBytes();
            cache_.remove(requestedPath_);
        }
        errorState_->hide();
        dismissLargeImageWarning();
        decode(requestedPath_);
    }

    void approveLargeImage()
    {
        const QString approvedPath = pendingLargeImagePath_;
        dismissLargeImageWarning();
        if (!approvedPath.isEmpty() && requestedPath_ == approvedPath) {
            decode(approvedPath, true);
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

    void present(const QString &path, const DecodedImage &decoded)
    {
        if (decoded.isNull() || requestedPath_ != path) {
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
        QStringList refreshed =
            directorySequenceForDirectory(directoryWatcher_->directories().constFirst());
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
        if (!currentImage_.isNull()) {
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

    void decode(const QString &path, const bool approvedLargeImage = false)
    {
        if (path.isEmpty() || cache_.contains(path) || decodesInFlight_.contains(path)) {
            return;
        }
        decodesInFlight_.insert(path);
#ifdef FLICK_ENABLE_TEST_HARNESS
        ++decodeCounts_[path];
        const int delayMilliseconds = qEnvironmentVariableIntValue("FLICK_TEST_DECODE_DELAY_MS");
        const qint64 configuredAllocationLimit =
            qEnvironmentVariableIntValue("FLICK_TEST_LARGE_ALLOCATION_LIMIT_BYTES");
        const qint64 allocationLimit = configuredAllocationLimit > 0
                                           ? configuredAllocationLimit
                                           : LargeImageAllocationLimit;
#else
        constexpr int delayMilliseconds = 0;
        constexpr qint64 allocationLimit = LargeImageAllocationLimit;
#endif
        auto *watcher = new QFutureWatcher<DecodedImage>(this);
        QObject::connect(watcher, &QFutureWatcher<DecodedImage>::finished, this,
                         [this, watcher] {
                             const DecodedImage decoded = watcher->result();
                             watcher->deleteLater();
                             decodesInFlight_.remove(decoded.path);
                             if (decoded.confirmationRequired) {
                                 if (requestedPath_ == decoded.path) {
                                     showLargeImageWarning(decoded);
                                 }
                                 return;
                             }
                             if (!decoded.isNull()) {
                                 insertCache(decoded.path, decoded);
                             }
                             if (requestedPath_ == decoded.path) {
                                 if (decoded.isNull()) {
                                     showDecodeError(decoded);
                                 } else {
                                     present(decoded.path, decoded);
                                     prefetchNeighbors();
                                 }
                             }
                         });
        watcher->setFuture(
            QtConcurrent::run([path, delayMilliseconds, approvedLargeImage, allocationLimit] {
#ifdef FLICK_ENABLE_TEST_HARNESS
            if (delayMilliseconds > 0) {
                QThread::msleep(static_cast<unsigned long>(delayMilliseconds));
            }
#endif
            QImageReader reader(path);
            reader.setAutoTransform(true);
            DecodedImage decoded;
            decoded.path = path;
            decoded.declaredSize = reader.size();
            if (decoded.declaredSize.isValid()) {
                const qint64 pixels = qint64(decoded.declaredSize.width()) *
                                      qint64(decoded.declaredSize.height());
                const qint64 bytesPerFrame =
                    pixels > std::numeric_limits<qint64>::max() / 4
                        ? std::numeric_limits<qint64>::max()
                        : pixels * 4;
                const qint64 frameCount = std::max(1, reader.imageCount());
                decoded.estimatedAllocationBytes =
                    bytesPerFrame > std::numeric_limits<qint64>::max() / frameCount
                        ? std::numeric_limits<qint64>::max()
                        : bytesPerFrame * frameCount;
                decoded.confirmationRequired =
                    !approvedLargeImage &&
                    (pixels > LargeImagePixelLimit ||
                     decoded.estimatedAllocationBytes > allocationLimit);
                if (decoded.confirmationRequired) {
                    return decoded;
                }
            }
            const AnimationMetadata metadata = animationMetadata(path);
            decoded.loopCount = metadata.repetitions;
            while (reader.canRead()) {
                const QImage frame = reader.read();
                if (frame.isNull()) {
                    break;
                }
                QImage colorManagedFrame = frame;
                if (!colorManagedFrame.colorSpace().isValid()) {
                    colorManagedFrame.setColorSpace(QColorSpace(QColorSpace::SRgb));
                }
                decoded.frames.append(std::move(colorManagedFrame));
                const qsizetype frameIndex = decoded.frames.size() - 1;
                decoded.frameDelays.append(frameIndex < metadata.frameDelays.size()
                                               ? metadata.frameDelays.at(frameIndex)
                                               : 100);
            }
            if (decoded.isNull()) {
                decoded.errorDetails = reader.errorString();
                if (decoded.errorDetails.isEmpty()) {
                    decoded.errorDetails = QStringLiteral("The image decoder returned no pixels.");
                }
            }
            return decoded;
            }));
    }

    struct CacheEntry
    {
        DecodedImage decoded;
        quint64 lastUse = 0;
    };

    void touch(const QString &path)
    {
        cache_[path].lastUse = ++accessCounter_;
    }

    void insertCache(const QString &path, const DecodedImage &decoded)
    {
        const qsizetype bytes = decoded.sizeInBytes();
        if (bytes > cacheBudgetBytes_) {
            return;
        }
        if (cache_.contains(path)) {
            cachedBytes_ -= cache_.value(path).decoded.sizeInBytes();
        }
        cache_.insert(path, CacheEntry{decoded, ++accessCounter_});
        cachedBytes_ += bytes;
        trimCacheToBudget();
    }

    void prefetchNeighbors()
    {
        for (const int neighbor : {currentIndex_ - 1, currentIndex_ + 1}) {
            if (neighbor >= 0 && neighbor < sequence_.size()) {
                decode(sequence_.at(neighbor));
            }
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

    void showDecodeError(const DecodedImage &decoded)
    {
        animationTimer_->stop();
        viewport_->hide();
        emptyState_->hide();
        errorExplanation_->setText(
            tr("%1 could not be displayed. You can retry or browse to another image.")
                .arg(QFileInfo(decoded.path).fileName()));
        errorDetails_->setText(decoded.errorDetails);
        errorDetailsButton_->setChecked(false);
        errorState_->show();
        setWindowTitle(tr("Flick — Error"));
    }

    void showLargeImageWarning(const DecodedImage &decoded)
    {
        pendingLargeImageSize_ = decoded.declaredSize;
        pendingLargeImagePath_ = decoded.path;
        viewport_->hide();
        emptyState_->hide();
        errorState_->hide();
        largeImageExplanation_->setText(
            tr("%1 declares %2 × %3 pixels and may require about %4 MB when decoded. "
               "Decode it anyway?")
                .arg(QFileInfo(decoded.path).fileName())
                .arg(decoded.declaredSize.width())
                .arg(decoded.declaredSize.height())
                .arg(decoded.estimatedAllocationBytes / (1024 * 1024)));
        largeImageWarning_->show();
    }

    QImage image_;
    std::unique_ptr<PlatformServices> platformServices_;
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
    DecodedImage currentImage_;
    int currentFrame_ = 0;
    int completedLoops_ = 0;
    int pausedDelayMilliseconds_ = 0;
    bool animationPaused_ = false;
    WheelAction wheelAction_ = WheelAction::Navigate;
    double zoom_ = 1.0;
    int rotationQuarterTurns_ = 0;
    bool dragging_ = false;
    QPointF lastDragPosition_;
    QHash<QString, CacheEntry> cache_;
    QSet<QString> decodesInFlight_;
    qsizetype cachedBytes_ = 0;
    qsizetype cacheBudgetBytes_ = DefaultCacheBudgetBytes;
    QColor viewportBackground_{QStringLiteral("#202020")};
    bool statusVisible_ = true;
    bool restoreWindowGeometry_ = false;
    QColorSpace displayColorSpace_{QColorSpace::SRgb};
    quint64 accessCounter_ = 0;
    QList<QAction *> imageActions_;
    QString informationText_;
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
    *capture = [&window, &context, waitUntilReady, capture] {
        if (waitUntilReady && window.isLoading()) {
            QTimer::singleShot(10, &context, *capture);
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
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Flick"));
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
            } else if (input.startsWith("SettingsState")) {
                fprintf(stdout, "%s\n", window.settingsState().constData());
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
