// SPDX-License-Identifier: GPL-3.0-or-later

#include "../src/platform_services.h"

#include <QGuiApplication>
#include <QScreen>
#include <QTest>

class MacosPlatformServicesTest final : public QObject
{
    Q_OBJECT

private slots:
    void exposesPrimaryDisplayColorSpace();
};

void MacosPlatformServicesTest::exposesPrimaryDisplayColorSpace()
{
    const auto services = createPlatformServices();
    const QColorSpace colorSpace =
        services->displayColorSpace(QGuiApplication::primaryScreen());
    QVERIFY2(colorSpace.isValid(), "AppKit did not expose an ICC-backed display color space");
}

QTEST_MAIN(MacosPlatformServicesTest)
#include "macos_platform_services_test.moc"
