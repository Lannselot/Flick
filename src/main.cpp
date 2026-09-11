// SPDX-License-Identifier: GPL-3.0-or-later

#include "flick_application.h"
#include "platform_services.h"
#include "viewer_window.h"

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QSettings>

#include <memory>

int main(int argc, char *argv[])
{
#ifdef FLICK_ENABLE_TEST_HARNESS
    const QString testSettingsRoot = qEnvironmentVariable("FLICK_TEST_SETTINGS_ROOT");
    if (!testSettingsRoot.isEmpty()) {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, testSettingsRoot);
    }
#endif

    FlickApplication application(argc, argv);
    installViewerWindowAccessibility();
    QApplication::setApplicationName(QStringLiteral("Flick"));
    QApplication::setApplicationDisplayName(QStringLiteral("Flick"));
    QApplication::setApplicationVersion(QStringLiteral(FLICK_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("org.flick.Flick"));
    QApplication::setOrganizationName(QStringLiteral("Flick"));

#ifdef FLICK_ENABLE_TEST_HARNESS
    if (qEnvironmentVariableIsSet("FLICK_TEST_DARK_CHROME")) {
        QPalette palette = application.palette();
        palette.setColor(QPalette::Window, QColor(QStringLiteral("#2b2b2b")));
        palette.setColor(QPalette::WindowText, Qt::white);
        palette.setColor(QPalette::Base, QColor(QStringLiteral("#202020")));
        palette.setColor(QPalette::Text, Qt::white);
        palette.setColor(QPalette::Button, QColor(QStringLiteral("#353535")));
        palette.setColor(QPalette::ButtonText, Qt::white);
        application.setPalette(palette);
    }
#endif

    const QStringList arguments = application.arguments();
    const QString initialPath = arguments.size() > 1 ? arguments.at(1) : QString{};
#ifdef FLICK_ENABLE_TEST_HARNESS
    auto platformServices = std::make_unique<TestPlatformServices>();
    TestPlatformServices *testPlatformServices = platformServices.get();
#else
    auto platformServices = createPlatformServices();
#endif
    ViewerWindowPtr window = createViewerWindow(initialPath, std::move(platformServices));
    application.setFileOpenHandler(
        [&window](const QString &path) { openViewerWindowFile(*window, path); });
    showViewerWindow(*window);

#ifdef FLICK_ENABLE_TEST_HARNESS
    installViewerWindowTestProtocol(*window, application, *testPlatformServices);
#endif
    return application.exec();
}
