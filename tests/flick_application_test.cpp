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

private:
    struct RunningFlick {
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
    QString writeFixture(const QString &encodedName, const QString &imageName);
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
    QVERIFY(QFile::setPermissions(runtime, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                               | QFileDevice::ExeOwner));

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

bool FlickApplicationTest::containsColor(const QImage &image, const QColor &color, const int tolerance)
{
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            if (qAbs(pixel.red() - color.red()) <= tolerance
                && qAbs(pixel.green() - color.green()) <= tolerance
                && qAbs(pixel.blue() - color.blue()) <= tolerance) {
                return true;
            }
        }
    }
    return false;
}

void FlickApplicationTest::displaysPngInTopLevelWindow()
{
    const QString path = writeFixture(QStringLiteral("known.png.base64"), QStringLiteral("known.png"));
    QVERIFY(!path.isEmpty());

    RunningFlick flick;
    start(flick, {path});
    const QImage screenshot = waitForScreenshot(flick);
    QCOMPARE(screenshot.size(), QSize(480, 320));
    QVERIFY(containsColor(screenshot, PngFixtureColor));
}

void FlickApplicationTest::displaysJpegInTopLevelWindow()
{
    const QString path = writeFixture(QStringLiteral("known.jpg.base64"), QStringLiteral("known.jpg"));
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

QTEST_MAIN(FlickApplicationTest)
#include "flick_application_test.moc"
