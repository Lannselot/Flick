// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform_services.h"

#include <QFile>
#include <QGuiApplication>
#include <QScreen>
#include <QUrl>

#include <algorithm>
#include <cstdlib>

#if defined(Q_OS_LINUX)
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QDBusReply>
#endif

#ifdef FLICK_HAVE_XCB_COLOR_PROFILE
#include <QtGui/qguiapplication_platform.h>
#include <xcb/xcb.h>
#endif

namespace {
QColorSpace colorSpaceFromIccFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QColorSpace::fromIccProfile(file.read(16 * 1024 * 1024));
}

#ifdef FLICK_HAVE_XCB_COLOR_PROFILE
QColorSpace x11DisplayColorSpace(const QScreen *screen)
{
    if (!screen || QGuiApplication::platformName() != QStringLiteral("xcb")) {
        return {};
    }
    const auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    xcb_connection_t *connection = x11 ? x11->connection() : nullptr;
    if (!connection) {
        return {};
    }
    const xcb_setup_t *setup = xcb_get_setup(connection);
    xcb_screen_iterator_t roots = xcb_setup_roots_iterator(setup);
    if (!roots.data) {
        return {};
    }
    const int screenIndex =
        std::max(0, int(QGuiApplication::screens().indexOf(const_cast<QScreen *>(screen))));
    const QByteArray propertyName =
        screenIndex == 0 ? QByteArrayLiteral("_ICC_PROFILE")
                         : QByteArrayLiteral("_ICC_PROFILE_") + QByteArray::number(screenIndex);
    const xcb_intern_atom_cookie_t atomCookie =
        xcb_intern_atom(connection, false, propertyName.size(), propertyName.constData());
    std::unique_ptr<xcb_intern_atom_reply_t, decltype(&std::free)> atomReply(
        xcb_intern_atom_reply(connection, atomCookie, nullptr), &std::free);
    if (!atomReply || atomReply->atom == XCB_ATOM_NONE) {
        return {};
    }
    const xcb_get_property_cookie_t propertyCookie =
        xcb_get_property(connection, false, roots.data->root, atomReply->atom,
                         XCB_GET_PROPERTY_TYPE_ANY, 0, 4 * 1024 * 1024);
    std::unique_ptr<xcb_get_property_reply_t, decltype(&std::free)> propertyReply(
        xcb_get_property_reply(connection, propertyCookie, nullptr), &std::free);
    if (!propertyReply || propertyReply->format != 8) {
        return {};
    }
    const QByteArray profile(
        static_cast<const char *>(xcb_get_property_value(propertyReply.get())),
        xcb_get_property_value_length(propertyReply.get()));
    return QColorSpace::fromIccProfile(profile);
}
#endif

#if defined(Q_OS_LINUX)
QColorSpace colordDisplayColorSpace(const QScreen *screen)
{
    if (!screen || screen->name().isEmpty()) {
        return {};
    }
    QDBusInterface manager(QStringLiteral("org.freedesktop.ColorManager"),
                           QStringLiteral("/org/freedesktop/ColorManager"),
                           QStringLiteral("org.freedesktop.ColorManager"));
    const QDBusReply<QDBusObjectPath> device =
        manager.call(QStringLiteral("FindDeviceByProperty"),
                     QStringLiteral("OutputName"), screen->name());
    if (!device.isValid() || device.value().path().isEmpty()) {
        return {};
    }
    QDBusInterface deviceInterface(QStringLiteral("org.freedesktop.ColorManager"),
                                   device.value().path(),
                                   QStringLiteral("org.freedesktop.ColorManager.Device"));
    const QDBusReply<QDBusObjectPath> profile =
        deviceInterface.call(QStringLiteral("GetProfileForQualifiers"),
                             QStringList{QStringLiteral("ColorSpace.RGB."),
                                         QStringLiteral("MediaType.Display."),
                                         QString{}});
    if (!profile.isValid() || profile.value().path().isEmpty()) {
        return {};
    }
    QDBusInterface profileInterface(QStringLiteral("org.freedesktop.ColorManager"),
                                    profile.value().path(),
                                    QStringLiteral("org.freedesktop.ColorManager.Profile"));
    return colorSpaceFromIccFile(profileInterface.property("Filename").toString());
}
#endif

class DefaultPlatformServices final : public PlatformServices
{
public:
    QColorSpace displayColorSpace(const QScreen *screen) const override
    {
#ifdef FLICK_HAVE_XCB_COLOR_PROFILE
        QColorSpace colorSpace = x11DisplayColorSpace(screen);
        if (colorSpace.isValid()) {
            return colorSpace;
        }
#endif
#if defined(Q_OS_LINUX)
        return colordDisplayColorSpace(screen);
#else
        Q_UNUSED(screen);
        return {};
#endif
    }

    bool revealFile(const QString &path) override
    {
#if defined(Q_OS_LINUX)
        QDBusInterface fileManager(QStringLiteral("org.freedesktop.FileManager1"),
                                   QStringLiteral("/org/freedesktop/FileManager1"),
                                   QStringLiteral("org.freedesktop.FileManager1"));
        const QDBusReply<void> reply =
            fileManager.call(QStringLiteral("ShowItems"),
                             QStringList{QUrl::fromLocalFile(path).toString()}, QString{});
        return reply.isValid();
#else
        Q_UNUSED(path);
        return false;
#endif
    }
};
} // namespace

std::unique_ptr<PlatformServices> createPlatformServices()
{
    return std::make_unique<DefaultPlatformServices>();
}

#ifdef FLICK_ENABLE_TEST_HARNESS
QColorSpace TestPlatformServices::displayColorSpace(const QScreen *screen) const
{
    Q_UNUSED(screen);
    return displayColorSpace_;
}

bool TestPlatformServices::revealFile(const QString &path)
{
    revealedPath_ = path;
    return true;
}

void TestPlatformServices::setDisplayIccProfile(const QString &path)
{
    displayColorSpace_ = colorSpaceFromIccFile(path);
}

QString TestPlatformServices::revealedPath() const
{
    return revealedPath_;
}
#endif
