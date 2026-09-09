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
    void movementSelectsAdjacentPathsAndReportsBoundaries();
    void explicitListMovementReportsBothBoundaries();
    void adjacentPathsFollowTheCurrentSelection();
    void reconciliationPreservesSelectionAcrossAdditions();
    void reconciliationSelectsNearestPathAfterRemovalOrRename();
    void reconciliationRecoversFromAnEmptyDirectory();
    void explicitSequencesIgnoreDirectoryReconciliation();
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

void BrowsingSequenceTest::movementSelectsAdjacentPathsAndReportsBoundaries()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString first = touch(directory, QStringLiteral("image1.png"));
    const QString second = touch(directory, QStringLiteral("image2.png"));
    const QString third = touch(directory, QStringLiteral("image3.png"));

    BrowsingSequence sequence = BrowsingSequence::directoryBacked(second);

    QCOMPARE(sequence.movePrevious(), BrowsingSequence::MoveOutcome::Selected);
    QCOMPARE(sequence.selectedPath(), QFileInfo(first).canonicalFilePath());
    QCOMPARE(sequence.movePrevious(), BrowsingSequence::MoveOutcome::Beginning);
    QCOMPARE(sequence.selectedPath(), QFileInfo(first).canonicalFilePath());
    QCOMPARE(sequence.moveNext(), BrowsingSequence::MoveOutcome::Selected);
    QCOMPARE(sequence.moveNext(), BrowsingSequence::MoveOutcome::Selected);
    QCOMPARE(sequence.selectedPath(), QFileInfo(third).canonicalFilePath());
    QCOMPARE(sequence.moveNext(), BrowsingSequence::MoveOutcome::End);
    QCOMPARE(sequence.selectedPath(), QFileInfo(third).canonicalFilePath());
}

void BrowsingSequenceTest::explicitListMovementReportsBothBoundaries()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString first = touch(directory, QStringLiteral("image1.png"));
    const QString second = touch(directory, QStringLiteral("image2.png"));
    BrowsingSequence sequence = BrowsingSequence::explicitList({second, first});

    QCOMPARE(sequence.movePrevious(), BrowsingSequence::MoveOutcome::Beginning);
    QCOMPARE(sequence.moveNext(), BrowsingSequence::MoveOutcome::Selected);
    QCOMPARE(sequence.selectedPath(), QFileInfo(second).canonicalFilePath());
    QCOMPARE(sequence.moveNext(), BrowsingSequence::MoveOutcome::End);
}

void BrowsingSequenceTest::adjacentPathsFollowTheCurrentSelection()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString first = touch(directory, QStringLiteral("image1.png"));
    const QString second = touch(directory, QStringLiteral("image2.png"));
    const QString third = touch(directory, QStringLiteral("image3.png"));
    BrowsingSequence sequence = BrowsingSequence::directoryBacked(second);

    QCOMPARE(sequence.adjacentPaths().previous, QFileInfo(first).canonicalFilePath());
    QCOMPARE(sequence.adjacentPaths().next, QFileInfo(third).canonicalFilePath());
    QCOMPARE(sequence.movePrevious(), BrowsingSequence::MoveOutcome::Selected);
    QVERIFY(sequence.adjacentPaths().previous.isEmpty());
    QCOMPARE(sequence.adjacentPaths().next, QFileInfo(second).canonicalFilePath());
}

void BrowsingSequenceTest::reconciliationPreservesSelectionAcrossAdditions()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString second = touch(directory, QStringLiteral("image2.png"));
    BrowsingSequence sequence = BrowsingSequence::directoryBacked(second);
    const QString first = touch(directory, QStringLiteral("image1.png"));

    QCOMPARE(sequence.reconcileDirectory(),
             BrowsingSequence::ReconcileOutcome::SelectionPreserved);
    QCOMPARE(sequence.paths(), QStringList({QFileInfo(first).canonicalFilePath(),
                                            QFileInfo(second).canonicalFilePath()}));
    QCOMPARE(sequence.selectedPath(), QFileInfo(second).canonicalFilePath());
    QCOMPARE(sequence.selectedIndex(), 1);
}

void BrowsingSequenceTest::reconciliationSelectsNearestPathAfterRemovalOrRename()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString first = touch(directory, QStringLiteral("image1.png"));
    const QString second = touch(directory, QStringLiteral("image2.png"));
    const QString third = touch(directory, QStringLiteral("image3.png"));
    BrowsingSequence sequence = BrowsingSequence::directoryBacked(second);

    QVERIFY(QFile::remove(second));
    QCOMPARE(sequence.reconcileDirectory(),
             BrowsingSequence::ReconcileOutcome::SelectionReplaced);
    QCOMPARE(sequence.selectedPath(), QFileInfo(third).canonicalFilePath());

    const QString renamed = directory.filePath(QStringLiteral("image4.png"));
    QVERIFY(QFile::rename(third, renamed));
    QCOMPARE(sequence.reconcileDirectory(),
             BrowsingSequence::ReconcileOutcome::SelectionReplaced);
    QCOMPARE(sequence.paths(), QStringList({QFileInfo(first).canonicalFilePath(),
                                            QFileInfo(renamed).canonicalFilePath()}));
    QCOMPARE(sequence.selectedPath(), QFileInfo(renamed).canonicalFilePath());
}

void BrowsingSequenceTest::reconciliationRecoversFromAnEmptyDirectory()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString first = touch(directory, QStringLiteral("image1.png"));
    BrowsingSequence sequence = BrowsingSequence::directoryBacked(first);

    QVERIFY(QFile::remove(first));
    QCOMPARE(sequence.reconcileDirectory(), BrowsingSequence::ReconcileOutcome::Empty);
    QCOMPARE(sequence.selectedIndex(), -1);
    QVERIFY(sequence.selectedPath().isEmpty());

    const QString recovered = touch(directory, QStringLiteral("image2.png"));
    QCOMPARE(sequence.reconcileDirectory(),
             BrowsingSequence::ReconcileOutcome::SelectionReplaced);
    QCOMPARE(sequence.selectedPath(), QFileInfo(recovered).canonicalFilePath());
}

void BrowsingSequenceTest::explicitSequencesIgnoreDirectoryReconciliation()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString first = touch(directory, QStringLiteral("image1.png"));
    BrowsingSequence sequence = BrowsingSequence::explicitList({first});
    QVERIFY(QFile::remove(first));

    QCOMPARE(sequence.reconcileDirectory(), BrowsingSequence::ReconcileOutcome::Unchanged);
    QCOMPARE(sequence.paths(), QStringList({QFileInfo(first).absoluteFilePath()}));
}

QTEST_GUILESS_MAIN(BrowsingSequenceTest)

#include "browsing_sequence_test.moc"
