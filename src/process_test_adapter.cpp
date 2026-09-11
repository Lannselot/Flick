// SPDX-License-Identifier: GPL-3.0-or-later

#include "process_test_adapter.h"

#include "flick_application.h"
#include "platform_services.h"
#include "settings_editor.h"
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
QByteArray encodeRect(const QRect &rect)
{
    return QByteArray::number(rect.x()) + ',' + QByteArray::number(rect.y()) + ',' +
           QByteArray::number(rect.width()) + ',' + QByteArray::number(rect.height());
}

QByteArray stateName(const ViewingSurface::State state)
{
    switch (state) {
    case ViewingSurface::State::Empty: return "empty";
    case ViewingSurface::State::Loading: return "loading";
    case ViewingSurface::State::Displayed: return "displayed";
    case ViewingSurface::State::Error: return "error";
    case ViewingSurface::State::LargeImageConfirmation: return "large-image";
    }
    return {};
}

QByteArray describeSettings(const Settings::Values &values)
{
    return QByteArray(values.wheelAction == Settings::WheelAction::Zoom ? "zoom" : "navigate") +
           '|' + values.background.name().toUtf8() + '|' +
           (values.statusVisible ? "visible" : "hidden") + '|' +
           QByteArray::number(values.cacheBudgetBytes) + '|' +
           (values.restoreWindowGeometry ? "restore" : "forget");
}

std::optional<Settings::Values> parseSettings(const QByteArray &input)
{
    const QList<QByteArray> fields = input.trimmed().split(':');
    if (fields.size() != 5) return std::nullopt;
    return Settings::Values{fields.at(0) == "zoom" ? Settings::WheelAction::Zoom
                                                    : Settings::WheelAction::Navigate,
                            QColor(QString::fromUtf8(fields.at(1))), fields.at(2).toInt() != 0,
                            fields.at(3).toLongLong() * 1024 * 1024,
                            fields.at(4).toInt() != 0};
}

QByteArray describeActions(const QList<TestActionSnapshot> &actions, const bool nested)
{
    QList<QByteArray> entries;
    for (const auto &action : actions) {
        if (action.separator) entries.append("---");
        else if (nested && !action.children.isEmpty())
            entries.append(action.text.toUtf8() + '[' + describeActions(action.children, true) + ']');
        else entries.append(action.text.toUtf8());
    }
    return entries.join('|');
}

QByteArray describePresentation(const ViewingSurface::PresentationSnapshot &state)
{
    if (state.dropTargetVisible) return "drop|" + state.dropTargetText.toUtf8();
    switch (state.state) {
    case ViewingSurface::State::Empty:
        return QByteArrayLiteral("empty|Open an image|Choose file|or drop it here|← → Browse · Wheel Navigate · Right-click Commands");
    case ViewingSurface::State::Loading:
        return "loading|" + state.loadingFilename.toUtf8() + '|' +
               (state.loadingIndicatorVisible ? "indicator-visible" : "indicator-hidden");
    case ViewingSurface::State::Displayed: return "displayed";
    case ViewingSurface::State::Error:
        return "error|" + state.errorExplanation.toUtf8() + '|' + state.errorRetryText.toUtf8() +
               '|' + state.errorDetailsActionText.toUtf8() + '|' + state.errorNavigationHint.toUtf8();
    case ViewingSurface::State::LargeImageConfirmation:
        return "large-image|" + state.largeImageExplanation.toUtf8() + "|Open anyway|Skip";
    }
    return {};
}

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
        if (waitUntilReady && window.snapshot().performance.loading) {
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
                fprintf(stdout, "%lld\n", static_cast<long long>(window.snapshot().performance.cacheBytes));
                fflush(stdout);
                return;
            } else if (input.startsWith("UiResponsiveness")) {
                const auto state = window.snapshot();
                const QByteArray ui = QByteArray(state.view.fullScreen ? "fullscreen" : "windowed") + '|' +
                    (state.view.statusVisible ? "status-visible" : "status-hidden") + '|' +
                    (state.view.pointerHidden ? "pointer-hidden" : "pointer-visible") + '|' + state.view.statusText.toUtf8();
                fprintf(stdout, "%d|%s\n", state.performance.decodesInFlight > 0 ? 1 : 0, ui.constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("SettingsState")) {
                fprintf(stdout, "%s\n", describeSettings(window.snapshot().settings.settings).constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("StoredSettingsState")) {
                fprintf(stdout, "%s\n",
                        describeSettings(Settings::Editor::readAccepted()).constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("SettingsFileName")) {
                fprintf(stdout, "%s\n",
                        Settings::Editor::settingsFilePathForTest().toUtf8().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("SettingsDialogStructure")) {
                const auto dialog = window.snapshot().settings.settingsDialog;
                if (!dialog.open) fprintf(stdout, "closed\n");
                else if (dialog.structureMatchesContract)
                    fprintf(stdout, "Navigation[Mouse wheel action]|Appearance[Viewing surface background|Show status overlay]|Performance & Window[Decoded cache budget|Restore window size and position]|Reset Defaults|Cancel|Apply\n");
                else fprintf(stdout, "invalid\n");
                fflush(stdout);
                return;
            } else if (input.startsWith("SettingsDialogGeometry")) {
                const QSize size = window.snapshot().settings.settingsDialog.size;
                fprintf(stdout, "%dx%d\n", size.width(), size.height());
                fflush(stdout);
                return;
            } else if (input.startsWith("SettingsDialogFocusOrder")) {
                const auto dialog = window.snapshot().settings.settingsDialog;
                if (dialog.focusOrderMatchesContract)
                    fprintf(stdout, "Mouse wheel action|Viewing surface background|Show status overlay|Decoded cache budget|Restore window size and position|Reset Defaults|Cancel|Apply\n");
                else fprintf(stdout, "invalid\n");
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
                const QSize size = window.snapshot().view.windowSize;
                fprintf(stdout, "%dx%d\n", size.width(), size.height());
                fflush(stdout);
                return;
            } else if (input.startsWith("PresentationState")) {
                fprintf(stdout, "%s\n", describePresentation(window.snapshot().presentation.presentation).constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("PresentationMotionContract")) {
                fprintf(stdout, "empty=optional-opacity|loading=immediate|displayed=immediate|error=optional-opacity|large-image=optional-opacity|reduced-motion=immediate\n");
                fflush(stdout);
                return;
            } else if (input.startsWith("ActivePresentationTransition")) {
                const auto state = window.snapshot().presentation.presentation;
                fprintf(stdout, "%s|%s\n", stateName(state.state).constData(),
                        state.activeOpacityEffect ? "opacity" : "immediate");
                fflush(stdout);
                return;
            } else if (input.startsWith("BackgroundPickerTitle")) {
                fprintf(stdout, "%s\n", window.snapshot().settings.settingsDialog.backgroundPickerTitle.toUtf8().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("SaveWindowGeometry")) {
                window.perform(ViewerWindowTestOperation::PersistWindowGeometry);
                fprintf(stdout, "saved\n");
                fflush(stdout);
                return;
            } else if (input.startsWith("ApplySettings:")) {
                const auto values = parseSettings(input.mid(14));
                if (values) {
                    window.applySettings(*values);
                }
                return;
            } else if (input.startsWith("OpenSettings")) {
                window.perform(ViewerWindowTestOperation::OpenSettings);
                return;
            } else if (input.startsWith("PreviewSettings:")) {
                const auto values = parseSettings(input.mid(16));
                if (values) {
                    window.setSettingsDialogValues(*values);
                }
                return;
            } else if (input.startsWith("ResetSettings")) {
                window.perform(ViewerWindowTestOperation::ResetSettings);
                return;
            } else if (input.startsWith("ApplySettingsDialog")) {
                window.perform(ViewerWindowTestOperation::ApplySettingsDialog);
                return;
            } else if (input.startsWith("CancelSettings")) {
                window.perform(ViewerWindowTestOperation::CancelSettingsDialog);
                return;
            } else if (input.startsWith("DisplayProfileChanged:")) {
                platformServices.setDisplayIccProfile(
                    QString::fromUtf8(input.mid(22).trimmed()));
                window.perform(ViewerWindowTestOperation::DisplayConfigurationChanged);
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
                const auto state = window.snapshot().view;
                fprintf(stdout, "%.6f,%d,%d,%d,%d,%d,%d\n", state.zoom,
                        state.scrollPosition.x(), state.scrollPosition.y(), state.viewportSize.width(),
                        state.viewportSize.height(), state.imageOrigin.x(), state.imageOrigin.y());
                fflush(stdout);
                return;
            } else if (input.startsWith("UiState")) {
                const auto state = window.snapshot().view;
                fprintf(stdout, "%s|%s|%s|%s\n", state.fullScreen ? "fullscreen" : "windowed",
                        state.statusVisible ? "status-visible" : "status-hidden",
                        state.pointerHidden ? "pointer-hidden" : "pointer-visible",
                        state.statusText.toUtf8().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("FocusViewingSurface")) {
                window.perform(ViewerWindowTestOperation::FocusViewingSurface);
                return;
            } else if (input.startsWith("InformationState")) {
                fprintf(stdout, "%s\n", window.snapshot().presentation.informationText.toUtf8().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("InformationDialogState")) {
                const auto dialog = window.snapshot().presentation.informationDialog;
                if (!dialog.open) fprintf(stdout, "closed\n");
                else fprintf(stdout, "open|%dx%d\n", dialog.size.width(), dialog.size.height());
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
                QList<QByteArray> entries;
                for (const auto &action : window.snapshot().commands.imageActions)
                    entries.append(action.text.toUtf8() + " [" + action.shortcut.toUtf8() + ']');
                fprintf(stdout, "%s\n", entries.join('|').constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("ContextMenuStructure")) {
                fprintf(stdout, "%s\n", describeActions(window.snapshot().commands.contextMenu, true).constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("ContextMenuGeometry")) {
                const auto state = window.snapshot().commands;
                fprintf(stdout, "%s|%s\n", encodeRect(state.contextMenuGeometry).constData(),
                        encodeRect(state.contextMenuScreenGeometry).constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("ApplicationMenuStructure")) {
                fprintf(stdout, "%s\n", describeActions(window.snapshot().commands.applicationMenu, true).constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("ApplicationMenuVisibility")) {
                fprintf(stdout, "%s\n", window.snapshot().commands.applicationMenuVisible
                                                   ? "visible"
                                                   : "hidden");
                fflush(stdout);
                return;
            } else if (input.startsWith("CommandAvailability")) {
                QList<QByteArray> entries;
                for (const auto &action : window.snapshot().commands.contextMenu)
                    if (!action.separator) entries.append(action.text.toUtf8() + '=' +
                        (action.enabled ? "enabled" : "disabled"));
                fprintf(stdout, "%s\n", entries.join('|').constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("QuitActionState")) {
                const auto state = window.snapshot().commands;
                fprintf(stdout, "%s|%s|%s\n", state.quitActionShared ? "shared" : "duplicated",
                        state.quitActionHasStandardRole ? "standard-role" : "custom-role",
                        state.quitActionHasStandardShortcut ? "standard-shortcut" : "custom-shortcut");
                fflush(stdout);
                return;
            } else if (input.startsWith("TriggerQuit")) {
                window.perform(ViewerWindowTestOperation::QuitApplication);
                return;
            } else if (input.startsWith("FocusState")) {
                const auto owner = window.snapshot().accessibility.focusOwner;
                fprintf(stdout, "%s\n", owner == TestFocusOwner::Menu ? "menu" :
                        owner == TestFocusOwner::Dialog ? "dialog" :
                        owner == TestFocusOwner::ViewingSurface ? "viewing-surface" : "other");
                fflush(stdout);
                return;
            } else if (input.startsWith("AccessibilityState")) {
                const auto state = window.snapshot().accessibility;
                QList<QByteArray> lines{state.accessibleImageName.toUtf8() + "|AccessibleRole=" +
                    (state.accessibleImageHasGraphicRole ? "Graphic" : "Unknown") + '|' +
                    state.accessibleImageDescription.toUtf8()};
                for (const auto &action : state.accessibleActions)
                    lines.append(action.text.toUtf8() + '|' + action.shortcut.toUtf8());
                fprintf(stdout, "%s\n", lines.join('\n').constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("RevealedPath")) {
                fprintf(stdout, "%s\n", platformServices.revealedPath().toUtf8().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("Feedback")) {
                fprintf(stdout, "%s\n", window.snapshot().view.statusText.toUtf8().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("ErrorState")) {
                const auto state = window.snapshot().presentation.presentation;
                fprintf(stdout, "%s|%s|%s|%s\n", state.state == ViewingSurface::State::Error ? "visible" : "hidden",
                        state.errorExplanation.toUtf8().constData(), state.errorDetails.toUtf8().constData(),
                        state.errorDetailsVisible ? "details-visible" : "details-hidden");
                fflush(stdout);
                return;
            } else if (input.startsWith("LargeImageState")) {
                const auto state = window.snapshot().presentation;
                fprintf(stdout, "%s|%dx%d\n",
                        state.presentation.state == ViewingSurface::State::LargeImageConfirmation ? "visible" : "hidden",
                        state.pendingLargeImageSize.width(), state.pendingLargeImageSize.height());
                fflush(stdout);
                return;
            } else if (input.startsWith("PrimaryActionState")) {
                const auto state = window.snapshot().presentation.presentation;
                if (state.primaryActionText.isEmpty()) fprintf(stdout, "\n");
                else fprintf(stdout, "%s:%s|%s:secondary\n", state.primaryActionText.toUtf8().constData(),
                             state.primaryActionIsDefault ? "default" : "secondary",
                             state.secondaryActionText.toUtf8().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("FocusDetails")) {
                window.focusPresentationAction(ViewingSurface::ActionRole::Details);
                return;
            } else if (input.startsWith("FocusSkip")) {
                window.focusPresentationAction(ViewingSurface::ActionRole::Secondary);
                return;
            } else if (input.startsWith("ToggleDetails")) {
                window.activatePresentationAction(ViewingSurface::ActionRole::Details);
            } else if (input.startsWith("ApproveLarge")) {
                window.activatePresentationAction(ViewingSurface::ActionRole::Primary);
            } else if (input.startsWith("RejectLarge")) {
                window.activatePresentationAction(ViewingSurface::ActionRole::Secondary);
            } else if (input.startsWith("FailExternalActions")) {
                window.perform(ViewerWindowTestOperation::FailExternalActions);
                return;
            } else if (input.startsWith("DecodeCount:")) {
                const QString path = QString::fromUtf8(input.mid(12).trimmed());
                fprintf(stdout, "%d\n", window.snapshot(path).performance.requestedPathDecodeCount);
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
                QDragEnterEvent dragEvent(window.rect(ViewerWindowTestRegion::Window).center(), Qt::CopyAction, mimeData,
                                          Qt::LeftButton, Qt::NoModifier);
                window.sendEvent(ViewerWindowTestRegion::Window, dragEvent);
                QDropEvent event(QPointF(window.rect(ViewerWindowTestRegion::Window).center()), Qt::CopyAction, mimeData,
                                 Qt::LeftButton, Qt::NoModifier);
                window.sendEvent(ViewerWindowTestRegion::Window, event);
                delete mimeData;
            } else if (input.startsWith("BeginDrag:")) {
                auto *mimeData = new QMimeData;
                mimeData->setUrls(
                    {QUrl::fromLocalFile(QString::fromUtf8(input.mid(10).trimmed()))});
                QDragEnterEvent event(window.rect(ViewerWindowTestRegion::Window).center(), Qt::CopyAction, mimeData,
                                      Qt::LeftButton, Qt::NoModifier);
                window.sendEvent(ViewerWindowTestRegion::Window, event);
                delete mimeData;
            } else if (input.startsWith("LeaveDrag")) {
                QDragLeaveEvent event;
                window.sendEvent(ViewerWindowTestRegion::Window, event);
            } else if (input.startsWith("SelectZoomWheelAction")) {
                window.perform(ViewerWindowTestOperation::SelectWheelZoom);
            } else if (input.startsWith("ContextMenuAtScreenEdge:")) {
                const QRect available =
                    window.availableScreenGeometry(ViewerWindowTestRegion::ViewingSurface);
                const QByteArray corner = input.mid(24).trimmed();
                const QPoint globalPosition = corner == "TopRight"      ? available.topRight()
                                              : corner == "BottomLeft"  ? available.bottomLeft()
                                              : corner == "BottomRight" ? available.bottomRight()
                                                                        : available.topLeft();
                QContextMenuEvent event(QContextMenuEvent::Mouse,
                                        window.rect(ViewerWindowTestRegion::ViewingSurface).center(),
                                        globalPosition);
                window.sendEvent(ViewerWindowTestRegion::ViewingSurface, event);
            } else if (input.startsWith("ContextMenu:")) {
                const QList<QByteArray> parts = input.trimmed().split(':');
                if (parts.size() == 3) {
                    const QPoint position(parts.at(1).toInt(), parts.at(2).toInt());
                    QContextMenuEvent event(QContextMenuEvent::Mouse, position,
                                            window.mapToGlobal(
                                                ViewerWindowTestRegion::ViewingSurface, position));
                    window.sendEvent(ViewerWindowTestRegion::ViewingSurface, event);
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
                    window.sendEvent(ViewerWindowTestRegion::ImageViewport, event);
                }
            } else if (input.startsWith("Drag:")) {
                const QList<QByteArray> parts = input.trimmed().split(':');
                if (parts.size() == 5) {
                    const QPointF start(parts.at(1).toInt(), parts.at(2).toInt());
                    const QPointF end(parts.at(3).toInt(), parts.at(4).toInt());
                    QMouseEvent press(QEvent::MouseButtonPress, start, start, start, Qt::LeftButton,
                                      Qt::LeftButton, Qt::NoModifier);
                    window.sendEvent(ViewerWindowTestRegion::ImageViewport, press);
                    QMouseEvent move(QEvent::MouseMove, end, end, end, Qt::NoButton, Qt::LeftButton,
                                     Qt::NoModifier);
                    window.sendEvent(ViewerWindowTestRegion::ImageViewport, move);
                    QMouseEvent release(QEvent::MouseButtonRelease, end, end, end, Qt::LeftButton,
                                        Qt::NoButton, Qt::NoModifier);
                    window.sendEvent(ViewerWindowTestRegion::ImageViewport, release);
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
                    window.sendEvent(ViewerWindowTestRegion::ImageViewport, event);
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
