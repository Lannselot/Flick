// SPDX-License-Identifier: GPL-3.0-or-later

#include "flick_application.h"

#include <QEvent>
#include <QFileOpenEvent>

#include <utility>

FlickApplication::FlickApplication(int &argc, char **argv)
    : QApplication(argc, argv)
{
}

void FlickApplication::setFileOpenHandler(FileOpenHandler handler)
{
    fileOpenHandler_ = std::move(handler);
    if (!fileOpenHandler_) {
        return;
    }
    const QStringList pendingPaths = std::exchange(pendingFileOpenPaths_, {});
    for (const QString &path : pendingPaths) {
        fileOpenHandler_(path);
    }
}

bool FlickApplication::event(QEvent *event)
{
    if (event->type() != QEvent::FileOpen) {
        return QApplication::event(event);
    }
    const QString path = static_cast<QFileOpenEvent *>(event)->file();
    if (!path.isEmpty()) {
        if (fileOpenHandler_) {
            fileOpenHandler_(path);
        } else {
            pendingFileOpenPaths_.append(path);
        }
    }
    event->accept();
    return true;
}
