// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>

class PlatformServices;
class QString;
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
