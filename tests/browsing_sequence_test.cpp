#include "browsing_sequence.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class BrowsingSequenceTest : public QObject
{
    Q_OBJECT

private slots:
    void directoryBackedFiltersCanonicalizesAndSortsNaturally();
    void explicitListFiltersCanonicalizesDeduplicatesAndSortsNaturally();
};

namespace {
QString touch(const QTemporaryDir &directory, const QString &name)
{
    const QString path = directory.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return {};
    }
    file.write("fixture");
    return path;
}
}

void BrowsingSequenceTest::directoryBackedFiltersCanonicalizesAndSortsNaturally()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString second = touch(directory, QStringLiteral("IMAGE2.PNG"));
    const QString tenth = touch(directory, QStringLiteral("image10.png"));
    const QString first = touch(directory, QStringLiteral("image1.jpg"));
    QVERIFY(!touch(directory, QStringLiteral(".hidden.png")).isEmpty());
    QVERIFY(!touch(directory, QStringLiteral("notes.txt")).isEmpty());

    const BrowsingSequence sequence = BrowsingSequence::directoryBacked(tenth);

    QCOMPARE(sequence.paths(), QStringList({QFileInfo(first).canonicalFilePath(),
                                            QFileInfo(second).canonicalFilePath(),
                                            QFileInfo(tenth).canonicalFilePath()}));
    QCOMPARE(sequence.selectedPath(), QFileInfo(tenth).canonicalFilePath());
    QCOMPARE(sequence.selectedIndex(), 2);
    QVERIFY(sequence.isDirectoryBacked());
}

void BrowsingSequenceTest::explicitListFiltersCanonicalizesDeduplicatesAndSortsNaturally()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString second = touch(directory, QStringLiteral("image2.GIF"));
    const QString tenth = touch(directory, QStringLiteral("Image10.bmp"));
    const QString unsupported = touch(directory, QStringLiteral("image1.txt"));
    const QString hidden = touch(directory, QStringLiteral(".image3.png"));
    const QString duplicate = directory.filePath(QStringLiteral("duplicate.gif"));
    QVERIFY(QFile::link(second, duplicate));

    const BrowsingSequence sequence =
        BrowsingSequence::explicitList({tenth, unsupported, duplicate, hidden, second});

    QCOMPARE(sequence.paths(), QStringList({QFileInfo(hidden).canonicalFilePath(),
                                            QFileInfo(second).canonicalFilePath(),
                                            QFileInfo(tenth).canonicalFilePath()}));
    QCOMPARE(sequence.selectedPath(), QFileInfo(hidden).canonicalFilePath());
    QCOMPARE(sequence.selectedIndex(), 0);
    QVERIFY(!sequence.isDirectoryBacked());
}

QTEST_GUILESS_MAIN(BrowsingSequenceTest)

#include "browsing_sequence_test.moc"
