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

class FlickApplicationAnimationTest final : public QObject, protected ApplicationProcessTest
{
    Q_OBJECT

private slots:
    void initTestCase() { initializeProcessTest(); }
    void animatedGifPreservesTimingAndFiniteLoop();
    void animatedWebpPreservesTimingAndLoops();
    void spacePausesAndResumesAnimationButDoesNotAffectStaticImages();
    void informationDialogReportsNaturalAnimationCompletion();
};

void FlickApplicationAnimationTest::animatedGifPreservesTimingAndFiniteLoop()
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

void FlickApplicationAnimationTest::animatedWebpPreservesTimingAndLoops()
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

void FlickApplicationAnimationTest::spacePausesAndResumesAnimationButDoesNotAffectStaticImages()
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

void FlickApplicationAnimationTest::informationDialogReportsNaturalAnimationCompletion()
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


QTEST_MAIN(FlickApplicationAnimationTest)
#include "flick_application_animation_test.moc"
