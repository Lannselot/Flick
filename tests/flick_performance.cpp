// SPDX-License-Identifier: GPL-3.0-or-later

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QThread>
#include <QtEndian>

namespace {
class FlickProcess
{
public:
    explicit FlickProcess(const QString &image, const qint64 cacheBudget = 0)
    {
        const QString runtime = environment_.filePath(QStringLiteral("runtime"));
        for (const QString &directory :
             {QStringLiteral("config"), QStringLiteral("data"), QStringLiteral("cache"),
              QStringLiteral("state"), QStringLiteral("runtime")}) {
            QDir().mkpath(environment_.filePath(directory));
        }
        QFile::setPermissions(runtime, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                          QFileDevice::ExeOwner);
        screenshot_ = environment_.filePath(QStringLiteral("visible.png"));
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        environment.insert(QStringLiteral("XDG_CONFIG_HOME"),
                           environment_.filePath(QStringLiteral("config")));
        environment.insert(QStringLiteral("XDG_DATA_HOME"),
                           environment_.filePath(QStringLiteral("data")));
        environment.insert(QStringLiteral("XDG_CACHE_HOME"),
                           environment_.filePath(QStringLiteral("cache")));
        environment.insert(QStringLiteral("XDG_STATE_HOME"),
                           environment_.filePath(QStringLiteral("state")));
        environment.insert(QStringLiteral("XDG_RUNTIME_DIR"), runtime);
        environment.insert(QStringLiteral("FLICK_TEST_SCREENSHOT_FILE"), screenshot_);
        if (cacheBudget > 0) {
            environment.insert(QStringLiteral("FLICK_TEST_CACHE_BUDGET_BYTES"),
                               QString::number(cacheBudget));
        }
        process_.setProcessEnvironment(environment);
        process_.start(qEnvironmentVariable("FLICK_EXECUTABLE"), {image});
    }

    bool started() { return process_.waitForStarted(5000); }
    qint64 pid() const { return process_.processId(); }

    qint64 waitForVisible()
    {
        QElapsedTimer timer;
        timer.start();
        while (!QFileInfo::exists(screenshot_) && timer.elapsed() < 10000) {
            QThread::msleep(2);
        }
        return QFileInfo::exists(screenshot_) ? timer.elapsed() : -1;
    }

    qint64 commandToVisible(const QByteArray &command)
    {
        QFile::remove(screenshot_);
        QElapsedTimer timer;
        timer.start();
        process_.write(command + '\n');
        process_.waitForBytesWritten(1000);
        const qint64 remaining = waitForVisible();
        return remaining < 0 ? -1 : timer.elapsed();
    }

    qint64 queryLatency(const QByteArray &query)
    {
        process_.readAllStandardOutput();
        QElapsedTimer timer;
        timer.start();
        process_.write(query + '\n');
        process_.waitForBytesWritten(1000);
        return process_.waitForReadyRead(5000) ? timer.elapsed() : -1;
    }

    qint64 queryLatencyDuringDecode()
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 5000) {
            process_.readAllStandardOutput();
            QElapsedTimer queryTimer;
            queryTimer.start();
            process_.write("UiResponsiveness\n");
            process_.waitForBytesWritten(1000);
            if (process_.waitForReadyRead(5000) &&
                process_.readAllStandardOutput().startsWith("1|")) {
                return queryTimer.elapsed();
            }
            QThread::msleep(1);
        }
        return -1;
    }

    qint64 cacheBytes()
    {
        process_.readAllStandardOutput();
        process_.write("CacheBytes\n");
        process_.waitForBytesWritten(1000);
        if (!process_.waitForReadyRead(5000)) {
            return -1;
        }
        return process_.readAllStandardOutput().trimmed().toLongLong();
    }

private:
    QTemporaryDir environment_;
    QString screenshot_;
    QProcess process_;
};

bool writeImage(const QString &path, const QSize size, const QColor color)
{
    QImage image(size, QImage::Format_RGB32);
    image.fill(color);
    return image.save(path, "PNG");
}

qint64 residentBytes(const qint64 pid)
{
#ifdef Q_OS_LINUX
    QFile status(QStringLiteral("/proc/%1/status").arg(pid));
    if (!status.open(QIODevice::ReadOnly)) {
        return -1;
    }
    const QList<QByteArray> lines = status.readAll().split('\n');
    for (const QByteArray &line : lines) {
        if (line.startsWith("VmRSS:")) {
            return line.simplified().split(' ').value(1).toLongLong() * 1024;
        }
    }
#else
    Q_UNUSED(pid);
#endif
    return -1;
}

bool populateSequence(const QString &directory, const QString &source, const int count)
{
    for (int index = 0; index < count; ++index) {
        const QString destination =
            QDir(directory).filePath(QStringLiteral("image-%1.png").arg(index, 5, 10, QChar('0')));
        if (!QFile::link(source, destination) && !QFile::copy(source, destination)) {
            return false;
        }
    }
    return true;
}

bool writeExceptionalBmp(const QString &path)
{
    QByteArray bmp(54, '\0');
    bmp[0] = 'B';
    bmp[1] = 'M';
    qToLittleEndian<quint32>(54, bmp.data() + 2);
    qToLittleEndian<quint32>(54, bmp.data() + 10);
    qToLittleEndian<quint32>(40, bmp.data() + 14);
    qToLittleEndian<qint32>(20000, bmp.data() + 18);
    qToLittleEndian<qint32>(10000, bmp.data() + 22);
    qToLittleEndian<quint16>(1, bmp.data() + 26);
    qToLittleEndian<quint16>(24, bmp.data() + 28);
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bmp) == bmp.size();
}

QJsonObject measurement(const qint64 value, const QString &unit)
{
    return {{QStringLiteral("value"), value}, {QStringLiteral("unit"), unit}};
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const bool smoke = application.arguments().contains(QStringLiteral("--smoke"));
    const QString executable = qEnvironmentVariable("FLICK_EXECUTABLE");
    if (executable.isEmpty() || !QFileInfo::exists(executable)) {
        qCritical("FLICK_EXECUTABLE must name the instrumented Flick executable");
        return 2;
    }

    QTemporaryDir corpus;
    if (!corpus.isValid()) {
        return 2;
    }
    const QSize representativeSize = smoke ? QSize(1024, 768) : QSize(5000, 4000);
    const QString first = corpus.filePath(QStringLiteral("00000.png"));
    const QString second = corpus.filePath(QStringLiteral("00001.png"));
    if (!writeImage(first, representativeSize, QColor(100, 149, 237)) ||
        !writeImage(second, representativeSize, QColor(255, 99, 71))) {
        return 2;
    }

    QElapsedTimer launchTimer;
    launchTimer.start();
    const qint64 cacheBudget = smoke ? 32 * 1024 * 1024 : 128 * 1024 * 1024;
    FlickProcess launch(first, cacheBudget);
    if (!launch.started()) {
        return 2;
    }
    const qint64 coldLaunch = launch.waitForVisible() < 0 ? -1 : launchTimer.elapsed();
    QThread::msleep(smoke ? 20 : 250);
    const qint64 navigation = launch.commandToVisible("Right");

    const qint64 rssBefore = residentBytes(launch.pid());
    for (int index = 0; index < (smoke ? 2 : 12); ++index) {
        launch.commandToVisible(index % 2 == 0 ? "Left" : "Right");
    }
    const qint64 rssAfter = residentBytes(launch.pid());
    const qint64 cachedBytes = launch.cacheBytes();

    const QString exceptional = corpus.filePath(QStringLiteral("exceptional.bmp"));
    if (!writeExceptionalBmp(exceptional)) {
        return 2;
    }
    FlickProcess guarded(exceptional, cacheBudget);
    if (!guarded.started() || guarded.waitForVisible() < 0) {
        return 2;
    }
    const qint64 exceptionalRss = residentBytes(guarded.pid());
    const qint64 exceptionalResponse = guarded.queryLatency("LargeImageState");

    QTemporaryDir slowCorpus;
    const QString slowImage = slowCorpus.filePath(QStringLiteral("decode.png"));
    if (!writeImage(slowImage, smoke ? QSize(1600, 1200) : QSize(8000, 6000),
                    QColor(65, 105, 225))) {
        return 2;
    }
    FlickProcess responsive(slowImage);
    if (!responsive.started()) {
        return 2;
    }
    const qint64 responsiveness = responsive.queryLatencyDuringDecode();
    if (responsiveness < 0) {
        qCritical("Could not observe an in-flight decode");
        return 2;
    }

    QTemporaryDir sequence;
    const QString tiny = sequence.filePath(QStringLiteral("source.png"));
    if (!writeImage(tiny, QSize(1, 1), Qt::black) ||
        !populateSequence(sequence.path(), tiny, smoke ? 100 : 10000)) {
        return 2;
    }
    if (!QFile::remove(tiny)) {
        return 2;
    }
    launchTimer.restart();
    FlickProcess many(QDir(sequence.path()).filePath(QStringLiteral("image-00000.png")));
    if (!many.started()) {
        return 2;
    }
    const qint64 sequenceConstruction = many.waitForVisible() < 0 ? -1 : launchTimer.elapsed();

    const QJsonObject result{
        {QStringLiteral("schema"), QStringLiteral("org.flick.performance.v1")},
        {QStringLiteral("mode"), smoke ? QStringLiteral("smoke") : QStringLiteral("representative")},
        {QStringLiteral("fixture_pixels"),
         static_cast<qint64>(representativeSize.width()) * representativeSize.height()},
        {QStringLiteral("sequence_items"), smoke ? 100 : 10000},
        {QStringLiteral("cold_launch_visible"), measurement(coldLaunch, QStringLiteral("ms"))},
        {QStringLiteral("prefetched_navigation"), measurement(navigation, QStringLiteral("ms"))},
        {QStringLiteral("ui_query_during_decode"),
         measurement(responsiveness, QStringLiteral("ms"))},
        {QStringLiteral("cache_reported"), measurement(cachedBytes, QStringLiteral("bytes"))},
        {QStringLiteral("rss_before_churn"), measurement(rssBefore, QStringLiteral("bytes"))},
        {QStringLiteral("rss_after_churn"), measurement(rssAfter, QStringLiteral("bytes"))},
        {QStringLiteral("rss_supported"), rssBefore >= 0 && rssAfter >= 0},
        {QStringLiteral("exceptional_image_rss"),
         measurement(exceptionalRss, QStringLiteral("bytes"))},
        {QStringLiteral("exceptional_image_response"),
         measurement(exceptionalResponse, QStringLiteral("ms"))},
        {QStringLiteral("sequence_construction"),
         measurement(sequenceConstruction, QStringLiteral("ms"))},
        {QStringLiteral("targets"),
         QJsonObject{{QStringLiteral("cold_launch_visible_ms"), 300},
                     {QStringLiteral("prefetched_navigation_ms"), 100}}}};
    fputs(QJsonDocument(result).toJson(QJsonDocument::Indented).constData(), stdout);
    return coldLaunch < 0 || navigation < 0 || responsiveness < 0 || cachedBytes < 0 ||
                   cachedBytes > cacheBudget || exceptionalResponse < 0 ||
#ifdef Q_OS_LINUX
                   rssBefore < 0 || rssAfter < 0 || exceptionalRss < 0 ||
#endif
                   sequenceConstruction < 0
               ? 1
               : 0;
}
