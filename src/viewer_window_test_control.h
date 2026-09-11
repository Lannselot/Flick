// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QStringList>

class ViewerWindow;
class QEvent;
class QKeyEvent;

enum class ViewerWindowTestTarget { Window, ViewingSurface, Viewport };

// The single test-only seam through which the process adapter inspects and controls a window.
class ViewerWindowTestControl
{
public:
    explicit ViewerWindowTestControl(ViewerWindow &window);

    void capture(const QString &path) const;
    QRect rect(ViewerWindowTestTarget target) const;
    QRect availableScreenGeometry(ViewerWindowTestTarget target) const;
    QPoint mapToGlobal(ViewerWindowTestTarget target, const QPoint &point) const;
    void sendEvent(ViewerWindowTestTarget target, QEvent &event) const;
    void sendKeyEvent(QKeyEvent &event, bool forceWindow) const;
    void resize(int width, int height);
    void close();
    void triggerAction(const QString &objectName);
    void focusDetails();
    void focusSkip();
    void toggleDetails();
    void approveLargeImage();
    void rejectLargeImage();
    bool isLoading() const;
    qsizetype cacheBytes() const;
    qsizetype decodesInFlight() const;
    int decodeCount(const QString &path) const;
    QByteArray viewState() const;
    QByteArray uiState() const;
    QByteArray informationState() const;
    QByteArray informationDialogState() const;
    QByteArray feedbackState() const;
    QByteArray errorState() const;
    QByteArray primaryActionState() const;
    QByteArray presentationMotionContract() const;
    QByteArray activePresentationTransition() const;
    QByteArray backgroundPickerTitle() const;
    QByteArray largeImageState() const;
    QByteArray contextActions() const;
    QByteArray contextMenuStructure() const;
    QByteArray contextMenuGeometry() const;
    QByteArray applicationMenuStructure() const;
    QByteArray commandAvailability() const;
    QByteArray quitActionState() const;
    QByteArray focusState() const;
    QByteArray accessibilityState() const;
    QByteArray settingsState() const;
    QByteArray storedSettingsState() const;
    QByteArray settingsFileName() const;
    QByteArray settingsDialogStructure() const;
    QByteArray settingsDialogGeometry() const;
    QByteArray settingsDialogFocusOrder() const;
    QByteArray windowGeometryState() const;
    QByteArray presentationState() const;

    void persistWindowGeometry();
    void applyTestSettings(const QStringList &values);
    void previewTestSettings(const QStringList &values);
    void resetTestSettings();
    void finishTestSettings(bool accepted);
    void displayConfigurationChanged();
    void focusViewingSurfaceForTest();
    void failExternalActionsForTest();

private:
    ViewerWindow &window_;
};
