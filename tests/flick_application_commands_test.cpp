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

class FlickApplicationCommandsTest final : public QObject, protected ApplicationProcessTest
{
    Q_OBJECT

private slots:
    void initTestCase() { initializeProcessTest(); }
    void copiesPathAndRenderedImageAndExposesContextCommands();
    void exposesGroupedCommandSurfacesWithoutNavigationRows();
    void quitCommandIsSharedAndExitsCleanly();
    void contextMenuStaysReachableNearEveryScreenEdge();
    void restoresViewingFocusAndAppliesEscapePrecedence();
    void exposesAccessibleKeyboardActions();
};

void FlickApplicationCommandsTest::copiesPathAndRenderedImageAndExposesContextCommands()
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

void FlickApplicationCommandsTest::exposesGroupedCommandSurfacesWithoutNavigationRows()
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

void FlickApplicationCommandsTest::quitCommandIsSharedAndExitsCleanly()
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

void FlickApplicationCommandsTest::contextMenuStaysReachableNearEveryScreenEdge()
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

void FlickApplicationCommandsTest::restoresViewingFocusAndAppliesEscapePrecedence()
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

void FlickApplicationCommandsTest::exposesAccessibleKeyboardActions()
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


QTEST_MAIN(FlickApplicationCommandsTest)
#include "flick_application_commands_test.moc"
