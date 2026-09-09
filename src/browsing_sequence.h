#pragma once

#include <QStringList>

class BrowsingSequence
{
public:
    static bool supports(const QString &path);
    static BrowsingSequence directoryBacked(const QString &selectedPath);
    static BrowsingSequence directoryBacked(const QString &directoryPath,
                                             const QString &selectedPath);
    static BrowsingSequence explicitList(const QStringList &paths);

    const QStringList &paths() const;
    QString selectedPath() const;
    int selectedIndex() const;
    bool isDirectoryBacked() const;

private:
    enum class Mode { DirectoryBacked, Explicit };

    BrowsingSequence(Mode mode, QStringList paths, int selectedIndex);
    static BrowsingSequence directoryBackedCanonical(const QString &directoryPath,
                                                      const QString &canonicalSelection);

    Mode mode_;
    QStringList paths_;
    int selectedIndex_ = -1;
};
