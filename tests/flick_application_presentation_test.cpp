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

namespace {
struct ReportedViewState
{
    double zoom = 0.0;
    QSize viewportSize;
};

ReportedViewState reportedViewState(const QByteArray &reply)
{
    const QList<QByteArray> fields = reply.split(',');
    return {fields.at(0).toDouble(), QSize(fields.at(3).toInt(), fields.at(4).toInt())};
}

double fitScale(const QSize viewportSize, const QSize imageSize)
{
    return std::min(double(viewportSize.width()) / imageSize.width(),
                    double(viewportSize.height()) / imageSize.height());
}
} // namespace

class FlickApplicationPresentationTest final : public QObject, protected ApplicationProcessTest
{
    Q_OBJECT

private slots:
    void initTestCase() { initializeProcessTest(); }
    void rendersSupportedStaticFormatsAndTransparency();
    void honorsEmbeddedProfilesAndDefaultsUntaggedImagesToSrgb();
    void updatesRenderingWhenTheDisplayProfileChanges();
    void appliesExifOrientation();
    void refitsAutomaticZoomToTheCurrentViewport();
    void preservesExplicitZoomPoliciesAcrossViewportChanges();
    void appliesInitialScalingAndKeyboardZoomModes();
    void highZoomRemainsResponsiveWithoutAllocatingTheFullScaledImage();
    void pointerZoomKeepsCursorOnTheSameImagePoint();
    void pansByDragAndShiftArrowsWhilePlainArrowsNavigateAndResetView();
    void wheelActionDefaultsToNavigationWithCtrlZoom();
    void wheelActionCanSwitchToZoomWithCtrlNavigation();
    void temporarilyRotatesCurrentViewAndResetsOnNavigation();
    void transientStatusReportsViewContextAndReappearsOnMouseMovement();
    void statusOverlayElidesLongNamesAndRestoresContextAfterFeedback();
    void informationDialogStaysLiveWhileBrowsing();
};

void FlickApplicationPresentationTest::rendersSupportedStaticFormatsAndTransparency()
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

void FlickApplicationPresentationTest::honorsEmbeddedProfilesAndDefaultsUntaggedImagesToSrgb()
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

void FlickApplicationPresentationTest::updatesRenderingWhenTheDisplayProfileChanges()
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

void FlickApplicationPresentationTest::appliesExifOrientation()
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

void FlickApplicationPresentationTest::refitsAutomaticZoomToTheCurrentViewport()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QColor color(199, 21, 133);
    const QSize imageSize(1600, 1200);
    const QString path =
        writeImage(directory, QStringLiteral("large.png"), color, imageSize);
    QVERIFY(!path.isEmpty());

    RunningFlick flick;
    start(flick, {}, path);
    waitForScreenshot(flick);
    sendCommand(flick, QByteArrayLiteral("Resize:800:600"));
    sendCommandAndWaitForScreenshot(flick, QByteArrayLiteral("CtrlO"));
    const QImage displayed = captureAfter(flick, 30);
    const ReportedViewState state = reportedViewState(
        sendQueryAndWaitForReply(flick, QByteArrayLiteral("ViewState")));
    const double expected = fitScale(state.viewportSize, imageSize);

    QVERIFY(qAbs(state.zoom - expected) < 0.001);
    QCOMPARE(colorBounds(displayed, color).size(),
             QSize(qRound(imageSize.width() * expected), qRound(imageSize.height() * expected)));
}

void FlickApplicationPresentationTest::preservesExplicitZoomPoliciesAcrossViewportChanges()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QSize largeSize(1600, 1200);
    const QSize smallSize(120, 80);
    const QString large = writeImage(directory, QStringLiteral("1-large.png"), QColor(Qt::red),
                                     largeSize);
    const QString small = writeImage(directory, QStringLiteral("2-small.png"), QColor(Qt::green),
                                     smallSize);
    QVERIFY(!large.isEmpty());
    QVERIFY(!small.isEmpty());

    const auto viewState = [this](RunningFlick &flick) {
        return reportedViewState(
            sendQueryAndWaitForReply(flick, QByteArrayLiteral("ViewState")));
    };

    RunningFlick automatic;
    start(automatic, {large});
    waitForScreenshot(automatic);
    for (const QSize windowSize : {QSize(800, 600), QSize(600, 420)}) {
        sendCommand(automatic, QByteArrayLiteral("Resize:") +
                                   QByteArray::number(windowSize.width()) + ':' +
                                   QByteArray::number(windowSize.height()));
        captureAfter(automatic, 30);
        const ReportedViewState state = viewState(automatic);
        QVERIFY(qAbs(state.zoom - fitScale(state.viewportSize, largeSize)) < 0.001);
    }
    sendCommandAndWaitForScreenshot(automatic, QByteArrayLiteral("F11"));
    ReportedViewState state = viewState(automatic);
    QVERIFY(qAbs(state.zoom - fitScale(state.viewportSize, largeSize)) < 0.001);
    sendCommandAndWaitForScreenshot(automatic, QByteArrayLiteral("Escape"));
    state = viewState(automatic);
    QVERIFY(qAbs(state.zoom - fitScale(state.viewportSize, largeSize)) < 0.001);

    sendCommandAndWaitForScreenshot(automatic, QByteArrayLiteral("ActualSize"));
    sendCommand(automatic, QByteArrayLiteral("Resize:760:520"));
    captureAfter(automatic, 30);
    QCOMPARE(viewState(automatic).zoom, 1.0);
    sendCommandAndWaitForScreenshot(automatic, QByteArrayLiteral("CtrlPlus"));
    const double manualZoom = viewState(automatic).zoom;
    sendCommandAndWaitForScreenshot(automatic, QByteArrayLiteral("F11"));
    QCOMPARE(viewState(automatic).zoom, manualZoom);
    sendCommandAndWaitForScreenshot(automatic, QByteArrayLiteral("Escape"));
    QCOMPARE(viewState(automatic).zoom, manualZoom);

    sendCommandAndWaitForScreenshot(automatic, QByteArrayLiteral("Right"));
    state = viewState(automatic);
    QCOMPARE(state.zoom, 1.0);
    sendCommand(automatic, QByteArrayLiteral("Resize:900:700"));
    captureAfter(automatic, 30);
    QCOMPARE(viewState(automatic).zoom, 1.0);

    sendCommandAndWaitForScreenshot(automatic, QByteArrayLiteral("Fit"));
    state = viewState(automatic);
    QVERIFY(state.zoom > 1.0);
    QVERIFY(qAbs(state.zoom - fitScale(state.viewportSize, smallSize)) < 0.001);
    sendCommand(automatic, QByteArrayLiteral("Resize:640:480"));
    captureAfter(automatic, 30);
    state = viewState(automatic);
    QVERIFY(state.zoom > 1.0);
    QVERIFY(qAbs(state.zoom - fitScale(state.viewportSize, smallSize)) < 0.001);
}

void FlickApplicationPresentationTest::appliesInitialScalingAndKeyboardZoomModes()
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
    const QImage initialLarge = waitForScreenshot(largeFlick);
    const QList<QByteArray> viewport =
        sendQueryAndWaitForReply(largeFlick, QByteArrayLiteral("ViewState")).split(',');
    const QSize visibleViewport(viewport.at(3).toInt(), viewport.at(4).toInt());
    const double initialFit = fitScale(visibleViewport, QSize(960, 640));
    QCOMPARE(colorBounds(initialLarge, color).size(),
             QSize(qRound(960 * initialFit), qRound(640 * initialFit)));
    const QImage largeActual =
        sendCommandAndWaitForScreenshot(largeFlick, QByteArrayLiteral("ActualSize"));
    QCOMPARE(colorBounds(largeActual, color).size(), visibleViewport);
}

void FlickApplicationPresentationTest::highZoomRemainsResponsiveWithoutAllocatingTheFullScaledImage()
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

void FlickApplicationPresentationTest::pointerZoomKeepsCursorOnTheSameImagePoint()
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

void FlickApplicationPresentationTest::pansByDragAndShiftArrowsWhilePlainArrowsNavigateAndResetView()
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

void FlickApplicationPresentationTest::wheelActionDefaultsToNavigationWithCtrlZoom()
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

void FlickApplicationPresentationTest::wheelActionCanSwitchToZoomWithCtrlNavigation()
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

void FlickApplicationPresentationTest::temporarilyRotatesCurrentViewAndResetsOnNavigation()
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

void FlickApplicationPresentationTest::transientStatusReportsViewContextAndReappearsOnMouseMovement()
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

void FlickApplicationPresentationTest::statusOverlayElidesLongNamesAndRestoresContextAfterFeedback()
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

void FlickApplicationPresentationTest::informationDialogStaysLiveWhileBrowsing()
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
        writeFixture(QStringLiteral("animated.webp.base64"), QStringLiteral("animated.webp"));
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


QTEST_MAIN(FlickApplicationPresentationTest)
#include "flick_application_presentation_test.moc"
