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

class FlickApplicationSettingsTest final : public QObject, protected ApplicationProcessTest
{
    Q_OBJECT

private slots:
    void initTestCase() { initializeProcessTest(); }
    void settingsApplyImmediatelyAndPersistAcrossLaunches();
    void testHarnessUsesExplicitSettingsRoot();
    void settingsDialogPreviewsCommitsRollsBackAndResets();
};

void FlickApplicationSettingsTest::settingsApplyImmediatelyAndPersistAcrossLaunches()
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

void FlickApplicationSettingsTest::testHarnessUsesExplicitSettingsRoot()
{
    QTemporaryDir settings;
    QVERIFY(settings.isValid());

    RunningFlick flick;
    start(flick, {}, {}, 0, 0, {}, 0, {}, false, settings.path());
    waitForScreenshot(flick);

    const QString settingsFile = QString::fromUtf8(
        sendQueryAndWaitForReply(flick, QByteArrayLiteral("SettingsFileName")));
    QVERIFY2(settingsFile.startsWith(settings.path()), qPrintable(settingsFile));
}

void FlickApplicationSettingsTest::settingsDialogPreviewsCommitsRollsBackAndResets()
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


QTEST_MAIN(FlickApplicationSettingsTest)
#include "flick_application_settings_test.moc"
