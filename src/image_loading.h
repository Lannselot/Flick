// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QImage>
#include <QList>
#include <QObject>
#include <QSize>
#include <QString>

#include <functional>
#include <memory>
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

class Loader final : public QObject
{
public:
    using OutcomeHandler = std::function<void(DecodeOutcome)>;
    using LoadedHandler = std::function<void(const LoadedImage &)>;

    explicit Loader(QObject *parent = nullptr);
    ~Loader() override;

    Loader(const Loader &) = delete;
    Loader &operator=(const Loader &) = delete;

    void setOutcomeHandler(OutcomeHandler handler);
    void setLoadedHandler(LoadedHandler handler);
    void setCurrentPath(const QString &path);
    bool request(const DecodeRequest &request);
    bool prefetch(const DecodeRequest &request);
    bool retry(const DecodeRequest &request);
    bool isLoading(const QString &path) const;
    bool hasRequestsInFlight() const;
    qsizetype requestsInFlight() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ImageLoading
