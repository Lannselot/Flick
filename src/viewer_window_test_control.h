// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "image_information.h"
#include "settings_editor.h"
#include "viewing_surface.h"

#include <QList>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>

class ViewerWindow;
class QEvent;
class QKeyEvent;

enum class ViewerWindowTestRegion { Window, ViewingSurface, ImageViewport };
enum class ViewerWindowTestOperation {
    OpenSettings, QuitApplication, SelectWheelZoom, FocusViewingSurface, PersistWindowGeometry,
    ResetSettings, ApplySettingsDialog, CancelSettingsDialog, DisplayConfigurationChanged,
    FailExternalActions
};

struct TestActionSnapshot
{
    QString text;
    QString shortcut;
    bool enabled = false;
    bool separator = false;
    QList<TestActionSnapshot> children;
};

enum class TestFocusOwner { Menu, Dialog, ViewingSurface, Other };

struct PerformanceSnapshot {
    bool loading = false;
    qsizetype cacheBytes = 0;
    qsizetype decodesInFlight = 0;
    int requestedPathDecodeCount = 0;
};
struct ViewSnapshot {
    double zoom = 0.0;
    QPoint scrollPosition;
    QSize viewportSize;
    QPoint imageOrigin;
    bool fullScreen = false;
    bool statusVisible = false;
    bool pointerHidden = false;
    QString statusText;
    QSize windowSize;
};
struct PresentationCapabilitySnapshot {
    QString informationText;
    ImageInformation::DialogState informationDialog;
    ViewingSurface::PresentationSnapshot presentation;
    QSize pendingLargeImageSize;
};
struct CommandSurfaceSnapshot {
    QList<TestActionSnapshot> imageActions;
    QList<TestActionSnapshot> contextMenu;
    QList<TestActionSnapshot> applicationMenu;
    QRect contextMenuGeometry;
    QRect contextMenuScreenGeometry;
    bool applicationMenuVisible = false;
    bool quitActionShared = false;
    bool quitActionHasStandardRole = false;
    bool quitActionHasStandardShortcut = false;
};
struct AccessibilitySnapshot {
    TestFocusOwner focusOwner = TestFocusOwner::Other;
    QString accessibleImageName;
    bool accessibleImageHasGraphicRole = false;
    QString accessibleImageDescription;
    QList<TestActionSnapshot> accessibleActions;
};
struct SettingsSnapshot {
    Settings::Values settings;
    Settings::DialogSnapshot settingsDialog;
};
struct ViewerWindowTestSnapshot {
    PerformanceSnapshot performance;
    ViewSnapshot view;
    PresentationCapabilitySnapshot presentation;
    CommandSurfaceSnapshot commands;
    AccessibilitySnapshot accessibility;
    SettingsSnapshot settings;
};

// The single test-only seam through which the process adapter inspects and controls a window.
class ViewerWindowTestControl
{
public:
    explicit ViewerWindowTestControl(ViewerWindow &window);

    ViewerWindowTestSnapshot snapshot(const QString &decodePath = {}) const;
    void perform(ViewerWindowTestOperation operation);
    void activatePresentationAction(ViewingSurface::ActionRole role);
    void focusPresentationAction(ViewingSurface::ActionRole role);
    void capture(const QString &path) const;
    QRect rect(ViewerWindowTestRegion region) const;
    QRect availableScreenGeometry(ViewerWindowTestRegion region) const;
    QPoint mapToGlobal(ViewerWindowTestRegion region, const QPoint &point) const;
    void sendEvent(ViewerWindowTestRegion region, QEvent &event) const;
    void sendKeyEvent(QKeyEvent &event, bool forceWindow) const;
    void resize(int width, int height);
    void close();
    void applySettings(const Settings::Values &values);
    void setSettingsDialogValues(const Settings::Values &values);

private:
    ViewerWindow &window_;
};
