// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform_services_macos.h"

#include "platform_services.h"

#include <QScreen>

#import <AppKit/AppKit.h>

namespace {
NSScreen *nativeScreenFor(const QScreen *screen)
{
    if (!screen) {
        return nil;
    }
    NSArray<NSScreen *> *screens = NSScreen.screens;
    if (screens.count == 0) {
        return nil;
    }
    const CGFloat primaryTop = NSMaxY(screens.firstObject.frame);
    const QRect qtGeometry = screen->geometry();
    for (NSScreen *candidate in screens) {
        const NSRect frame = candidate.frame;
        const QRect candidateGeometry(qRound(NSMinX(frame)),
                                      qRound(primaryTop - NSMaxY(frame)),
                                      qRound(NSWidth(frame)), qRound(NSHeight(frame)));
        if (candidateGeometry == qtGeometry) {
            return candidate;
        }
    }
    return nil;
}

QColorSpace colorSpaceForScreen(const QScreen *screen)
{
    NSScreen *nativeScreen = nativeScreenFor(screen);
    if (!nativeScreen) {
        return {};
    }
    NSColorSpace *nativeColorSpace = nativeScreen.colorSpace;
    NSData *profile = nativeColorSpace.ICCProfileData;
    if (!profile || profile.length == 0) {
        return {};
    }
    const QByteArray iccProfile(static_cast<const char *>(profile.bytes),
                                static_cast<qsizetype>(profile.length));
    return QColorSpace::fromIccProfile(iccProfile);
}

class MacPlatformServices final : public PlatformServices
{
public:
    QColorSpace displayColorSpace(const QScreen *screen) const override
    {
        return colorSpaceForScreen(screen);
    }

    bool revealFile(const QString &path) override
    {
        if (path.isEmpty()) {
            return false;
        }
        @autoreleasepool {
            NSString *nativePath = [NSString stringWithCharacters:
                reinterpret_cast<const unichar *>(path.utf16())
                length:static_cast<NSUInteger>(path.size())];
            NSURL *url = [NSURL fileURLWithPath:nativePath];
            [NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:@[url]];
        }
        return true;
    }
};
} // namespace

std::unique_ptr<PlatformServices> createMacPlatformServices()
{
    return std::make_unique<MacPlatformServices>();
}
