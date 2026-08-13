// SPDX-License-Identifier: GPL-3.0-or-later

#include "../src/flick_application.h"

#include <QFileOpenEvent>
#include <QTest>

class FlickFileOpenEventTest final : public QObject
{
    Q_OBJECT

private slots:
    void deliversEventToActiveWindow();
    void retainsLaunchEventUntilWindowExists();
};

void FlickFileOpenEventTest::deliversEventToActiveWindow()
{
    auto *application = static_cast<FlickApplication *>(QCoreApplication::instance());
    QVERIFY(application);
    QString openedPath;
    application->setFileOpenHandler([&openedPath](const QString &path) { openedPath = path; });

    QFileOpenEvent event(QStringLiteral("/tmp/active.png"));
    QCoreApplication::sendEvent(application, &event);

    QCOMPARE(openedPath, QStringLiteral("/tmp/active.png"));
    QVERIFY(event.isAccepted());
}

void FlickFileOpenEventTest::retainsLaunchEventUntilWindowExists()
{
    auto *application = static_cast<FlickApplication *>(QCoreApplication::instance());
    QVERIFY(application);
    application->setFileOpenHandler({});

    QFileOpenEvent event(QStringLiteral("/tmp/launch.webp"));
    QCoreApplication::sendEvent(application, &event);

    QString openedPath;
    application->setFileOpenHandler([&openedPath](const QString &path) { openedPath = path; });
    QCOMPARE(openedPath, QStringLiteral("/tmp/launch.webp"));
}

int main(int argc, char *argv[])
{
    FlickApplication application(argc, argv);
    FlickFileOpenEventTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "flick_file_open_event_test.moc"
