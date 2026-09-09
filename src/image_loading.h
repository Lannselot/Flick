// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QImage>
#include <QList>
#include <QSize>
#include <QString>

#include <variant>

namespace ImageLoading {

struct LoadedImage
{
    QString path;
    QList<QImage> frames;
    QList<int> frameDelays;
    int loopCount = 0;

    qsizetype sizeInBytes() const;
};

struct DecodeFailure
{
    QString path;
    QString details;
};

struct ConfirmationRequired
{
    QString path;
    QSize declaredSize;
    qint64 estimatedAllocationBytes = 0;
};

using DecodeOutcome = std::variant<LoadedImage, DecodeFailure, ConfirmationRequired>;

struct DecodeRequest
{
    QString path;
    bool exceptionalDimensionsApproved = false;
    qint64 allocationLimitBytes = 1024LL * 1024 * 1024;
};

DecodeOutcome decode(const DecodeRequest &request);

} // namespace ImageLoading
