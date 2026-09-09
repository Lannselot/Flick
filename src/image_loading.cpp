// SPDX-License-Identifier: GPL-3.0-or-later

#include "image_loading.h"

#include <QColorSpace>
#include <QFile>
#include <QFutureWatcher>
#include <QImageReader>
#include <QSet>
#include <QThread>
#include <QtEndian>
#include <QtConcurrentRun>

#include <algorithm>
#include <limits>

namespace {
constexpr qint64 LargeImagePixelLimit = 100'000'000;

int testDecodeDelayMilliseconds(const QString &path)
{
#ifdef FLICK_ENABLE_TEST_HARNESS
    const QString delayedPath = qEnvironmentVariable("FLICK_TEST_DECODE_DELAY_PATH");
    if (!delayedPath.isEmpty() && delayedPath != path) {
        return 0;
    }
    return qEnvironmentVariableIntValue("FLICK_TEST_DECODE_DELAY_MS");
#else
    Q_UNUSED(path)
    return 0;
#endif
}

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
        const quint32 chunkSize = qFromLittleEndian<quint32>(
            reinterpret_cast<const uchar *>(data.constData() + offset + 4));
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
} // namespace

namespace ImageLoading {

qsizetype LoadedImage::sizeInBytes() const
{
    qsizetype bytes = 0;
    for (const QImage &frame : frames) {
        bytes += frame.sizeInBytes();
    }
    return bytes;
}

DecodeOutcome decode(const DecodeRequest &request)
{
    QImageReader reader(request.path);
    reader.setAutoTransform(true);
    const QSize declaredSize = reader.size();
    qint64 estimatedAllocationBytes = 0;
    if (declaredSize.isValid()) {
        const qint64 pixels = qint64(declaredSize.width()) * qint64(declaredSize.height());
        const qint64 bytesPerFrame = pixels > std::numeric_limits<qint64>::max() / 4
                                         ? std::numeric_limits<qint64>::max()
                                         : pixels * 4;
        const qint64 frameCount = std::max(1, reader.imageCount());
        estimatedAllocationBytes =
            bytesPerFrame > std::numeric_limits<qint64>::max() / frameCount
                ? std::numeric_limits<qint64>::max()
                : bytesPerFrame * frameCount;
        if (!request.exceptionalDimensionsApproved &&
            (pixels > LargeImagePixelLimit ||
             estimatedAllocationBytes > request.allocationLimitBytes)) {
            return ConfirmationRequired{request.path, declaredSize, estimatedAllocationBytes};
        }
    }

    const AnimationMetadata metadata = animationMetadata(request.path);
    LoadedImage loaded{request.path, {}, {}, metadata.repetitions};
    while (reader.canRead()) {
        QImage frame = reader.read();
        if (frame.isNull()) {
            break;
        }
        if (!frame.colorSpace().isValid()) {
            frame.setColorSpace(QColorSpace(QColorSpace::SRgb));
        }
        loaded.frames.append(std::move(frame));
        const qsizetype frameIndex = loaded.frames.size() - 1;
        loaded.frameDelays.append(frameIndex < metadata.frameDelays.size()
                                      ? metadata.frameDelays.at(frameIndex)
                                      : 100);
    }
    if (loaded.frames.isEmpty()) {
        return DecodeFailure{request.path, reader.errorString()};
    }
    return loaded;
}

class Loader::Impl
{
public:
    explicit Impl(Loader *owner)
        : owner(owner)
    {
    }

    bool schedule(const DecodeRequest &request, const bool makeCurrent)
    {
        if (request.path.isEmpty()) {
            return false;
        }
        if (makeCurrent) {
            currentPath = request.path;
        }
        if (inFlight.contains(request.path)) {
            return false;
        }
        inFlight.insert(request.path);
        auto *watcher = new QFutureWatcher<DecodeOutcome>(owner);
        QObject::connect(watcher, &QFutureWatcher<DecodeOutcome>::finished, owner,
                         [this, watcher] {
                             DecodeOutcome outcome = watcher->result();
                             watcher->deleteLater();
                             const QString path = std::visit(
                                 [](const auto &result) { return result.path; }, outcome);
                             inFlight.remove(path);
                             if (const auto *loaded = std::get_if<LoadedImage>(&outcome);
                                 loaded && loadedHandler) {
                                 loadedHandler(*loaded);
                             }
                             if (path == currentPath && outcomeHandler) {
                                 outcomeHandler(std::move(outcome));
                             }
                         });
        const int delay = testDecodeDelayMilliseconds(request.path);
        watcher->setFuture(QtConcurrent::run([request, delay] {
            if (delay > 0) {
                QThread::msleep(static_cast<unsigned long>(delay));
            }
            return decode(request);
        }));
        return true;
    }

    Loader *owner;
    QString currentPath;
    QSet<QString> inFlight;
    OutcomeHandler outcomeHandler;
    LoadedHandler loadedHandler;
};

Loader::Loader(QObject *parent)
    : QObject(parent)
    , impl_(std::make_unique<Impl>(this))
{
}

Loader::~Loader() = default;

void Loader::setOutcomeHandler(OutcomeHandler handler)
{
    impl_->outcomeHandler = std::move(handler);
}

void Loader::setLoadedHandler(LoadedHandler handler)
{
    impl_->loadedHandler = std::move(handler);
}

void Loader::setCurrentPath(const QString &path)
{
    impl_->currentPath = path;
}

bool Loader::request(const DecodeRequest &request)
{
    return impl_->schedule(request, true);
}

bool Loader::prefetch(const DecodeRequest &request)
{
    return impl_->schedule(request, false);
}

bool Loader::retry(const DecodeRequest &request)
{
    return impl_->schedule(request, true);
}

bool Loader::isLoading(const QString &path) const
{
    return impl_->inFlight.contains(path);
}

bool Loader::hasRequestsInFlight() const
{
    return !impl_->inFlight.isEmpty();
}

qsizetype Loader::requestsInFlight() const
{
    return impl_->inFlight.size();
}

} // namespace ImageLoading
