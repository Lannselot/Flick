// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QApplication>
#include <QStringList>

#include <functional>

class FlickApplication final : public QApplication
{
public:
    using FileOpenHandler = std::function<void(const QString &)>;

    FlickApplication(int &argc, char **argv);

    void setFileOpenHandler(FileOpenHandler handler);

protected:
    bool event(QEvent *event) override;

private:
    FileOpenHandler fileOpenHandler_;
    QStringList pendingFileOpenPaths_;
};
