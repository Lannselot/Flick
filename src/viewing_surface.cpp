// SPDX-License-Identifier: GPL-3.0-or-later

#include "viewing_surface.h"

#include <QFileInfo>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QResizeEvent>
#include <QSettings>
#include <QStackedLayout>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
constexpr int StatusVisibilityMilliseconds = 2000;
constexpr int FeedbackVisibilityMilliseconds = 1500;

constexpr int StatusFadeMilliseconds = 160;
} // namespace

ViewingSurface::ViewingSurface(QWidget *displayedContent, Commands commands,
                               QWidget *parent, Configuration configuration)
    : QWidget(parent), commands_(std::move(commands)), displayedContent_(displayedContent)
#ifdef FLICK_ENABLE_TEST_HARNESS
      ,
      loadingIndicatorDelayMilliseconds_(configuration.loadingIndicatorDelayMilliseconds),
      reducedMotion_(configuration.reducedMotion)
#endif
{
#ifndef FLICK_ENABLE_TEST_HARNESS
  Q_UNUSED(configuration)
#endif
  setObjectName(QStringLiteral("viewingSurface"));
  setAutoFillBackground(true);
  QPalette surfacePalette = palette();
  surfacePalette.setColor(QPalette::Window, QColor(QStringLiteral("#181A1B")));
  setPalette(surfacePalette);
  stack_ = new QStackedLayout(this);
  stack_->setContentsMargins(0, 0, 0, 0);
  stack_->setSpacing(0);

  emptyState_ = new QWidget;
  auto *emptyLayout = new QVBoxLayout(emptyState_);
  emptyLayout->setAlignment(Qt::AlignCenter);
  auto *emptyMark = new QLabel(tr("Flick"));
  auto *emptyTitle = new QLabel(tr("Open an image"));
  auto *chooseFile = new QPushButton(tr("Choose file"));
  auto *dropHint = new QLabel(tr("or drop it here"));
  auto *teaching =
      new QLabel(tr("← → Browse · Wheel Navigate · Right-click Commands"));
  for (QLabel *label : {emptyMark, emptyTitle, dropHint, teaching})
    label->setAlignment(Qt::AlignCenter);
  emptyState_->setAccessibleName(tr("Open an image"));
  chooseFile->setAccessibleDescription(tr("Choose a local image to display."));
  QObject::connect(chooseFile, &QPushButton::clicked, this,
                   commands_.chooseFile);
  emptyLayout->addWidget(emptyMark, 0, Qt::AlignHCenter);
  emptyLayout->addWidget(emptyTitle, 0, Qt::AlignHCenter);
  emptyLayout->addSpacing(8);
  emptyLayout->addWidget(chooseFile, 0, Qt::AlignHCenter);
  emptyLayout->addWidget(dropHint, 0, Qt::AlignHCenter);
  emptyLayout->addSpacing(8);
  emptyLayout->addWidget(teaching, 0, Qt::AlignHCenter);

  loadingState_ = new QWidget;
  auto *loadingLayout = new QVBoxLayout(loadingState_);
  loadingLayout->setAlignment(Qt::AlignCenter);
  loadingIndicator_ = new QLabel(tr("Loading…"));
  loadingFilename_ = new QLabel;
  loadingIndicator_->setAlignment(Qt::AlignCenter);
  loadingFilename_->setAlignment(Qt::AlignCenter);
  loadingState_->setAccessibleName(tr("Loading image"));
  loadingLayout->addWidget(loadingIndicator_, 0, Qt::AlignHCenter);
  loadingLayout->addWidget(loadingFilename_, 0, Qt::AlignHCenter);

  errorState_ = new QWidget;
  auto *errorStateLayout = new QVBoxLayout(errorState_);
  errorStateLayout->setAlignment(Qt::AlignCenter);
  auto *errorCard = new QWidget;
  errorCard->setObjectName(QStringLiteral("errorCard"));
  errorCard->setMaximumWidth(440);
  errorCard->setStyleSheet(
      QStringLiteral("QWidget#errorCard { background: palette(window); "
                     "border-radius: 6px; }"));
  auto *errorLayout = new QVBoxLayout(errorCard);
  errorLayout->setContentsMargins(20, 20, 20, 20);
  errorExplanation_ = new QLabel;
  errorExplanation_->setAlignment(Qt::AlignCenter);
  errorExplanation_->setWordWrap(true);
  errorExplanation_->setAccessibleName(tr("Image error"));
  errorDetailsButton_ = new QToolButton;
  errorDetailsButton_->setText(tr("Details"));
  errorDetailsButton_->setAccessibleDescription(
      tr("Shows or hides technical decoder details."));
  errorDetailsButton_->setCheckable(true);
  errorDetails_ = new QLabel;
  errorDetails_->setAlignment(Qt::AlignCenter);
  errorDetails_->setWordWrap(true);
  errorDetails_->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                         Qt::TextSelectableByKeyboard);
  errorDetails_->hide();
  QObject::connect(errorDetailsButton_, &QToolButton::toggled, errorDetails_,
                   &QWidget::setVisible);
  errorRetryButton_ = new QPushButton(tr("Retry"));
  QObject::connect(errorRetryButton_, &QPushButton::clicked, this,
                   commands_.retry);
  auto *errorButtons = new QWidget;
  auto *errorButtonLayout = new QHBoxLayout(errorButtons);
  errorButtonLayout->addWidget(errorRetryButton_);
  errorButtonLayout->addWidget(errorDetailsButton_);
  errorNavigationHint_ =
      new QLabel(tr("Left and right still browse adjacent images."));
  errorNavigationHint_->setAlignment(Qt::AlignCenter);
  errorLayout->addWidget(errorExplanation_);
  errorLayout->addWidget(errorButtons, 0, Qt::AlignHCenter);
  errorLayout->addWidget(errorNavigationHint_, 0, Qt::AlignHCenter);
  errorLayout->addWidget(errorDetails_);
  errorStateLayout->addWidget(errorCard, 0, Qt::AlignCenter);

  largeImageWarning_ = new QWidget;
  auto *warningStateLayout = new QVBoxLayout(largeImageWarning_);
  warningStateLayout->setAlignment(Qt::AlignCenter);
  auto *warningCard = new QWidget;
  warningCard->setObjectName(QStringLiteral("warningCard"));
  warningCard->setMaximumWidth(440);
  warningCard->setStyleSheet(
      QStringLiteral("QWidget#warningCard { background: palette(window); "
                     "border-radius: 6px; }"));
  auto *warningLayout = new QVBoxLayout(warningCard);
  warningLayout->setContentsMargins(20, 20, 20, 20);
  largeImageExplanation_ = new QLabel;
  largeImageExplanation_->setAlignment(Qt::AlignCenter);
  largeImageExplanation_->setWordWrap(true);
  largeImageExplanation_->setAccessibleName(tr("Large image warning"));
  auto *warningButtons = new QWidget;
  auto *warningButtonLayout = new QHBoxLayout(warningButtons);
  largeImageApproveButton_ = new QPushButton(tr("Open anyway"));
  largeImageApproveButton_->setObjectName(QStringLiteral("approveLargeImage"));
  largeImageApproveButton_->setAccessibleDescription(
      tr("Allows decoding of the current exceptionally large image."));
  auto *rejectLarge = new QPushButton(tr("Skip"));
  rejectLarge->setObjectName(QStringLiteral("rejectLargeImage"));
  rejectLarge->setAccessibleDescription(
      tr("Cancels decoding of the current exceptionally large image."));
  warningButtonLayout->addWidget(largeImageApproveButton_);
  warningButtonLayout->addWidget(rejectLarge);
  QObject::connect(largeImageApproveButton_, &QPushButton::clicked, this,
                   commands_.approveLargeImage);
  QObject::connect(rejectLarge, &QPushButton::clicked, this,
                   commands_.rejectLargeImage);
  warningLayout->addWidget(largeImageExplanation_);
  warningLayout->addWidget(warningButtons, 0, Qt::AlignHCenter);
  warningStateLayout->addWidget(warningCard, 0, Qt::AlignCenter);

  for (QWidget *state : {displayedContent_, emptyState_, loadingState_,
                         errorState_, largeImageWarning_})
    stack_->addWidget(state);

  statusDisplay_ = new QLabel(this);
  statusDisplay_->setAlignment(Qt::AlignCenter);
  statusDisplay_->setAccessibleName(tr("Image status"));
  statusDisplay_->setAccessibleDescription(
      tr("Current filename, sequence position, and zoom level."));
  statusDisplay_->setAttribute(Qt::WA_TransparentForMouseEvents);
  statusDisplay_->setObjectName(QStringLiteral("statusOverlay"));
  statusDisplay_->setStyleSheet(QStringLiteral(
      "QLabel#statusOverlay { color: #f5f5f5; background-color: rgba(20, 20, "
      "20, 238); border-radius: 11px; padding: 4px 10px; }"));
  statusOpacity_ = new QGraphicsOpacityEffect(statusDisplay_);
  statusDisplay_->setGraphicsEffect(statusOpacity_);
  statusFade_ = new QPropertyAnimation(statusOpacity_, "opacity", this);
  statusFade_->setDuration(StatusFadeMilliseconds);
  statusFade_->setStartValue(1.0);
  statusFade_->setEndValue(0.0);
  QObject::connect(statusFade_, &QPropertyAnimation::finished, this, [this] {
    statusDisplay_->hide();
    statusOpacity_->setOpacity(1.0);
    if (commands_.isFullscreen())
      commands_.hidePointer();
  });
  statusDisplay_->hide();
  statusTimer_ = new QTimer(this);
  statusTimer_->setSingleShot(true);
  QObject::connect(statusTimer_, &QTimer::timeout, this, [this] {
    if (statusIsFeedback_ && commands_.hasCurrentImage())
      showStatus();
    else {
      statusIsFeedback_ = false;
      hideStatus();
    }
  });

  loadingTimer_ = new QTimer(this);
  loadingTimer_->setSingleShot(true);
  QObject::connect(loadingTimer_, &QTimer::timeout, this, [this] {
    if (commands_.currentImageIsLoading()) {
      loadingIndicator_->show();
      loadingFilename_->show();
    }
  });

  dropOverlay_ = new QWidget(this);
  dropOverlay_->setAttribute(Qt::WA_TransparentForMouseEvents);
  dropOverlay_->setStyleSheet(
      QStringLiteral("QWidget { background: transparent; border: 2px solid "
                     "palette(highlight); } QLabel { border: none; }"));
  auto *dropLayout = new QVBoxLayout(dropOverlay_);
  dropLayout->setAlignment(Qt::AlignCenter);
  dropLabel_ = new QLabel;
  dropLabel_->setAlignment(Qt::AlignCenter);
  dropLabel_->setStyleSheet(QStringLiteral(
      "QLabel { background: rgba(16, 17, 18, 230); color: white; padding: 12px "
      "18px; border-radius: 6px; font-weight: 600; }"));
  dropLayout->addWidget(dropLabel_, 0, Qt::AlignCenter);
  dropOverlay_->hide();
  QSettings settings;
  browsingTeachingComplete_ =
      settings.value(QStringLiteral("teaching/browsingComplete"), false)
          .toBool();
  fullscreenTeachingComplete_ =
      settings.value(QStringLiteral("teaching/fullscreenComplete"), false)
          .toBool();
  showEmpty();
}

void ViewingSurface::showEmpty() {
  loadingTimer_->stop();
  showPresentation(emptyState_, State::Empty);
}
void ViewingSurface::beginLoading(const QString &filename) {
  loadingFilename_->setText(filename);
  loadingIndicator_->hide();
  loadingFilename_->hide();
  showPresentation(loadingState_, State::Loading);
#ifdef FLICK_ENABLE_TEST_HARNESS
  loadingTimer_->start(loadingIndicatorDelayMilliseconds_);
#else
  loadingTimer_->start(120);
#endif
}
void ViewingSurface::showDisplayed() {
  loadingTimer_->stop();
  showPresentation(displayedContent_, State::Displayed);
}
void ViewingSurface::showError(const QString &filename,
                               const QString &details) {
  loadingTimer_->stop();
  errorExplanation_->setText(
      tr("This image could not be displayed\n%1").arg(filename));
  errorDetails_->setText(details.isEmpty()
                             ? tr("The image decoder returned no pixels.")
                             : details);
  errorDetailsButton_->setChecked(false);
  showPresentation(errorState_, State::Error);
  errorRetryButton_->setDefault(true);
}
void ViewingSurface::showLargeImageConfirmation(const QString &message) {
  loadingTimer_->stop();
  largeImageExplanation_->setText(message);
  showPresentation(largeImageWarning_, State::LargeImageConfirmation);
  largeImageApproveButton_->setDefault(true);
}
void ViewingSurface::dismissLargeImageConfirmation() {
  largeImageWarning_->hide();
}
bool ViewingSurface::isLargeImageConfirmationVisible() const {
  return state_ == State::LargeImageConfirmation;
}
bool ViewingSurface::activatePrimaryAction() {
  if (state_ == State::Error) {
    errorRetryButton_->click();
    return true;
  }
  if (state_ == State::LargeImageConfirmation) {
    largeImageApproveButton_->click();
    return true;
  }
  return false;
}

void ViewingSurface::showStatus() {
  if (!statusEnabled_ || !commands_.hasCurrentImage())
    return;
  statusIsFeedback_ = false;
  statusFade_->stop();
  statusOpacity_->setOpacity(1.0);
  const StatusContext context = commands_.statusContext();
  const QString suffix = tr("%1 / %2 — %3%")
                             .arg(context.position)
                             .arg(context.count)
                             .arg(context.zoomPercent);
  const int maximumWidth = qMax(1, qMin(440, width() - 40));
  statusDisplay_->setMaximumWidth(maximumWidth);
  const int filenameWidth = qMax(
      1, maximumWidth - 20 -
             statusDisplay_->fontMetrics().horizontalAdvance(QStringLiteral(" — ") + suffix));
  const QString filename = statusDisplay_->fontMetrics().elidedText(
      context.filename, Qt::ElideMiddle, filenameWidth);
  statusDisplay_->setText(filename + QStringLiteral(" — ") + suffix);
  statusDisplay_->setToolTip(context.filename);
  statusDisplay_->adjustSize();
  positionStatus();
  statusDisplay_->show();
  statusDisplay_->raise();
  statusTimer_->start(StatusVisibilityMilliseconds);
}
void ViewingSurface::showFeedback(const QString &message) {
  if (!statusEnabled_)
    return;
  statusIsFeedback_ = true;
  statusFade_->stop();
  statusOpacity_->setOpacity(1.0);
  statusDisplay_->setText(message);
  statusDisplay_->setMaximumWidth(qMax(1, qMin(440, width() - 40)));
  statusDisplay_->adjustSize();
  positionStatus();
  statusDisplay_->show();
  statusDisplay_->raise();
  statusTimer_->start(FeedbackVisibilityMilliseconds);
}
void ViewingSurface::queueFeedback(const QString &message) {
  pendingFeedback_ = message;
}
void ViewingSurface::currentImageDisplayed() {
  if (!pendingFeedback_.isEmpty()) {
    const QString feedback = pendingFeedback_;
    pendingFeedback_.clear();
    showFeedback(feedback);
  } else if (!browsingTeachingComplete_) {
    showFeedback(tr("← → Browse · Right-click for commands"));
  }
}
void ViewingSurface::markBrowsingTeachingComplete() {
  if (browsingTeachingComplete_)
    return;
  browsingTeachingComplete_ = true;
  QSettings settings;
  settings.setValue(QStringLiteral("teaching/browsingComplete"), true);
  settings.sync();
}
void ViewingSurface::enteredFullscreen() {
  if (!fullscreenTeachingComplete_) {
    fullscreenTeachingComplete_ = true;
    QSettings settings;
    settings.setValue(QStringLiteral("teaching/fullscreenComplete"), true);
    settings.sync();
    showFeedback(tr("F11 or Esc to exit fullscreen"));
  } else {
    showStatus();
  }
}
void ViewingSurface::hideStatus() {
  statusTimer_->stop();
  if (!statusDisplay_->isVisible())
    return;
  if (
#ifdef FLICK_ENABLE_TEST_HARNESS
      reducedMotion_ ||
#endif
      style()->styleHint(QStyle::SH_Widget_Animation_Duration, nullptr, this) <=
          0) {
    statusDisplay_->hide();
    if (commands_.isFullscreen())
      commands_.hidePointer();
    return;
  }
  statusFade_->start();
}
void ViewingSurface::setStatusVisible(bool visible) {
  statusEnabled_ = visible;
  if (!visible) {
    statusTimer_->stop();
    statusFade_->stop();
    statusDisplay_->hide();
  }
}
bool ViewingSurface::statusVisible() const {
  return statusDisplay_->isVisible();
}
bool ViewingSurface::statusEnabled() const { return statusEnabled_; }
QString ViewingSurface::statusText() const { return statusDisplay_->text(); }

void ViewingSurface::showDropTarget(int count) {
  dropLabel_->setText(count == 1 ? tr("Drop to open")
                                 : tr("Drop to browse %1 images").arg(count));
  dropOverlay_->setGeometry(rect());
  dropOverlay_->show();
  dropOverlay_->raise();
}
void ViewingSurface::hideDropTarget() { dropOverlay_->hide(); }
bool ViewingSurface::dropTargetVisible() const {
  return dropOverlay_->isVisible();
}
QString ViewingSurface::dropTargetText() const { return dropLabel_->text(); }
ViewingSurface::State ViewingSurface::state() const { return state_; }

#ifdef FLICK_ENABLE_TEST_HARNESS
ViewingSurface::PresentationSnapshot ViewingSurface::presentationSnapshot() const {
  QWidget *current = stack_->currentWidget();
  const bool error = state_ == State::Error;
  const bool large = state_ == State::LargeImageConfirmation;
  return {state_,
          dropTargetVisible(),
          dropTargetText(),
          loadingFilename_->text(),
          loadingIndicator_->isVisible(),
          errorExplanation_->text(),
          errorDetails_->text(),
          errorDetails_->isVisible(),
          errorRetryButton_->text(),
          errorDetailsButton_->text(),
          errorNavigationHint_->text(),
          largeImageExplanation_->text(),
          error ? errorRetryButton_->text() : large ? largeImageApproveButton_->text() : QString{},
          error ? errorRetryButton_->isDefault() : large && largeImageApproveButton_->isDefault(),
          error ? errorDetailsButton_->text() : large ? tr("Skip") : QString{},
          usesOptionalOpacity(state_),
          current != nullptr && current->graphicsEffect() != nullptr};
}
#endif

void ViewingSurface::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  dropOverlay_->setGeometry(rect());
  positionStatus();
}
void ViewingSurface::positionStatus() {
  if (!statusDisplay_)
    return;
  statusDisplay_->move((width() - statusDisplay_->width()) / 2,
                       qMax(12, height() - statusDisplay_->height() - 20));
}
bool ViewingSurface::usesOptionalOpacity(State state) {
  return state == State::Empty || state == State::Error ||
         state == State::LargeImageConfirmation;
}
void ViewingSurface::showPresentation(QWidget *widget, State state) {
  state_ = state;
  stack_->setCurrentWidget(widget);
  if (!usesOptionalOpacity(state) ||
#ifdef FLICK_ENABLE_TEST_HARNESS
      reducedMotion_ ||
#endif
      style()->styleHint(QStyle::SH_Widget_Animation_Duration, nullptr, this) <=
          0 ||
      !isVisible())
    return;
  auto *effect = new QGraphicsOpacityEffect(widget);
  widget->setGraphicsEffect(effect);
  auto *fade = new QPropertyAnimation(effect, "opacity", widget);
  fade->setDuration(160);
  fade->setStartValue(0.0);
  fade->setEndValue(1.0);
  QObject::connect(fade, &QPropertyAnimation::finished, widget, [widget, fade] {
    widget->setGraphicsEffect(nullptr);
    fade->deleteLater();
  });
  fade->start();
}
