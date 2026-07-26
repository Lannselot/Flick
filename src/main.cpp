// SPDX-License-Identifier: GPL-3.0-or-later

#include <QApplication>
#include <QAction>
#include <QActionGroup>
#include <QCollator>
#include <QContextMenuEvent>
#include <QFutureWatcher>
#include <QHash>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMimeData>
#include <QMouseEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSet>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QWheelEvent>
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

enum class WheelAction
{
    Navigate,
    Zoom
};

struct DecodedImage
{
    QString path;
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

class ViewerWindow final : public QWidget
{
public:
    explicit ViewerWindow(const QString &imagePath)
    {
#ifdef FLICK_ENABLE_TEST_HARNESS
        const qint64 testBudget = qEnvironmentVariableIntValue("FLICK_TEST_CACHE_BUDGET_BYTES");
        if (testBudget > 0) {
            cacheBudgetBytes_ = testBudget;
        }
#endif
        setWindowTitle(QStringLiteral("Flick"));
        setMinimumSize(480, 320);
        setAcceptDrops(true);

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);

        imageLabel_ = new QLabel;
        imageLabel_->setAlignment(Qt::AlignCenter);

        viewport_ = new QScrollArea;
        viewport_->setAlignment(Qt::AlignCenter);
        viewport_->setBackgroundRole(QPalette::Dark);
        viewport_->setFocusPolicy(Qt::NoFocus);
        viewport_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        viewport_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        viewport_->setWidget(imageLabel_);
        viewport_->viewport()->installEventFilter(this);
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

        emptyState_ = new QLabel(tr(EmptyStateText));
        emptyState_->setAlignment(Qt::AlignCenter);
        emptyState_->setAccessibleName(tr(EmptyStateText));

        layout->addWidget(viewport_);
        layout->addWidget(emptyState_);
        layout->addWidget(boundaryMessage_);
        showEmptyState();

        if (!imagePath.isEmpty()) {
            openDirectoryBacked(imagePath);
        }
    }

    bool isLoading() const
    {
        return decodesInFlight_.contains(requestedPath_);
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
#endif

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
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
        if (event->key() == Qt::Key_O && event->modifiers().testFlag(Qt::ControlModifier)) {
            openFromFilePicker();
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
    void setWheelAction(const WheelAction action)
    {
        wheelAction_ = action;
        QSettings().setValue(QStringLiteral("view/wheelAction"),
                             action == WheelAction::Zoom ? QStringLiteral("zoom")
                                                         : QStringLiteral("navigate"));
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

    static QStringList directorySequence(const QString &imagePath)
    {
        const QDir directory = QFileInfo(imagePath).absoluteDir();
        QStringList paths;
        for (const QFileInfo &entry : directory.entryInfoList(QDir::Files)) {
            if (isSupportedImage(entry.filePath())) {
                paths.append(entry.canonicalFilePath());
            }
        }
        sortNaturally(paths);
        return paths;
    }

    void openDirectoryBacked(const QString &path)
    {
        if (!isSupportedImage(path)) {
            showFeedback(tr("Unsupported dropped content"));
            return;
        }
        const QString canonicalPath = QFileInfo(path).canonicalFilePath();
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
        QSettings().setValue(QStringLiteral("filePicker/lastDirectory"),
                             QFileInfo(selectedPath).absolutePath());
        openDirectoryBacked(selectedPath);
    }

    void displayImage(const int index)
    {
        currentIndex_ = index;
        requestedPath_ = sequence_.at(index);
        if (cache_.contains(requestedPath_)) {
            touch(requestedPath_);
            present(requestedPath_, cache_.value(requestedPath_).decoded);
            prefetchNeighbors();
            return;
        }
        decode(requestedPath_);
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
        applyInitialZoom();
        showFrame(currentFrame_);
        scheduleCenterView();
        if (currentImage_.frames.size() > 1) {
            animationTimer_->start(std::max(1, currentImage_.frameDelays.at(currentFrame_)));
        }
        emptyState_->hide();
        viewport_->show();
        setWindowTitle(tr("Flick — %1").arg(QFileInfo(path).fileName()));
        boundaryTimer_->stop();
        boundaryMessage_->hide();
    }

    void showFrame(const int index)
    {
        image_ = currentImage_.frames.at(index);
        renderImage();
    }

    double fitZoom() const
    {
        if (image_.isNull()) {
            return 1.0;
        }
        const QSize available = viewport_->viewport()->size();
        return std::min(double(available.width()) / image_.width(),
                        double(available.height()) / image_.height());
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

    void renderImage()
    {
        const QSize displayedSize = displayedImageSize();
        imageLabel_->setPixmap(
            QPixmap::fromImage(image_).scaled(displayedSize, Qt::IgnoreAspectRatio,
                                               Qt::SmoothTransformation));
        imageLabel_->setFixedSize(displayedSize + viewport_->viewport()->size());
    }

    QSize displayedImageSize() const
    {
        return QSize(qMax(1, qRound(image_.width() * zoom_)),
                     qMax(1, qRound(image_.height() * zoom_)));
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

    void decode(const QString &path)
    {
        if (path.isEmpty() || cache_.contains(path) || decodesInFlight_.contains(path)) {
            return;
        }
        decodesInFlight_.insert(path);
#ifdef FLICK_ENABLE_TEST_HARNESS
        ++decodeCounts_[path];
        const int delayMilliseconds = qEnvironmentVariableIntValue("FLICK_TEST_DECODE_DELAY_MS");
#else
        constexpr int delayMilliseconds = 0;
#endif
        auto *watcher = new QFutureWatcher<DecodedImage>(this);
        QObject::connect(watcher, &QFutureWatcher<DecodedImage>::finished, this,
                         [this, watcher] {
                             const DecodedImage decoded = watcher->result();
                             watcher->deleteLater();
                             decodesInFlight_.remove(decoded.path);
                             if (!decoded.isNull()) {
                                 insertCache(decoded.path, decoded);
                             }
                             if (requestedPath_ == decoded.path) {
                                 if (decoded.isNull()) {
                                     showEmptyState();
                                 } else {
                                     present(decoded.path, decoded);
                                     prefetchNeighbors();
                                 }
                             }
                         });
        watcher->setFuture(QtConcurrent::run([path, delayMilliseconds] {
#ifdef FLICK_ENABLE_TEST_HARNESS
            if (delayMilliseconds > 0) {
                QThread::msleep(static_cast<unsigned long>(delayMilliseconds));
            }
#endif
            QImageReader reader(path);
            reader.setAutoTransform(true);
            DecodedImage decoded;
            decoded.path = path;
            const AnimationMetadata metadata = animationMetadata(path);
            decoded.loopCount = metadata.repetitions;
            while (reader.canRead()) {
                const QImage frame = reader.read();
                if (frame.isNull()) {
                    break;
                }
                decoded.frames.append(frame);
                const qsizetype frameIndex = decoded.frames.size() - 1;
                decoded.frameDelays.append(frameIndex < metadata.frameDelays.size()
                                               ? metadata.frameDelays.at(frameIndex)
                                               : 100);
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
        while (cachedBytes_ > cacheBudgetBytes_ && !cache_.isEmpty()) {
            QString oldestPath;
            quint64 oldestUse = std::numeric_limits<quint64>::max();
            for (auto it = cache_.cbegin(); it != cache_.cend(); ++it) {
                if (it.value().lastUse < oldestUse && it.key() != requestedPath_) {
                    oldestPath = it.key();
                    oldestUse = it.value().lastUse;
                }
            }
            if (oldestPath.isEmpty()) {
                break;
            }
            cachedBytes_ -= cache_.value(oldestPath).decoded.sizeInBytes();
            cache_.remove(oldestPath);
        }
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
        emptyState_->show();
    }

    QImage image_;
    QLabel *imageLabel_ = nullptr;
    QScrollArea *viewport_ = nullptr;
    QLabel *boundaryMessage_ = nullptr;
    QTimer *boundaryTimer_ = nullptr;
    QTimer *animationTimer_ = nullptr;
    QLabel *emptyState_ = nullptr;
    QStringList sequence_;
    int currentIndex_ = -1;
    QString requestedPath_;
    DecodedImage currentImage_;
    int currentFrame_ = 0;
    int completedLoops_ = 0;
    int pausedDelayMilliseconds_ = 0;
    bool animationPaused_ = false;
    WheelAction wheelAction_ = WheelAction::Navigate;
    double zoom_ = 1.0;
    bool dragging_ = false;
    QPointF lastDragPosition_;
    QHash<QString, CacheEntry> cache_;
    QSet<QString> decodesInFlight_;
    qsizetype cachedBytes_ = 0;
    qsizetype cacheBudgetBytes_ = DefaultCacheBudgetBytes;
    quint64 accessCounter_ = 0;
#ifdef FLICK_ENABLE_TEST_HARNESS
    QHash<QString, int> decodeCounts_;
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
    ViewerWindow window(imagePath);
    window.show();
    window.setFocus();
#ifdef FLICK_ENABLE_TEST_HARNESS
    scheduleCapture(window, application, true);
    QSocketNotifier testCommands(STDIN_FILENO, QSocketNotifier::Read, &application);
    QObject::connect(
        &testCommands, &QSocketNotifier::activated, &application, [&application, &window] {
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
            } else if (input.startsWith("ViewState")) {
                fprintf(stdout, "%s\n", window.viewState().constData());
                fflush(stdout);
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
                    QWidget *target = window.findChild<QScrollArea *>()->viewport();
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
            } else if (!input.startsWith("Capture")) {
                const bool ctrlO = input.startsWith("CtrlO");
                const bool shift = input.startsWith("Shift");
                const int qtKey = ctrlO                         ? Qt::Key_O
                                  : input.startsWith("Fit")     ? Qt::Key_F
                                  : input.startsWith("ActualSize") ? Qt::Key_1
                                  : input.startsWith("ShiftLeft") ? Qt::Key_Left
                                  : input.startsWith("ShiftRight") ? Qt::Key_Right
                                  : input.startsWith("ShiftUp") ? Qt::Key_Up
                                  : input.startsWith("ShiftDown") ? Qt::Key_Down
                                  : input.startsWith("Left")    ? Qt::Key_Left
                                  : input.startsWith("Space")   ? Qt::Key_Space
                                                                : Qt::Key_Right;
                QKeyEvent event(QEvent::KeyPress, qtKey,
                                ctrlO    ? Qt::ControlModifier
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
