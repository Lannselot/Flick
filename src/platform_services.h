// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColorSpace>
#include <QString>

#include <memory>

class QScreen;

class PlatformServices
{
public:
    virtual ~PlatformServices() = default;

    virtual QColorSpace displayColorSpace(const QScreen *screen) const = 0;
    virtual bool revealFile(const QString &path) = 0;
};

std::unique_ptr<PlatformServices> createPlatformServices();

#ifdef FLICK_ENABLE_TEST_HARNESS
class TestPlatformServices final : public PlatformServices
{
public:
    QColorSpace displayColorSpace(const QScreen *screen) const override;
    bool revealFile(const QString &path) override;

    void setDisplayIccProfile(const QString &path);
    QString revealedPath() const;

private:
    QColorSpace displayColorSpace_{QColorSpace::SRgb};
    QString revealedPath_;
};
#endif
