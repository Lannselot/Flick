// SPDX-License-Identifier: GPL-3.0-or-later

#include <QApplication>
#include <QCollator>
#include <QFutureWatcher>
#include <QHash>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMimeData>
#include <QScrollArea>
#include <QSettings>
#include <QSet>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QtConcurrentRun>

#include <algorithm>
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

struct DecodedImage
{
    QString path;
    QImage image;
};

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
        viewport_->setWidget(imageLabel_);
        setFocusPolicy(Qt::StrongFocus);

        boundaryMessage_ = new QLabel;
        boundaryMessage_->setAlignment(Qt::AlignCenter);
        boundaryMessage_->setAccessibleName(tr("Sequence boundary message"));
        boundaryMessage_->hide();
        boundaryTimer_ = new QTimer(this);
        boundaryTimer_->setSingleShot(true);
        QObject::connect(boundaryTimer_, &QTimer::timeout, boundaryMessage_, &QWidget::hide);

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
#endif

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_O && event->modifiers().testFlag(Qt::ControlModifier)) {
            openFromFilePicker();
            return;
        }
        if (event->key() == Qt::Key_Left) {
            navigate(-1);
            return;
        }
        if (event->key() == Qt::Key_Right) {
            navigate(1);
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
            present(requestedPath_, cache_.value(requestedPath_).image);
            prefetchNeighbors();
            return;
        }
        decode(requestedPath_);
    }

    void present(const QString &path, const QImage &image)
    {
        if (image.isNull() || requestedPath_ != path) {
            return;
        }
        image_ = image;
        imageLabel_->setPixmap(QPixmap::fromImage(image_));
        imageLabel_->setMinimumSize(image_.size());
        emptyState_->hide();
        viewport_->show();
        setWindowTitle(tr("Flick — %1").arg(QFileInfo(path).fileName()));
        boundaryTimer_->stop();
        boundaryMessage_->hide();
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
                             if (!decoded.image.isNull()) {
                                 insertCache(decoded.path, decoded.image);
                             }
                             if (requestedPath_ == decoded.path) {
                                 if (decoded.image.isNull()) {
                                     showEmptyState();
                                 } else {
                                     present(decoded.path, decoded.image);
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
            return DecodedImage{path, reader.read()};
        }));
    }

    struct CacheEntry
    {
        QImage image;
        quint64 lastUse = 0;
    };

    void touch(const QString &path)
    {
        cache_[path].lastUse = ++accessCounter_;
    }

    void insertCache(const QString &path, const QImage &image)
    {
        const qsizetype bytes = image.sizeInBytes();
        if (bytes > cacheBudgetBytes_) {
            return;
        }
        if (cache_.contains(path)) {
            cachedBytes_ -= cache_.value(path).image.sizeInBytes();
        }
        cache_.insert(path, CacheEntry{image, ++accessCounter_});
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
            cachedBytes_ -= cache_.value(oldestPath).image.sizeInBytes();
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
    QLabel *emptyState_ = nullptr;
    QStringList sequence_;
    int currentIndex_ = -1;
    QString requestedPath_;
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
    auto initialCapture = std::make_shared<std::function<void()>>();
    *initialCapture = [&application, &window, initialCapture] {
        if (window.isLoading()) {
            QTimer::singleShot(10, &application, *initialCapture);
            return;
        }
        captureVisibleWindow(window);
    };
    QTimer::singleShot(0, &application, *initialCapture);
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
            } else if (!input.startsWith("Capture")) {
                const bool ctrlO = input.startsWith("CtrlO");
                const int qtKey = ctrlO ? Qt::Key_O
                                        : input.startsWith("Left") ? Qt::Key_Left : Qt::Key_Right;
                QKeyEvent event(QEvent::KeyPress, qtKey,
                                ctrlO ? Qt::ControlModifier : Qt::NoModifier);
                QWidget *target = QApplication::focusWidget();
                QApplication::sendEvent(target != nullptr ? target : &window, &event);
            }
            auto captureWhenReady = std::make_shared<std::function<void()>>();
            *captureWhenReady = [&application, &window, captureImmediately, captureWhenReady] {
                if (!captureImmediately && window.isLoading()) {
                    QTimer::singleShot(10, &application, *captureWhenReady);
                    return;
                }
                captureVisibleWindow(window);
            };
            QTimer::singleShot(0, &application, *captureWhenReady);
        });
#endif

    return application.exec();
}
