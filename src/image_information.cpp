// SPDX-License-Identifier: GPL-3.0-or-later

#include "image_information.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <utility>

namespace ImageInformation {
namespace {

QString describe(const Snapshot &snapshot) {
  if (snapshot.path.isEmpty()) {
    return QDialog::tr("No current image");
  }
  const QString dimensions =
      snapshot.decodedAvailability == DecodedAvailability::Available
          ? QDialog::tr("%1 × %2")
                .arg(snapshot.dimensions.width())
                .arg(snapshot.dimensions.height())
      : snapshot.decodedAvailability == DecodedAvailability::Unavailable
          ? QDialog::tr("Unavailable")
          : QDialog::tr("Loading…");
  QString animation;
  switch (snapshot.animationState) {
  case AnimationState::Static:
    animation = QDialog::tr("Static image");
    break;
  case AnimationState::Playing:
    animation = QDialog::tr("Playing");
    break;
  case AnimationState::Paused:
    animation = QDialog::tr("Paused");
    break;
  case AnimationState::Finished:
    animation = QDialog::tr("Finished");
    break;
  case AnimationState::Unavailable:
    animation = QDialog::tr("Unavailable");
    break;
  }
  return QDialog::tr("Path: %1\nFormat: %2\nDimensions: %3\nSize: %4 bytes\n"
                     "Modified: %5\nZoom: %6%\nRotation: %7°\nAnimation: "
                     "%8\nPosition: %9 / %10")
      .arg(snapshot.path, snapshot.format)
      .arg(dimensions)
      .arg(snapshot.fileSize)
      .arg(QLocale().toString(snapshot.modified, QLocale::ShortFormat))
      .arg(snapshot.zoomPercent)
      .arg(snapshot.rotationDegrees)
      .arg(animation)
      .arg(snapshot.position)
      .arg(snapshot.sequenceSize);
}

} // namespace

class Dialog::Implementation final : public QObject {
public:
  Implementation(QWidget &owner, std::function<void()> closed)
      : owner(owner), closed(std::move(closed)) {
    owner.installEventFilter(this);
  }

  bool eventFilter(QObject *watched, QEvent *event) override {
    if (watched == &owner && dialog != nullptr &&
        event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
      dialog->close();
      return true;
    }
    return QObject::eventFilter(watched, event);
  }

  QWidget &owner;
  std::function<void()> closed;
  QString text;
  QDialog *dialog = nullptr;
  QLabel *facts = nullptr;
};

Dialog::Dialog(QWidget &owner, std::function<void()> closed)
    : implementation_(
          std::make_unique<Implementation>(owner, std::move(closed))) {}

Dialog::~Dialog() {
  implementation_->owner.removeEventFilter(implementation_.get());
  if (implementation_->dialog != nullptr) {
    QObject::disconnect(implementation_->dialog, nullptr,
                        &implementation_->owner, nullptr);
    delete implementation_->dialog;
  }
}

void Dialog::open(const Snapshot &snapshot) {
  if (implementation_->dialog != nullptr) {
    implementation_->dialog->raise();
    implementation_->dialog->activateWindow();
    return;
  }
  implementation_->text = describe(snapshot);
  auto *dialog = new QDialog(&implementation_->owner, Qt::Tool);
  implementation_->dialog = dialog;
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setModal(false);
  dialog->setWindowTitle(QDialog::tr("Image Information"));
  dialog->setMaximumSize(480, 320);
  auto *layout = new QVBoxLayout(dialog);
  implementation_->facts = new QLabel(implementation_->text, dialog);
  implementation_->facts->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                                  Qt::TextSelectableByKeyboard);
  implementation_->facts->setWordWrap(true);
  implementation_->facts->setAccessibleName(
      QDialog::tr("Current image information"));
  auto *factsViewport = new QScrollArea(dialog);
  factsViewport->setWidgetResizable(true);
  factsViewport->setFrameShape(QFrame::NoFrame);
  factsViewport->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  factsViewport->setWidget(implementation_->facts);
  factsViewport->setMinimumSize(360, 180);
  layout->addWidget(factsViewport);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
  QObject::connect(buttons, &QDialogButtonBox::rejected, dialog,
                   &QDialog::close);
  layout->addWidget(buttons);
  auto *implementation = implementation_.get();
  QObject::connect(dialog, &QDialog::finished, &implementation_->owner,
                   [implementation] {
    implementation->dialog = nullptr;
    implementation->facts = nullptr;
    QTimer::singleShot(0, &implementation->owner, implementation->closed);
  });
  dialog->show();
}

void Dialog::update(const Snapshot &snapshot) {
  if (implementation_->dialog == nullptr)
    return;
  implementation_->text = describe(snapshot);
  implementation_->facts->setText(implementation_->text);
  implementation_->dialog->adjustSize();
}

bool Dialog::isOpen() const { return implementation_->dialog != nullptr; }
QString Dialog::text() const { return implementation_->text; }
DialogState Dialog::state() const {
  return {.open = implementation_->dialog != nullptr,
          .size = implementation_->dialog != nullptr
                      ? implementation_->dialog->size()
                      : QSize{}};
}

} // namespace ImageInformation
