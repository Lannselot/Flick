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
#include "application_process_test_support.h"

class FlickApplicationBrowsingTest final : public QObject, protected ApplicationProcessTest
{
    Q_OBJECT

private slots:
    void initTestCase() { initializeProcessTest(); }
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
};

void FlickApplicationBrowsingTest::displaysPngInTopLevelWindow()
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

void FlickApplicationBrowsingTest::displaysJpegInTopLevelWindow()
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

void FlickApplicationBrowsingTest::displaysStableEmptyStateWithoutImage()
{
    RunningFlick flick;
    start(flick);
    const QImage screenshot = waitForScreenshot(flick);
    QCOMPARE(screenshot.size(), QSize(480, 320));
    QVERIFY(!containsColor(screenshot, PngFixtureColor));
    QVERIFY(!containsColor(screenshot, JpegFixtureColor, JpegColorTolerance));
}

void FlickApplicationBrowsingTest::presentsCoherentEmptyErrorAndLargeImageStates()
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

void FlickApplicationBrowsingTest::delaysLoadingPresentationAndClearsPreviousImage()
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
    start(flick, {first}, {}, 2000, 0, {}, 0, {}, false, {}, second);
    QVERIFY(containsColor(waitForScreenshot(flick), firstColor));

    sendCommand(flick, QByteArrayLiteral("Right"));
    QTest::qWait(50);
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("PresentationState")),
             QByteArrayLiteral("loading|image2.png|indicator-hidden"));
    const QImage beforeThreshold = captureAfter(flick, 0);
    QVERIFY(!containsColor(beforeThreshold, firstColor));

    QTest::qWait(500);
    const QByteArray loading =
        sendQueryAndWaitForReply(flick, QByteArrayLiteral("PresentationState"));
    QCOMPARE(loading, QByteArrayLiteral("loading|image2.png|indicator-visible"));
}

void FlickApplicationBrowsingTest::validDragFeedbackRestoresThePreviousPresentation()
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

void FlickApplicationBrowsingTest::separateInvocationsRemainIndependent()
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

void FlickApplicationBrowsingTest::browsesNaturallySortedVisibleSupportedImages()
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

void FlickApplicationBrowsingTest::opensSelectedImageWithCtrlO()
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

void FlickApplicationBrowsingTest::singleImageDropBrowsesContainingDirectory()
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

void FlickApplicationBrowsingTest::multipleImageDropBrowsesOnlySupportedDroppedFilesInNaturalOrder()
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

void FlickApplicationBrowsingTest::directorySequenceTracksExternalFilesystemChanges()
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

void FlickApplicationBrowsingTest::explicitListIgnoresExternalDirectoryAdditions()
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

void FlickApplicationBrowsingTest::cancelledPickerAndUnsupportedDropRemainStable()
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

void FlickApplicationBrowsingTest::decodingRemainsResponsiveAndStaleResultsAreIgnored()
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

void FlickApplicationBrowsingTest::prefetchedImagesAreReusedAndCacheIsBounded()
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

void FlickApplicationBrowsingTest::decodeFailureExplainsTheProblemAndKeepsNavigationUsable()
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

void FlickApplicationBrowsingTest::technicalDetailsRemainSecondaryAndEnterRecoversAfterRepair()
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

void FlickApplicationBrowsingTest::extremeDimensionsRequireConfirmationBeforeBackgroundDecode()
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


QTEST_MAIN(FlickApplicationBrowsingTest)
#include "flick_application_browsing_test.moc"
