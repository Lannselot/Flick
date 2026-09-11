// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QImage>
#include <QProcess>
#include <QRect>
#include <QTemporaryDir>

inline const QColor PngFixtureColor(100, 149, 237);
inline const QColor JpegFixtureColor(254, 99, 71);
inline constexpr int JpegColorTolerance = 2;

class ApplicationProcessTest
{
protected:
    struct RunningFlick
    {
        QProcess process;
        QTemporaryDir environment;
        QString screenshotPath;

        ~RunningFlick();
    };

    void initializeProcessTest();
    void start(RunningFlick &flick, const QStringList &arguments = {},
               const QString &pickerSelection = {}, int decodeDelayMilliseconds = 0,
               int cacheBudgetBytes = 0, const QString &configHome = {},
               qint64 largeAllocationLimitBytes = 0, const QString &scaleFactor = {},
               bool darkChrome = false, const QString &settingsRoot = {},
               const QString &delayedDecodePath = {});
    QImage waitForScreenshot(const RunningFlick &flick);
    QImage pressKeyAndWaitForScreenshot(RunningFlick &flick, Qt::Key key);
    void sendCommand(RunningFlick &flick, const QByteArray &command);
    void selectZoomWheelAction(RunningFlick &flick);
    QImage sendCommandAndWaitForScreenshot(RunningFlick &flick, const QByteArray &command);
    QImage captureAfter(RunningFlick &flick, int delayMilliseconds);
    QByteArray sendQueryAndWaitForReply(RunningFlick &flick, const QByteArray &command);
    QString writeFixture(const QString &encodedName, const QString &imageName,
                         const QString &directory = {});
    static QString writeImage(const QTemporaryDir &directory, const QString &name,
                              const QColor &color, QSize size = QSize(32, 24));
    static QStringList writeStatusSequence(const QTemporaryDir &directory);
    static bool containsColor(const QImage &image, const QColor &color, int tolerance = 0);
    static QRect colorBounds(const QImage &image, const QColor &color, int tolerance = 0);

private:
    QString executable_;
    QTemporaryDir fixtures_;
};
