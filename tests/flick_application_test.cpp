// SPDX-License-Identifier: GPL-3.0-or-later

#include <QDir>
#include <QColorSpace>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QKeySequence>
#include <QProcess>
#include <QRect>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>

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
    void presentsCoherentEmptyErrorAndLargeImageStates();
    void delaysLoadingPresentationAndClearsPreviousImage();
    void validDragFeedbackRestoresThePreviousPresentation();
    void separateInvocationsRemainIndependent();
    void browsesNaturallySortedVisibleSupportedImages();
    void opensSelectedImageWithCtrlO();
    void singleImageDropBrowsesContainingDirectory();
    void multipleImageDropBrowsesOnlySupportedDroppedFilesInNaturalOrder();
    void directorySequenceTracksExternalFilesystemChanges();
    void explicitListIgnoresExternalDirectoryAdditions();
    void cancelledPickerAndUnsupportedDropRemainStable();
    void decodingRemainsResponsiveAndStaleResultsAreIgnored();
    void prefetchedImagesAreReusedAndCacheIsBounded();
    void decodeFailureExplainsTheProblemAndKeepsNavigationUsable();
    void technicalDetailsRemainSecondaryAndEnterRecoversAfterRepair();
    void extremeDimensionsRequireConfirmationBeforeBackgroundDecode();
    void rendersSupportedStaticFormatsAndTransparency();
    void honorsEmbeddedProfilesAndDefaultsUntaggedImagesToSrgb();
    void updatesRenderingWhenTheDisplayProfileChanges();
    void appliesExifOrientation();
    void animatedGifPreservesTimingAndFiniteLoop();
    void animatedWebpPreservesTimingAndLoops();
    void spacePausesAndResumesAnimationButDoesNotAffectStaticImages();
    void appliesInitialScalingAndKeyboardZoomModes();
    void highZoomRemainsResponsiveWithoutAllocatingTheFullScaledImage();
    void pointerZoomKeepsCursorOnTheSameImagePoint();
    void pansByDragAndShiftArrowsWhilePlainArrowsNavigateAndResetView();
    void wheelActionDefaultsToNavigationWithCtrlZoom();
    void wheelActionCanSwitchToZoomWithCtrlNavigation();
    void settingsApplyImmediatelyAndPersistAcrossLaunches();
    void settingsDialogPreviewsCommitsRollsBackAndResets();
    void temporarilyRotatesCurrentViewAndResetsOnNavigation();
    void togglesFullscreenFromKeyboardAndPointer();
    void transientStatusReportsViewContextAndReappearsOnMouseMovement();
    void statusOverlayElidesLongNamesAndRestoresContextAfterFeedback();
    void firstUseTeachingPersistsAfterBrowsingIsLearned();
    void fullscreenTeachingAppearsOnlyOnFirstEntry();
    void fullscreenInactivityHidesStatusAndPointerWithoutBlockingKeyboard();
    void informationDialogStaysLiveWhileBrowsing();
    void informationDialogReportsNaturalAnimationCompletion();
    void copiesPathAndRenderedImageAndExposesContextCommands();
    void exposesGroupedCommandSurfacesWithoutNavigationRows();
    void quitCommandIsSharedAndExitsCleanly();
    void contextMenuStaysReachableNearEveryScreenEdge();
    void restoresViewingFocusAndAppliesEscapePrecedence();
    void revealsCurrentFileAndReportsExternalActionFailures();
    void exposesAccessibleKeyboardActions();
    void usesViewingSurfaceVocabularyAndMotionContract();

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
               int cacheBudgetBytes = 0, const QString &configHome = {},
               qint64 largeAllocationLimitBytes = 0, const QString &scaleFactor = {},
               bool darkChrome = false);
    QImage waitForScreenshot(const RunningFlick &flick);
    QImage pressKeyAndWaitForScreenshot(RunningFlick &flick, Qt::Key key);
    void sendCommand(RunningFlick &flick, const QByteArray &command);
    void selectZoomWheelAction(RunningFlick &flick);
    QImage sendCommandAndWaitForScreenshot(RunningFlick &flick, const QByteArray &command);
    QImage captureAfter(RunningFlick &flick, int delayMilliseconds);
    QByteArray sendQueryAndWaitForReply(RunningFlick &flick, const QByteArray &command);
    QString writeFixture(const QString &encodedName, const QString &imageName,
                         const QString &directory = {});
    static QString writeImage(const QTemporaryDir &directory, const QString &name,
                              const QColor &color, QSize size = QSize(32, 24));
    static QStringList writeStatusSequence(const QTemporaryDir &directory);
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

QString FlickApplicationTest::writeFixture(const QString &encodedName, const QString &imageName,
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

void FlickApplicationTest::start(RunningFlick &flick, const QStringList &arguments,
                                 const QString &pickerSelection,
                                 const int decodeDelayMilliseconds,
                                 const int cacheBudgetBytes,
                                 const QString &configHome,
                                 const qint64 largeAllocationLimitBytes,
                                 const QString &scaleFactor, const bool darkChrome)
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
    if (!scaleFactor.isEmpty()) {
        environment.insert(QStringLiteral("QT_SCALE_FACTOR"), scaleFactor);
    }
    if (darkChrome) {
        environment.insert(QStringLiteral("FLICK_TEST_DARK_CHROME"), QStringLiteral("1"));
    }
    if (decodeDelayMilliseconds > 0) {
        environment.insert(QStringLiteral("FLICK_TEST_DECODE_DELAY_MS"),
                           QString::number(decodeDelayMilliseconds));
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
    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("SelectZoomWheelAction"));
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

QStringList FlickApplicationTest::writeStatusSequence(const QTemporaryDir &directory)
{
    return {writeImage(directory, QStringLiteral("image1.png"), QColor(Qt::red), QSize(120, 80)),
            writeImage(directory, QStringLiteral("image2.png"), QColor(Qt::green),
                       QSize(120, 80))};
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

void FlickApplicationTest::presentsCoherentEmptyErrorAndLargeImageStates()
{
    RunningFlick empty;
    start(empty);
    waitForScreenshot(empty);
    const QByteArray emptyState =
        sendQueryAndWaitForReply(empty, QByteArrayLiteral("PresentationState"));
    QVERIFY(emptyState.contains("empty|Open an image|Choose file|or drop it here"));
    QVERIFY(emptyState.contains("Browse"));
    sendCommand(empty, QByteArrayLiteral("Resize:800:600"));
    QCOMPARE(sendQueryAndWaitForReply(empty, QByteArrayLiteral("PresentationState")), emptyState);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString malformedPath = directory.filePath(QStringLiteral("broken.png"));
    QFile malformed(malformedPath);
    QVERIFY(malformed.open(QIODevice::WriteOnly));
    QCOMPARE(malformed.write("not a png"), 9);
    malformed.close();
    RunningFlick error;
    start(error, {malformedPath});
    waitForScreenshot(error);
    const QByteArray errorState =
        sendQueryAndWaitForReply(error, QByteArrayLiteral("PresentationState"));
    QVERIFY(errorState.startsWith("error|This image could not be displayed"));
    QVERIFY(errorState.contains("Retry|Details"));
    QVERIFY(errorState.contains("Left and right still browse"));
}

void FlickApplicationTest::delaysLoadingPresentationAndClearsPreviousImage()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QColor firstColor(245, 222, 179);
    const QString first = writeImage(directory, QStringLiteral("image1.png"), firstColor);
    const QString second =
        writeImage(directory, QStringLiteral("image2.png"), QColor(72, 61, 139));
    QVERIFY(!first.isEmpty());
    QVERIFY(!second.isEmpty());

    RunningFlick flick;
    start(flick, {first}, {}, 700);
    QVERIFY(containsColor(waitForScreenshot(flick), firstColor));

    sendCommand(flick, QByteArrayLiteral("Right"));
    QTest::qWait(50);
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("PresentationState")),
             QByteArrayLiteral("loading|image2.png|indicator-hidden"));
    const QImage beforeThreshold = captureAfter(flick, 0);
    QVERIFY(!containsColor(beforeThreshold, firstColor));

    QTest::qWait(100);
    const QByteArray loading =
        sendQueryAndWaitForReply(flick, QByteArrayLiteral("PresentationState"));
    QCOMPARE(loading, QByteArrayLiteral("loading|image2.png|indicator-visible"));
}

void FlickApplicationTest::validDragFeedbackRestoresThePreviousPresentation()
{
    const QString path = writeFixture(QStringLiteral("known.png.base64"),
                                      QStringLiteral("drag-target.png"));
    QVERIFY(!path.isEmpty());
    RunningFlick flick;
    start(flick, {path});
    const QImage displayed = waitForScreenshot(flick);

    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("BeginDrag:") + path.toUtf8());
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("PresentationState")),
             QByteArrayLiteral("drop|Drop to open"));
    const QImage dragFeedback = captureAfter(flick, 0);
    QVERIFY(dragFeedback != displayed);

    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("LeaveDrag"));
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("PresentationState")),
             QByteArrayLiteral("displayed"));
    QCOMPARE(captureAfter(flick, 0), displayed);
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
    QVERIFY(containsColor(captureAfter(flick, 1600), firstColor));
    QVERIFY(sendQueryAndWaitForReply(flick, QByteArrayLiteral("Feedback"))
                .endsWith(QByteArrayLiteral("1 / 3 — 100%")));

    const QImage middle = pressKeyAndWaitForScreenshot(flick, Qt::Key_Right);
    QVERIFY(containsColor(middle, secondColor));
    const QImage next = pressKeyAndWaitForScreenshot(flick, Qt::Key_Right);
    QVERIFY(containsColor(next, tenthColor));
    const QImage endBoundary = pressKeyAndWaitForScreenshot(flick, Qt::Key_Right);
    QVERIFY(containsColor(endBoundary, tenthColor));
    QVERIFY(endBoundary != next);
    QVERIFY(containsColor(captureAfter(flick, 1600), tenthColor));
    QVERIFY(sendQueryAndWaitForReply(flick, QByteArrayLiteral("Feedback"))
                .endsWith(QByteArrayLiteral("3 / 3 — 100%")));
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

    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("LastPickerDirectory")),
             directory.path().toUtf8());
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

void FlickApplicationTest::directorySequenceTracksExternalFilesystemChanges()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QColor firstColor(220, 20, 60);
    const QColor secondColor(50, 205, 50);
    const QColor thirdColor(65, 105, 225);
    const QString first = writeImage(directory, QStringLiteral("image1.png"), firstColor);
    const QString third = writeImage(directory, QStringLiteral("image3.png"), thirdColor);
    QVERIFY(!first.isEmpty());
    QVERIFY(!third.isEmpty());

    RunningFlick flick;
    start(flick, {first});
    QVERIFY(containsColor(waitForScreenshot(flick), firstColor));

    const QString second = writeImage(directory, QStringLiteral("image2.png"), secondColor);
    QVERIFY(!second.isEmpty());
    QTest::qWait(1000);
    QVERIFY(containsColor(pressKeyAndWaitForScreenshot(flick, Qt::Key_Right), secondColor));

    QVERIFY(QFile::remove(second));
    QTRY_VERIFY_WITH_TIMEOUT(
        containsColor(captureAfter(flick, 50), thirdColor), 3000);
    QVERIFY(sendQueryAndWaitForReply(flick, QByteArrayLiteral("Feedback"))
                .contains("Current image is no longer available"));

    const QString renamed = directory.filePath(QStringLiteral("image4.png"));
    QVERIFY(QFile::rename(third, renamed));
    QTRY_VERIFY_WITH_TIMEOUT(
        containsColor(captureAfter(flick, 50), thirdColor), 3000);
    QVERIFY(sendQueryAndWaitForReply(flick, QByteArrayLiteral("Feedback"))
                .contains("Current image is no longer available"));

    QVERIFY(QFile::remove(renamed));
    QVERIFY(QFile::remove(first));
    QTest::qWait(1700);
    RunningFlick empty;
    start(empty);
    QCOMPARE(captureAfter(flick, 50), waitForScreenshot(empty));
    QVERIFY(flick.process.state() == QProcess::Running);
}

void FlickApplicationTest::explicitListIgnoresExternalDirectoryAdditions()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QColor firstColor(255, 140, 0);
    const QColor thirdColor(0, 206, 209);
    const QString first = writeImage(directory, QStringLiteral("image1.png"), firstColor);
    const QString third = writeImage(directory, QStringLiteral("image3.png"), thirdColor);
    QVERIFY(!first.isEmpty());
    QVERIFY(!third.isEmpty());

    RunningFlick flick;
    start(flick);
    waitForScreenshot(flick);
    const QByteArray drop = QByteArrayLiteral("Drop:") + first.toUtf8() + '|' + third.toUtf8();
    QVERIFY(containsColor(sendCommandAndWaitForScreenshot(flick, drop), firstColor));

    QVERIFY(!writeImage(directory, QStringLiteral("image2.png"), QColor(Qt::magenta)).isEmpty());
    QTest::qWait(200);
    QVERIFY(containsColor(pressKeyAndWaitForScreenshot(flick, Qt::Key_Right), thirdColor));
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

void FlickApplicationTest::decodeFailureExplainsTheProblemAndKeepsNavigationUsable()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString malformedPath = directory.filePath(QStringLiteral("image1.png"));
    QFile malformed(malformedPath);
    QVERIFY(malformed.open(QIODevice::WriteOnly));
    QCOMPARE(malformed.write("not a png"), 9);
    malformed.close();
    const QString deniedPath =
        writeImage(directory, QStringLiteral("image2.png"), QColor(Qt::yellow));
    QVERIFY(!deniedPath.isEmpty());
    QVERIFY(QFile::setPermissions(deniedPath, {}));
    const QColor recoverableColor(46, 139, 87);
    const QString adjacentPath =
        writeImage(directory, QStringLiteral("image3.png"), recoverableColor);
    QVERIFY(!adjacentPath.isEmpty());

    RunningFlick flick;
    start(flick, {malformedPath});
    waitForScreenshot(flick);
    const QList<QByteArray> error =
        sendQueryAndWaitForReply(flick, QByteArrayLiteral("ErrorState")).split('|');
    QCOMPARE(error.at(0), QByteArrayLiteral("visible"));
    QVERIFY(error.at(1).contains("could not be displayed"));
    QVERIFY(!error.at(2).isEmpty());

    pressKeyAndWaitForScreenshot(flick, Qt::Key_Right);
    const QList<QByteArray> denied =
        sendQueryAndWaitForReply(flick, QByteArrayLiteral("ErrorState")).split('|');
    QCOMPARE(denied.at(0), QByteArrayLiteral("visible"));
    QVERIFY(!denied.at(2).isEmpty());

    const QImage adjacent = pressKeyAndWaitForScreenshot(flick, Qt::Key_Right);
    QVERIFY(containsColor(adjacent, recoverableColor));
    QVERIFY(flick.process.state() == QProcess::Running);
    QVERIFY(QFile::setPermissions(deniedPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
}

void FlickApplicationTest::technicalDetailsRemainSecondaryAndEnterRecoversAfterRepair()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QColor recoveredColor(255, 127, 80);
    const QString path =
        writeImage(directory, QStringLiteral("truncated.png"), recoveredColor, QSize(80, 60));
    QVERIFY(!path.isEmpty());
    QFile fixture(path);
    QVERIFY(fixture.open(QIODevice::ReadOnly));
    const QByteArray validBytes = fixture.readAll();
    fixture.close();
    QVERIFY(fixture.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(fixture.write(validBytes.first(24)), 24);
    fixture.close();

    RunningFlick flick;
    start(flick, {path});
    waitForScreenshot(flick);
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("PrimaryActionState")),
             QByteArrayLiteral("Retry:default|Details:secondary"));
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("ErrorState")).split('|').at(3),
             QByteArrayLiteral("details-hidden"));
    sendCommand(flick, QByteArrayLiteral("FocusDetails"));
    sendCommand(flick, QByteArrayLiteral("Enter"));
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("PrimaryActionState")),
             QByteArrayLiteral("Retry:default|Details:secondary"));
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("ErrorState")).split('|').at(3),
             QByteArrayLiteral("details-visible"));
    QCOMPARE(sendQueryAndWaitForReply(flick,
                                      QByteArrayLiteral("DecodeCount:") + path.toUtf8()),
             QByteArrayLiteral("1"));

    QVERIFY(fixture.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(fixture.write(validBytes), validBytes.size());
    fixture.close();
    sendCommand(flick, QByteArrayLiteral("FocusViewingSurface"));
    const QImage recovered =
        sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("Enter"));
    QVERIFY(containsColor(recovered, recoveredColor));
    QCOMPARE(sendQueryAndWaitForReply(flick,
                                      QByteArrayLiteral("DecodeCount:") + path.toUtf8()),
             QByteArrayLiteral("2"));
}

void FlickApplicationTest::extremeDimensionsRequireConfirmationBeforeBackgroundDecode()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QByteArray bmp(54, '\0');
    bmp[0] = 'B';
    bmp[1] = 'M';
    qToLittleEndian<quint32>(54, bmp.data() + 10);
    qToLittleEndian<quint32>(40, bmp.data() + 14);
    qToLittleEndian<qint32>(20000, bmp.data() + 18);
    qToLittleEndian<qint32>(10000, bmp.data() + 22);
    qToLittleEndian<quint16>(1, bmp.data() + 26);
    qToLittleEndian<quint16>(24, bmp.data() + 28);
    const QString path = directory.filePath(QStringLiteral("extreme.bmp"));
    QFile fixture(path);
    QVERIFY(fixture.open(QIODevice::WriteOnly));
    QCOMPARE(fixture.write(bmp), bmp.size());
    fixture.close();

    RunningFlick rejected;
    start(rejected, {path});
    waitForScreenshot(rejected);
    QList<QByteArray> warning =
        sendQueryAndWaitForReply(rejected, QByteArrayLiteral("LargeImageState")).split('|');
    QCOMPARE(warning.at(0), QByteArrayLiteral("visible"));
    QCOMPARE(warning.at(1), QByteArrayLiteral("20000x10000"));
    const QByteArray warningPresentation =
        sendQueryAndWaitForReply(rejected, QByteArrayLiteral("PresentationState"));
    QVERIFY(warningPresentation.contains("20000 × 10000"));
    QVERIFY(warningPresentation.endsWith("Open anyway|Skip"));
    QCOMPARE(sendQueryAndWaitForReply(rejected, QByteArrayLiteral("PrimaryActionState")),
             QByteArrayLiteral("Open anyway:default|Skip:secondary"));
    sendCommandAndWaitForScreenshot(rejected, QByteArrayLiteral("Escape"));
    QVERIFY(sendQueryAndWaitForReply(rejected, QByteArrayLiteral("PresentationState"))
                .startsWith("empty|"));
    QCOMPARE(sendQueryAndWaitForReply(rejected,
                                      QByteArrayLiteral("DecodeCount:") + path.toUtf8()),
             QByteArrayLiteral("1"));

    RunningFlick approved;
    start(approved, {path}, {}, 700);
    waitForScreenshot(approved);
    sendCommand(approved, QByteArrayLiteral("FocusSkip"));
    sendCommandAndWaitForScreenshot(approved, QByteArrayLiteral("Enter"));
    QVERIFY(sendQueryAndWaitForReply(approved, QByteArrayLiteral("PresentationState"))
                .startsWith("empty|"));
    QCOMPARE(sendQueryAndWaitForReply(approved,
                                      QByteArrayLiteral("DecodeCount:") + path.toUtf8()),
             QByteArrayLiteral("1"));
    sendCommandAndWaitForScreenshot(approved, QByteArrayLiteral("Drop:") + path.toUtf8());
    QCOMPARE(sendQueryAndWaitForReply(approved, QByteArrayLiteral("PrimaryActionState")),
             QByteArrayLiteral("Open anyway:default|Skip:secondary"));
    sendCommand(approved, QByteArrayLiteral("FocusViewingSurface"));
    QElapsedTimer responsiveness;
    responsiveness.start();
    sendCommand(approved, QByteArrayLiteral("Enter"));
    QCOMPARE(sendQueryAndWaitForReply(approved, QByteArrayLiteral("PresentationState")),
             QByteArrayLiteral("loading|extreme.bmp|indicator-hidden"));
    sendCommandAndWaitForScreenshot(approved, QByteArrayLiteral("Capture"));
    QVERIFY2(responsiveness.elapsed() < 500, "approved large-image decode blocked the UI thread");
    QTRY_COMPARE_WITH_TIMEOUT(
        sendQueryAndWaitForReply(approved, QByteArrayLiteral("DecodeCount:") + path.toUtf8()),
        QByteArrayLiteral("3"), 5000);

    const QString animatedPath =
        writeFixture(QStringLiteral("animated.gif.base64"), QStringLiteral("allocation.gif"));
    QVERIFY(!animatedPath.isEmpty());
    RunningFlick allocationGuard;
    start(allocationGuard, {animatedPath}, {}, 0, 0, {}, 1);
    waitForScreenshot(allocationGuard);
    QCOMPARE(sendQueryAndWaitForReply(allocationGuard,
                                      QByteArrayLiteral("LargeImageState")).split('|').at(0),
             QByteArrayLiteral("visible"));

    const QString adjacentPath =
        writeImage(directory, QStringLiteral("ordinary.png"), QColor(Qt::cyan));
    QVERIFY(!adjacentPath.isEmpty());
    RunningFlick staleConfirmation;
    start(staleConfirmation, {path});
    waitForScreenshot(staleConfirmation);
    sendCommandAndWaitForScreenshot(staleConfirmation, QByteArrayLiteral("Drop:") +
                                                           adjacentPath.toUtf8());
    QCOMPARE(sendQueryAndWaitForReply(staleConfirmation,
                                      QByteArrayLiteral("LargeImageState")).split('|').at(0),
             QByteArrayLiteral("hidden"));
    sendCommand(staleConfirmation, QByteArrayLiteral("ApproveLarge"));
    QCOMPARE(sendQueryAndWaitForReply(staleConfirmation,
                                      QByteArrayLiteral("DecodeCount:") + adjacentPath.toUtf8()),
             QByteArrayLiteral("1"));
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

void FlickApplicationTest::honorsEmbeddedProfilesAndDefaultsUntaggedImagesToSrgb()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString tagged =
        writeFixture(QStringLiteral("tagged-display-p3.png.base64"),
                     QStringLiteral("1-tagged.png"), directory.path());
    const QString untagged =
        writeImage(directory, QStringLiteral("2-untagged.png"), QColor(64, 128, 192));
    QVERIFY(!tagged.isEmpty());
    QVERIFY(!untagged.isEmpty());

    RunningFlick flick;
    start(flick, {tagged});
    const QImage taggedScreenshot = waitForScreenshot(flick);
    QVERIFY(containsColor(taggedScreenshot, QColor(255, 119, 0), 2));
    const QImage untaggedScreenshot = pressKeyAndWaitForScreenshot(flick, Qt::Key_Right);
    QVERIFY(containsColor(untaggedScreenshot, QColor(64, 128, 192)));
}

void FlickApplicationTest::updatesRenderingWhenTheDisplayProfileChanges()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString image =
        writeImage(directory, QStringLiteral("gray.png"), QColor(128, 128, 128));
    const QString profile =
        writeFixture(QStringLiteral("linear-srgb.icc.base64"),
                     QStringLiteral("linear-srgb.icc"), directory.path());
    QVERIFY(!image.isEmpty());
    QVERIFY(!profile.isEmpty());

    RunningFlick flick;
    start(flick, {image});
    const QImage initial = waitForScreenshot(flick);
    QVERIFY(containsColor(initial, QColor(128, 128, 128)));

    sendCommand(flick, QByteArrayLiteral("DisplayProfileChanged:") + profile.toUtf8());
    const QImage linear = captureAfter(flick, 50);
    QVERIFY(colorBounds(linear, QColor(128, 128, 128)).size() != QSize(32, 24));
    QVERIFY(containsColor(linear, QColor(55, 55, 55), 1));
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
    bool loopedToFirstFrame = false;
    QElapsedTimer loopWait;
    loopWait.start();
    while (!loopedToFirstFrame && loopWait.elapsed() < 1000) {
        loopedToFirstFrame =
            containsColor(captureAfter(flick, 25), QColor(220, 20, 60), 5);
    }
    QVERIFY2(loopedToFirstFrame, "animated WebP did not loop back to its first frame");
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
    const QSize fittedSmallSize = colorBounds(smallFit, color).size();
    QVERIFY(fittedSmallSize.width() >= 440);
    QVERIFY(fittedSmallSize.height() >= 293);
    const QImage smallActual =
        sendCommandAndWaitForScreenshot(smallFlick, QByteArrayLiteral("ActualSize"));
    QCOMPARE(colorBounds(smallActual, color).size(), QSize(120, 80));
    sendCommandAndWaitForScreenshot(smallFlick, QByteArrayLiteral("CtrlPlus"));
    QCOMPARE(sendQueryAndWaitForReply(smallFlick, QByteArrayLiteral("ViewState"))
                 .split(',')
                 .first()
                 .toDouble(),
             1.25);
    sendCommandAndWaitForScreenshot(smallFlick, QByteArrayLiteral("CtrlMinus"));
    QCOMPARE(sendQueryAndWaitForReply(smallFlick, QByteArrayLiteral("ViewState"))
                 .split(',')
                 .first()
                 .toDouble(),
             1.0);

    RunningFlick largeFlick;
    start(largeFlick, {large});
    QCOMPARE(colorBounds(waitForScreenshot(largeFlick), color).size(), QSize(478, 296));
    const QImage largeActual =
        sendCommandAndWaitForScreenshot(largeFlick, QByteArrayLiteral("ActualSize"));
    QCOMPARE(colorBounds(largeActual, color).size(), QSize(478, 296));
}

void FlickApplicationTest::highZoomRemainsResponsiveWithoutAllocatingTheFullScaledImage()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeImage(directory, QStringLiteral("large.png"), QColor(Qt::cyan),
                                    QSize(960, 640));
    QVERIFY(!path.isEmpty());

    RunningFlick flick;
    start(flick, {path});
    waitForScreenshot(flick);
    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("ActualSize"));
    for (int step = 0; step < 10; ++step) {
        sendCommand(flick, QByteArrayLiteral("CtrlPlus"));
        sendQueryAndWaitForReply(flick, QByteArrayLiteral("ViewState"));
    }
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("ViewState"))
                 .split(',')
                 .first()
                 .toDouble(),
             9.313226);

#if defined(Q_OS_LINUX)
    QFile processStatus(QStringLiteral("/proc/%1/status").arg(flick.process.processId()));
    QVERIFY(processStatus.open(QIODevice::ReadOnly));
    const QByteArray status = processStatus.readAll();
    const qsizetype residentMemoryLine = status.indexOf("VmRSS:");
    QVERIFY(residentMemoryLine >= 0);
    const QByteArray residentMemory =
        status.mid(residentMemoryLine + 6, status.indexOf('\n', residentMemoryLine) -
                                               residentMemoryLine - 6)
            .trimmed()
            .split(' ')
            .first();
    QVERIFY2(residentMemory.toLongLong() < 128 * 1024,
             qPrintable(QStringLiteral("Flick used %1 kB at 931% zoom")
                            .arg(QString::fromLatin1(residentMemory))));
#endif
    QVERIFY(flick.process.state() == QProcess::Running);
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
    QVERIFY(configured.process.waitForFinished(5000));

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

void FlickApplicationTest::settingsApplyImmediatelyAndPersistAcrossLaunches()
{
    const QString first =
        writeFixture(QStringLiteral("known.png.base64"), QStringLiteral("settings.png"));
    QTemporaryDir sharedConfiguration;
    QVERIFY(sharedConfiguration.isValid());
    const QString configHome = sharedConfiguration.filePath(QStringLiteral("config"));

    RunningFlick configured;
    start(configured, {first}, {}, 0, 0, configHome);
    waitForScreenshot(configured);
    sendCommand(configured, QByteArrayLiteral("ApplySettings:zoom:#123456:0:16:1"));
    QCOMPARE(sendQueryAndWaitForReply(configured, QByteArrayLiteral("SettingsState")),
             QByteArrayLiteral("zoom|#123456|hidden|16777216|restore"));
    sendCommand(configured, QByteArrayLiteral("Resize:720:480"));
    QCOMPARE(sendQueryAndWaitForReply(configured, QByteArrayLiteral("WindowGeometry")),
             QByteArrayLiteral("720x480"));
    QCOMPARE(sendQueryAndWaitForReply(configured, QByteArrayLiteral("SaveWindowGeometry")),
             QByteArrayLiteral("saved"));
    configured.process.terminate();
    QVERIFY(configured.process.waitForFinished(2000));

    RunningFlick relaunched;
    start(relaunched, {first}, {}, 0, 0, configHome);
    waitForScreenshot(relaunched);
    QCOMPARE(sendQueryAndWaitForReply(relaunched, QByteArrayLiteral("SettingsState")),
             QByteArrayLiteral("zoom|#123456|hidden|16777216|restore"));
    QCOMPARE(sendQueryAndWaitForReply(relaunched, QByteArrayLiteral("WindowGeometry")),
             QByteArrayLiteral("720x480"));
    sendCommand(relaunched, QByteArrayLiteral("ApplySettings:navigate:#202020:1:32:0"));
    QCOMPARE(sendQueryAndWaitForReply(relaunched, QByteArrayLiteral("SettingsState")),
             QByteArrayLiteral("navigate|#202020|visible|33554432|forget"));
    sendCommand(relaunched, QByteArrayLiteral("Resize:640:400"));
    QCOMPARE(sendQueryAndWaitForReply(relaunched, QByteArrayLiteral("WindowGeometry")),
             QByteArrayLiteral("640x400"));
    relaunched.process.terminate();
    QVERIFY(relaunched.process.waitForFinished(2000));

    RunningFlick withoutRestoration;
    start(withoutRestoration, {first}, {}, 0, 0, configHome);
    waitForScreenshot(withoutRestoration);
    QVERIFY(sendQueryAndWaitForReply(withoutRestoration, QByteArrayLiteral("WindowGeometry")) !=
            QByteArrayLiteral("640x400"));
}

void FlickApplicationTest::settingsDialogPreviewsCommitsRollsBackAndResets()
{
    QTemporaryDir sharedConfiguration;
    QVERIFY(sharedConfiguration.isValid());
    const QString configHome = sharedConfiguration.filePath(QStringLiteral("config"));

    RunningFlick flick;
    start(flick, {}, {}, 0, 0, configHome);
    waitForScreenshot(flick);

    sendCommand(flick, QByteArrayLiteral("OpenSettings"));
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("SettingsDialogStructure")),
             QByteArrayLiteral("Navigation[Mouse wheel action]|Appearance[Viewing surface background|Show "
                               "status overlay]|Performance & Window[Decoded cache budget|Restore "
                               "window size and position]|Reset Defaults|Cancel|Apply"));
    const QList<QByteArray> dialogSize =
        sendQueryAndWaitForReply(flick, QByteArrayLiteral("SettingsDialogGeometry")).split('x');
    QCOMPARE(dialogSize.size(), 2);
    QVERIFY(dialogSize.at(0).toInt() <= 480);
    QVERIFY(dialogSize.at(1).toInt() <= 320);
    const QByteArray focusOrder =
        sendQueryAndWaitForReply(flick, QByteArrayLiteral("SettingsDialogFocusOrder"));
    qsizetype previousPosition = -1;
    for (const QByteArray &name :
         {QByteArrayLiteral("Mouse wheel action"), QByteArrayLiteral("Viewing surface background"),
          QByteArrayLiteral("Show status overlay"), QByteArrayLiteral("Decoded cache budget"),
          QByteArrayLiteral("Restore window size and position")}) {
        const qsizetype position = focusOrder.indexOf(name);
        QVERIFY(position > previousPosition);
        previousPosition = position;
    }
    QVERIFY(focusOrder.contains("Reset Defaults"));
    QVERIFY(focusOrder.contains("Cancel"));
    QVERIFY(focusOrder.contains("Apply"));

    RunningFlick highDpiDark;
    start(highDpiDark, {}, {}, 0, 0, {}, 0, QStringLiteral("2"), true);
    waitForScreenshot(highDpiDark);
    sendCommand(highDpiDark, QByteArrayLiteral("OpenSettings"));
    const QList<QByteArray> highDpiDialogSize =
        sendQueryAndWaitForReply(highDpiDark, QByteArrayLiteral("SettingsDialogGeometry"))
            .split('x');
    QCOMPARE(highDpiDialogSize.size(), 2);
    QVERIFY(highDpiDialogSize.at(0).toInt() <= 480);
    QVERIFY(highDpiDialogSize.at(1).toInt() <= 320);
    sendCommand(flick, QByteArrayLiteral("PreviewSettings:zoom:#123456:0:16:1"));
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("SettingsState")),
             QByteArrayLiteral("zoom|#123456|hidden|16777216|restore"));
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("StoredSettingsState")),
             QByteArrayLiteral("navigate|#181a1b|visible|536870912|forget"));

    sendCommand(flick, QByteArrayLiteral("CancelSettings"));
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("SettingsState")),
             QByteArrayLiteral("navigate|#181a1b|visible|536870912|forget"));

    sendCommand(flick, QByteArrayLiteral("OpenSettings"));
    sendCommand(flick, QByteArrayLiteral("PreviewSettings:zoom:#abcdef:0:32:1"));
    sendCommand(flick, QByteArrayLiteral("ResetSettings"));
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("SettingsState")),
             QByteArrayLiteral("navigate|#181a1b|visible|536870912|forget"));
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("StoredSettingsState")),
             QByteArrayLiteral("navigate|#181a1b|visible|536870912|forget"));

    sendCommand(flick, QByteArrayLiteral("PreviewSettings:zoom:#234567:0:64:1"));
    sendCommand(flick, QByteArrayLiteral("ApplySettingsDialog"));
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("StoredSettingsState")),
             QByteArrayLiteral("zoom|#234567|hidden|67108864|restore"));
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("FocusState")),
             QByteArrayLiteral("viewing-surface"));

    flick.process.terminate();
    QVERIFY(flick.process.waitForFinished(2000));
    RunningFlick relaunched;
    start(relaunched, {}, {}, 0, 0, configHome);
    waitForScreenshot(relaunched);
    QCOMPARE(sendQueryAndWaitForReply(relaunched, QByteArrayLiteral("SettingsState")),
             QByteArrayLiteral("zoom|#234567|hidden|67108864|restore"));
}

void FlickApplicationTest::temporarilyRotatesCurrentViewAndResetsOnNavigation()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QColor firstColor(255, 69, 0);
    const QColor secondColor(50, 205, 50);
    const QString first = writeImage(directory, QStringLiteral("image1.png"), firstColor,
                                     QSize(120, 80));
    const QString second = writeImage(directory, QStringLiteral("image2.png"), secondColor,
                                      QSize(120, 80));
    QVERIFY(!first.isEmpty());
    QVERIFY(!second.isEmpty());

    QFile source(first);
    QVERIFY(source.open(QIODevice::ReadOnly));
    const QByteArray bytesBefore = source.readAll();
    source.close();

    RunningFlick flick;
    start(flick, {first});
    const QSize initialSize = colorBounds(waitForScreenshot(flick), firstColor).size();
    QVERIFY(initialSize.width() > initialSize.height());

    const QImage left = sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("RotateLeft"));
    const QSize leftSize = colorBounds(left, firstColor).size();
    QVERIFY(leftSize.height() > leftSize.width());
    const QImage fitted = sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("Fit"));
    const QSize fittedSize = colorBounds(fitted, firstColor).size();
    QVERIFY(fittedSize.height() >= 295);
    QVERIFY(fittedSize.height() > fittedSize.width());
    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("ActualSize"));
    const QImage restored =
        sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("RotateRight"));
    const QSize restoredSize = colorBounds(restored, firstColor).size();
    QVERIFY(restoredSize.width() > restoredSize.height());

    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("RotateRight"));
    const QImage next = sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("Right"));
    const QSize nextSize = colorBounds(next, secondColor).size();
    QVERIFY(nextSize.width() > nextSize.height());
    const QImage previous = sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("Left"));
    const QSize previousSize = colorBounds(previous, firstColor).size();
    QVERIFY(previousSize.width() > previousSize.height());

    QVERIFY(source.open(QIODevice::ReadOnly));
    QCOMPARE(source.readAll(), bytesBefore);

    QTemporaryDir otherDirectory;
    QVERIFY(otherDirectory.isValid());
    const QString incoming =
        writeImage(otherDirectory, QStringLiteral("incoming.png"), secondColor, QSize(120, 80));
    QVERIFY(!incoming.isEmpty());

    RunningFlick asynchronous;
    start(asynchronous, {first}, {}, 250);
    waitForScreenshot(asynchronous);
    sendCommand(asynchronous, QByteArrayLiteral("Drop:") + incoming.toUtf8());
    const QImage loaded =
        sendCommandAndWaitForScreenshot(asynchronous, QByteArrayLiteral("RotateRight"));
    const QSize loadedSize = colorBounds(loaded, secondColor).size();
    QVERIFY(loadedSize.width() > loadedSize.height());
}

void FlickApplicationTest::togglesFullscreenFromKeyboardAndPointer()
{
    const QString path =
        writeFixture(QStringLiteral("known.png.base64"), QStringLiteral("fullscreen.png"));
    QVERIFY(!path.isEmpty());

    RunningFlick flick;
    start(flick, {path});
    waitForScreenshot(flick);
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("UiState")).split('|').at(0),
             QByteArrayLiteral("windowed"));

    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("F11"));
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("UiState")).split('|').at(0),
             QByteArrayLiteral("fullscreen"));
    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("Escape"));
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("UiState")).split('|').at(0),
             QByteArrayLiteral("windowed"));

    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("DoubleClick:250:150"));
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("UiState")).split('|').at(0),
             QByteArrayLiteral("fullscreen"));
    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("DoubleClick:250:150"));
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("UiState")).split('|').at(0),
             QByteArrayLiteral("windowed"));
}

void FlickApplicationTest::transientStatusReportsViewContextAndReappearsOnMouseMovement()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QStringList sequence = writeStatusSequence(directory);
    QVERIFY(!sequence.contains(QString{}));

    RunningFlick flick;
    start(flick, {sequence.first()});
    waitForScreenshot(flick);
    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("Move:200:120"));
    QList<QByteArray> state =
        sendQueryAndWaitForReply(flick, QByteArrayLiteral("UiState")).split('|');
    QCOMPARE(state.at(1), QByteArrayLiteral("status-visible"));
    QCOMPARE(state.at(3), QByteArrayLiteral("image1.png — 1 / 2 — 100%"));

    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("CtrlWheel:250:150:120"));
    state = sendQueryAndWaitForReply(flick, QByteArrayLiteral("UiState")).split('|');
    QCOMPARE(state.at(3), QByteArrayLiteral("image1.png — 1 / 2 — 125%"));
    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("Right"));
    state = sendQueryAndWaitForReply(flick, QByteArrayLiteral("UiState")).split('|');
    QCOMPARE(state.at(3), QByteArrayLiteral("image2.png — 2 / 2 — 100%"));

    QTest::qWait(2200);
    state = sendQueryAndWaitForReply(flick, QByteArrayLiteral("UiState")).split('|');
    QCOMPARE(state.at(1), QByteArrayLiteral("status-hidden"));
    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("Move:200:120"));
    state = sendQueryAndWaitForReply(flick, QByteArrayLiteral("UiState")).split('|');
    QCOMPARE(state.at(1), QByteArrayLiteral("status-visible"));
}

void FlickApplicationTest::statusOverlayElidesLongNamesAndRestoresContextAfterFeedback()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString longName = QString(180, QLatin1Char('a')) + QStringLiteral(".png");
    const QString path = writeImage(directory, longName, QColor(Qt::red), QSize(120, 80));
    QVERIFY(!path.isEmpty());

    RunningFlick flick;
    start(flick, {path});
    waitForScreenshot(flick);
    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("Move:200:120"));
    const QList<QByteArray> initial =
        sendQueryAndWaitForReply(flick, QByteArrayLiteral("UiState")).split('|');
    QCOMPARE(initial.at(1), QByteArrayLiteral("status-visible"));
    QVERIFY(initial.at(3).contains(QByteArrayLiteral("…")));

    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("Right"));
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("Feedback")),
             QByteArrayLiteral("End of folder"));
    QTest::qWait(1600);
    const QList<QByteArray> restored =
        sendQueryAndWaitForReply(flick, QByteArrayLiteral("UiState")).split('|');
    QCOMPARE(restored.at(1), QByteArrayLiteral("status-visible"));
    QVERIFY(restored.at(3).endsWith(QByteArrayLiteral("1 / 1 — 100%")));

    sendCommand(flick, QByteArrayLiteral("ApplySettings:navigate:#181a1b:0:512:0"));
    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("Right"));
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("UiState")).split('|').at(1),
             QByteArrayLiteral("status-hidden"));
}

void FlickApplicationTest::firstUseTeachingPersistsAfterBrowsingIsLearned()
{
    QTemporaryDir directory;
    QTemporaryDir config;
    QVERIFY(directory.isValid());
    QVERIFY(config.isValid());
    const QStringList sequence = writeStatusSequence(directory);
    QVERIFY(!sequence.contains(QString{}));

    RunningFlick firstLaunch;
    start(firstLaunch, {sequence.first()}, {}, 0, 0, config.path());
    waitForScreenshot(firstLaunch);
    QCOMPARE(sendQueryAndWaitForReply(firstLaunch, QByteArrayLiteral("Feedback")),
             QByteArrayLiteral("← → Browse · Right-click for commands"));
    sendCommandAndWaitForScreenshot(firstLaunch, QByteArrayLiteral("Right"));

    RunningFlick secondLaunch;
    start(secondLaunch, {sequence.first()}, {}, 0, 0, config.path());
    waitForScreenshot(secondLaunch);
    const QByteArray status =
        sendQueryAndWaitForReply(secondLaunch, QByteArrayLiteral("Feedback"));
    QVERIFY(status.endsWith(QByteArrayLiteral("1 / 2 — 100%")));
}

void FlickApplicationTest::fullscreenTeachingAppearsOnlyOnFirstEntry()
{
    const QString path =
        writeFixture(QStringLiteral("known.png.base64"), QStringLiteral("fullscreen-teaching.png"));
    QVERIFY(!path.isEmpty());

    RunningFlick flick;
    start(flick, {path});
    waitForScreenshot(flick);
    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("F11"));
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("Feedback")),
             QByteArrayLiteral("F11 or Esc to exit fullscreen"));
    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("Escape"));
    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("F11"));
    const QByteArray normalStatus =
        sendQueryAndWaitForReply(flick, QByteArrayLiteral("Feedback"));
    QVERIFY(normalStatus.startsWith(QByteArrayLiteral("fullscreen-teaching.png — ")));
    QVERIFY(normalStatus.endsWith(QByteArrayLiteral(" — 100%")));
}

void FlickApplicationTest::fullscreenInactivityHidesStatusAndPointerWithoutBlockingKeyboard()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QStringList sequence = writeStatusSequence(directory);
    QVERIFY(!sequence.contains(QString{}));

    RunningFlick flick;
    start(flick, {sequence.first()});
    waitForScreenshot(flick);
    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("F11"));
    QTest::qWait(3700);
    QList<QByteArray> state =
        sendQueryAndWaitForReply(flick, QByteArrayLiteral("UiState")).split('|');
    QCOMPARE(state.at(1), QByteArrayLiteral("status-hidden"));
    QCOMPARE(state.at(2), QByteArrayLiteral("pointer-hidden"));

    const QImage next = sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("Right"));
    QVERIFY(containsColor(next, QColor(Qt::green)));
    state = sendQueryAndWaitForReply(flick, QByteArrayLiteral("UiState")).split('|');
    QCOMPARE(state.at(0), QByteArrayLiteral("fullscreen"));
    QCOMPARE(state.at(1), QByteArrayLiteral("status-visible"));
    QCOMPARE(state.at(2), QByteArrayLiteral("pointer-hidden"));

    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("Move:200:120"));
    state = sendQueryAndWaitForReply(flick, QByteArrayLiteral("UiState")).split('|');
    QCOMPARE(state.at(1), QByteArrayLiteral("status-visible"));
    QCOMPARE(state.at(2), QByteArrayLiteral("pointer-visible"));
}

void FlickApplicationTest::informationDialogStaysLiveWhileBrowsing()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString first =
        writeImage(directory, QStringLiteral("facts.png"), QColor(Qt::cyan), QSize(32, 24));
    const QString second =
        writeImage(directory, QStringLiteral("next.png"), QColor(Qt::magenta), QSize(16, 12));
    const QString unavailable = directory.filePath(QStringLiteral("unavailable.png"));
    QFile unavailableFile(unavailable);
    QVERIFY(unavailableFile.open(QIODevice::WriteOnly));
    QVERIFY(unavailableFile.write("not an image") > 0);
    unavailableFile.close();
    QVERIFY(!first.isEmpty());
    QVERIFY(!second.isEmpty());

    RunningFlick flick;
    start(flick, {first});
    waitForScreenshot(flick);
    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("Information"));
    const QByteArray information =
        sendQueryAndWaitForReply(flick, QByteArrayLiteral("InformationState"));
    QVERIFY(information.contains(QFileInfo(first).canonicalFilePath().toUtf8()));
    QVERIFY(information.contains("PNG"));
    QVERIFY(information.contains("32 × 24"));
    QVERIFY(information.contains(QByteArray::number(QFileInfo(first).size())));
    QVERIFY(information.contains("Modified"));
    QVERIFY(information.contains("100%"));
    QVERIFY(information.contains("Rotation: 0°"));
    QVERIFY(information.contains("Animation: Static image"));
    QVERIFY(information.contains("1 / 3"));

    sendCommand(flick, QByteArrayLiteral("FocusViewingSurface"));
    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("Right"));
    const QByteArray nextInformation =
        sendQueryAndWaitForReply(flick, QByteArrayLiteral("InformationState"));
    QVERIFY(nextInformation.contains(QFileInfo(second).canonicalFilePath().toUtf8()));
    QVERIFY(nextInformation.contains("16 × 12"));
    QVERIFY(nextInformation.contains("2 / 3"));
    QVERIFY(sendQueryAndWaitForReply(flick, QByteArrayLiteral("InformationDialogState"))
                .startsWith("open|"));
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("FocusState")),
             QByteArrayLiteral("viewing-surface"));
    const QList<QByteArray> dialogSize =
        sendQueryAndWaitForReply(flick, QByteArrayLiteral("InformationDialogState"))
            .mid(5)
            .split('x');
    QCOMPARE(dialogSize.size(), 2);
    QVERIFY(dialogSize.at(0).toInt() <= 480);
    QVERIFY(dialogSize.at(1).toInt() <= 320);

    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("RotateRight"));
    QVERIFY(sendQueryAndWaitForReply(flick, QByteArrayLiteral("InformationState"))
                .contains("Rotation: 90°"));
    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("CtrlPlus"));
    QVERIFY(sendQueryAndWaitForReply(flick, QByteArrayLiteral("InformationState"))
                .contains("Zoom: 125%"));

    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("Right"));
    QTRY_VERIFY_WITH_TIMEOUT(
        sendQueryAndWaitForReply(flick, QByteArrayLiteral("InformationState"))
            .contains("Unavailable"),
        3000);

    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("Escape"));
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("InformationDialogState")),
             QByteArrayLiteral("closed"));
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("FocusState")),
             QByteArrayLiteral("viewing-surface"));

    const QString animated =
        writeFixture(QStringLiteral("animated.gif.base64"), QStringLiteral("animated.gif"));
    QVERIFY(!animated.isEmpty());
    RunningFlick animation;
    start(animation, {animated}, {}, 0, 0, {}, 0, QStringLiteral("2"));
    waitForScreenshot(animation);
    sendCommandAndWaitForScreenshot(animation, QByteArrayLiteral("Information"));
    QVERIFY(sendQueryAndWaitForReply(animation, QByteArrayLiteral("InformationState"))
                .contains("Animation: Playing"));
    sendCommandAndWaitForScreenshot(animation, QByteArrayLiteral("Escape"));
    sendCommandAndWaitForScreenshot(animation, QByteArrayLiteral("Space"));
    sendCommandAndWaitForScreenshot(animation, QByteArrayLiteral("Information"));
    QVERIFY(sendQueryAndWaitForReply(animation, QByteArrayLiteral("InformationState"))
                .contains("Animation: Paused"));
    const QList<QByteArray> highDpiSize =
        sendQueryAndWaitForReply(animation, QByteArrayLiteral("InformationDialogState"))
            .mid(5)
            .split('x');
    QVERIFY(highDpiSize.at(0).toInt() <= 480);
    QVERIFY(highDpiSize.at(1).toInt() <= 320);
    sendCommandAndWaitForScreenshot(animation, QByteArrayLiteral("Escape"));
    sendCommandAndWaitForScreenshot(animation, QByteArrayLiteral("Space"));
    sendCommandAndWaitForScreenshot(animation, QByteArrayLiteral("Information"));
    QVERIFY(sendQueryAndWaitForReply(animation, QByteArrayLiteral("InformationState"))
                .contains("Animation: Playing"));
}

void FlickApplicationTest::informationDialogReportsNaturalAnimationCompletion()
{
    const QString animated =
        writeFixture(QStringLiteral("animated.gif.base64"), QStringLiteral("finite.gif"));
    QVERIFY(!animated.isEmpty());

    RunningFlick flick;
    start(flick, {animated});
    waitForScreenshot(flick);
    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("Information"));
    QVERIFY(sendQueryAndWaitForReply(flick, QByteArrayLiteral("InformationState"))
                .contains("Animation: Playing"));

    QTRY_VERIFY_WITH_TIMEOUT(
        sendQueryAndWaitForReply(flick, QByteArrayLiteral("InformationState"))
            .contains("Animation: Finished"),
        2000);

    const QString looping =
        writeFixture(QStringLiteral("animated.webp.base64"), QStringLiteral("looping.webp"));
    QVERIFY(!looping.isEmpty());
    RunningFlick infinite;
    start(infinite, {looping});
    waitForScreenshot(infinite);
    sendCommandAndWaitForScreenshot(infinite, QByteArrayLiteral("Information"));
    QTest::qWait(900);
    QVERIFY(sendQueryAndWaitForReply(infinite, QByteArrayLiteral("InformationState"))
                .contains("Animation: Playing"));
}

void FlickApplicationTest::copiesPathAndRenderedImageAndExposesContextCommands()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path =
        writeImage(directory, QStringLiteral("clipboard.png"), QColor(Qt::yellow), QSize(32, 24));
    QVERIFY(!path.isEmpty());

    RunningFlick flick;
    start(flick, {path});
    waitForScreenshot(flick);
    sendCommand(flick, QByteArrayLiteral("CopyPath"));
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("ClipboardText")),
             QFileInfo(path).canonicalFilePath().toUtf8());
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("Feedback")),
             QByteArrayLiteral("File path copied"));

    sendCommand(flick, QByteArrayLiteral("RotateRight"));
    sendCommand(flick, QByteArrayLiteral("CopyImage"));
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("ClipboardImageSize")),
             QByteArrayLiteral("24x32"));
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("Feedback")),
             QByteArrayLiteral("Image copied"));

    const QByteArray actions =
        sendQueryAndWaitForReply(flick, QByteArrayLiteral("ContextActions"));
    QVERIFY(actions.contains("Information [I]"));
    const auto actionLabel = [](const char *name, const QKeySequence &shortcut) {
        return QByteArray(name) + " [" + shortcut.toString(QKeySequence::NativeText).toUtf8() + ']';
    };
    QVERIFY(actions.contains(actionLabel("Copy Image", QKeySequence(Qt::CTRL | Qt::Key_C))));
    QVERIFY(actions.contains(actionLabel(
        "Copy Path", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C))));
    QVERIFY(actions.contains(actionLabel(
        "Show in File Manager", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R))));

    QTemporaryDir incomingDirectory;
    QVERIFY(incomingDirectory.isValid());
    const QString incoming = writeImage(incomingDirectory, QStringLiteral("incoming.png"),
                                        QColor(Qt::blue), QSize(16, 12));
    QVERIFY(!incoming.isEmpty());
    RunningFlick asynchronous;
    start(asynchronous, {path}, {}, 500);
    waitForScreenshot(asynchronous);
    sendCommand(asynchronous, QByteArrayLiteral("CopyPath"));
    sendCommand(asynchronous, QByteArrayLiteral("Drop:") + incoming.toUtf8());
    sendCommand(asynchronous, QByteArrayLiteral("CopyPath"));
    QCOMPARE(sendQueryAndWaitForReply(asynchronous, QByteArrayLiteral("ClipboardText")),
             QFileInfo(path).canonicalFilePath().toUtf8());
    QTRY_VERIFY_WITH_TIMEOUT(
        containsColor(captureAfter(asynchronous, 50), QColor(Qt::blue)), 5000);
    sendCommand(asynchronous, QByteArrayLiteral("CopyPath"));
    QCOMPARE(sendQueryAndWaitForReply(asynchronous, QByteArrayLiteral("ClipboardText")),
             QFileInfo(incoming).canonicalFilePath().toUtf8());
}

void FlickApplicationTest::exposesGroupedCommandSurfacesWithoutNavigationRows()
{
    RunningFlick empty;
    start(empty);
    waitForScreenshot(empty);
    QVERIFY(sendQueryAndWaitForReply(empty, QByteArrayLiteral("CommandAvailability"))
                .contains("Fit to Window=disabled"));
    sendCommandAndWaitForScreenshot(empty, QByteArrayLiteral("ContextMenu:10:10"));
    QCOMPARE(sendQueryAndWaitForReply(empty, QByteArrayLiteral("FocusState")),
             QByteArrayLiteral("menu"));

    const QString path =
        writeFixture(QStringLiteral("known.png.base64"), QStringLiteral("commands.png"));
    QVERIFY(!path.isEmpty());

    RunningFlick flick;
    start(flick, {path});
    waitForScreenshot(flick);

    const QByteArray context =
        sendQueryAndWaitForReply(flick, QByteArrayLiteral("ContextMenuStructure"));
    QCOMPARE(context,
             QByteArrayLiteral("Open Image|---|Fit to Window|Actual Size|Zoom In|Zoom Out|Toggle "
                               "Fullscreen|---|Rotate Left|Rotate Right|Pause or Resume "
                               "Animation|Information|---|Copy Image|Copy Path|Show in File "
                               "Manager|---|Settings|---|Quit Flick"));
    QVERIFY(!context.contains("Previous Image"));
    QVERIFY(!context.contains("Next Image"));

    const QByteArray application =
        sendQueryAndWaitForReply(flick, QByteArrayLiteral("ApplicationMenuStructure"));
    QVERIFY(application.contains("File[Open Image"));
    QVERIFY(application.contains("View[Fit to Window|Actual Size|Zoom In|Zoom Out|Toggle Fullscreen]"));
    QVERIFY(application.contains("Image[Rotate Left|Rotate Right|Pause or Resume Animation|Information]"));
    QVERIFY(application.contains("Help[About Flick]"));
    QVERIFY(application.contains("Settings"));
    QVERIFY(sendQueryAndWaitForReply(flick, QByteArrayLiteral("CommandAvailability"))
                .contains("Fit to Window=enabled"));
}

void FlickApplicationTest::quitCommandIsSharedAndExitsCleanly()
{
    RunningFlick flick;
    start(flick);
    waitForScreenshot(flick);

    const QByteArray context =
        sendQueryAndWaitForReply(flick, QByteArrayLiteral("ContextMenuStructure"));
    QVERIFY(context.endsWith("Settings|---|Quit Flick"));

    const QByteArray application =
        sendQueryAndWaitForReply(flick, QByteArrayLiteral("ApplicationMenuStructure"));
    QVERIFY(application.contains("File[Open Image|Settings|Quit Flick]"));
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("QuitActionState")),
             QByteArrayLiteral("shared|standard-role|standard-shortcut"));

    sendCommand(flick, QByteArrayLiteral("TriggerQuit"));
    QTRY_COMPARE_WITH_TIMEOUT(flick.process.state(), QProcess::NotRunning, 2000);
    QCOMPARE(flick.process.exitStatus(), QProcess::NormalExit);
    QCOMPARE(flick.process.exitCode(), 0);
}

void FlickApplicationTest::contextMenuStaysReachableNearEveryScreenEdge()
{
    RunningFlick flick;
    start(flick);
    waitForScreenshot(flick);

    for (const QByteArray &corner : {QByteArrayLiteral("TopLeft"),
                                     QByteArrayLiteral("TopRight"),
                                     QByteArrayLiteral("BottomLeft"),
                                     QByteArrayLiteral("BottomRight")}) {
        sendCommandAndWaitForScreenshot(flick,
                                        QByteArrayLiteral("ContextMenuAtScreenEdge:") + corner);
        const QList<QByteArray> geometry =
            sendQueryAndWaitForReply(flick, QByteArrayLiteral("ContextMenuGeometry")).split('|');
        QCOMPARE(geometry.size(), 2);
        const QList<QByteArray> menu = geometry.at(0).split(',');
        const QList<QByteArray> available = geometry.at(1).split(',');
        QCOMPARE(menu.size(), 4);
        QCOMPARE(available.size(), 4);
        const QRect menuRect(menu.at(0).toInt(), menu.at(1).toInt(), menu.at(2).toInt(),
                             menu.at(3).toInt());
        const QRect availableRect(available.at(0).toInt(), available.at(1).toInt(),
                                  available.at(2).toInt(), available.at(3).toInt());
        QVERIFY(availableRect.contains(menuRect));
        sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("Escape"));
    }
}

void FlickApplicationTest::restoresViewingFocusAndAppliesEscapePrecedence()
{
    const QString path =
        writeFixture(QStringLiteral("known.png.base64"), QStringLiteral("focus.png"));
    QVERIFY(!path.isEmpty());

    RunningFlick flick;
    start(flick, {path});
    waitForScreenshot(flick);
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("FocusState")),
             QByteArrayLiteral("viewing-surface"));

    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("Information"));
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("FocusState")),
             QByteArrayLiteral("dialog"));
    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("Escape"));
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("FocusState")),
             QByteArrayLiteral("viewing-surface"));

    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("F11"));
    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("ContextMenu:10:10"));
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("FocusState")),
             QByteArrayLiteral("menu"));
    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("Escape"));
    QVERIFY(sendQueryAndWaitForReply(flick, QByteArrayLiteral("UiState"))
                .startsWith("fullscreen"));
    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("Escape"));
    QVERIFY(sendQueryAndWaitForReply(flick, QByteArrayLiteral("UiState"))
                .startsWith("windowed"));
}

void FlickApplicationTest::revealsCurrentFileAndReportsExternalActionFailures()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path =
        writeImage(directory, QStringLiteral("reveal.png"), QColor(Qt::green), QSize(32, 24));
    QVERIFY(!path.isEmpty());

    RunningFlick flick;
    start(flick, {path});
    waitForScreenshot(flick);
    sendCommand(flick, QByteArrayLiteral("Reveal"));
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("RevealedPath")),
             QFileInfo(path).canonicalFilePath().toUtf8());

    RunningFlick failing;
    failing.process.setProcessEnvironment(QProcessEnvironment());
    start(failing, {path});
    waitForScreenshot(failing);
    sendCommand(failing, QByteArrayLiteral("FailExternalActions"));
    sendCommand(failing, QByteArrayLiteral("CopyPath"));
    QVERIFY(sendQueryAndWaitForReply(failing, QByteArrayLiteral("Feedback"))
                .contains("Could not copy"));
    QVERIFY(failing.process.state() == QProcess::Running);
    sendCommand(failing, QByteArrayLiteral("Reveal"));
    QVERIFY(sendQueryAndWaitForReply(failing, QByteArrayLiteral("Feedback"))
                .contains("Could not show"));
    QVERIFY(failing.process.state() == QProcess::Running);
}

void FlickApplicationTest::exposesAccessibleKeyboardActions()
{
    const QString path =
        writeFixture(QStringLiteral("known.png.base64"), QStringLiteral("accessible.png"));
    QVERIFY(!path.isEmpty());

    RunningFlick flick;
    start(flick, {path});
    waitForScreenshot(flick);

    const QByteArray accessibility =
        sendQueryAndWaitForReply(flick, QByteArrayLiteral("AccessibilityState"));
    QVERIFY(accessibility.contains("Viewing surface|AccessibleRole=Graphic|"));
    const QByteArray openAction =
        QByteArrayLiteral("Open Image|") +
        QKeySequence(QKeySequence::Open).toString(QKeySequence::NativeText).toUtf8();
    QVERIFY(accessibility.contains(openAction));
    const auto accessibleAction = [](const char *name, const QKeySequence &shortcut) {
        return QByteArray(name) + '|' + shortcut.toString(QKeySequence::NativeText).toUtf8();
    };
    QVERIFY(accessibility.contains(accessibleAction("Previous Image", QKeySequence(Qt::Key_Left))));
    QVERIFY(accessibility.contains(accessibleAction("Next Image", QKeySequence(Qt::Key_Right))));
    QVERIFY(accessibility.contains(accessibleAction(
        "Pan Left", QKeySequence(Qt::SHIFT | Qt::Key_Left))));
    QVERIFY(accessibility.contains(accessibleAction(
        "Pan Right", QKeySequence(Qt::SHIFT | Qt::Key_Right))));
    QVERIFY(accessibility.contains(accessibleAction(
        "Pan Up", QKeySequence(Qt::SHIFT | Qt::Key_Up))));
    QVERIFY(accessibility.contains(accessibleAction(
        "Pan Down", QKeySequence(Qt::SHIFT | Qt::Key_Down))));
    QVERIFY(accessibility.contains(accessibleAction("Fit to Window", QKeySequence(Qt::Key_F))));
    QVERIFY(accessibility.contains(accessibleAction("Actual Size", QKeySequence(Qt::Key_1))));
    QVERIFY(accessibility.contains(accessibleAction(
        "Toggle Fullscreen", QKeySequence(Qt::Key_F11))));
    QVERIFY(accessibility.contains("Settings|"));
}

void FlickApplicationTest::usesViewingSurfaceVocabularyAndMotionContract()
{
    RunningFlick flick;
    start(flick);
    waitForScreenshot(flick);

    sendCommand(flick, QByteArrayLiteral("OpenSettings"));
    const QByteArray settings =
        sendQueryAndWaitForReply(flick, QByteArrayLiteral("SettingsDialogStructure"));
    QVERIFY(settings.contains("Viewing surface background"));
    QVERIFY(!settings.contains("Viewport background"));
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("BackgroundPickerTitle")),
             QByteArrayLiteral("Viewing Surface Background"));
    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("Escape"));

    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("PresentationMotionContract")),
             QByteArrayLiteral("empty=optional-opacity|loading=immediate|displayed=immediate|"
                               "error=optional-opacity|large-image=optional-opacity|"
                               "reduced-motion=immediate"));

    QTemporaryDir invalidImageDirectory;
    QVERIFY(invalidImageDirectory.isValid());
    const QString invalidImage =
        invalidImageDirectory.filePath(QStringLiteral("invalid.png"));
    QFile invalidFile(invalidImage);
    QVERIFY(invalidFile.open(QIODevice::WriteOnly));
    QCOMPARE(invalidFile.write("not an image"), 12);
    invalidFile.close();
    RunningFlick reducedMotion;
    start(reducedMotion, {invalidImage});
    waitForScreenshot(reducedMotion);
    QVERIFY(sendQueryAndWaitForReply(reducedMotion, QByteArrayLiteral("PresentationState"))
                .startsWith("error|"));
    QCOMPARE(sendQueryAndWaitForReply(reducedMotion,
                                      QByteArrayLiteral("ActivePresentationTransition")),
             QByteArrayLiteral("error|immediate"));
}

QTEST_MAIN(FlickApplicationTest)
#include "flick_application_test.moc"
