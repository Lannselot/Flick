// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDateTime>
#include <QSize>
#include <QString>

#include <functional>
#include <memory>

class QWidget;

namespace ImageInformation {

enum class DecodedAvailability { Loading, Unavailable, Available };
enum class AnimationState { Unavailable, Static, Playing, Paused, Finished };

struct Snapshot {
  QString path;
  QString format;
  qint64 fileSize = 0;
  QDateTime modified;
  DecodedAvailability decodedAvailability = DecodedAvailability::Loading;
  QSize dimensions;
  int zoomPercent = 100;
  int rotationDegrees = 0;
  AnimationState animationState = AnimationState::Unavailable;
  int position = 0;
  int sequenceSize = 0;
};

struct DialogState {
  bool open = false;
  QSize size;
};

class Dialog final {
public:
  Dialog(QWidget &owner, std::function<void()> closed);
  ~Dialog();
  Dialog(const Dialog &) = delete;
  Dialog &operator=(const Dialog &) = delete;

  void open(const Snapshot &snapshot);
  void update(const Snapshot &snapshot);
  bool isOpen() const;
  QString text() const;
  DialogState state() const;

private:
  class Implementation;
  std::unique_ptr<Implementation> implementation_;
};

} // namespace ImageInformation
