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
    return {Mode::DirectoryBacked, paths, int(paths.indexOf(canonicalSelection)),
            directory.absolutePath()};
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

BrowsingSequence::BrowsingSequence(Mode mode, QStringList paths, const int selectedIndex,
                                   QString directoryPath)
    : mode_(mode), paths_(std::move(paths)), selectedIndex_(selectedIndex),
      directoryPath_(std::move(directoryPath))
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

BrowsingSequence::MoveOutcome BrowsingSequence::movePrevious()
{
    if (selectedIndex_ <= 0) {
        return MoveOutcome::Beginning;
    }
    --selectedIndex_;
    return MoveOutcome::Selected;
}

BrowsingSequence::MoveOutcome BrowsingSequence::moveNext()
{
    if (selectedIndex_ < 0 || selectedIndex_ + 1 >= paths_.size()) {
        return MoveOutcome::End;
    }
    ++selectedIndex_;
    return MoveOutcome::Selected;
}

BrowsingSequence::AdjacentPaths BrowsingSequence::adjacentPaths() const
{
    const auto pathAt = [this](const int index) {
        return index >= 0 && index < paths_.size() ? paths_.at(index) : QString{};
    };
    return {pathAt(selectedIndex_ - 1), pathAt(selectedIndex_ + 1)};
}

BrowsingSequence::ReconcileOutcome BrowsingSequence::reconcileDirectory()
{
    if (mode_ != Mode::DirectoryBacked) {
        return ReconcileOutcome::Unchanged;
    }

    const QString previousPath = selectedPath();
    const int previousIndex = selectedIndex_;
    BrowsingSequence refreshed = directoryBacked(directoryPath_, previousPath);
    if (refreshed.paths_ == paths_) {
        return ReconcileOutcome::Unchanged;
    }

    paths_ = std::move(refreshed.paths_);
    if (paths_.isEmpty()) {
        selectedIndex_ = -1;
        return ReconcileOutcome::Empty;
    }

    const int preservedIndex = paths_.indexOf(previousPath);
    if (preservedIndex >= 0) {
        selectedIndex_ = preservedIndex;
        return ReconcileOutcome::SelectionPreserved;
    }

    selectedIndex_ = std::clamp(previousIndex, 0, int(paths_.size()) - 1);
    return ReconcileOutcome::SelectionReplaced;
}
