// SPDX-License-Identifier: GPL-3.0-or-later

#include <QApplication>
#include <QCollator>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#ifdef FLICK_ENABLE_TEST_HARNESS
#include <QPixmap>
#include <QSocketNotifier>
#include <unistd.h>
#endif

namespace {
constexpr auto EmptyStateText = "No image open";

class ViewerWindow final : public QWidget
{
public:
    explicit ViewerWindow(const QString &imagePath)
    {
        setWindowTitle(QStringLiteral("Flick"));
        setMinimumSize(480, 320);

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

        if (imagePath.isEmpty()) {
            showEmptyState(layout);
            return;
        }

        sequence_ = directorySequence(imagePath);
        const QString canonicalPath = QFileInfo(imagePath).canonicalFilePath();
        const int openedIndex = sequence_.indexOf(canonicalPath);
        if (openedIndex < 0 || !displayImage(openedIndex)) {
            showEmptyState(layout);
            return;
        }

        layout->addWidget(viewport_);
        layout->addWidget(boundaryMessage_);
    }

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
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

private:
    static QStringList directorySequence(const QString &imagePath)
    {
        static const QStringList supportedSuffixes = {QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                                      QStringLiteral("png"), QStringLiteral("webp"),
                                                      QStringLiteral("gif"), QStringLiteral("bmp")};
        const QDir directory = QFileInfo(imagePath).absoluteDir();
        QStringList paths;
        for (const QFileInfo &entry : directory.entryInfoList(QDir::Files)) {
            if (supportedSuffixes.contains(entry.suffix(), Qt::CaseInsensitive)) {
                paths.append(entry.canonicalFilePath());
            }
        }
        QCollator collator(QLocale::English);
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        collator.setNumericMode(true);
        std::sort(paths.begin(), paths.end(),
                  [&collator](const QString &left, const QString &right) {
                      const QString leftName = QFileInfo(left).fileName();
                      const QString rightName = QFileInfo(right).fileName();
                      const int naturalOrder = collator.compare(leftName, rightName);
                      return naturalOrder == 0 ? leftName < rightName : naturalOrder < 0;
                  });
        return paths;
    }

    bool displayImage(const int index)
    {
        QImageReader reader(sequence_.at(index));
        reader.setAutoTransform(true);
        const QImage image = reader.read();
        if (image.isNull()) {
            return false;
        }
        currentIndex_ = index;
        image_ = image;
        imageLabel_->setPixmap(QPixmap::fromImage(image_));
        imageLabel_->setMinimumSize(image_.size());
        setWindowTitle(tr("Flick — %1").arg(QFileInfo(sequence_.at(currentIndex_)).fileName()));
        boundaryTimer_->stop();
        boundaryMessage_->hide();
        return true;
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

    void showEmptyState(QVBoxLayout *layout)
    {
        const QString emptyText = tr(EmptyStateText);
        auto *emptyState = new QLabel(emptyText);
        emptyState->setAlignment(Qt::AlignCenter);
        emptyState->setAccessibleName(emptyText);
        layout->addWidget(emptyState);
    }

    QImage image_;
    QLabel *imageLabel_ = nullptr;
    QScrollArea *viewport_ = nullptr;
    QLabel *boundaryMessage_ = nullptr;
    QTimer *boundaryTimer_ = nullptr;
    QStringList sequence_;
    int currentIndex_ = -1;
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
    QTimer::singleShot(0, &application, [&window] { captureVisibleWindow(window); });
    QSocketNotifier testCommands(STDIN_FILENO, QSocketNotifier::Read, &application);
    QObject::connect(
        &testCommands, &QSocketNotifier::activated, &application, [&application, &window] {
            char command[16] = {};
            const auto bytesRead = ::read(STDIN_FILENO, command, sizeof(command));
            if (bytesRead <= 0) {
                return;
            }
            const QByteArray key(command, bytesRead);
            if (!key.startsWith("Capture")) {
                const int qtKey = key.startsWith("Left") ? Qt::Key_Left : Qt::Key_Right;
                QKeyEvent event(QEvent::KeyPress, qtKey, Qt::NoModifier);
                QWidget *target = QApplication::focusWidget();
                QApplication::sendEvent(target != nullptr ? target : &window, &event);
            }
            QTimer::singleShot(0, &application, [&window] { captureVisibleWindow(window); });
        });
#endif

    return application.exec();
}
