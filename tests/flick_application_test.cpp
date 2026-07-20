// SPDX-License-Identifier: GPL-3.0-or-later

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

namespace {
const QColor PngFixtureColor(100, 149, 237);
const QColor JpegFixtureColor(254, 99, 71);
constexpr int JpegColorTolerance = 2;
} // namespace

class FlickApplicationTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void displaysPngInTopLevelWindow();
    void displaysJpegInTopLevelWindow();
    void displaysStableEmptyStateWithoutImage();
    void separateInvocationsRemainIndependent();
    void browsesNaturallySortedVisibleSupportedImages();

private:
    struct RunningFlick
    {
        QProcess process;
        QTemporaryDir environment;
        QString screenshotPath;

        ~RunningFlick()
        {
            if (process.state() != QProcess::NotRunning) {
                process.terminate();
                process.waitForFinished(2000);
            }
        }
    };

    void start(RunningFlick &flick, const QStringList &arguments = {});
    QImage waitForScreenshot(const RunningFlick &flick);
    QImage pressKeyAndWaitForScreenshot(RunningFlick &flick, Qt::Key key);
    QImage captureAfter(RunningFlick &flick, int delayMilliseconds);
    QString writeFixture(const QString &encodedName, const QString &imageName);
    static QString writeImage(const QTemporaryDir &directory, const QString &name,
                              const QColor &color);
    static bool containsColor(const QImage &image, const QColor &color, int tolerance = 0);

    QString executable_;
    QTemporaryDir fixtures_;
};

void FlickApplicationTest::initTestCase()
{
    executable_ = qEnvironmentVariable("FLICK_EXECUTABLE");
    QVERIFY2(!executable_.isEmpty(), "FLICK_EXECUTABLE is not set");
    QVERIFY2(QFile::exists(executable_), qPrintable(executable_));
    QVERIFY(fixtures_.isValid());
}

QString FlickApplicationTest::writeFixture(const QString &encodedName, const QString &imageName)
{
    QFile encoded(QStringLiteral(FLICK_FIXTURE_DIR "/") + encodedName);
    if (!encoded.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray fixture = QByteArray::fromBase64(encoded.readAll());
    const QString path = fixtures_.filePath(imageName);
    QFile image(path);
    if (!image.open(QIODevice::WriteOnly) || image.write(fixture) != fixture.size()) {
        return {};
    }
    return path;
}

void FlickApplicationTest::start(RunningFlick &flick, const QStringList &arguments)
{
    QVERIFY(flick.environment.isValid());
    const QString config = flick.environment.filePath(QStringLiteral("config"));
    const QString data = flick.environment.filePath(QStringLiteral("data"));
    const QString cache = flick.environment.filePath(QStringLiteral("cache"));
    const QString state = flick.environment.filePath(QStringLiteral("state"));
    const QString runtime = flick.environment.filePath(QStringLiteral("runtime"));
    QVERIFY(QDir().mkpath(config));
    QVERIFY(QDir().mkpath(data));
    QVERIFY(QDir().mkpath(cache));
    QVERIFY(QDir().mkpath(state));
    QVERIFY(QDir().mkpath(runtime));
    QVERIFY(QFile::setPermissions(runtime, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                               QFileDevice::ExeOwner));

    flick.screenshotPath = flick.environment.filePath(QStringLiteral("window.png"));
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    environment.insert(QStringLiteral("XDG_CONFIG_HOME"), config);
    environment.insert(QStringLiteral("XDG_DATA_HOME"), data);
    environment.insert(QStringLiteral("XDG_CACHE_HOME"), cache);
    environment.insert(QStringLiteral("XDG_STATE_HOME"), state);
    environment.insert(QStringLiteral("XDG_RUNTIME_DIR"), runtime);
    environment.insert(QStringLiteral("FLICK_TEST_SCREENSHOT_FILE"), flick.screenshotPath);
    flick.process.setProcessEnvironment(environment);
    flick.process.start(executable_, arguments);
    QVERIFY2(flick.process.waitForStarted(), qPrintable(flick.process.errorString()));
}

QImage FlickApplicationTest::waitForScreenshot(const RunningFlick &flick)
{
    QElapsedTimer timer;
    timer.start();
    QImage screenshot;
    while (screenshot.isNull() && timer.elapsed() < 5000) {
        QTest::qWait(20);
        screenshot.load(flick.screenshotPath);
    }
    if (screenshot.isNull()) {
        QTest::qFail("Flick did not capture its visible window", __FILE__, __LINE__);
        return {};
    }
    return screenshot;
}

QImage FlickApplicationTest::pressKeyAndWaitForScreenshot(RunningFlick &flick, const Qt::Key key)
{
    if (!QFile::remove(flick.screenshotPath)) {
        QTest::qFail("Could not remove the previous screenshot", __FILE__, __LINE__);
        return {};
    }
    const QByteArray command =
        key == Qt::Key_Left ? QByteArrayLiteral("Left\n") : QByteArrayLiteral("Right\n");
    if (flick.process.write(command) != command.size() || !flick.process.waitForBytesWritten()) {
        QTest::qFail("Could not send a key to Flick", __FILE__, __LINE__);
        return {};
    }
    return waitForScreenshot(flick);
}

QImage FlickApplicationTest::captureAfter(RunningFlick &flick, const int delayMilliseconds)
{
    QTest::qWait(delayMilliseconds);
    if (!QFile::remove(flick.screenshotPath)) {
        QTest::qFail("Could not remove the previous screenshot", __FILE__, __LINE__);
        return {};
    }
    const QByteArray command = QByteArrayLiteral("Capture\n");
    if (flick.process.write(command) != command.size() || !flick.process.waitForBytesWritten()) {
        QTest::qFail("Could not request a Flick screenshot", __FILE__, __LINE__);
        return {};
    }
    return waitForScreenshot(flick);
}

QString FlickApplicationTest::writeImage(const QTemporaryDir &directory, const QString &name,
                                         const QColor &color)
{
    QImage image(QSize(32, 24), QImage::Format_RGB32);
    image.fill(color);
    const QString path = directory.filePath(name);
    return image.save(path, "PNG") ? path : QString{};
}

bool FlickApplicationTest::containsColor(const QImage &image, const QColor &color,
                                         const int tolerance)
{
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            if (qAbs(pixel.red() - color.red()) <= tolerance &&
                qAbs(pixel.green() - color.green()) <= tolerance &&
                qAbs(pixel.blue() - color.blue()) <= tolerance) {
                return true;
            }
        }
    }
    return false;
}

void FlickApplicationTest::displaysPngInTopLevelWindow()
{
    const QString path =
        writeFixture(QStringLiteral("known.png.base64"), QStringLiteral("known.png"));
    QVERIFY(!path.isEmpty());

    RunningFlick flick;
    start(flick, {path});
    const QImage screenshot = waitForScreenshot(flick);
    QCOMPARE(screenshot.size(), QSize(480, 320));
    QVERIFY(containsColor(screenshot, PngFixtureColor));
}

void FlickApplicationTest::displaysJpegInTopLevelWindow()
{
    const QString path =
        writeFixture(QStringLiteral("known.jpg.base64"), QStringLiteral("known.jpg"));
    QVERIFY(!path.isEmpty());

    RunningFlick flick;
    start(flick, {path});
    const QImage screenshot = waitForScreenshot(flick);
    QCOMPARE(screenshot.size(), QSize(480, 320));
    QVERIFY(containsColor(screenshot, JpegFixtureColor, JpegColorTolerance));
}

void FlickApplicationTest::displaysStableEmptyStateWithoutImage()
{
    RunningFlick flick;
    start(flick);
    const QImage screenshot = waitForScreenshot(flick);
    QCOMPARE(screenshot.size(), QSize(480, 320));
    QVERIFY(!containsColor(screenshot, PngFixtureColor));
    QVERIFY(!containsColor(screenshot, JpegFixtureColor, JpegColorTolerance));
}

void FlickApplicationTest::separateInvocationsRemainIndependent()
{
    RunningFlick first;
    RunningFlick second;
    start(first);
    start(second);
    const QImage firstWindow = waitForScreenshot(first);
    const QImage secondWindow = waitForScreenshot(second);
    QVERIFY(!firstWindow.isNull());
    QVERIFY(!secondWindow.isNull());
    QVERIFY(first.process.state() == QProcess::Running);
    QVERIFY(second.process.state() == QProcess::Running);
    QVERIFY(first.process.processId() != second.process.processId());
    QCOMPARE(firstWindow, secondWindow);
}

void FlickApplicationTest::browsesNaturallySortedVisibleSupportedImages()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QColor firstColor(220, 20, 60);
    const QColor secondColor(50, 205, 50);
    const QColor tenthColor(65, 105, 225);
    const QString first = writeImage(directory, QStringLiteral("image1.png"), firstColor);
    const QString second = writeImage(directory, QStringLiteral("IMAGE2.PNG"), secondColor);
    const QString tenth = writeImage(directory, QStringLiteral("image10.jpg"), tenthColor);
    QVERIFY(!first.isEmpty());
    QVERIFY(!second.isEmpty());
    QVERIFY(!tenth.isEmpty());
    QVERIFY(!writeImage(directory, QStringLiteral(".image0.png"), QColor(Qt::black)).isEmpty());
    QFile unrelated(directory.filePath(QStringLiteral("image3.txt")));
    QVERIFY(unrelated.open(QIODevice::WriteOnly));
    QCOMPARE(unrelated.write("not an image"), 12);
    unrelated.close();

    RunningFlick flick;
    start(flick, {second});
    const QImage initial = waitForScreenshot(flick);
    QVERIFY(containsColor(initial, secondColor));

    const QImage previous = pressKeyAndWaitForScreenshot(flick, Qt::Key_Left);
    QVERIFY(containsColor(previous, firstColor));
    const QImage startBoundary = pressKeyAndWaitForScreenshot(flick, Qt::Key_Left);
    QVERIFY(containsColor(startBoundary, firstColor));
    QVERIFY(startBoundary != previous);
    QCOMPARE(captureAfter(flick, 1600), previous);

    const QImage middle = pressKeyAndWaitForScreenshot(flick, Qt::Key_Right);
    QVERIFY(containsColor(middle, secondColor));
    const QImage next = pressKeyAndWaitForScreenshot(flick, Qt::Key_Right);
    QVERIFY(containsColor(next, tenthColor));
    const QImage endBoundary = pressKeyAndWaitForScreenshot(flick, Qt::Key_Right);
    QVERIFY(containsColor(endBoundary, tenthColor));
    QVERIFY(endBoundary != next);
    QCOMPARE(captureAfter(flick, 1600), next);
}

QTEST_MAIN(FlickApplicationTest)
#include "flick_application_test.moc"
