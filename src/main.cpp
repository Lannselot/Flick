// SPDX-License-Identifier: GPL-3.0-or-later

#include <QApplication>
#include <QFileInfo>
#include <QImageReader>
#include <QLabel>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#ifdef FLICK_ENABLE_TEST_HARNESS
#include <QPixmap>
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

        if (imagePath.isEmpty()) {
            showEmptyState(layout);
            return;
        }

        QImageReader reader(imagePath);
        reader.setAutoTransform(true);
        image_ = reader.read();
        if (image_.isNull()) {
            showEmptyState(layout);
            return;
        }

        const QString canonicalPath = QFileInfo(imagePath).canonicalFilePath();
        setWindowTitle(tr("Flick — %1").arg(QFileInfo(canonicalPath).fileName()));

        auto *imageLabel = new QLabel;
        imageLabel->setAlignment(Qt::AlignCenter);
        imageLabel->setPixmap(QPixmap::fromImage(image_));
        imageLabel->setMinimumSize(image_.size());

        auto *viewport = new QScrollArea;
        viewport->setAlignment(Qt::AlignCenter);
        viewport->setBackgroundRole(QPalette::Dark);
        viewport->setWidget(imageLabel);
        layout->addWidget(viewport);
    }

private:
    void showEmptyState(QVBoxLayout *layout)
    {
        const QString emptyText = tr(EmptyStateText);
        auto *emptyState = new QLabel(emptyText);
        emptyState->setAlignment(Qt::AlignCenter);
        emptyState->setAccessibleName(emptyText);
        layout->addWidget(emptyState);
    }

    QImage image_;
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
#ifdef FLICK_ENABLE_TEST_HARNESS
    QTimer::singleShot(0, &application, [&window] { captureVisibleWindow(window); });
#endif

    return application.exec();
}
