// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>

class FlickApplication;
class PlatformServices;
class QString;
class TestPlatformServices;
class ViewerWindow;

struct ViewerWindowDeleter
{
    void operator()(ViewerWindow *window) const;
};

using ViewerWindowPtr = std::unique_ptr<ViewerWindow, ViewerWindowDeleter>;

ViewerWindowPtr createViewerWindow(const QString &initialPath,
                                   std::unique_ptr<PlatformServices> platformServices);
void installViewerWindowAccessibility();
void openViewerWindowFile(ViewerWindow &window, const QString &path);
void showViewerWindow(ViewerWindow &window);

#ifdef FLICK_ENABLE_TEST_HARNESS
void installViewerWindowTestProtocol(ViewerWindow &window, FlickApplication &application,
                                     TestPlatformServices &platformServices);
#endif
