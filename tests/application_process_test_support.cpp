// SPDX-License-Identifier: GPL-3.0-or-later

#include "application_process_test_support.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTest>

ApplicationProcessTest::RunningFlick::~RunningFlick()
{
    if (process.state() != QProcess::NotRunning) {
        process.terminate();
        process.waitForFinished(2000);
    }
}

void ApplicationProcessTest::initializeProcessTest()
{
    executable_ = qEnvironmentVariable("FLICK_EXECUTABLE");
    QVERIFY2(!executable_.isEmpty(), "FLICK_EXECUTABLE is not set");
    QVERIFY2(QFile::exists(executable_), qPrintable(executable_));
    QVERIFY(fixtures_.isValid());
}

QString ApplicationProcessTest::writeFixture(const QString &encodedName, const QString &imageName,
                                           const QString &directory)
{
    QFile encoded(QStringLiteral(FLICK_FIXTURE_DIR "/") + encodedName);
    if (!encoded.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray fixture = QByteArray::fromBase64(encoded.readAll());
    const QString path =
        directory.isEmpty() ? fixtures_.filePath(imageName) : QDir(directory).filePath(imageName);
    QFile image(path);
    if (!image.open(QIODevice::WriteOnly) || image.write(fixture) != fixture.size()) {
        return {};
    }
    return path;
}

void ApplicationProcessTest::start(RunningFlick &flick, const QStringList &arguments,
                                 const QString &pickerSelection,
                                 const int decodeDelayMilliseconds,
                                 const int cacheBudgetBytes,
                                 const QString &configHome,
                                 const qint64 largeAllocationLimitBytes,
                                 const QString &scaleFactor, const bool darkChrome,
                                 const QString &settingsRoot,
                                 const QString &delayedDecodePath)
{
    QVERIFY(flick.environment.isValid());
    const QString config = configHome.isEmpty()
                               ? flick.environment.filePath(QStringLiteral("config"))
                               : configHome;
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
    environment.insert(QStringLiteral("FLICK_TEST_FILE_PICKER_SELECTION"), pickerSelection);
    environment.insert(QStringLiteral("FLICK_TEST_REDUCED_MOTION"), QStringLiteral("1"));
    environment.insert(QStringLiteral("FLICK_TEST_LOADING_INDICATOR_DELAY_MS"),
                       QStringLiteral("500"));
    environment.insert(QStringLiteral("FLICK_TEST_SETTINGS_ROOT"),
                       settingsRoot.isEmpty() ? config : settingsRoot);
    if (!scaleFactor.isEmpty()) {
        environment.insert(QStringLiteral("QT_SCALE_FACTOR"), scaleFactor);
    }
    if (darkChrome) {
        environment.insert(QStringLiteral("FLICK_TEST_DARK_CHROME"), QStringLiteral("1"));
    }
    if (decodeDelayMilliseconds > 0) {
        environment.insert(QStringLiteral("FLICK_TEST_DECODE_DELAY_MS"),
                           QString::number(decodeDelayMilliseconds));
        if (!delayedDecodePath.isEmpty()) {
            environment.insert(QStringLiteral("FLICK_TEST_DECODE_DELAY_PATH"),
                               delayedDecodePath);
        }
    }
    if (cacheBudgetBytes > 0) {
        environment.insert(QStringLiteral("FLICK_TEST_CACHE_BUDGET_BYTES"),
                           QString::number(cacheBudgetBytes));
    }
    if (largeAllocationLimitBytes > 0) {
        environment.insert(QStringLiteral("FLICK_TEST_LARGE_ALLOCATION_LIMIT_BYTES"),
                           QString::number(largeAllocationLimitBytes));
    }
    flick.process.setProcessEnvironment(environment);
    flick.process.start(executable_, arguments);
    QVERIFY2(flick.process.waitForStarted(), qPrintable(flick.process.errorString()));
}

QImage ApplicationProcessTest::waitForScreenshot(const RunningFlick &flick)
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

QImage ApplicationProcessTest::pressKeyAndWaitForScreenshot(RunningFlick &flick, const Qt::Key key)
{
    return sendCommandAndWaitForScreenshot(
        flick, key == Qt::Key_Left ? QByteArrayLiteral("Left") : QByteArrayLiteral("Right"));
}

void ApplicationProcessTest::sendCommand(RunningFlick &flick, const QByteArray &command)
{
    const QByteArray terminatedCommand = command + '\n';
    QVERIFY(flick.process.write(terminatedCommand) == terminatedCommand.size());
    QVERIFY(flick.process.waitForBytesWritten());
    QTest::qWait(20);
}

void ApplicationProcessTest::selectZoomWheelAction(RunningFlick &flick)
{
    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("SelectZoomWheelAction"));
}

QImage ApplicationProcessTest::sendCommandAndWaitForScreenshot(RunningFlick &flick,
                                                              const QByteArray &command)
{
    if (QFile::exists(flick.screenshotPath) && !QFile::remove(flick.screenshotPath)) {
        QTest::qFail("Could not remove the previous screenshot", __FILE__, __LINE__);
        return {};
    }
    const QByteArray terminatedCommand = command + '\n';
    if (flick.process.write(terminatedCommand) != terminatedCommand.size() ||
        !flick.process.waitForBytesWritten()) {
        QTest::qFail("Could not send a key to Flick", __FILE__, __LINE__);
        return {};
    }
    return waitForScreenshot(flick);
}

QImage ApplicationProcessTest::captureAfter(RunningFlick &flick, const int delayMilliseconds)
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

QByteArray ApplicationProcessTest::sendQueryAndWaitForReply(RunningFlick &flick,
                                                           const QByteArray &command)
{
    flick.process.readAllStandardOutput();
    const QByteArray terminatedCommand = command + '\n';
    if (flick.process.write(terminatedCommand) != terminatedCommand.size() ||
        !flick.process.waitForBytesWritten() || !flick.process.waitForReadyRead(5000)) {
        QTest::qFail("Flick did not answer a test query", __FILE__, __LINE__);
        return {};
    }
    return flick.process.readAllStandardOutput().trimmed();
}

QString ApplicationProcessTest::writeImage(const QTemporaryDir &directory, const QString &name,
                                         const QColor &color, const QSize size)
{
    QImage image(size, QImage::Format_RGB32);
    image.fill(color);
    const QString path = directory.filePath(name);
    return image.save(path, "PNG") ? path : QString{};
}

QStringList ApplicationProcessTest::writeStatusSequence(const QTemporaryDir &directory)
{
    return {writeImage(directory, QStringLiteral("image1.png"), QColor(Qt::red), QSize(120, 80)),
            writeImage(directory, QStringLiteral("image2.png"), QColor(Qt::green),
                       QSize(120, 80))};
}

bool ApplicationProcessTest::containsColor(const QImage &image, const QColor &color,
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

QRect ApplicationProcessTest::colorBounds(const QImage &image, const QColor &color,
                                        const int tolerance)
{
    QRect bounds;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            if (qAbs(pixel.red() - color.red()) <= tolerance &&
                qAbs(pixel.green() - color.green()) <= tolerance &&
                qAbs(pixel.blue() - color.blue()) <= tolerance) {
                bounds = bounds.united(QRect(x, y, 1, 1));
            }
        }
    }
    return bounds;
}
