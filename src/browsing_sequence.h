#pragma once

#include <QStringList>

class BrowsingSequence
{
public:
    enum class MoveOutcome { Selected, Beginning, End };
    enum class ReconcileOutcome { Unchanged, SelectionPreserved, SelectionReplaced, Empty };
    struct AdjacentPaths {
        QString previous;
        QString next;
    };

    static bool supports(const QString &path);
    static BrowsingSequence directoryBacked(const QString &selectedPath);
    static BrowsingSequence directoryBacked(const QString &directoryPath,
                                             const QString &selectedPath);
    static BrowsingSequence explicitList(const QStringList &paths);

    const QStringList &paths() const;
    QString selectedPath() const;
    int selectedIndex() const;
    bool isDirectoryBacked() const;
    MoveOutcome movePrevious();
    MoveOutcome moveNext();
    AdjacentPaths adjacentPaths() const;
    ReconcileOutcome reconcileDirectory();

private:
    enum class Mode { DirectoryBacked, Explicit };

    BrowsingSequence(Mode mode, QStringList paths, int selectedIndex,
                     QString directoryPath = {});
    static BrowsingSequence directoryBackedCanonical(const QString &directoryPath,
                                                      const QString &canonicalSelection);

    Mode mode_;
    QStringList paths_;
    int selectedIndex_ = -1;
    QString directoryPath_;
};
