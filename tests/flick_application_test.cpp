// SPDX-License-Identifier: GPL-3.0-or-later

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QProcess>
#include <QRect>
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
    void opensSelectedImageWithCtrlO();
    void singleImageDropBrowsesContainingDirectory();
    void multipleImageDropBrowsesOnlySupportedDroppedFilesInNaturalOrder();
    void cancelledPickerAndUnsupportedDropRemainStable();
    void decodingRemainsResponsiveAndStaleResultsAreIgnored();
    void prefetchedImagesAreReusedAndCacheIsBounded();
    void rendersSupportedStaticFormatsAndTransparency();
    void appliesExifOrientation();
    void animatedGifPreservesTimingAndFiniteLoop();
    void animatedWebpPreservesTimingAndLoops();
    void spacePausesAndResumesAnimationButDoesNotAffectStaticImages();
    void appliesInitialScalingAndKeyboardZoomModes();
    void pointerZoomKeepsCursorOnTheSameImagePoint();
    void pansByDragAndShiftArrowsWhilePlainArrowsNavigateAndResetView();
    void wheelActionDefaultsToNavigationWithCtrlZoom();
    void wheelActionCanSwitchToZoomWithCtrlNavigation();

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

    void start(RunningFlick &flick, const QStringList &arguments = {},
               const QString &pickerSelection = {}, int decodeDelayMilliseconds = 0,
               int cacheBudgetBytes = 0, const QString &configHome = {});
    QImage waitForScreenshot(const RunningFlick &flick);
    QImage pressKeyAndWaitForScreenshot(RunningFlick &flick, Qt::Key key);
    void sendCommand(RunningFlick &flick, const QByteArray &command);
    void selectZoomWheelAction(RunningFlick &flick);
    QImage sendCommandAndWaitForScreenshot(RunningFlick &flick, const QByteArray &command);
    QImage captureAfter(RunningFlick &flick, int delayMilliseconds);
    QByteArray sendQueryAndWaitForReply(RunningFlick &flick, const QByteArray &command);
    QString writeFixture(const QString &encodedName, const QString &imageName);
    static QString writeImage(const QTemporaryDir &directory, const QString &name,
                              const QColor &color, QSize size = QSize(32, 24));
    static bool containsColor(const QImage &image, const QColor &color, int tolerance = 0);
    static QRect colorBounds(const QImage &image, const QColor &color, int tolerance = 0);

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

void FlickApplicationTest::start(RunningFlick &flick, const QStringList &arguments,
                                 const QString &pickerSelection,
                                 const int decodeDelayMilliseconds,
                                 const int cacheBudgetBytes,
                                 const QString &configHome)
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
    if (decodeDelayMilliseconds > 0) {
        environment.insert(QStringLiteral("FLICK_TEST_DECODE_DELAY_MS"),
                           QString::number(decodeDelayMilliseconds));
    }
    if (cacheBudgetBytes > 0) {
        environment.insert(QStringLiteral("FLICK_TEST_CACHE_BUDGET_BYTES"),
                           QString::number(cacheBudgetBytes));
    }
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
    return sendCommandAndWaitForScreenshot(
        flick, key == Qt::Key_Left ? QByteArrayLiteral("Left") : QByteArrayLiteral("Right"));
}

void FlickApplicationTest::sendCommand(RunningFlick &flick, const QByteArray &command)
{
    const QByteArray terminatedCommand = command + '\n';
    QVERIFY(flick.process.write(terminatedCommand) == terminatedCommand.size());
    QVERIFY(flick.process.waitForBytesWritten());
    QTest::qWait(20);
}

void FlickApplicationTest::selectZoomWheelAction(RunningFlick &flick)
{
    sendCommand(flick, QByteArrayLiteral("ContextMenu:250:150"));
    sendCommand(flick, QByteArrayLiteral("MenuDown"));
    sendCommand(flick, QByteArrayLiteral("MenuDown"));
    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("MenuEnter"));
}

QImage FlickApplicationTest::sendCommandAndWaitForScreenshot(RunningFlick &flick,
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

QByteArray FlickApplicationTest::sendQueryAndWaitForReply(RunningFlick &flick,
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

QString FlickApplicationTest::writeImage(const QTemporaryDir &directory, const QString &name,
                                         const QColor &color, const QSize size)
{
    QImage image(size, QImage::Format_RGB32);
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

QRect FlickApplicationTest::colorBounds(const QImage &image, const QColor &color,
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

void FlickApplicationTest::opensSelectedImageWithCtrlO()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QColor selectedColor(138, 43, 226);
    const QString selected =
        writeImage(directory, QStringLiteral("selected.png"), selectedColor);
    QVERIFY(!selected.isEmpty());

    RunningFlick flick;
    start(flick, {}, selected);
    waitForScreenshot(flick);
    const QImage opened = sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("CtrlO"));
    QVERIFY(containsColor(opened, selectedColor));
}

void FlickApplicationTest::singleImageDropBrowsesContainingDirectory()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QColor firstColor(255, 140, 0);
    const QColor secondColor(0, 206, 209);
    const QString first = writeImage(directory, QStringLiteral("photo1.png"), firstColor);
    const QString second = writeImage(directory, QStringLiteral("photo2.png"), secondColor);
    QVERIFY(!first.isEmpty());
    QVERIFY(!second.isEmpty());

    RunningFlick flick;
    start(flick);
    waitForScreenshot(flick);
    const QImage dropped =
        sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("Drop:") + first.toUtf8());
    QVERIFY(containsColor(dropped, firstColor));
    QVERIFY(containsColor(pressKeyAndWaitForScreenshot(flick, Qt::Key_Right), secondColor));
}

void FlickApplicationTest::multipleImageDropBrowsesOnlySupportedDroppedFilesInNaturalOrder()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QColor secondColor(46, 139, 87);
    const QColor tenthColor(199, 21, 133);
    const QString omitted = writeImage(directory, QStringLiteral("image1.png"), QColor(Qt::red));
    const QString second = writeImage(directory, QStringLiteral("IMAGE2.PNG"), secondColor);
    const QString tenth = writeImage(directory, QStringLiteral("image10.png"), tenthColor);
    QVERIFY(!omitted.isEmpty());
    QVERIFY(!second.isEmpty());
    QVERIFY(!tenth.isEmpty());
    const QString unsupported = directory.filePath(QStringLiteral("notes.txt"));
    QFile textFile(unsupported);
    QVERIFY(textFile.open(QIODevice::WriteOnly));
    textFile.write("notes");
    textFile.close();

    RunningFlick flick;
    start(flick);
    waitForScreenshot(flick);
    const QByteArray drop = QByteArrayLiteral("Drop:") + tenth.toUtf8() + '|' +
                            unsupported.toUtf8() + '|' + second.toUtf8();
    const QImage initial = sendCommandAndWaitForScreenshot(flick, drop);
    QVERIFY(containsColor(initial, secondColor));
    const QImage next = pressKeyAndWaitForScreenshot(flick, Qt::Key_Right);
    QVERIFY(containsColor(next, tenthColor));
    const QImage boundary = pressKeyAndWaitForScreenshot(flick, Qt::Key_Right);
    QVERIFY(containsColor(boundary, tenthColor));
    QVERIFY(boundary != next);
}

void FlickApplicationTest::cancelledPickerAndUnsupportedDropRemainStable()
{
    RunningFlick flick;
    start(flick);
    const QImage empty = waitForScreenshot(flick);
    QCOMPARE(sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("CtrlO")), empty);

    const QImage feedback =
        sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("Drop:/tmp/not-an-image.txt"));
    QVERIFY(feedback != empty);
    QCOMPARE(captureAfter(flick, 1600), empty);
    QVERIFY(flick.process.state() == QProcess::Running);
}

void FlickApplicationTest::decodingRemainsResponsiveAndStaleResultsAreIgnored()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QColor firstColor(178, 34, 34);
    const QColor secondColor(34, 139, 34);
    const QString first = writeImage(directory, QStringLiteral("image1.png"), firstColor);
    const QString second = writeImage(directory, QStringLiteral("image2.png"), secondColor);
    QVERIFY(!first.isEmpty());
    QVERIFY(!second.isEmpty());

    RunningFlick flick;
    start(flick, {first}, {}, 700);
    QElapsedTimer responsiveness;
    responsiveness.start();
    const QImage loading = sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("Capture"));
    QVERIFY2(responsiveness.elapsed() < 500, "UI command handling blocked on image decoding");
    QVERIFY(!containsColor(loading, firstColor));

    QTest::qWait(800);
    QVERIFY(containsColor(captureAfter(flick, 0), firstColor));

    flick.process.write("Right\n");
    QVERIFY(flick.process.waitForBytesWritten());
    QTest::qWait(50);
    flick.process.write("Left\n");
    QVERIFY(flick.process.waitForBytesWritten());
    QTest::qWait(800);
    const QImage settled = captureAfter(flick, 0);
    QVERIFY(containsColor(settled, firstColor));
    QVERIFY(!containsColor(settled, secondColor));
}

void FlickApplicationTest::prefetchedImagesAreReusedAndCacheIsBounded()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QStringList paths;
    for (int index = 0; index < 10; ++index) {
        const QString path =
            writeImage(directory, QStringLiteral("image%1.png").arg(index, 2, 10, QLatin1Char('0')),
                       QColor::fromHsv(index * 30, 220, 220), QSize(256, 256));
        QVERIFY(!path.isEmpty());
        paths.append(path);
    }

    constexpr int CacheBudgetBytes = 600000;
    RunningFlick flick;
    start(flick, {paths.first()}, {}, 0, CacheBudgetBytes);
    waitForScreenshot(flick);
    QTRY_COMPARE_WITH_TIMEOUT(sendQueryAndWaitForReply(flick, QByteArrayLiteral("DecodeCount:") +
                                                                 paths.at(1).toUtf8()),
                              QByteArrayLiteral("1"), 5000);

    pressKeyAndWaitForScreenshot(flick, Qt::Key_Right);
    QCOMPARE(sendQueryAndWaitForReply(flick,
                                      QByteArrayLiteral("DecodeCount:") + paths.at(1).toUtf8()),
             QByteArrayLiteral("1"));

    for (int index = 2; index < paths.size(); ++index) {
        pressKeyAndWaitForScreenshot(flick, Qt::Key_Right);
    }
    const qint64 cachedBytes =
        sendQueryAndWaitForReply(flick, QByteArrayLiteral("CacheBytes")).toLongLong();
    QVERIFY(cachedBytes > 0);
    QVERIFY(cachedBytes <= CacheBudgetBytes);

    for (int index = paths.size() - 2; index >= 0; --index) {
        pressKeyAndWaitForScreenshot(flick, Qt::Key_Left);
    }
    QVERIFY(sendQueryAndWaitForReply(flick,
                                     QByteArrayLiteral("DecodeCount:") + paths.first().toUtf8())
                .toInt() > 1);
    QVERIFY(sendQueryAndWaitForReply(flick, QByteArrayLiteral("CacheBytes")).toLongLong() <=
            CacheBudgetBytes);
}

void FlickApplicationTest::rendersSupportedStaticFormatsAndTransparency()
{
    const QList<QPair<QString, QColor>> fixtures = {
        {QStringLiteral("static.jpg"), QColor(220, 20, 60)},
        {QStringLiteral("static.png"), QColor(220, 20, 60)},
        {QStringLiteral("static.webp"), QColor(50, 205, 50)},
        {QStringLiteral("static.gif"), QColor(65, 105, 225)},
        {QStringLiteral("static.bmp"), QColor(255, 140, 0)},
    };
    for (const auto &[name, color] : fixtures) {
        const QString path = writeFixture(name + QStringLiteral(".base64"), name);
        QVERIFY2(!path.isEmpty(), qPrintable(name));
        RunningFlick flick;
        start(flick, {path});
        QVERIFY2(containsColor(waitForScreenshot(flick), color, 3), qPrintable(name));
    }

    const QString transparentPath =
        writeFixture(QStringLiteral("transparent.png.base64"), QStringLiteral("transparent.png"));
    QVERIFY(!transparentPath.isEmpty());
    RunningFlick transparent;
    start(transparent, {transparentPath});
    const QImage screenshot = waitForScreenshot(transparent);
    const QRect purple = colorBounds(screenshot, QColor(138, 43, 226));
    QCOMPARE(purple.size(), QSize(4, 6));
    QCOMPARE(screenshot.pixelColor(purple.right() + 1, purple.top()),
             screenshot.pixelColor(purple.left() - 1, purple.top()));
}

void FlickApplicationTest::appliesExifOrientation()
{
    const QString path =
        writeFixture(QStringLiteral("oriented.jpg.base64"), QStringLiteral("oriented.jpg"));
    QVERIFY(!path.isEmpty());
    RunningFlick flick;
    start(flick, {path});
    const QRect red = colorBounds(waitForScreenshot(flick), QColor(220, 20, 60), 12);
    QVERIFY(!red.isEmpty());
    QVERIFY2(red.height() > red.width(), "EXIF orientation 6 was not applied");
}

void FlickApplicationTest::animatedGifPreservesTimingAndFiniteLoop()
{
    const QString path =
        writeFixture(QStringLiteral("animated.gif.base64"), QStringLiteral("animated.gif"));
    QVERIFY(!path.isEmpty());
    RunningFlick flick;
    start(flick, {path});
    QVERIFY(containsColor(waitForScreenshot(flick), QColor(220, 20, 60), 3));
    QVERIFY(containsColor(captureAfter(flick, 170), QColor(50, 205, 50), 3));
    QVERIFY(containsColor(captureAfter(flick, 900), QColor(50, 205, 50)));
}

void FlickApplicationTest::animatedWebpPreservesTimingAndLoops()
{
    const QString path =
        writeFixture(QStringLiteral("animated.webp.base64"), QStringLiteral("animated.webp"));
    QVERIFY(!path.isEmpty());
    RunningFlick flick;
    start(flick, {path});
    QVERIFY(containsColor(waitForScreenshot(flick), QColor(220, 20, 60), 5));
    QVERIFY(containsColor(captureAfter(flick, 170), QColor(50, 205, 50), 3));
    QVERIFY(containsColor(captureAfter(flick, 230), QColor(220, 20, 60), 5));
}

void FlickApplicationTest::spacePausesAndResumesAnimationButDoesNotAffectStaticImages()
{
    const QString animatedPath =
        writeFixture(QStringLiteral("animated.webp.base64"), QStringLiteral("pausable.webp"));
    QVERIFY(!animatedPath.isEmpty());
    RunningFlick animated;
    start(animated, {animatedPath});
    waitForScreenshot(animated);
    QVERIFY(containsColor(captureAfter(animated, 170), QColor(50, 205, 50), 3));
    const QImage paused =
        sendCommandAndWaitForScreenshot(animated, QByteArrayLiteral("Space"));
    QCOMPARE(captureAfter(animated, 500), paused);
    sendCommandAndWaitForScreenshot(animated, QByteArrayLiteral("Space"));
    bool resumedToRed = false;
    QElapsedTimer resumeTimer;
    resumeTimer.start();
    while (!resumedToRed && resumeTimer.elapsed() < 500) {
        resumedToRed =
            containsColor(captureAfter(animated, 20), QColor(220, 20, 60), 5);
    }
    QVERIFY2(resumedToRed, "Animation did not resume from its paused frame");

    const QString staticPath =
        writeFixture(QStringLiteral("static.png.base64"), QStringLiteral("still.png"));
    QVERIFY(!staticPath.isEmpty());
    RunningFlick still;
    start(still, {staticPath});
    const QImage before = waitForScreenshot(still);
    const QImage after =
        sendCommandAndWaitForScreenshot(still, QByteArrayLiteral("Space"));
    QCOMPARE(colorBounds(after, PngFixtureColor), colorBounds(before, PngFixtureColor));
}

void FlickApplicationTest::appliesInitialScalingAndKeyboardZoomModes()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QColor color(255, 20, 147);
    const QString small =
        writeImage(directory, QStringLiteral("small.png"), color, QSize(120, 80));
    const QString large =
        writeImage(directory, QStringLiteral("large.png"), color, QSize(960, 640));
    QVERIFY(!small.isEmpty());
    QVERIFY(!large.isEmpty());

    RunningFlick smallFlick;
    start(smallFlick, {small});
    const QSize initialSmall = colorBounds(waitForScreenshot(smallFlick), color).size();
    QCOMPARE(initialSmall.width(), 120);
    QVERIFY(initialSmall.height() >= 79 && initialSmall.height() <= 80);
    const QImage smallFit =
        sendCommandAndWaitForScreenshot(smallFlick, QByteArrayLiteral("Fit"));
    QCOMPARE(colorBounds(smallFit, color).size(), QSize(477, 318));
    const QImage smallActual =
        sendCommandAndWaitForScreenshot(smallFlick, QByteArrayLiteral("ActualSize"));
    QCOMPARE(colorBounds(smallActual, color).size(), QSize(120, 80));

    RunningFlick largeFlick;
    start(largeFlick, {large});
    QCOMPARE(colorBounds(waitForScreenshot(largeFlick), color).size(), QSize(478, 318));
    const QImage largeActual =
        sendCommandAndWaitForScreenshot(largeFlick, QByteArrayLiteral("ActualSize"));
    QCOMPARE(colorBounds(largeActual, color).size(), QSize(478, 318));
}

void FlickApplicationTest::pointerZoomKeepsCursorOnTheSameImagePoint()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeImage(directory, QStringLiteral("small.png"), QColor(Qt::cyan),
                                    QSize(120, 80));
    QVERIFY(!path.isEmpty());

    RunningFlick flick;
    start(flick, {path});
    waitForScreenshot(flick);
    const QList<QByteArray> before =
        sendQueryAndWaitForReply(flick, QByteArrayLiteral("ViewState")).split(',');
    QCOMPARE(before.size(), 7);

    constexpr int CursorX = 250;
    constexpr int CursorY = 150;
    sendCommandAndWaitForScreenshot(
        flick, QByteArrayLiteral("CtrlWheel:250:150:120"));
    const QList<QByteArray> after =
        sendQueryAndWaitForReply(flick, QByteArrayLiteral("ViewState")).split(',');
    QCOMPARE(after.size(), 7);
    const double oldZoom = before.at(0).toDouble();
    const double newZoom = after.at(0).toDouble();
    QVERIFY(newZoom > oldZoom);
    const double oldImageX = (before.at(1).toInt() + CursorX - before.at(5).toInt()) / oldZoom;
    const double oldImageY = (before.at(2).toInt() + CursorY - before.at(6).toInt()) / oldZoom;
    const double newImageX = (after.at(1).toInt() + CursorX - after.at(5).toInt()) / newZoom;
    const double newImageY = (after.at(2).toInt() + CursorY - after.at(6).toInt()) / newZoom;
    QVERIFY(qAbs(oldImageX - newImageX) < 1.0);
    QVERIFY(qAbs(oldImageY - newImageY) < 1.0);
}

void FlickApplicationTest::pansByDragAndShiftArrowsWhilePlainArrowsNavigateAndResetView()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QColor firstColor(255, 69, 0);
    const QColor secondColor(50, 205, 50);
    const QString first = writeImage(directory, QStringLiteral("image1.png"), firstColor,
                                     QSize(960, 640));
    const QString second = writeImage(directory, QStringLiteral("image2.png"), secondColor,
                                      QSize(120, 80));
    QVERIFY(!first.isEmpty());
    QVERIFY(!second.isEmpty());

    RunningFlick flick;
    start(flick, {first});
    waitForScreenshot(flick);
    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("ActualSize"));
    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("Drag:220:160:170:120"));
    const QList<QByteArray> dragged =
        sendQueryAndWaitForReply(flick, QByteArrayLiteral("ViewState")).split(',');
    QCOMPARE(dragged.size(), 7);
    QVERIFY(dragged.at(1).toInt() >= 530);
    QVERIFY(dragged.at(2).toInt() >= 360);

    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("ShiftRight"));
    const QList<QByteArray> keyboardPanned =
        sendQueryAndWaitForReply(flick, QByteArrayLiteral("ViewState")).split(',');
    QVERIFY(keyboardPanned.at(1).toInt() > dragged.at(1).toInt());

    const QImage next = sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("Right"));
    QVERIFY(containsColor(next, secondColor));
    const QList<QByteArray> reset =
        sendQueryAndWaitForReply(flick, QByteArrayLiteral("ViewState")).split(',');
    QCOMPARE(reset.at(0).toDouble(), 1.0);
    QCOMPARE(reset.at(1).toInt(), 60);
    QCOMPARE(reset.at(2).toInt(), 40);
}

void FlickApplicationTest::wheelActionDefaultsToNavigationWithCtrlZoom()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QColor firstColor(220, 20, 60);
    const QColor secondColor(65, 105, 225);
    const QString first = writeImage(directory, QStringLiteral("image1.png"), firstColor);
    const QString second = writeImage(directory, QStringLiteral("image2.png"), secondColor);
    QVERIFY(!first.isEmpty());
    QVERIFY(!second.isEmpty());

    RunningFlick flick;
    start(flick, {first});
    QVERIFY(containsColor(waitForScreenshot(flick), firstColor));

    const QImage next =
        sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("Wheel:250:150:-120"));
    QVERIFY(containsColor(next, secondColor));
    const QList<QByteArray> before =
        sendQueryAndWaitForReply(flick, QByteArrayLiteral("ViewState")).split(',');
    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("CtrlWheel:250:150:120"));
    const QList<QByteArray> after =
        sendQueryAndWaitForReply(flick, QByteArrayLiteral("ViewState")).split(',');
    QVERIFY(after.at(0).toDouble() > before.at(0).toDouble());
}

void FlickApplicationTest::wheelActionCanSwitchToZoomWithCtrlNavigation()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QColor firstColor(255, 140, 0);
    const QColor secondColor(46, 139, 87);
    const QString first = writeImage(directory, QStringLiteral("image1.png"), firstColor);
    const QString second = writeImage(directory, QStringLiteral("image2.png"), secondColor);
    QVERIFY(!first.isEmpty());
    QVERIFY(!second.isEmpty());

    RunningFlick flick;
    start(flick, {first});
    QVERIFY(containsColor(waitForScreenshot(flick), firstColor));
    selectZoomWheelAction(flick);

    const QList<QByteArray> before =
        sendQueryAndWaitForReply(flick, QByteArrayLiteral("ViewState")).split(',');
    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("Wheel:250:150:120"));
    const QList<QByteArray> after =
        sendQueryAndWaitForReply(flick, QByteArrayLiteral("ViewState")).split(',');
    QVERIFY(after.at(0).toDouble() > before.at(0).toDouble());

    const QImage next =
        sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("CtrlWheel:250:150:-120"));
    QVERIFY(containsColor(next, secondColor));

    QTemporaryDir sharedConfiguration;
    QVERIFY(sharedConfiguration.isValid());
    const QString configHome = sharedConfiguration.filePath(QStringLiteral("config"));
    RunningFlick configured;
    start(configured, {first}, {}, 0, 0, configHome);
    waitForScreenshot(configured);
    selectZoomWheelAction(configured);
    configured.process.terminate();
    QVERIFY(configured.process.waitForFinished(2000));

    RunningFlick relaunched;
    start(relaunched, {first}, {}, 0, 0, configHome);
    waitForScreenshot(relaunched);
    const QList<QByteArray> persistedBefore =
        sendQueryAndWaitForReply(relaunched, QByteArrayLiteral("ViewState")).split(',');
    sendCommandAndWaitForScreenshot(relaunched, QByteArrayLiteral("Wheel:250:150:120"));
    const QList<QByteArray> persistedAfter =
        sendQueryAndWaitForReply(relaunched, QByteArrayLiteral("ViewState")).split(',');
    QVERIFY(persistedAfter.at(0).toDouble() > persistedBefore.at(0).toDouble());
}

QTEST_MAIN(FlickApplicationTest)
#include "flick_application_test.moc"
