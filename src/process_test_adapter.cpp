// SPDX-License-Identifier: GPL-3.0-or-later

#include "process_test_adapter.h"

#include "flick_application.h"
#include "platform_services.h"
#include "viewer_window_test_control.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QSocketNotifier>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QWheelEvent>
#include <QWidget>

#include <cstdio>
#include <functional>
#include <memory>
#include <unistd.h>

namespace {
#ifdef FLICK_ENABLE_TEST_HARNESS
void captureVisibleWindow(ViewerWindowTestControl &window)
{
    const QString screenshotPath = qEnvironmentVariable("FLICK_TEST_SCREENSHOT_FILE");
    if (screenshotPath.isEmpty()) {
        return;
    }
    window.capture(screenshotPath);
}

void scheduleCapture(ViewerWindowTestControl &window, QObject &context, const bool waitUntilReady)
{
    auto capture = std::make_shared<std::function<void()>>();
    auto readyForCapture = std::make_shared<bool>(false);
    *capture = [&window, &context, waitUntilReady, capture, readyForCapture] {
        if (waitUntilReady && window.isLoading()) {
            QTimer::singleShot(10, &context, *capture);
            return;
        }
        if (waitUntilReady && !*readyForCapture) {
            *readyForCapture = true;
            QTimer::singleShot(0, &context, *capture);
            return;
        }
        captureVisibleWindow(window);
    };
    QTimer::singleShot(0, &context, *capture);
}
#endif
} // namespace

#ifdef FLICK_ENABLE_TEST_HARNESS
void installProcessTestAdapter(ViewerWindowTestControl &window,
                                                    FlickApplication &application,
                                                    TestPlatformServices &platformServices)
{
    scheduleCapture(window, application, true);
    auto *testCommands = new QSocketNotifier(STDIN_FILENO, QSocketNotifier::Read, &application);
    QObject::connect(
        testCommands, &QSocketNotifier::activated, &application,
        [&application, &window, &platformServices] {
            char command[4096] = {};
            const auto bytesRead = ::read(STDIN_FILENO, command, sizeof(command));
            if (bytesRead <= 0) {
                return;
            }
            const QByteArray input(command, bytesRead);
            const bool captureImmediately = input.startsWith("Capture");
            if (input.startsWith("CacheBytes")) {
                fprintf(stdout, "%lld\n", static_cast<long long>(window.cacheBytes()));
                fflush(stdout);
                return;
            } else if (input.startsWith("UiResponsiveness")) {
                fprintf(stdout, "%d|%s\n", window.decodesInFlight() > 0 ? 1 : 0,
                        window.uiState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("SettingsState")) {
                fprintf(stdout, "%s\n", window.settingsState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("StoredSettingsState")) {
                fprintf(stdout, "%s\n", window.storedSettingsState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("SettingsFileName")) {
                fprintf(stdout, "%s\n", window.settingsFileName().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("SettingsDialogStructure")) {
                fprintf(stdout, "%s\n", window.settingsDialogStructure().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("SettingsDialogGeometry")) {
                fprintf(stdout, "%s\n", window.settingsDialogGeometry().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("SettingsDialogFocusOrder")) {
                fprintf(stdout, "%s\n", window.settingsDialogFocusOrder().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("LastPickerDirectory")) {
                fprintf(stdout, "%s\n",
                        QSettings()
                            .value(QStringLiteral("filePicker/lastDirectory"))
                            .toString()
                            .toUtf8()
                            .constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("WindowGeometry")) {
                fprintf(stdout, "%s\n", window.windowGeometryState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("PresentationState")) {
                fprintf(stdout, "%s\n", window.presentationState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("PresentationMotionContract")) {
                fprintf(stdout, "%s\n", window.presentationMotionContract().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("ActivePresentationTransition")) {
                fprintf(stdout, "%s\n", window.activePresentationTransition().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("BackgroundPickerTitle")) {
                fprintf(stdout, "%s\n", window.backgroundPickerTitle().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("SaveWindowGeometry")) {
                window.persistWindowGeometry();
                fprintf(stdout, "saved\n");
                fflush(stdout);
                return;
            } else if (input.startsWith("ApplySettings:")) {
                window.applyTestSettings(
                    QString::fromUtf8(input.mid(14).trimmed()).split(QLatin1Char(':')));
                return;
            } else if (input.startsWith("OpenSettings")) {
                window.triggerAction(QStringLiteral("settingsAction"));
                return;
            } else if (input.startsWith("PreviewSettings:")) {
                window.previewTestSettings(
                    QString::fromUtf8(input.mid(16).trimmed()).split(QLatin1Char(':')));
                return;
            } else if (input.startsWith("ResetSettings")) {
                window.resetTestSettings();
                return;
            } else if (input.startsWith("ApplySettingsDialog")) {
                window.finishTestSettings(true);
                return;
            } else if (input.startsWith("CancelSettings")) {
                window.finishTestSettings(false);
                return;
            } else if (input.startsWith("DisplayProfileChanged:")) {
                platformServices.setDisplayIccProfile(
                    QString::fromUtf8(input.mid(22).trimmed()));
                window.displayConfigurationChanged();
            } else if (input.startsWith("Resize:")) {
                const QList<QByteArray> size = input.mid(7).trimmed().split(':');
                if (size.size() == 2) {
                    window.resize(size.at(0).toInt(), size.at(1).toInt());
                }
                return;
            } else if (input.startsWith("Close")) {
                window.close();
                application.quit();
                return;
            } else if (input.startsWith("ViewState")) {
                fprintf(stdout, "%s\n", window.viewState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("UiState")) {
                fprintf(stdout, "%s\n", window.uiState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("FocusViewingSurface")) {
                window.focusViewingSurfaceForTest();
                return;
            } else if (input.startsWith("InformationState")) {
                fprintf(stdout, "%s\n", window.informationState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("InformationDialogState")) {
                fprintf(stdout, "%s\n", window.informationDialogState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("ClipboardText")) {
                fprintf(stdout, "%s\n", QApplication::clipboard()->text().toUtf8().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("ClipboardImageSize")) {
                const QSize size = QApplication::clipboard()->image().size();
                fprintf(stdout, "%dx%d\n", size.width(), size.height());
                fflush(stdout);
                return;
            } else if (input.startsWith("ContextActions")) {
                fprintf(stdout, "%s\n", window.contextActions().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("ContextMenuStructure")) {
                fprintf(stdout, "%s\n", window.contextMenuStructure().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("ContextMenuGeometry")) {
                fprintf(stdout, "%s\n", window.contextMenuGeometry().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("ApplicationMenuStructure")) {
                fprintf(stdout, "%s\n", window.applicationMenuStructure().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("CommandAvailability")) {
                fprintf(stdout, "%s\n", window.commandAvailability().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("QuitActionState")) {
                fprintf(stdout, "%s\n", window.quitActionState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("TriggerQuit")) {
                window.triggerAction(QStringLiteral("applicationQuitAction"));
                return;
            } else if (input.startsWith("FocusState")) {
                fprintf(stdout, "%s\n", window.focusState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("AccessibilityState")) {
                fprintf(stdout, "%s\n", window.accessibilityState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("RevealedPath")) {
                fprintf(stdout, "%s\n", platformServices.revealedPath().toUtf8().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("Feedback")) {
                fprintf(stdout, "%s\n", window.feedbackState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("ErrorState")) {
                fprintf(stdout, "%s\n", window.errorState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("LargeImageState")) {
                fprintf(stdout, "%s\n", window.largeImageState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("PrimaryActionState")) {
                fprintf(stdout, "%s\n", window.primaryActionState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("FocusDetails")) {
                window.focusDetails();
                return;
            } else if (input.startsWith("FocusSkip")) {
                window.focusSkip();
                return;
            } else if (input.startsWith("ToggleDetails")) {
                window.toggleDetails();
            } else if (input.startsWith("ApproveLarge")) {
                window.approveLargeImage();
            } else if (input.startsWith("RejectLarge")) {
                window.rejectLargeImage();
            } else if (input.startsWith("FailExternalActions")) {
                window.failExternalActionsForTest();
                return;
            } else if (input.startsWith("DecodeCount:")) {
                const QString path = QString::fromUtf8(input.mid(12).trimmed());
                fprintf(stdout, "%d\n", window.decodeCount(path));
                fflush(stdout);
                return;
            } else if (input.startsWith("Drop:")) {
                const QList<QByteArray> encodedPaths = input.mid(5).trimmed().split('|');
                auto *mimeData = new QMimeData;
                QList<QUrl> urls;
                for (const QByteArray &encodedPath : encodedPaths) {
                    urls.append(QUrl::fromLocalFile(QString::fromUtf8(encodedPath)));
                }
                mimeData->setUrls(urls);
                QDragEnterEvent dragEvent(window.rect(ViewerWindowTestTarget::Window).center(), Qt::CopyAction, mimeData,
                                          Qt::LeftButton, Qt::NoModifier);
                window.sendEvent(ViewerWindowTestTarget::Window, dragEvent);
                QDropEvent event(QPointF(window.rect(ViewerWindowTestTarget::Window).center()), Qt::CopyAction, mimeData,
                                 Qt::LeftButton, Qt::NoModifier);
                window.sendEvent(ViewerWindowTestTarget::Window, event);
                delete mimeData;
            } else if (input.startsWith("BeginDrag:")) {
                auto *mimeData = new QMimeData;
                mimeData->setUrls(
                    {QUrl::fromLocalFile(QString::fromUtf8(input.mid(10).trimmed()))});
                QDragEnterEvent event(window.rect(ViewerWindowTestTarget::Window).center(), Qt::CopyAction, mimeData,
                                      Qt::LeftButton, Qt::NoModifier);
                window.sendEvent(ViewerWindowTestTarget::Window, event);
                delete mimeData;
            } else if (input.startsWith("LeaveDrag")) {
                QDragLeaveEvent event;
                window.sendEvent(ViewerWindowTestTarget::Window, event);
            } else if (input.startsWith("SelectZoomWheelAction")) {
                window.triggerAction(QStringLiteral("wheelZoomAction"));
            } else if (input.startsWith("ContextMenuAtScreenEdge:")) {
                const QRect available =
                    window.availableScreenGeometry(ViewerWindowTestTarget::ViewingSurface);
                const QByteArray corner = input.mid(24).trimmed();
                const QPoint globalPosition = corner == "TopRight"      ? available.topRight()
                                              : corner == "BottomLeft"  ? available.bottomLeft()
                                              : corner == "BottomRight" ? available.bottomRight()
                                                                        : available.topLeft();
                QContextMenuEvent event(QContextMenuEvent::Mouse,
                                        window.rect(ViewerWindowTestTarget::ViewingSurface).center(),
                                        globalPosition);
                window.sendEvent(ViewerWindowTestTarget::ViewingSurface, event);
            } else if (input.startsWith("ContextMenu:")) {
                const QList<QByteArray> parts = input.trimmed().split(':');
                if (parts.size() == 3) {
                    const QPoint position(parts.at(1).toInt(), parts.at(2).toInt());
                    QContextMenuEvent event(QContextMenuEvent::Mouse, position,
                                            window.mapToGlobal(
                                                ViewerWindowTestTarget::ViewingSurface, position));
                    window.sendEvent(ViewerWindowTestTarget::ViewingSurface, event);
                }
            } else if (input.startsWith("MenuDown") || input.startsWith("MenuEnter")) {
                if (QWidget *menu = QApplication::activePopupWidget()) {
                    const int key = input.startsWith("MenuDown") ? Qt::Key_Down : Qt::Key_Return;
                    QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
                    QApplication::sendEvent(menu, &event);
                }
            } else if (input.startsWith("Wheel:") || input.startsWith("CtrlWheel:")) {
                const QList<QByteArray> parts = input.trimmed().split(':');
                if (parts.size() == 4) {
                    const QPointF position(parts.at(1).toInt(), parts.at(2).toInt());
                    const Qt::KeyboardModifiers modifiers =
                        input.startsWith("CtrlWheel:") ? Qt::ControlModifier : Qt::NoModifier;
                    QWheelEvent event(position, position, QPoint(), QPoint(0, parts.at(3).toInt()),
                                      Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
                    window.sendEvent(ViewerWindowTestTarget::Viewport, event);
                }
            } else if (input.startsWith("Drag:")) {
                const QList<QByteArray> parts = input.trimmed().split(':');
                if (parts.size() == 5) {
                    const QPointF start(parts.at(1).toInt(), parts.at(2).toInt());
                    const QPointF end(parts.at(3).toInt(), parts.at(4).toInt());
                    QMouseEvent press(QEvent::MouseButtonPress, start, start, start, Qt::LeftButton,
                                      Qt::LeftButton, Qt::NoModifier);
                    window.sendEvent(ViewerWindowTestTarget::Viewport, press);
                    QMouseEvent move(QEvent::MouseMove, end, end, end, Qt::NoButton, Qt::LeftButton,
                                     Qt::NoModifier);
                    window.sendEvent(ViewerWindowTestTarget::Viewport, move);
                    QMouseEvent release(QEvent::MouseButtonRelease, end, end, end, Qt::LeftButton,
                                        Qt::NoButton, Qt::NoModifier);
                    window.sendEvent(ViewerWindowTestTarget::Viewport, release);
                }
            } else if (input.startsWith("Move:") || input.startsWith("DoubleClick:")) {
                const QList<QByteArray> parts = input.trimmed().split(':');
                if (parts.size() == 3) {
                    const QPointF position(parts.at(1).toInt(), parts.at(2).toInt());
                    const QEvent::Type type = input.startsWith("DoubleClick:")
                                                  ? QEvent::MouseButtonDblClick
                                                  : QEvent::MouseMove;
                    const Qt::MouseButton button =
                        type == QEvent::MouseButtonDblClick ? Qt::LeftButton : Qt::NoButton;
                    QMouseEvent event(type, position, position, position, button, button,
                                      Qt::NoModifier);
                    window.sendEvent(ViewerWindowTestTarget::Viewport, event);
                }
            } else if (!input.startsWith("Capture")) {
                const bool ctrlO = input.startsWith("CtrlO");
                const bool copyImage = input.startsWith("CopyImage");
                const bool copyPath = input.startsWith("CopyPath");
                const bool reveal = input.startsWith("Reveal");
                const bool ctrlPlus = input.startsWith("CtrlPlus");
                const bool ctrlMinus = input.startsWith("CtrlMinus");
                const bool shift = input.startsWith("Shift");
                const int qtKey = ctrlO                             ? Qt::Key_O
                                  : copyImage                       ? Qt::Key_C
                                  : copyPath                        ? Qt::Key_C
                                  : reveal                          ? Qt::Key_R
                                  : ctrlPlus                        ? Qt::Key_Plus
                                  : ctrlMinus                       ? Qt::Key_Minus
                                  : input.startsWith("Information") ? Qt::Key_I
                                  : input.startsWith("Fit")         ? Qt::Key_F
                                  : input.startsWith("ActualSize")  ? Qt::Key_1
                                  : input.startsWith("ShiftLeft")   ? Qt::Key_Left
                                  : input.startsWith("ShiftRight")  ? Qt::Key_Right
                                  : input.startsWith("ShiftUp")     ? Qt::Key_Up
                                  : input.startsWith("ShiftDown")   ? Qt::Key_Down
                                  : input.startsWith("RotateLeft")  ? Qt::Key_L
                                  : input.startsWith("RotateRight") ? Qt::Key_R
                                  : input.startsWith("F11")         ? Qt::Key_F11
                                  : input.startsWith("Refresh")     ? Qt::Key_F5
                                  : input.startsWith("Enter")       ? Qt::Key_Return
                                  : input.startsWith("Escape")      ? Qt::Key_Escape
                                  : input.startsWith("Left")        ? Qt::Key_Left
                                  : input.startsWith("Space")       ? Qt::Key_Space
                                                                    : Qt::Key_Right;
                QKeyEvent event(QEvent::KeyPress, qtKey,
                                (ctrlO || copyImage || ctrlPlus || ctrlMinus) ? Qt::ControlModifier
                                : (copyPath || reveal) ? Qt::ControlModifier | Qt::ShiftModifier
                                : shift                ? Qt::ShiftModifier
                                                       : Qt::NoModifier);
                window.sendKeyEvent(event, input.startsWith("Enter"));
            }
            scheduleCapture(window, application, !captureImmediately);
        });
}
#endif
