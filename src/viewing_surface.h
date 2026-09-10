// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QWidget>

#include <functional>

class QLabel;
class QGraphicsOpacityEffect;
class QPropertyAnimation;
class QPushButton;
class QStackedLayout;
class QTimer;
class QToolButton;

class ViewingSurface final : public QWidget {
public:
  enum class State { Empty, Loading, Displayed, Error, LargeImageConfirmation };

  struct StatusContext {
    QString filename;
    int position = 0;
    int count = 0;
    int zoomPercent = 100;
  };

  struct Commands {
    std::function<void()> chooseFile;
    std::function<void()> retry;
    std::function<void()> approveLargeImage;
    std::function<void()> rejectLargeImage;
    std::function<bool()> currentImageIsLoading;
    std::function<bool()> hasCurrentImage;
    std::function<StatusContext()> statusContext;
    std::function<bool()> isFullscreen;
    std::function<void()> hidePointer;
  };

  explicit ViewingSurface(QWidget *displayedContent, Commands commands,
                          QWidget *parent = nullptr);

  void showEmpty();
  void beginLoading(const QString &filename);
  void showDisplayed(bool animate = true);
  void showError(const QString &filename, const QString &details);
  void showLargeImageConfirmation(const QString &message);
  void dismissLargeImageConfirmation();
  bool isLargeImageConfirmationVisible() const;

  void showStatus();
  void showFeedback(const QString &message);
  void queueFeedback(const QString &message);
  void currentImageDisplayed();
  void markBrowsingTeachingComplete();
  void enteredFullscreen();
  void hideStatus();
  void setStatusVisible(bool visible);
  bool statusVisible() const;
  bool statusEnabled() const;
  QString statusText() const;

  void showDropTarget(int supportedCount);
  void hideDropTarget();
  bool dropTargetVisible() const;
  QString dropTargetText() const;

  State state() const;
  QByteArray presentationDescription() const;
  QByteArray errorDescription() const;

protected:
  void resizeEvent(QResizeEvent *event) override;

private:
  void showPresentation(QWidget *widget, State state, bool animate = true);
  void positionStatus();

  Commands commands_;
  QStackedLayout *stack_ = nullptr;
  QWidget *displayedContent_ = nullptr;
  QWidget *emptyState_ = nullptr;
  QWidget *loadingState_ = nullptr;
  QLabel *loadingIndicator_ = nullptr;
  QLabel *loadingFilename_ = nullptr;
  QTimer *loadingTimer_ = nullptr;
  QWidget *errorState_ = nullptr;
  QLabel *errorExplanation_ = nullptr;
  QLabel *errorDetails_ = nullptr;
  QToolButton *errorDetailsButton_ = nullptr;
  QPushButton *errorRetryButton_ = nullptr;
  QLabel *errorNavigationHint_ = nullptr;
  QWidget *largeImageWarning_ = nullptr;
  QLabel *largeImageExplanation_ = nullptr;
  QLabel *statusDisplay_ = nullptr;
  QTimer *statusTimer_ = nullptr;
  QGraphicsOpacityEffect *statusOpacity_ = nullptr;
  QPropertyAnimation *statusFade_ = nullptr;
  QWidget *dropOverlay_ = nullptr;
  QLabel *dropLabel_ = nullptr;
  State state_ = State::Empty;
  bool statusIsFeedback_ = false;
  bool statusEnabled_ = true;
  bool browsingTeachingComplete_ = false;
  bool fullscreenTeachingComplete_ = false;
  QString pendingFeedback_;
};
