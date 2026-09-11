// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QtTypes>

#include <memory>
#include <optional>

class PlatformServices;
class ViewerWindow;

struct ViewerWindowDeleter
{
    void operator()(ViewerWindow *window) const;
};

using ViewerWindowPtr = std::unique_ptr<ViewerWindow, ViewerWindowDeleter>;

struct ViewerWindowConfiguration
{
#ifdef FLICK_ENABLE_TEST_HARNESS
    std::optional<qsizetype> cacheBudgetBytes;
    std::optional<qint64> largeImageAllocationLimitBytes;
    std::optional<QString> filePickerSelection;
    QString delayedDecodePath;
    int decodeDelayMilliseconds = 0;
    int loadingIndicatorDelayMilliseconds = 120;
    bool reducedMotion = false;
#endif
};

ViewerWindowPtr createViewerWindow(const QString &initialPath,
                                   std::unique_ptr<PlatformServices> platformServices,
                                   ViewerWindowConfiguration configuration = {});
void installViewerWindowAccessibility();
void openViewerWindowFile(ViewerWindow &window, const QString &path);
void showViewerWindow(ViewerWindow &window);
