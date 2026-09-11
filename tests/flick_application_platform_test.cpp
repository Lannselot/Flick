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

class FlickApplicationPlatformTest final : public QObject, protected ApplicationProcessTest
{
    Q_OBJECT

private slots:
    void initTestCase() { initializeProcessTest(); }
    void togglesFullscreenFromKeyboardAndPointer();
    void firstUseTeachingPersistsAfterBrowsingIsLearned();
    void fullscreenTeachingAppearsOnlyOnFirstEntry();
    void fullscreenInactivityHidesStatusAndPointerWithoutBlockingKeyboard();
    void revealsCurrentFileAndReportsExternalActionFailures();
    void usesViewingSurfaceVocabularyAndMotionContract();
};

void FlickApplicationPlatformTest::togglesFullscreenFromKeyboardAndPointer()
{
    const QString path =
        writeFixture(QStringLiteral("known.png.base64"), QStringLiteral("fullscreen.png"));
    QVERIFY(!path.isEmpty());

    RunningFlick flick;
    start(flick, {path});
    waitForScreenshot(flick);
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("UiState")).split('|').at(0),
             QByteArrayLiteral("windowed"));
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("ApplicationMenuVisibility")),
             QByteArrayLiteral("visible"));

    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("F11"));
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("UiState")).split('|').at(0),
             QByteArrayLiteral("fullscreen"));
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("ApplicationMenuVisibility")),
             QByteArrayLiteral("hidden"));
    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("Escape"));
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("UiState")).split('|').at(0),
             QByteArrayLiteral("windowed"));
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("ApplicationMenuVisibility")),
             QByteArrayLiteral("visible"));

    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("DoubleClick:250:150"));
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("UiState")).split('|').at(0),
             QByteArrayLiteral("fullscreen"));
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("ApplicationMenuVisibility")),
             QByteArrayLiteral("hidden"));
    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("DoubleClick:250:150"));
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("UiState")).split('|').at(0),
             QByteArrayLiteral("windowed"));
    QCOMPARE(sendQueryAndWaitForReply(flick, QByteArrayLiteral("ApplicationMenuVisibility")),
             QByteArrayLiteral("visible"));
}

void FlickApplicationPlatformTest::firstUseTeachingPersistsAfterBrowsingIsLearned()
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

void FlickApplicationPlatformTest::fullscreenTeachingAppearsOnlyOnFirstEntry()
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

void FlickApplicationPlatformTest::fullscreenInactivityHidesStatusAndPointerWithoutBlockingKeyboard()
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

void FlickApplicationPlatformTest::revealsCurrentFileAndReportsExternalActionFailures()
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

void FlickApplicationPlatformTest::usesViewingSurfaceVocabularyAndMotionContract()
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

QTEST_MAIN(FlickApplicationPlatformTest)
#include "flick_application_platform_test.moc"
