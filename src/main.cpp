// SPDX-License-Identifier: GPL-3.0-or-later

#include "flick_application.h"
#include "platform_services.h"
#include "viewer_window.h"

#include <QApplication>
#include <memory>

int main(int argc, char *argv[])
{
    FlickApplication application(argc, argv);
    installViewerWindowAccessibility();
    QApplication::setApplicationName(QStringLiteral("Flick"));
    QApplication::setApplicationDisplayName(QStringLiteral("Flick"));
    QApplication::setApplicationVersion(QStringLiteral(FLICK_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("org.flick.Flick"));
    QApplication::setOrganizationName(QStringLiteral("Flick"));

    const QStringList arguments = application.arguments();
    const QString initialPath = arguments.size() > 1 ? arguments.at(1) : QString{};
    auto platformServices = createPlatformServices();
    ViewerWindowPtr window = createViewerWindow(initialPath, std::move(platformServices));
    application.setFileOpenHandler(
        [&window](const QString &path) { openViewerWindowFile(*window, path); });
    showViewerWindow(*window);

    return application.exec();
}
