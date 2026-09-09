#include "browsing_sequence.h"

#include <QCollator>
#include <QDir>
#include <QFileInfo>
#include <QLocale>

#include <algorithm>
#include <utility>

namespace {
void sortNaturally(QStringList &paths)
{
    QCollator collator(QLocale::English);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
    std::sort(paths.begin(), paths.end(), [&collator](const QString &left, const QString &right) {
        const QString leftName = QFileInfo(left).fileName();
        const QString rightName = QFileInfo(right).fileName();
        const int naturalOrder = collator.compare(leftName, rightName);
        return naturalOrder == 0 ? leftName < rightName : naturalOrder < 0;
    });
}
}

bool BrowsingSequence::supports(const QString &path)
{
    static const QStringList supportedSuffixes = {QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                                  QStringLiteral("png"), QStringLiteral("webp"),
                                                  QStringLiteral("gif"), QStringLiteral("bmp")};
    const QFileInfo file(path);
    return file.isFile() && supportedSuffixes.contains(file.suffix(), Qt::CaseInsensitive);
}

BrowsingSequence BrowsingSequence::directoryBacked(const QString &selectedPath)
{
    const QString canonicalSelection = QFileInfo(selectedPath).canonicalFilePath();
    return directoryBackedCanonical(QFileInfo(canonicalSelection).absolutePath(),
                                    canonicalSelection);
}

BrowsingSequence BrowsingSequence::directoryBacked(const QString &directoryPath,
                                                    const QString &selectedPath)
{
    return directoryBackedCanonical(directoryPath, QFileInfo(selectedPath).canonicalFilePath());
}

BrowsingSequence BrowsingSequence::directoryBackedCanonical(
    const QString &directoryPath, const QString &canonicalSelection)
{
    QStringList paths;
    const QDir directory(directoryPath);
    for (const QFileInfo &entry : directory.entryInfoList(QDir::Files)) {
        if (supports(entry.filePath())) {
            paths.append(entry.canonicalFilePath());
        }
    }
    sortNaturally(paths);
    return {Mode::DirectoryBacked, paths, int(paths.indexOf(canonicalSelection))};
}

BrowsingSequence BrowsingSequence::explicitList(const QStringList &paths)
{
    QStringList supportedPaths;
    for (const QString &path : paths) {
        if (supports(path)) {
            const QString canonicalPath = QFileInfo(path).canonicalFilePath();
            if (!supportedPaths.contains(canonicalPath)) {
                supportedPaths.append(canonicalPath);
            }
        }
    }
    sortNaturally(supportedPaths);
    return {Mode::Explicit, supportedPaths, supportedPaths.isEmpty() ? -1 : 0};
}

BrowsingSequence::BrowsingSequence(Mode mode, QStringList paths, const int selectedIndex)
    : mode_(mode), paths_(std::move(paths)), selectedIndex_(selectedIndex)
{
}

const QStringList &BrowsingSequence::paths() const
{
    return paths_;
}

QString BrowsingSequence::selectedPath() const
{
    return selectedIndex_ < 0 ? QString{} : paths_.at(selectedIndex_);
}

int BrowsingSequence::selectedIndex() const
{
    return selectedIndex_;
}

bool BrowsingSequence::isDirectoryBacked() const
{
    return mode_ == Mode::DirectoryBacked;
}
