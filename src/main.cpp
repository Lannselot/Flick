// SPDX-License-Identifier: GPL-3.0-or-later

#include "flick_application.h"
#include "image_loading.h"
#include "platform_services.h"

#include <QApplication>
#include <QAbstractButton>
#include <QAccessible>
#include <QAccessibleWidget>
#include <QAction>
#include <QActionGroup>
#include <QClipboard>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QColorSpace>
#include <QColorDialog>
#include "browsing_sequence.h"
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QCursor>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHash>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFrame>
#include <QFormLayout>
#include <QGraphicsOpacityEffect>
#include <QGroupBox>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMimeData>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QScrollBar>
#include <QScreen>
#include <QSettings>
#include <QSet>
#include <QSpinBox>
#include <QStackedLayout>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QTransform>
#include <QVBoxLayout>
#include <QWidget>
#include <QWheelEvent>
#include <QWindow>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>

#ifdef FLICK_ENABLE_TEST_HARNESS
#include <QPixmap>
#include <QSocketNotifier>
#include <QUrl>
#include <unistd.h>
#endif

namespace {
constexpr qsizetype DefaultCacheBudgetBytes = 512LL * 1024 * 1024;
constexpr int StatusVisibilityMilliseconds = 2000;
constexpr int FeedbackVisibilityMilliseconds = 1500;
constexpr int StatusFadeMilliseconds = 160;
constexpr int KeyboardPanStep = 40;
constexpr qint64 LargeImageAllocationLimit = 1024LL * 1024 * 1024;
constexpr int LoadingIndicatorDelayMilliseconds = 120;

enum class WheelAction
{
    Navigate,
    Zoom
};

using ImageLoading::LoadedImage;

QAccessibleInterface *flickAccessibleInterface(const QString &, QObject *object)
{
    auto *widget = qobject_cast<QWidget *>(object);
    if (widget && widget->objectName() == QStringLiteral("imageLabel")) {
        return new QAccessibleWidget(widget, QAccessible::Graphic);
    }
    return nullptr;
}

class ImageCanvas final : public QLabel
{
public:
    void showImage(const QImage &image, const QSize &displayedSize, const QSize &viewportSize)
    {
        image_ = image;
        displayedSize_ = displayedSize;
        setFixedSize(displayedSize + viewportSize);
        update();
    }

    void clearImage()
    {
        image_ = {};
        displayedSize_ = {};
        setFixedSize(QSize());
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        if (image_.isNull()) {
            return;
        }
        QPainter painter(this);
        painter.setClipRect(event->rect());
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const QRect target((width() - displayedSize_.width()) / 2,
                           (height() - displayedSize_.height()) / 2,
                           displayedSize_.width(), displayedSize_.height());
        const QRect visibleTarget = target.intersected(event->rect());
        if (visibleTarget.isEmpty()) {
            return;
        }
        const double sourceScaleX = double(image_.width()) / target.width();
        const double sourceScaleY = double(image_.height()) / target.height();
        const QRectF source((visibleTarget.left() - target.left()) * sourceScaleX,
                            (visibleTarget.top() - target.top()) * sourceScaleY,
                            visibleTarget.width() * sourceScaleX,
                            visibleTarget.height() * sourceScaleY);
        painter.drawImage(QRectF(visibleTarget), image_, source);
    }

private:
    QImage image_;
    QSize displayedSize_;
};

class ViewerWindow final : public QWidget
{
    enum class PresentationState
    {
        Empty,
        Loading,
        Displayed,
        Error,
        LargeImageConfirmation
    };

public:
    ViewerWindow(const QString &imagePath, std::unique_ptr<PlatformServices> platformServices)
        : platformServices_(std::move(platformServices))
        , imageLoader_(this)
    {
        setWindowTitle(QStringLiteral("Flick"));
        setObjectName(QStringLiteral("viewingSurfaceFocus"));
        setMinimumSize(480, 320);
        setAcceptDrops(true);

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        surface_ = new QWidget;
        surface_->setObjectName(QStringLiteral("viewingSurface"));
        surface_->setAutoFillBackground(true);
        QPalette surfacePalette = surface_->palette();
        surfacePalette.setColor(QPalette::Window, QColor(QStringLiteral("#181A1B")));
        surface_->setPalette(surfacePalette);
        surfaceStack_ = new QStackedLayout(surface_);
        surfaceStack_->setContentsMargins(0, 0, 0, 0);
        surfaceStack_->setSpacing(0);

        imageLabel_ = new ImageCanvas;
        imageLabel_->setObjectName(QStringLiteral("imageLabel"));
        imageLabel_->setAlignment(Qt::AlignCenter);
        imageLabel_->setAccessibleName(tr("Image viewport"));
        imageLabel_->setAccessibleDescription(
            tr("Displays the current image; use the application actions to navigate and zoom."));
        imageLabel_->setMouseTracking(true);
        imageLabel_->installEventFilter(this);

        viewport_ = new QScrollArea;
        viewport_->setAlignment(Qt::AlignCenter);
        viewport_->setBackgroundRole(QPalette::Dark);
        viewport_->setFocusPolicy(Qt::NoFocus);
        viewport_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        viewport_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        viewport_->setWidget(imageLabel_);
        viewport_->viewport()->installEventFilter(this);
        surface_->installEventFilter(this);
        viewport_->viewport()->setMouseTracking(true);
        viewport_->setContextMenuPolicy(Qt::CustomContextMenu);
        auto *wheelActionGroup = new QActionGroup(this);
        wheelActionGroup->setExclusive(true);
        auto *navigateWithWheel = new QAction(tr("Wheel navigates images"), wheelActionGroup);
        navigateWithWheel->setObjectName(QStringLiteral("wheelNavigateAction"));
        navigateWithWheel->setCheckable(true);
        auto *zoomWithWheel = new QAction(tr("Wheel zooms image"), wheelActionGroup);
        zoomWithWheel->setObjectName(QStringLiteral("wheelZoomAction"));
        zoomWithWheel->setCheckable(true);
        viewport_->addAction(navigateWithWheel);
        viewport_->addAction(zoomWithWheel);
        navigateWithWheel->setChecked(wheelAction_ == WheelAction::Navigate);
        zoomWithWheel->setChecked(wheelAction_ == WheelAction::Zoom);
        QObject::connect(navigateWithWheel, &QAction::triggered, this, [this] {
            setWheelAction(WheelAction::Navigate);
        });
        QObject::connect(zoomWithWheel, &QAction::triggered, this, [this] {
            setWheelAction(WheelAction::Zoom);
        });
        addViewerActions();
        addImageActions();

        QTimer::singleShot(0, this, [this] {
            if (windowHandle()) {
                QObject::connect(windowHandle(), &QWindow::screenChanged, this,
                                 [this] { displayConfigurationChanged(); });
            }
            displayConfigurationChanged();
        });
        setFocusPolicy(Qt::StrongFocus);

        animationTimer_ = new QTimer(this);
        animationTimer_->setSingleShot(true);
        QObject::connect(animationTimer_, &QTimer::timeout, this, [this] {
            advanceAnimation();
        });
        directoryWatcher_ = new QFileSystemWatcher(this);
        QObject::connect(directoryWatcher_, &QFileSystemWatcher::directoryChanged, this,
                         [this] {
                             refreshDirectorySequence();
                         });

        statusDisplay_ = new QLabel(surface_);
        statusDisplay_->setAlignment(Qt::AlignCenter);
        statusDisplay_->setAccessibleName(tr("Image status"));
        statusDisplay_->setAccessibleDescription(
            tr("Current filename, sequence position, and zoom level."));
        statusDisplay_->setAttribute(Qt::WA_TransparentForMouseEvents);
        statusDisplay_->setObjectName(QStringLiteral("statusOverlay"));
        statusDisplay_->setStyleSheet(QStringLiteral(
            "QLabel#statusOverlay { color: #f5f5f5; background-color: rgba(20, 20, 20, 238); "
            "border-radius: 11px; padding: 4px 10px; }"));
        statusOpacity_ = new QGraphicsOpacityEffect(statusDisplay_);
        statusDisplay_->setGraphicsEffect(statusOpacity_);
        statusFade_ = new QPropertyAnimation(statusOpacity_, "opacity", this);
        statusFade_->setDuration(StatusFadeMilliseconds);
        statusFade_->setStartValue(1.0);
        statusFade_->setEndValue(0.0);
        QObject::connect(statusFade_, &QPropertyAnimation::finished, this, [this] {
            statusDisplay_->hide();
            statusOpacity_->setOpacity(1.0);
            if (isFullScreen()) {
                viewport_->viewport()->setCursor(Qt::BlankCursor);
            }
        });
        statusDisplay_->hide();
        statusTimer_ = new QTimer(this);
        statusTimer_->setSingleShot(true);
        QObject::connect(statusTimer_, &QTimer::timeout, this, [this] {
            if (statusIsFeedback_ && browsingSequence_.selectedIndex() >= 0) {
                showStatus(false);
            } else {
                statusIsFeedback_ = false;
                hideStatus();
            }
        });
        loadSettings();

        loadingTimer_ = new QTimer(this);
        loadingTimer_->setSingleShot(true);
        QObject::connect(loadingTimer_, &QTimer::timeout, this, [this] {
            if (imageLoader_.isLoading(browsingSequence_.selectedPath())) {
                loadingIndicator_->show();
                loadingFilename_->show();
            }
        });

        imageLoader_.setOutcomeHandler([this](ImageLoading::DecodeOutcome outcome) {
            loadingTimer_->stop();
            if (const auto *confirmation =
                    std::get_if<ImageLoading::ConfirmationRequired>(&outcome)) {
                showLargeImageWarning(*confirmation);
                return;
            }
            if (const auto *loaded = std::get_if<LoadedImage>(&outcome)) {
                present(loaded->path, *loaded);
                prefetchNeighbors();
                return;
            }
            showDecodeError(std::get<ImageLoading::DecodeFailure>(outcome));
        });

        emptyState_ = new QWidget;
        auto *emptyLayout = new QVBoxLayout(emptyState_);
        emptyLayout->setAlignment(Qt::AlignCenter);
        auto *emptyMark = new QLabel(tr("Flick"));
        auto *emptyTitle = new QLabel(tr("Open an image"));
        auto *chooseFile = new QPushButton(tr("Choose file"));
        auto *dropHint = new QLabel(tr("or drop it here"));
        auto *teaching = new QLabel(tr("← → Browse · Wheel Navigate · Right-click Commands"));
        emptyMark->setAlignment(Qt::AlignCenter);
        emptyTitle->setAlignment(Qt::AlignCenter);
        dropHint->setAlignment(Qt::AlignCenter);
        teaching->setAlignment(Qt::AlignCenter);
        emptyState_->setAccessibleName(tr("Open an image"));
        chooseFile->setAccessibleDescription(tr("Choose a local image to display."));
        QObject::connect(chooseFile, &QPushButton::clicked, this, [this] { openFromFilePicker(); });
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
        errorCard->setStyleSheet(QStringLiteral(
            "QWidget#errorCard { background: palette(window); border-radius: 6px; }"));
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
        errorDetails_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        errorDetails_->setTextInteractionFlags(errorDetails_->textInteractionFlags() |
                                               Qt::TextSelectableByKeyboard);
        errorDetails_->hide();
        QObject::connect(errorDetailsButton_, &QToolButton::toggled, errorDetails_,
                         &QWidget::setVisible);
        errorRetryButton_ = new QPushButton(tr("Retry"));
        QObject::connect(errorRetryButton_, &QPushButton::clicked, this,
                         [this] { retryCurrentImage(); });
        auto *errorButtons = new QWidget;
        auto *errorButtonLayout = new QHBoxLayout(errorButtons);
        errorButtonLayout->addWidget(errorRetryButton_);
        errorButtonLayout->addWidget(errorDetailsButton_);
        errorNavigationHint_ = new QLabel(tr("Left and right still browse adjacent images."));
        errorNavigationHint_->setAlignment(Qt::AlignCenter);
        errorLayout->addWidget(errorExplanation_);
        errorLayout->addWidget(errorButtons, 0, Qt::AlignHCenter);
        errorLayout->addWidget(errorNavigationHint_, 0, Qt::AlignHCenter);
        errorLayout->addWidget(errorDetails_);
        errorStateLayout->addWidget(errorCard, 0, Qt::AlignCenter);
        errorState_->hide();

        largeImageWarning_ = new QWidget;
        auto *warningStateLayout = new QVBoxLayout(largeImageWarning_);
        warningStateLayout->setAlignment(Qt::AlignCenter);
        auto *warningCard = new QWidget;
        warningCard->setObjectName(QStringLiteral("warningCard"));
        warningCard->setMaximumWidth(440);
        warningCard->setStyleSheet(QStringLiteral(
            "QWidget#warningCard { background: palette(window); border-radius: 6px; }"));
        auto *warningLayout = new QVBoxLayout(warningCard);
        warningLayout->setContentsMargins(20, 20, 20, 20);
        largeImageExplanation_ = new QLabel;
        largeImageExplanation_->setAlignment(Qt::AlignCenter);
        largeImageExplanation_->setWordWrap(true);
        largeImageExplanation_->setAccessibleName(tr("Large image warning"));
        auto *warningButtons = new QWidget;
        auto *warningButtonLayout = new QHBoxLayout(warningButtons);
        auto *approveLarge = new QPushButton(tr("Open anyway"));
        approveLarge->setObjectName(QStringLiteral("approveLargeImage"));
        approveLarge->setAccessibleDescription(
            tr("Allows decoding of the current exceptionally large image."));
        auto *rejectLarge = new QPushButton(tr("Skip"));
        rejectLarge->setObjectName(QStringLiteral("rejectLargeImage"));
        rejectLarge->setAccessibleDescription(
            tr("Cancels decoding of the current exceptionally large image."));
        warningButtonLayout->addWidget(approveLarge);
        warningButtonLayout->addWidget(rejectLarge);
        QObject::connect(approveLarge, &QPushButton::clicked, this, [this] {
            approveLargeImage();
        });
        QObject::connect(rejectLarge, &QPushButton::clicked, this, [this] {
            rejectLargeImage();
        });
        warningLayout->addWidget(largeImageExplanation_);
        warningLayout->addWidget(warningButtons, 0, Qt::AlignHCenter);
        warningStateLayout->addWidget(warningCard, 0, Qt::AlignCenter);
        largeImageWarning_->hide();

        for (QWidget *state : {emptyState_, loadingState_, errorState_, largeImageWarning_}) {
            state->installEventFilter(this);
        }

        surfaceStack_->addWidget(viewport_);
        surfaceStack_->addWidget(emptyState_);
        surfaceStack_->addWidget(loadingState_);
        surfaceStack_->addWidget(errorState_);
        surfaceStack_->addWidget(largeImageWarning_);
        layout->addWidget(surface_);

        dropOverlay_ = new QWidget(surface_);
        dropOverlay_->setAttribute(Qt::WA_TransparentForMouseEvents);
        dropOverlay_->setStyleSheet(QStringLiteral(
            "QWidget { background: transparent; border: 2px solid palette(highlight); } "
            "QLabel { border: none; }"));
        auto *dropLayout = new QVBoxLayout(dropOverlay_);
        dropLayout->setAlignment(Qt::AlignCenter);
        dropLabel_ = new QLabel;
        dropLabel_->setAlignment(Qt::AlignCenter);
        dropLabel_->setStyleSheet(QStringLiteral(
            "QLabel { background: rgba(16, 17, 18, 230); color: white; padding: 12px 18px; "
            "border-radius: 6px; font-weight: 600; }"));
        dropLayout->addWidget(dropLabel_, 0, Qt::AlignCenter);
        dropOverlay_->hide();
        showEmptyState();

        auto *settingsAction = new QAction(tr("Settings"), this);
        settingsAction->setObjectName(QStringLiteral("settingsAction"));
        settingsAction->setShortcut(QKeySequence::Preferences);
        settingsAction->setShortcutContext(Qt::WindowShortcut);
        QObject::connect(settingsAction, &QAction::triggered, this, [this] {
            showSettings();
        });
        viewport_->addAction(settingsAction);
        addAction(settingsAction);

        addCommandSurfaces(settingsAction, layout);

        if (!imagePath.isEmpty()) {
            openDirectoryBacked(imagePath);
        }
    }

    bool isLoading() const
    {
        return imageLoader_.isLoading(browsingSequence_.selectedPath());
    }

    void displayConfigurationChanged()
    {
        refreshDisplayColorSpace();
    }

    void persistWindowGeometry()
    {
        QSettings settings;
        if (restoreWindowGeometry_) {
            settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
        } else {
            settings.remove(QStringLiteral("window/geometry"));
        }
        settings.sync();
    }

    void openExternalFile(const QString &path)
    {
        openDirectoryBacked(path);
        showNormal();
        raise();
        activateWindow();
    }

#ifdef FLICK_ENABLE_TEST_HARNESS
    qsizetype cacheBytes() const
    {
        return imageLoader_.cacheBytes();
    }

    qsizetype decodesInFlight() const
    {
        return imageLoader_.requestsInFlight();
    }

    int decodeCount(const QString &path) const
    {
        return decodeCounts_.value(QFileInfo(path).canonicalFilePath());
    }

    QByteArray viewState() const
    {
        return QByteArray::number(zoom_, 'f', 6) + ',' +
               QByteArray::number(viewport_->horizontalScrollBar()->value()) + ',' +
               QByteArray::number(viewport_->verticalScrollBar()->value()) + ',' +
               QByteArray::number(viewport_->viewport()->width()) + ',' +
               QByteArray::number(viewport_->viewport()->height()) + ',' +
               QByteArray::number(imageOrigin().x()) + ',' +
               QByteArray::number(imageOrigin().y());
    }

    QByteArray uiState() const
    {
        return QByteArray(isFullScreen() ? "fullscreen" : "windowed") + '|' +
               (statusDisplay_->isVisible() ? "status-visible" : "status-hidden") + '|' +
               (viewport_->viewport()->cursor().shape() == Qt::BlankCursor ? "pointer-hidden"
                                                                           : "pointer-visible") +
               '|' + statusDisplay_->text().toUtf8();
    }

    QByteArray informationState() const
    {
        return informationText_.toUtf8();
    }

    QByteArray informationDialogState() const
    {
        return informationDialog_ == nullptr
                   ? QByteArrayLiteral("closed")
                   : QByteArrayLiteral("open|") + QByteArray::number(informationDialog_->width()) +
                         'x' + QByteArray::number(informationDialog_->height());
    }

    void focusViewingSurfaceForTest()
    {
        activateWindow();
        viewport_->setFocus(Qt::OtherFocusReason);
    }

    QByteArray feedbackState() const
    {
        return statusDisplay_->text().toUtf8();
    }

    QByteArray errorState() const
    {
        return QByteArray(errorState_->isVisible() ? "visible" : "hidden") + '|' +
               errorExplanation_->text().toUtf8() + '|' + errorDetails_->text().toUtf8() + '|' +
               (errorDetails_->isVisible() ? "details-visible" : "details-hidden");
    }

    QByteArray largeImageState() const
    {
        return QByteArray(largeImageWarning_->isVisible() ? "visible" : "hidden") + '|' +
               QByteArray::number(pendingLargeImageSize_.width()) + 'x' +
               QByteArray::number(pendingLargeImageSize_.height());
    }

    QByteArray contextActions() const
    {
        QStringList descriptions;
        for (const QAction *action : viewport_->actions()) {
            if (!action->objectName().startsWith(QStringLiteral("image"))) {
                continue;
            }
            descriptions.append(action->text() + QStringLiteral(" [") +
                                action->shortcut().toString(QKeySequence::NativeText) +
                                QStringLiteral("]"));
        }
        return descriptions.join(QLatin1Char('|')).toUtf8();
    }

    QByteArray contextMenuStructure() const
    {
        return menuStructure(contextMenu_).toUtf8();
    }

    QByteArray applicationMenuStructure() const
    {
        return menuStructure(applicationMenuBar_).toUtf8();
    }

    QByteArray commandAvailability() const
    {
        QStringList entries;
        for (const QAction *action : contextMenu_->actions()) {
            if (!action->isSeparator()) {
                entries.append(action->text() + QLatin1Char('=') +
                               (action->isEnabled() ? QStringLiteral("enabled")
                                                    : QStringLiteral("disabled")));
            }
        }
        return entries.join(QLatin1Char('|')).toUtf8();
    }

    QByteArray focusState() const
    {
        if (QApplication::activePopupWidget() != nullptr) {
            return QByteArrayLiteral("menu");
        }
        if (QApplication::activeModalWidget() != nullptr ||
            qobject_cast<QDialog *>(QApplication::activeWindow()) != nullptr) {
            return QByteArrayLiteral("dialog");
        }
        QWidget *focused = QApplication::focusWidget();
        return (focused == this || (focused != nullptr && isAncestorOf(focused)))
                   ? QByteArrayLiteral("viewing-surface")
                   : QByteArrayLiteral("other");
    }

    QByteArray settingsState() const
    {
        return settingsStateBytes(currentSettings());
    }

    QByteArray storedSettingsState() const
    {
        return settingsStateBytes(readStoredSettings());
    }

    QByteArray settingsDialogStructure() const
    {
        if (settingsDialog_ == nullptr) {
            return QByteArrayLiteral("closed");
        }
        const QList<QGroupBox *> groups = settingsDialog_->findChildren<QGroupBox *>();
        QStringList descriptions;
        for (const QGroupBox *group : groups) {
            QStringList controls;
            for (const QWidget *child : group->findChildren<QWidget *>(QString{},
                                                                       Qt::FindDirectChildrenOnly)) {
                if (!child->accessibleName().isEmpty()) {
                    controls.append(child->accessibleName());
                }
            }
            descriptions.append(group->title() + QLatin1Char('[') +
                                controls.join(QLatin1Char('|')) + QLatin1Char(']'));
        }
        const QStringList buttonLabels{
            settingsButtons_->button(QDialogButtonBox::Reset)->text(),
            settingsButtons_->button(QDialogButtonBox::Cancel)->text(),
            settingsButtons_->button(QDialogButtonBox::Apply)->text()};
        return (descriptions + buttonLabels).join(QLatin1Char('|')).toUtf8();
    }

    QByteArray settingsDialogGeometry() const
    {
        return settingsDialog_ == nullptr
                   ? QByteArrayLiteral("0x0")
                   : QByteArray::number(settingsDialog_->width()) + 'x' +
                         QByteArray::number(settingsDialog_->height());
    }

    QByteArray settingsDialogFocusOrder() const
    {
        if (settingsDialog_ == nullptr || settingsWheel_ == nullptr) {
            return {};
        }
        QStringList names;
        const QWidget *widget = settingsWheel_;
        do {
            if (widget->focusPolicy() != Qt::NoFocus) {
                QString name = widget->accessibleName();
                if (name.isEmpty()) {
                    if (const auto *button = qobject_cast<const QAbstractButton *>(widget)) {
                        name = button->text();
                    }
                }
                if (!name.isEmpty()) {
                    names.append(name);
                }
            }
            widget = widget->nextInFocusChain();
        } while (widget != settingsWheel_ && widget != nullptr);
        return names.join(QLatin1Char('|')).toUtf8();
    }

    QByteArray accessibilityState() const
    {
        const QAccessibleInterface *interface =
            QAccessible::queryAccessibleInterface(imageLabel_);
        const QString role =
            interface && interface->role() == QAccessible::Graphic ? QStringLiteral("Graphic")
                                                                   : QStringLiteral("Unknown");
        QStringList descriptions{imageLabel_->accessibleName() +
                                 QStringLiteral("|AccessibleRole=") + role + QLatin1Char('|') +
                                 imageLabel_->accessibleDescription()};
        for (const QAction *action : viewport_->actions()) {
            if (!action->objectName().startsWith(QStringLiteral("viewer")) &&
                action->objectName() != QStringLiteral("settingsAction")) {
                continue;
            }
            descriptions.append(action->text() + QLatin1Char('|') +
                                action->shortcut().toString(QKeySequence::NativeText));
        }
        return descriptions.join(QLatin1Char('\n')).toUtf8();
    }

    QByteArray windowGeometryState() const
    {
        return QByteArray::number(width()) + 'x' + QByteArray::number(height());
    }

    QByteArray presentationState() const
    {
        if (dropOverlay_->isVisible()) {
            return QByteArrayLiteral("drop|") + dropLabel_->text().toUtf8();
        }
        switch (presentationState_) {
        case PresentationState::Empty:
            return QByteArrayLiteral(
                "empty|Open an image|Choose file|or drop it here|← → Browse · Wheel Navigate · "
                "Right-click Commands");
        case PresentationState::Loading:
            return QByteArrayLiteral("loading|") + loadingFilename_->text().toUtf8() + '|' +
                   (loadingIndicator_->isVisible() ? QByteArrayLiteral("indicator-visible")
                                                   : QByteArrayLiteral("indicator-hidden"));
        case PresentationState::Displayed:
            return QByteArrayLiteral("displayed");
        case PresentationState::Error:
            return QByteArrayLiteral("error|") + errorExplanation_->text().toUtf8() + '|' +
                   errorRetryButton_->text().toUtf8() + '|' +
                   errorDetailsButton_->text().toUtf8() + '|' +
                   errorNavigationHint_->text().toUtf8();
        case PresentationState::LargeImageConfirmation:
            return QByteArrayLiteral("large-image|") + largeImageExplanation_->text().toUtf8() +
                   QByteArrayLiteral("|Open anyway|Skip");
        }
        return {};
    }

    void applyTestSettings(const QStringList &values)
    {
        if (const auto settings = parseTestSettings(values)) {
            applySettings(settings->wheelAction, settings->background, settings->statusVisible,
                          settings->cacheBudgetBytes, settings->restoreWindowGeometry);
        }
    }

    void previewTestSettings(const QStringList &values)
    {
        const auto settings = parseTestSettings(values);
        if (settingsDialog_ == nullptr || !settings) {
            return;
        }
        setSettingsControls(*settings);
    }

    void resetTestSettings()
    {
        if (settingsButtons_ != nullptr) {
            settingsButtons_->button(QDialogButtonBox::Reset)->click();
        }
    }

    void finishTestSettings(const bool apply)
    {
        if (settingsButtons_ == nullptr) {
            return;
        }
        settingsButtons_->button(apply ? QDialogButtonBox::Apply : QDialogButtonBox::Cancel)
            ->click();
    }

    void failExternalActionsForTest()
    {
        failExternalActionsForTest_ = true;
    }

#endif

protected:
    void closeEvent(QCloseEvent *event) override
    {
        persistWindowGeometry();
        QWidget::closeEvent(event);
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        if (dropOverlay_) {
            dropOverlay_->setGeometry(surface_->rect());
        }
        positionStatusDisplay();
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == viewport_->viewport() && event->type() == QEvent::Resize) {
            positionStatusDisplay();
        }
        if ((watched == viewport_->viewport() || watched == imageLabel_) &&
            event->type() == QEvent::MouseMove) {
            showStatus(true);
        }
        if (event->type() == QEvent::ContextMenu) {
            markBrowsingTeachingComplete();
            const auto *contextEvent = static_cast<QContextMenuEvent *>(event);
            contextMenu_->popup(contextEvent->globalPos());
            return true;
        }
        if ((watched == viewport_->viewport() || watched == imageLabel_) &&
            event->type() == QEvent::MouseButtonDblClick) {
            toggleFullscreen();
            return true;
        }
        if (watched == viewport_->viewport() && event->type() == QEvent::Wheel) {
            const auto *wheel = static_cast<QWheelEvent *>(event);
            const int wheelDelta = wheel->angleDelta().y() != 0
                                       ? wheel->angleDelta().y()
                                       : wheel->pixelDelta().y() * 8;
            const bool alternate = wheel->modifiers().testFlag(Qt::ControlModifier);
            const bool zoom = (wheelAction_ == WheelAction::Zoom) != alternate;
            if (zoom) {
                zoomAt(wheel->position(), wheelDelta);
            } else if (wheelDelta != 0) {
                navigate(wheelDelta > 0 ? -1 : 1);
            }
            return true;
        }
        if (watched == viewport_->viewport() && event->type() == QEvent::MouseButtonPress) {
            const auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton) {
                dragging_ = true;
                lastDragPosition_ = mouse->position();
                viewport_->viewport()->grabMouse();
                return true;
            }
        }
        if (watched == viewport_->viewport() && event->type() == QEvent::MouseMove && dragging_) {
            const auto *mouse = static_cast<QMouseEvent *>(event);
            if (!mouse->buttons().testFlag(Qt::LeftButton)) {
                dragging_ = false;
                viewport_->viewport()->releaseMouse();
                return true;
            }
            const QPointF movement = mouse->position() - lastDragPosition_;
            panBy(-qRound(movement.x()), -qRound(movement.y()));
            lastDragPosition_ = mouse->position();
            return true;
        }
        if (watched == viewport_->viewport() && event->type() == QEvent::MouseButtonRelease) {
            const auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton && dragging_) {
                dragging_ = false;
                viewport_->viewport()->releaseMouse();
                return true;
            }
        }
        return QWidget::eventFilter(watched, event);
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Escape && informationDialog_ != nullptr) {
            informationDialog_->close();
            return;
        }
        if (event->key() == Qt::Key_F11) {
            toggleFullscreen();
            return;
        }
        if (event->key() == Qt::Key_F5) {
            retryCurrentImage();
            return;
        }
        if (event->key() == Qt::Key_Escape &&
            presentationState_ == PresentationState::LargeImageConfirmation) {
            rejectLargeImage();
            return;
        }
        if (event->key() == Qt::Key_Escape && isFullScreen()) {
            leaveFullscreen();
            return;
        }
        if (event->matches(QKeySequence::Open)) {
            openFromFilePicker();
            return;
        }
        if (event->matches(QKeySequence::ZoomIn)) {
            setZoomCentered(zoom_ * 1.25);
            return;
        }
        if (event->matches(QKeySequence::ZoomOut)) {
            setZoomCentered(zoom_ / 1.25);
            return;
        }
        if (event->key() == Qt::Key_1) {
            setZoomCentered(1.0);
            return;
        }
        if (event->key() == Qt::Key_F) {
            fitToViewport();
            return;
        }
        if (event->key() == Qt::Key_L) {
            rotateView(-1);
            return;
        }
        if (event->key() == Qt::Key_R) {
            rotateView(1);
            return;
        }
        if (event->modifiers().testFlag(Qt::ShiftModifier)) {
            if (event->key() == Qt::Key_Left) {
                panBy(-KeyboardPanStep, 0);
                return;
            }
            if (event->key() == Qt::Key_Right) {
                panBy(KeyboardPanStep, 0);
                return;
            }
            if (event->key() == Qt::Key_Up) {
                panBy(0, -KeyboardPanStep);
                return;
            }
            if (event->key() == Qt::Key_Down) {
                panBy(0, KeyboardPanStep);
                return;
            }
        }
        if (event->key() == Qt::Key_Left) {
            navigate(-1);
            return;
        }
        if (event->key() == Qt::Key_Right) {
            navigate(1);
            return;
        }
        if (event->key() == Qt::Key_Space) {
            toggleAnimation();
            return;
        }
        QWidget::keyPressEvent(event);
    }

    void dragEnterEvent(QDragEnterEvent *event) override
    {
        int supportedCount = 0;
        for (const QUrl &url : event->mimeData()->urls()) {
            if (url.isLocalFile() && BrowsingSequence::supports(url.toLocalFile())) {
                ++supportedCount;
            }
        }
        if (supportedCount == 0) {
            if (event->mimeData()->hasUrls()) {
                event->acceptProposedAction();
            }
            return;
        }
        dropLabel_->setText(supportedCount == 1
                                ? tr("Drop to open")
                                : tr("Drop to browse %1 images").arg(supportedCount));
        dropOverlay_->setGeometry(surface_->rect());
        dropOverlay_->show();
        dropOverlay_->raise();
        event->acceptProposedAction();
    }

    void dragLeaveEvent(QDragLeaveEvent *event) override
    {
        hideDropOverlay();
        event->accept();
    }

    void dropEvent(QDropEvent *event) override
    {
        QStringList paths;
        for (const QUrl &url : event->mimeData()->urls()) {
            if (url.isLocalFile()) {
                paths.append(url.toLocalFile());
            }
        }
        hideDropOverlay();
        openDroppedPaths(paths);
        event->acceptProposedAction();
    }

private:
    struct SettingsValues
    {
        WheelAction wheelAction = WheelAction::Navigate;
        QColor background{QStringLiteral("#181A1B")};
        bool statusVisible = true;
        qsizetype cacheBudgetBytes = DefaultCacheBudgetBytes;
        bool restoreWindowGeometry = false;
    };

    template <typename MenuContainer>
    static QString menuStructure(const MenuContainer *container)
    {
        if (container == nullptr) {
            return {};
        }
        QStringList entries;
        for (const QAction *action : container->actions()) {
            if (action->isSeparator()) {
                entries.append(QStringLiteral("---"));
            } else if (action->menu() != nullptr) {
                entries.append(action->text() + QLatin1Char('[') +
                               menuStructure(action->menu()) + QLatin1Char(']'));
            } else {
                entries.append(action->text());
            }
        }
        return entries.join(QLatin1Char('|'));
    }

    QAction *commandAction(const char *objectName) const
    {
        QAction *action = findChild<QAction *>(QString::fromLatin1(objectName));
        Q_ASSERT(action != nullptr);
        return action;
    }

    void restoreViewingFocus()
    {
        activateWindow();
        setFocus(Qt::OtherFocusReason);
    }

    void addCommandSurfaces(QAction *settingsAction, QVBoxLayout *windowLayout)
    {
        const QList<QAction *> viewCommands{
            commandAction("viewerFitAction"), commandAction("viewerActualSizeAction"),
            commandAction("viewerZoomInAction"), commandAction("viewerZoomOutAction"),
            commandAction("viewerFullscreenAction")};
        const QList<QAction *> imageCommands{
            commandAction("viewerRotateLeftAction"), commandAction("viewerRotateRightAction"),
            commandAction("viewerAnimationAction"), commandAction("imageInformationAction")};
        const QList<QAction *> copyAndRevealCommands{
            commandAction("imageCopyAction"), commandAction("imageCopyPathAction"),
            commandAction("imageRevealAction")};
        contextMenu_ = new QMenu(this);
        contextMenu_->addAction(commandAction("viewerOpenAction"));
        contextMenu_->addSeparator();
        for (QAction *action : viewCommands) {
            contextMenu_->addAction(action);
        }
        contextMenu_->addSeparator();
        for (QAction *action : imageCommands) {
            contextMenu_->addAction(action);
        }
        contextMenu_->addSeparator();
        for (QAction *action : copyAndRevealCommands) {
            contextMenu_->addAction(action);
        }
        contextMenu_->addSeparator();
        contextMenu_->addAction(settingsAction);
        QObject::connect(contextMenu_, &QMenu::aboutToHide, this,
                         [this] {
                             QTimer::singleShot(0, this, [this] { restoreViewingFocus(); });
                         });

        applicationMenuBar_ = new QMenuBar;
#if defined(Q_OS_MACOS)
        applicationMenuBar_->setNativeMenuBar(true);
#else
        applicationMenuBar_->setNativeMenuBar(false);
#endif
        windowLayout->setMenuBar(applicationMenuBar_);
        QMenu *fileMenu = applicationMenuBar_->addMenu(tr("File"));
        fileMenu->addAction(commandAction("viewerOpenAction"));
        fileMenu->addAction(settingsAction);
#if defined(Q_OS_MACOS)
        settingsAction->setMenuRole(QAction::PreferencesRole);
#endif
        QMenu *viewMenu = applicationMenuBar_->addMenu(tr("View"));
        for (QAction *action : viewCommands) {
            viewMenu->addAction(action);
        }
        QMenu *imageMenu = applicationMenuBar_->addMenu(tr("Image"));
        for (QAction *action : imageCommands) {
            imageMenu->addAction(action);
        }
        QMenu *helpMenu = applicationMenuBar_->addMenu(tr("Help"));
        auto *aboutAction = helpMenu->addAction(tr("About Flick"));
        aboutAction->setMenuRole(QAction::AboutRole);
        QObject::connect(aboutAction, &QAction::triggered, this, [this] {
            QMessageBox::about(this, tr("About Flick"),
                               tr("Flick %1\nA color-managed image viewer.")
                                   .arg(QCoreApplication::applicationVersion()));
            setFocus();
        });
#if defined(Q_OS_MACOS)
        auto *quitAction = fileMenu->addAction(tr("Quit Flick"));
        quitAction->setMenuRole(QAction::QuitRole);
        quitAction->setShortcut(QKeySequence::Quit);
        QObject::connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);
#endif
    }

    void loadSettings()
    {
        QSettings settings;
        SettingsValues values = readStoredSettings();
        browsingTeachingComplete_ =
            settings.value(QStringLiteral("teaching/browsingComplete"), false).toBool();
        fullscreenTeachingComplete_ =
            settings.value(QStringLiteral("teaching/fullscreenComplete"), false).toBool();
#ifdef FLICK_ENABLE_TEST_HARNESS
        const qint64 testBudget = qEnvironmentVariableIntValue("FLICK_TEST_CACHE_BUDGET_BYTES");
        if (testBudget > 0) {
            values.cacheBudgetBytes = testBudget;
        }
#endif
        if (values.cacheBudgetBytes <= 0) {
            values.cacheBudgetBytes = DefaultCacheBudgetBytes;
        }
        wheelAction_ = values.wheelAction;
        viewportBackground_ = values.background;
        statusVisible_ = values.statusVisible;
        imageLoader_.setCacheBudget(values.cacheBudgetBytes);
        restoreWindowGeometry_ = values.restoreWindowGeometry;
        findChild<QAction *>(QStringLiteral("wheelNavigateAction"))
            ->setChecked(wheelAction_ == WheelAction::Navigate);
        findChild<QAction *>(QStringLiteral("wheelZoomAction"))
            ->setChecked(wheelAction_ == WheelAction::Zoom);
        applyViewportBackground();
        if (restoreWindowGeometry_) {
            restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
        }
    }

    void applyViewportBackground()
    {
        QPalette palette = viewport_->viewport()->palette();
        palette.setColor(QPalette::Window, viewportBackground_);
        palette.setColor(QPalette::Base, viewportBackground_);
        palette.setColor(QPalette::Dark, viewportBackground_);
        viewport_->viewport()->setAutoFillBackground(true);
        viewport_->viewport()->setPalette(palette);
    }

    void applySettings(const WheelAction wheelAction, const QColor &background,
                       const bool statusVisible, const qsizetype cacheBudgetBytes,
                       const bool restoreWindowGeometry)
    {
        previewSettings({wheelAction, background, statusVisible, cacheBudgetBytes,
                         restoreWindowGeometry});
        persistSettings(currentSettings());
    }

    SettingsValues currentSettings() const
    {
        return {wheelAction_, viewportBackground_, statusVisible_, imageLoader_.cacheBudget(),
                restoreWindowGeometry_};
    }

    static SettingsValues defaultSettings()
    {
        return {};
    }

    static SettingsValues readStoredSettings()
    {
        QSettings settings;
        SettingsValues values;
        values.wheelAction =
            settings.value(QStringLiteral("view/wheelAction"), QStringLiteral("navigate"))
                        .toString() == QStringLiteral("zoom")
                ? WheelAction::Zoom
                : WheelAction::Navigate;
        values.background =
            QColor(settings.value(QStringLiteral("view/background"), values.background.name())
                       .toString());
        if (!values.background.isValid()) {
            values.background = defaultSettings().background;
        }
        values.statusVisible =
            settings.value(QStringLiteral("view/statusVisible"), values.statusVisible).toBool();
        values.cacheBudgetBytes =
            settings.value(QStringLiteral("cache/budgetBytes"), values.cacheBudgetBytes)
                .toLongLong();
        values.restoreWindowGeometry =
            settings.value(QStringLiteral("window/restoreGeometry"),
                           values.restoreWindowGeometry)
                .toBool();
        return values;
    }

    static QByteArray settingsStateBytes(const SettingsValues &values)
    {
        return QByteArray(values.wheelAction == WheelAction::Zoom ? "zoom" : "navigate") + '|' +
               values.background.name().toUtf8() + '|' +
               (values.statusVisible ? "visible" : "hidden") + '|' +
               QByteArray::number(values.cacheBudgetBytes) + '|' +
               (values.restoreWindowGeometry ? "restore" : "forget");
    }

#ifdef FLICK_ENABLE_TEST_HARNESS
    static std::optional<SettingsValues> parseTestSettings(const QStringList &fields)
    {
        if (fields.size() != 5) {
            return std::nullopt;
        }
        return SettingsValues{
            fields.at(0) == QStringLiteral("zoom") ? WheelAction::Zoom : WheelAction::Navigate,
            QColor(fields.at(1)), fields.at(2).toInt() != 0,
            fields.at(3).toLongLong() * 1024 * 1024, fields.at(4).toInt() != 0};
    }
#endif

    void previewSettings(const SettingsValues &values)
    {
        setWheelAction(values.wheelAction, false);
        viewportBackground_ = values.background.isValid()
                                  ? values.background
                                  : defaultSettings().background;
        statusVisible_ = values.statusVisible;
        imageLoader_.setCacheBudget(
            std::max<qsizetype>(1024 * 1024, values.cacheBudgetBytes));
        restoreWindowGeometry_ = values.restoreWindowGeometry;
        if (!statusVisible_) {
            statusTimer_->stop();
            statusDisplay_->hide();
        } else {
            showStatus(false);
        }
        applyViewportBackground();
        if (auto *navigate = findChild<QAction *>(QStringLiteral("wheelNavigateAction"))) {
            navigate->setChecked(wheelAction_ == WheelAction::Navigate);
        }
        if (auto *zoom = findChild<QAction *>(QStringLiteral("wheelZoomAction"))) {
            zoom->setChecked(wheelAction_ == WheelAction::Zoom);
        }
    }

    void persistSettings(const SettingsValues &values)
    {
        QSettings settings;
        settings.setValue(QStringLiteral("view/wheelAction"),
                          values.wheelAction == WheelAction::Zoom ? QStringLiteral("zoom")
                                                                   : QStringLiteral("navigate"));
        settings.setValue(QStringLiteral("view/background"), values.background.name());
        settings.setValue(QStringLiteral("view/statusVisible"), values.statusVisible);
        settings.setValue(QStringLiteral("cache/budgetBytes"), values.cacheBudgetBytes);
        settings.setValue(QStringLiteral("window/restoreGeometry"),
                          values.restoreWindowGeometry);
        if (!values.restoreWindowGeometry) {
            settings.remove(QStringLiteral("window/geometry"));
        }
        settings.sync();
    }

    void showSettings()
    {
        if (settingsDialog_ != nullptr) {
            settingsDialog_->raise();
            settingsDialog_->activateWindow();
            return;
        }
        settingsOpeningValues_ = currentSettings();
        settingsDialog_ = new QDialog(this);
        settingsDialog_->setAttribute(Qt::WA_DeleteOnClose);
        settingsDialog_->setWindowTitle(tr("Settings"));
        settingsDialog_->setWindowModality(Qt::WindowModal);
        settingsDialog_->setMinimumWidth(380);
        auto *layout = new QVBoxLayout(settingsDialog_);
        layout->setContentsMargins(12, 8, 12, 8);
        layout->setSpacing(4);

        auto *navigation = new QGroupBox(tr("Navigation"), settingsDialog_);
        auto *navigationLayout = new QFormLayout(navigation);
        navigationLayout->setContentsMargins(8, 12, 8, 6);
        navigationLayout->setVerticalSpacing(4);
        settingsWheel_ = new QComboBox(navigation);
        settingsWheel_->setAccessibleName(tr("Mouse wheel action"));
        settingsWheel_->addItem(tr("Navigate images"), QStringLiteral("navigate"));
        settingsWheel_->addItem(tr("Zoom image"), QStringLiteral("zoom"));
        navigationLayout->addRow(tr("Mouse wheel:"), settingsWheel_);
        layout->addWidget(navigation);

        auto *appearance = new QGroupBox(tr("Appearance"), settingsDialog_);
        auto *appearanceLayout = new QFormLayout(appearance);
        appearanceLayout->setContentsMargins(8, 12, 8, 6);
        appearanceLayout->setVerticalSpacing(4);
        settingsBackground_ = new QPushButton(appearance);
        settingsBackground_->setAccessibleName(tr("Viewport background"));
        settingsStatus_ = new QCheckBox(tr("Show status overlay"), appearance);
        settingsStatus_->setAccessibleName(tr("Show status overlay"));
        appearanceLayout->addRow(tr("Viewport background:"), settingsBackground_);
        appearanceLayout->addRow(QString{}, settingsStatus_);
        layout->addWidget(appearance);

        auto *performance = new QGroupBox(tr("Performance & Window"), settingsDialog_);
        auto *performanceLayout = new QFormLayout(performance);
        performanceLayout->setContentsMargins(8, 12, 8, 6);
        performanceLayout->setVerticalSpacing(4);
        settingsCache_ = new QSpinBox(performance);
        settingsCache_->setAccessibleName(tr("Decoded cache budget"));
        settingsCache_->setRange(1, 16384);
        settingsCache_->setSuffix(tr(" MB"));
        settingsGeometry_ =
            new QCheckBox(tr("Restore window size and position"), performance);
        settingsGeometry_->setAccessibleName(tr("Restore window size and position"));
        performanceLayout->addRow(tr("Decoded cache budget:"), settingsCache_);
        performanceLayout->addRow(QString{}, settingsGeometry_);
        layout->addWidget(performance);

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Reset | QDialogButtonBox::Cancel |
                                                 QDialogButtonBox::Apply,
                                             settingsDialog_);
        settingsButtons_ = buttons;
        buttons->button(QDialogButtonBox::Reset)->setText(tr("Reset Defaults"));
        layout->addWidget(buttons);

        const auto previewControls = [this] { previewSettings(settingsControlsValues()); };
        QObject::connect(settingsWheel_, &QComboBox::currentIndexChanged, settingsDialog_,
                         previewControls);
        QObject::connect(settingsStatus_, &QCheckBox::toggled, settingsDialog_, previewControls);
        QObject::connect(settingsCache_, &QSpinBox::valueChanged, settingsDialog_, previewControls);
        QObject::connect(settingsGeometry_, &QCheckBox::toggled, settingsDialog_, previewControls);
        QObject::connect(settingsBackground_, &QPushButton::clicked, settingsDialog_, [this] {
            const QColor selected = QColorDialog::getColor(settingsDialogBackground_, settingsDialog_,
                                                            tr("Viewport Background"));
            if (selected.isValid()) {
                settingsDialogBackground_ = selected;
                settingsBackground_->setText(selected.name());
                previewSettings(settingsControlsValues());
            }
        });
        QObject::connect(buttons->button(QDialogButtonBox::Reset), &QPushButton::clicked,
                         settingsDialog_, [this] { setSettingsControls(defaultSettings()); });
        QObject::connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked,
                         settingsDialog_, [this] {
                             persistSettings(currentSettings());
                             settingsDialog_->accept();
                         });
        QObject::connect(buttons, &QDialogButtonBox::rejected, settingsDialog_,
                         &QDialog::reject);
        QObject::connect(settingsDialog_, &QDialog::rejected, this,
                         [this] { previewSettings(settingsOpeningValues_); });
        QObject::connect(settingsDialog_, &QDialog::finished, this, [this] {
            settingsDialog_ = nullptr;
            settingsWheel_ = nullptr;
            settingsBackground_ = nullptr;
            settingsStatus_ = nullptr;
            settingsCache_ = nullptr;
            settingsGeometry_ = nullptr;
            settingsButtons_ = nullptr;
            restoreViewingFocus();
        });
        setSettingsControls(settingsOpeningValues_);
        settingsDialog_->open();
    }

    SettingsValues settingsControlsValues() const
    {
        return {settingsWheel_->currentData() == QStringLiteral("zoom") ? WheelAction::Zoom
                                                                         : WheelAction::Navigate,
                settingsDialogBackground_, settingsStatus_->isChecked(),
                static_cast<qsizetype>(settingsCache_->value()) * 1024 * 1024,
                settingsGeometry_->isChecked()};
    }

    void setSettingsControls(const SettingsValues &values)
    {
        settingsDialogBackground_ = values.background;
        settingsWheel_->setCurrentIndex(values.wheelAction == WheelAction::Zoom ? 1 : 0);
        settingsBackground_->setText(values.background.name());
        settingsStatus_->setChecked(values.statusVisible);
        settingsCache_->setValue(static_cast<int>(values.cacheBudgetBytes / (1024 * 1024)));
        settingsGeometry_->setChecked(values.restoreWindowGeometry);
        previewSettings(values);
    }

    void addImageActions()
    {
        const auto addImageAction = [this](const QString &text, const QString &objectName,
                                           const QKeySequence &shortcut, auto operation) {
            QAction *action =
                addViewportAction(text, objectName, shortcut, std::move(operation));
            action->setEnabled(false);
            imageActions_.append(action);
        };
        addImageAction(tr("Information"), QStringLiteral("imageInformationAction"),
                       QKeySequence(Qt::Key_I), [this] { showInformation(); });
        addImageAction(tr("Copy Image"), QStringLiteral("imageCopyAction"), QKeySequence::Copy,
                       [this] { copyRenderedImage(); });
        addImageAction(tr("Copy Path"), QStringLiteral("imageCopyPathAction"),
                       QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C),
                       [this] { copyCurrentPath(); });
        addImageAction(tr("Show in File Manager"), QStringLiteral("imageRevealAction"),
                       QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R),
                       [this] { revealCurrentFile(); });
    }

    void addViewerActions()
    {
        addViewportAction(tr("Open Image"), QStringLiteral("viewerOpenAction"),
                          QKeySequence::Open, [this] { openFromFilePicker(); });
        addViewportAction(tr("Previous Image"), QStringLiteral("viewerPreviousAction"),
                          QKeySequence(Qt::Key_Left), [this] { navigate(-1); });
        addViewportAction(tr("Next Image"), QStringLiteral("viewerNextAction"),
                          QKeySequence(Qt::Key_Right), [this] { navigate(1); });
        addViewportAction(tr("Pan Left"), QStringLiteral("viewerPanLeftAction"),
                          QKeySequence(Qt::SHIFT | Qt::Key_Left),
                          [this] { panBy(-KeyboardPanStep, 0); });
        addViewportAction(tr("Pan Right"), QStringLiteral("viewerPanRightAction"),
                          QKeySequence(Qt::SHIFT | Qt::Key_Right),
                          [this] { panBy(KeyboardPanStep, 0); });
        addViewportAction(tr("Pan Up"), QStringLiteral("viewerPanUpAction"),
                          QKeySequence(Qt::SHIFT | Qt::Key_Up),
                          [this] { panBy(0, -KeyboardPanStep); });
        addViewportAction(tr("Pan Down"), QStringLiteral("viewerPanDownAction"),
                          QKeySequence(Qt::SHIFT | Qt::Key_Down),
                          [this] { panBy(0, KeyboardPanStep); });
        const auto addImageViewerAction = [this](const QString &text, const QString &objectName,
                                                 const QKeySequence &shortcut, auto operation) {
            QAction *action = addViewportAction(text, objectName, shortcut, std::move(operation));
            action->setEnabled(false);
            imageActions_.append(action);
        };
        addImageViewerAction(tr("Zoom In"), QStringLiteral("viewerZoomInAction"),
                             QKeySequence::ZoomIn, [this] { setZoomCentered(zoom_ * 1.25); });
        addImageViewerAction(tr("Zoom Out"), QStringLiteral("viewerZoomOutAction"),
                             QKeySequence::ZoomOut, [this] { setZoomCentered(zoom_ / 1.25); });
        addImageViewerAction(tr("Actual Size"), QStringLiteral("viewerActualSizeAction"),
                             QKeySequence(Qt::Key_1), [this] { setZoomCentered(1.0); });
        addImageViewerAction(tr("Fit to Window"), QStringLiteral("viewerFitAction"),
                             QKeySequence(Qt::Key_F), [this] { fitToViewport(); });
        addImageViewerAction(tr("Rotate Left"), QStringLiteral("viewerRotateLeftAction"),
                             QKeySequence(Qt::Key_L), [this] { rotateView(-1); });
        addImageViewerAction(tr("Rotate Right"), QStringLiteral("viewerRotateRightAction"),
                             QKeySequence(Qt::Key_R), [this] { rotateView(1); });
        addImageViewerAction(tr("Pause or Resume Animation"),
                             QStringLiteral("viewerAnimationAction"), QKeySequence(Qt::Key_Space),
                             [this] { toggleAnimation(); });
        addViewportAction(tr("Toggle Fullscreen"), QStringLiteral("viewerFullscreenAction"),
                          QKeySequence(Qt::Key_F11), [this] { toggleFullscreen(); });
        addViewportAction(tr("Retry"), QStringLiteral("viewerRetryAction"),
                          QKeySequence(Qt::Key_F5), [this] { retryCurrentImage(); });
    }

    QAction *addViewportAction(const QString &text, const QString &objectName,
                               const QKeySequence &shortcut,
                               std::function<void()> operation)
    {
        auto *action = new QAction(text, this);
        action->setObjectName(objectName);
        action->setShortcut(shortcut);
        action->setShortcutContext(Qt::WindowShortcut);
        QObject::connect(action, &QAction::triggered, this, std::move(operation));
        viewport_->addAction(action);
        return action;
    }

    QString imageInformation() const
    {
        const QString selectedPath = browsingSequence_.selectedPath();
        if (selectedPath.isEmpty()) {
            return tr("No current image");
        }
        const QFileInfo file(selectedPath);
        QString format = file.suffix().toUpper();
        if (format == QStringLiteral("JPG")) {
            format = QStringLiteral("JPEG");
        }
        const bool displayed = !image_.isNull() && currentImage_.path == selectedPath;
        const QString dimensions =
            displayed ? tr("%1 × %2").arg(image_.width()).arg(image_.height())
                      : presentationState_ == PresentationState::Error ? tr("Unavailable")
                                                                       : tr("Loading…");
        const QString animation =
            !displayed ? tr("Unavailable")
            : currentImage_.frames.size() < 2 ? tr("Static image")
            : animationPaused_ ? tr("Paused")
                               : tr("Playing");
        return tr("Path: %1\nFormat: %2\nDimensions: %3\nSize: %4 bytes\n"
                  "Modified: %5\nZoom: %6%\nRotation: %7°\nAnimation: %8\nPosition: %9 / %10")
            .arg(file.absoluteFilePath(), format)
            .arg(dimensions)
            .arg(file.size())
            .arg(QLocale().toString(file.lastModified(), QLocale::ShortFormat))
            .arg(qRound(zoom_ * 100))
            .arg(rotationQuarterTurns_ * 90)
            .arg(animation)
            .arg(browsingSequence_.selectedIndex() + 1)
            .arg(browsingSequence_.paths().size());
    }

    void updateInformation()
    {
        if (informationDialog_ == nullptr) {
            return;
        }
        informationText_ = imageInformation();
        informationFacts_->setText(informationText_);
        informationDialog_->adjustSize();
    }

    void showInformation()
    {
        if (browsingSequence_.selectedIndex() < 0 || image_.isNull()) {
            return;
        }
        if (informationDialog_ != nullptr) {
            informationDialog_->raise();
            informationDialog_->activateWindow();
            return;
        }
        informationText_ = imageInformation();
        informationDialog_ = new QDialog(this, Qt::Tool);
        informationDialog_->setAttribute(Qt::WA_DeleteOnClose);
        informationDialog_->setModal(false);
        informationDialog_->setWindowTitle(tr("Image Information"));
        informationDialog_->setMaximumSize(480, 320);
        auto *layout = new QVBoxLayout(informationDialog_);
        informationFacts_ = new QLabel(informationText_, informationDialog_);
        informationFacts_->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                                    Qt::TextSelectableByKeyboard);
        informationFacts_->setWordWrap(true);
        informationFacts_->setAccessibleName(tr("Current image information"));
        auto *factsViewport = new QScrollArea(informationDialog_);
        factsViewport->setWidgetResizable(true);
        factsViewport->setFrameShape(QFrame::NoFrame);
        factsViewport->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        factsViewport->setWidget(informationFacts_);
        factsViewport->setMinimumSize(360, 180);
        layout->addWidget(factsViewport);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, informationDialog_);
        QObject::connect(buttons, &QDialogButtonBox::rejected, informationDialog_, &QDialog::close);
        layout->addWidget(buttons);
        informationDialog_->show();
        QObject::connect(informationDialog_, &QDialog::finished, this,
                         [this] {
                             informationDialog_ = nullptr;
                             informationFacts_ = nullptr;
                             QTimer::singleShot(0, this, [this] { restoreViewingFocus(); });
                         });
    }

    bool externalActionCanRun(const QString &failureMessage)
    {
#ifdef FLICK_ENABLE_TEST_HARNESS
        if (failExternalActionsForTest_) {
            showFeedback(failureMessage);
            return false;
        }
#else
        Q_UNUSED(failureMessage);
#endif
        return true;
    }

    void copyCurrentPath()
    {
        if (browsingSequence_.selectedIndex() < 0 ||
            !externalActionCanRun(tr("Could not copy the current file path"))) {
            return;
        }
        QClipboard *clipboard = QApplication::clipboard();
        if (clipboard == nullptr) {
            showFeedback(tr("Could not copy the current file path"));
            return;
        }
        const QString path = QFileInfo(currentImage_.path).absoluteFilePath();
        clipboard->setText(path);
        if (clipboard->text() != path) {
            showFeedback(tr("Could not copy the current file path"));
        } else {
            showFeedback(tr("File path copied"));
        }
    }

    void copyRenderedImage()
    {
        if (image_.isNull() || !externalActionCanRun(tr("Could not copy the current image"))) {
            return;
        }
        QClipboard *clipboard = QApplication::clipboard();
        if (clipboard == nullptr) {
            showFeedback(tr("Could not copy the current image"));
            return;
        }
        const QImage content = rotatedImage();
        clipboard->setImage(content);
        if (clipboard->image() != content) {
            showFeedback(tr("Could not copy the current image"));
        } else {
            showFeedback(tr("Image copied"));
        }
    }

    void revealCurrentFile()
    {
        if (browsingSequence_.selectedIndex() < 0 ||
            !externalActionCanRun(tr("Could not show the current file in the file manager"))) {
            return;
        }
        if (!platformServices_->revealFile(
                QFileInfo(currentImage_.path).absoluteFilePath())) {
            showFeedback(tr("Could not show the current file in the file manager"));
        }
    }

    void setWheelAction(const WheelAction action, const bool persist = true)
    {
        wheelAction_ = action;
        if (persist) {
            QSettings().setValue(QStringLiteral("view/wheelAction"),
                                 action == WheelAction::Zoom ? QStringLiteral("zoom")
                                                             : QStringLiteral("navigate"));
        }
    }

    void toggleFullscreen()
    {
        if (isFullScreen()) {
            leaveFullscreen();
        } else {
            showFullScreen();
            if (!fullscreenTeachingComplete_) {
                fullscreenTeachingComplete_ = true;
                QSettings settings;
                settings.setValue(QStringLiteral("teaching/fullscreenComplete"), true);
                settings.sync();
                showFeedback(tr("F11 or Esc to exit fullscreen"));
            } else {
                showStatus(true);
            }
        }
    }

    void leaveFullscreen()
    {
        showNormal();
        viewport_->viewport()->unsetCursor();
        showStatus(false);
    }

    void updateStatusText()
    {
        const QString currentPath = browsingSequence_.selectedPath();
        if (currentPath.isEmpty()) {
            return;
        }
        const QString context =
            tr("%1 / %2 — %3%")
                .arg(browsingSequence_.selectedIndex() + 1)
                .arg(browsingSequence_.paths().size())
                .arg(qRound(zoom_ * 100));
        constexpr int HorizontalSafeMargin = 40;
        const int maximumWidth = qMax(1, qMin(440, surface_->width() - HorizontalSafeMargin));
        statusDisplay_->setMaximumWidth(maximumWidth);
        const int textWidth = maximumWidth - 20;
        const int filenameWidth =
            qMax(1, textWidth - statusDisplay_->fontMetrics().horizontalAdvance(
                                    QStringLiteral(" — ") + context));
        const QString filename = statusDisplay_->fontMetrics().elidedText(
            QFileInfo(currentPath).fileName(), Qt::ElideMiddle, filenameWidth);
        statusDisplay_->setText(filename + QStringLiteral(" — ") + context);
        statusDisplay_->setToolTip(QFileInfo(currentPath).fileName());
        statusDisplay_->adjustSize();
        positionStatusDisplay();
    }

    void positionStatusDisplay()
    {
        if (statusDisplay_ == nullptr || viewport_ == nullptr) {
            return;
        }
        constexpr int BottomMargin = 12;
        const QSize surfaceSize = surface_->size();
        statusDisplay_->move((surfaceSize.width() - statusDisplay_->width()) / 2,
                             surfaceSize.height() - statusDisplay_->height() - BottomMargin);
        statusDisplay_->raise();
    }

    void showStatus(const bool revealPointer)
    {
        if (browsingSequence_.selectedIndex() < 0 || !statusVisible_) {
            return;
        }
        updateStatusText();
        statusIsFeedback_ = false;
        statusFade_->stop();
        statusOpacity_->setOpacity(1.0);
        statusDisplay_->show();
        statusDisplay_->raise();
        if (revealPointer) {
            viewport_->viewport()->unsetCursor();
        }
        statusTimer_->start(StatusVisibilityMilliseconds);
    }

    void hideStatus()
    {
        const bool reducedMotion = qEnvironmentVariableIsSet("FLICK_TEST_REDUCED_MOTION") ||
            style()->styleHint(QStyle::SH_Widget_Animation_Duration, nullptr, this) <= 0;
        if (reducedMotion || !statusDisplay_->isVisible()) {
            statusDisplay_->hide();
            if (isFullScreen()) {
                viewport_->viewport()->setCursor(Qt::BlankCursor);
            }
            return;
        }
        statusFade_->start();
    }

    void markBrowsingTeachingComplete()
    {
        if (browsingTeachingComplete_) {
            return;
        }
        browsingTeachingComplete_ = true;
        QSettings settings;
        settings.setValue(QStringLiteral("teaching/browsingComplete"), true);
        settings.sync();
    }

    void openDirectoryBacked(const QString &path)
    {
        const BrowsingSequence browsingSequence = BrowsingSequence::directoryBacked(path);
        if (browsingSequence.selectedIndex() < 0) {
            showFeedback(tr("Unsupported dropped content"));
            return;
        }
        const QString canonicalPath = browsingSequence.selectedPath();
        const QString directoryPath = QFileInfo(canonicalPath).absolutePath();
        directoryWatcher_->removePaths(directoryWatcher_->directories());
        directoryWatcher_->addPath(directoryPath);
        browsingSequence_ = browsingSequence;
        displaySelectedImage();
    }

    void openExplicitList(const QStringList &paths)
    {
        directoryWatcher_->removePaths(directoryWatcher_->directories());
        const BrowsingSequence browsingSequence = BrowsingSequence::explicitList(paths);
        browsingSequence_ = browsingSequence;
        if (browsingSequence_.paths().isEmpty()) {
            showFeedback(tr("No supported images in drop"));
            return;
        }
        displaySelectedImage();
    }

    void openDroppedPaths(const QStringList &paths)
    {
        if (paths.size() == 1 && BrowsingSequence::supports(paths.first())) {
            openDirectoryBacked(paths.first());
        } else {
            openExplicitList(paths);
        }
    }

    void openFromFilePicker()
    {
        QSettings settings;
        const QString initialDirectory =
            settings.value(QStringLiteral("filePicker/lastDirectory"), QDir::homePath()).toString();
#ifdef FLICK_ENABLE_TEST_HARNESS
        if (qEnvironmentVariableIsSet("FLICK_TEST_FILE_PICKER_SELECTION")) {
            const QString testSelection = qEnvironmentVariable("FLICK_TEST_FILE_PICKER_SELECTION");
            if (testSelection.isEmpty()) {
                return;
            }
            QTimer::singleShot(0, this, [testSelection] {
                if (auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
                    dialog->selectFile(testSelection);
                    static_cast<QDialog *>(dialog)->accept();
                }
            });
        }
#endif
        const QString selectedPath = QFileDialog::getOpenFileName(
            this, tr("Open Image"), initialDirectory,
            tr("Images (*.jpg *.jpeg *.png *.webp *.gif *.bmp)"));
        restoreViewingFocus();
        if (selectedPath.isEmpty()) {
            return;
        }
        const BrowsingSequence browsingSequence = BrowsingSequence::directoryBacked(selectedPath);
        if (browsingSequence.selectedIndex() < 0) {
            showFeedback(tr("Unsupported image"));
            return;
        }
        pendingFilePickerPath_ = QFileInfo(selectedPath).canonicalFilePath();
        openDirectoryBacked(selectedPath);
    }

    void displaySelectedImage(const bool animateDisplayed = true)
    {
        animateDisplayedPresentation_ = animateDisplayed;
        dismissLargeImageWarning();
        rotationQuarterTurns_ = 0;
        const QString currentPath = browsingSequence_.selectedPath();
        imageLoader_.setCurrentPath(currentPath);
        for (QAction *action : imageActions_) {
            action->setEnabled(false);
        }
        currentImage_ = {};
        image_ = {};
        imageLabel_->clearImage();
        statusDisplay_->hide();
        loadingFilename_->setText(QFileInfo(currentPath).fileName());
        loadingIndicator_->hide();
        loadingFilename_->hide();
        showPresentation(loadingState_, PresentationState::Loading);
        updateInformation();
        loadingTimer_->start(LoadingIndicatorDelayMilliseconds);
        requestDecode(currentPath);
    }

    void retryCurrentImage()
    {
        const QString currentPath = browsingSequence_.selectedPath();
        if (currentPath.isEmpty()) {
            return;
        }
        dismissLargeImageWarning();
        loadingFilename_->setText(QFileInfo(currentPath).fileName());
        loadingIndicator_->hide();
        loadingFilename_->hide();
        showPresentation(loadingState_, PresentationState::Loading);
        updateInformation();
        loadingTimer_->start(LoadingIndicatorDelayMilliseconds);
        retryDecode(currentPath);
    }

    void approveLargeImage()
    {
        const QString approvedPath = pendingLargeImagePath_;
        dismissLargeImageWarning();
        if (!approvedPath.isEmpty() && browsingSequence_.selectedPath() == approvedPath) {
            loadingFilename_->setText(QFileInfo(approvedPath).fileName());
            loadingIndicator_->hide();
            loadingFilename_->hide();
            showPresentation(loadingState_, PresentationState::Loading);
            updateInformation();
            loadingTimer_->start(LoadingIndicatorDelayMilliseconds);
            retryDecode(approvedPath, true);
        }
    }

    void rejectLargeImage()
    {
        const bool rejectingCurrent =
            !pendingLargeImagePath_.isEmpty() &&
            browsingSequence_.selectedPath() == pendingLargeImagePath_;
        dismissLargeImageWarning();
        if (rejectingCurrent) {
            const BrowsingSequence::AdjacentPaths adjacent = browsingSequence_.adjacentPaths();
            if (!adjacent.next.isEmpty()) {
                navigate(1);
            } else if (!adjacent.previous.isEmpty()) {
                navigate(-1);
            } else {
                browsingSequence_ = BrowsingSequence::explicitList({});
                imageLoader_.setCurrentPath({});
                showEmptyState();
            }
            showFeedback(tr("Large image skipped"));
        }
    }

    void dismissLargeImageWarning()
    {
        largeImageWarning_->hide();
        pendingLargeImagePath_.clear();
    }

    void present(const QString &path, const LoadedImage &decoded)
    {
        if (decoded.frames.isEmpty() || browsingSequence_.selectedPath() != path) {
            return;
        }
        animationTimer_->stop();
        currentImage_ = decoded;
        currentFrame_ = 0;
        completedLoops_ = 0;
        animationPaused_ = false;
        pausedDelayMilliseconds_ = 0;
        rotationQuarterTurns_ = 0;
        applyInitialZoom();
        showFrame(currentFrame_);
        showStatus(false);
        scheduleCenterView();
        if (currentImage_.frames.size() > 1) {
            animationTimer_->start(std::max(1, currentImage_.frameDelays.at(currentFrame_)));
        }
        showPresentation(viewport_, PresentationState::Displayed, animateDisplayedPresentation_);
        if (path == pendingFilePickerPath_) {
            QSettings settings;
            settings.setValue(QStringLiteral("filePicker/lastDirectory"),
                              QFileInfo(path).absolutePath());
            settings.sync();
            pendingFilePickerPath_.clear();
        }
        for (QAction *action : imageActions_) {
            action->setEnabled(true);
        }
        commandAction("viewerAnimationAction")
            ->setEnabled(currentImage_.frames.size() > 1);
        updateInformation();
        if (informationDialog_ == nullptr) {
            restoreViewingFocus();
        }
        setWindowTitle(tr("Flick — %1").arg(QFileInfo(path).fileName()));
        if (!pendingFeedback_.isEmpty()) {
            const QString feedback = pendingFeedback_;
            pendingFeedback_.clear();
            showFeedback(feedback);
        } else if (!browsingTeachingComplete_) {
            showFeedback(tr("← → Browse · Right-click for commands"));
        }
    }

    void refreshDirectorySequence()
    {
        if (directoryWatcher_->directories().isEmpty()) {
            return;
        }
        const BrowsingSequence::ReconcileOutcome outcome =
            browsingSequence_.reconcileDirectory();
        if (outcome == BrowsingSequence::ReconcileOutcome::Unchanged) {
            return;
        }
        if (outcome == BrowsingSequence::ReconcileOutcome::SelectionPreserved) {
            updateStatusText();
            updateInformation();
            prefetchNeighbors();
            return;
        }

        if (outcome == BrowsingSequence::ReconcileOutcome::Empty) {
            imageLoader_.setCurrentPath({});
            currentImage_ = {};
            image_ = {};
            animationTimer_->stop();
            setWindowTitle(QStringLiteral("Flick"));
            showEmptyState();
            showFeedback(tr("Current image is no longer available"));
            return;
        }

        pendingFeedback_ = tr("Current image is no longer available");
        displaySelectedImage(false);
    }

    void showFrame(const int index)
    {
        const QImage &source = currentImage_.frames.at(index);
        const QColorSpace target =
            displayColorSpace_.isValid() ? displayColorSpace_ : QColorSpace(QColorSpace::SRgb);
        image_ = source.colorSpace() == target ? source : source.convertedToColorSpace(target);
        image_.setColorSpace({});
        renderImage();
    }

    void refreshDisplayColorSpace()
    {
        QScreen *activeScreen = windowHandle() ? windowHandle()->screen() : screen();
        applyDisplayColorSpace(platformServices_->displayColorSpace(activeScreen));
    }

    void applyDisplayColorSpace(const QColorSpace &exposed)
    {
        const QColorSpace next = exposed.isValid() ? exposed : QColorSpace(QColorSpace::SRgb);
        if (displayColorSpace_ == next) {
            return;
        }
        displayColorSpace_ = next;
        if (!currentImage_.frames.isEmpty()) {
            showFrame(currentFrame_);
        }
    }

    double fitZoom() const
    {
        if (image_.isNull()) {
            return 1.0;
        }
        const QSize available = viewport_->viewport()->size();
        const QSize imageSize = rotatedImageSize();
        return std::min(double(available.width()) / imageSize.width(),
                        double(available.height()) / imageSize.height());
    }

    void applyInitialZoom()
    {
        image_ = currentImage_.frames.at(0);
        zoom_ = std::min(1.0, fitZoom());
    }

    void fitToViewport()
    {
        if (!image_.isNull()) {
            setZoomCentered(fitZoom());
        }
    }

    void setZoom(const double zoom)
    {
        if (image_.isNull()) {
            return;
        }
        zoom_ = std::clamp(zoom, 0.01, 64.0);
        renderImage();
        updateInformation();
        showStatus(false);
    }

    void setZoomCentered(const double zoom)
    {
        setZoom(zoom);
        scheduleCenterView();
    }

    void zoomAt(const QPointF &viewportPosition, const int angleDelta)
    {
        if (image_.isNull() || angleDelta == 0) {
            return;
        }
        auto *horizontal = viewport_->horizontalScrollBar();
        auto *vertical = viewport_->verticalScrollBar();
        const QPoint originBefore = imageOrigin();
        const QPointF imagePoint(
            (horizontal->value() + viewportPosition.x() - originBefore.x()) / zoom_,
            (vertical->value() + viewportPosition.y() - originBefore.y()) / zoom_);
        const double steps = angleDelta / 120.0;
        setZoom(zoom_ * std::pow(1.25, steps));
        const QPoint originAfter = imageOrigin();
        horizontal->setValue(
            qRound(originAfter.x() + imagePoint.x() * zoom_ - viewportPosition.x()));
        vertical->setValue(
            qRound(originAfter.y() + imagePoint.y() * zoom_ - viewportPosition.y()));
    }

    void panBy(const int horizontalDistance, const int verticalDistance)
    {
        auto *horizontal = viewport_->horizontalScrollBar();
        auto *vertical = viewport_->verticalScrollBar();
        horizontal->setValue(horizontal->value() + horizontalDistance);
        vertical->setValue(vertical->value() + verticalDistance);
    }

    void rotateView(const int quarterTurns)
    {
        if (image_.isNull()) {
            return;
        }
        rotationQuarterTurns_ = (rotationQuarterTurns_ + quarterTurns) % 4;
        if (rotationQuarterTurns_ < 0) {
            rotationQuarterTurns_ += 4;
        }
        renderImage();
        updateInformation();
        scheduleCenterView();
        showStatus(false);
    }

    void renderImage()
    {
        const QSize displayedSize = displayedImageSize();
        imageLabel_->showImage(rotatedImage(), displayedSize, viewport_->viewport()->size());
    }

    QImage rotatedImage() const
    {
        if (rotationQuarterTurns_ == 0) {
            return image_;
        }
        return image_.transformed(QTransform().rotate(rotationQuarterTurns_ * 90));
    }

    QSize rotatedImageSize() const
    {
        return rotationQuarterTurns_ % 2 == 0 ? image_.size()
                                              : QSize(image_.height(), image_.width());
    }

    QSize displayedImageSize() const
    {
        const QSize imageSize = rotatedImageSize();
        return QSize(qMax(1, qRound(imageSize.width() * zoom_)),
                     qMax(1, qRound(imageSize.height() * zoom_)));
    }

    QPoint imageOrigin() const
    {
        const QSize displayedSize = displayedImageSize();
        return QPoint((imageLabel_->width() - displayedSize.width()) / 2,
                      (imageLabel_->height() - displayedSize.height()) / 2);
    }

    void centerView()
    {
        viewport_->horizontalScrollBar()->setValue(
            viewport_->horizontalScrollBar()->maximum() / 2);
        viewport_->verticalScrollBar()->setValue(viewport_->verticalScrollBar()->maximum() / 2);
    }

    void scheduleCenterView()
    {
        QTimer::singleShot(0, this, [this] {
            centerView();
        });
    }

    void advanceAnimation()
    {
        if (currentImage_.frames.size() < 2 || animationPaused_) {
            return;
        }
        if (currentFrame_ + 1 < currentImage_.frames.size()) {
            ++currentFrame_;
        } else if (currentImage_.loopCount < 0 || completedLoops_ < currentImage_.loopCount) {
            currentFrame_ = 0;
            ++completedLoops_;
        } else {
            return;
        }
        showFrame(currentFrame_);
        animationTimer_->start(std::max(1, currentImage_.frameDelays.at(currentFrame_)));
    }

    void toggleAnimation()
    {
        if (currentImage_.frames.size() < 2) {
            return;
        }
        if (animationPaused_) {
            animationPaused_ = false;
            animationTimer_->start(std::max(1, pausedDelayMilliseconds_));
        } else {
            pausedDelayMilliseconds_ = std::max(1, animationTimer_->remainingTime());
            animationPaused_ = true;
            animationTimer_->stop();
        }
        updateInformation();
    }

    ImageLoading::DecodeRequest decodeRequest(const QString &path,
                                              const bool approvedLargeImage = false) const
    {
#ifdef FLICK_ENABLE_TEST_HARNESS
        const qint64 configuredAllocationLimit =
            qEnvironmentVariableIntValue("FLICK_TEST_LARGE_ALLOCATION_LIMIT_BYTES");
        const qint64 allocationLimit = configuredAllocationLimit > 0
                                           ? configuredAllocationLimit
                                           : LargeImageAllocationLimit;
#else
        constexpr qint64 allocationLimit = LargeImageAllocationLimit;
#endif
        return {path, approvedLargeImage, allocationLimit};
    }

    void recordScheduledDecode(const QString &path, const bool scheduled)
    {
#ifdef FLICK_ENABLE_TEST_HARNESS
        if (scheduled) {
            ++decodeCounts_[path];
        }
#else
        Q_UNUSED(path)
        Q_UNUSED(scheduled)
#endif
    }

    void requestDecode(const QString &path)
    {
        if (!path.isEmpty()) {
            recordScheduledDecode(path, imageLoader_.request(decodeRequest(path)));
        }
    }

    void retryDecode(const QString &path, const bool approvedLargeImage = false)
    {
        if (!path.isEmpty()) {
            recordScheduledDecode(
                path, imageLoader_.retry(decodeRequest(path, approvedLargeImage)));
        }
    }

    void prefetchNeighbors()
    {
        const auto requestFor = [this](const QString &path)
            -> std::optional<ImageLoading::DecodeRequest> {
            return path.isEmpty() ? std::nullopt
                                  : std::optional<ImageLoading::DecodeRequest>(decodeRequest(path));
        };
        const BrowsingSequence::AdjacentPaths adjacent = browsingSequence_.adjacentPaths();
        const QList<QString> scheduled = imageLoader_.prefetchAdjacent(
            requestFor(adjacent.previous), requestFor(adjacent.next));
        for (const QString &path : scheduled) {
            recordScheduledDecode(path, true);
        }
    }

    void navigate(const int offset)
    {
        const BrowsingSequence::MoveOutcome outcome =
            offset < 0 ? browsingSequence_.movePrevious() : browsingSequence_.moveNext();
        if (outcome != BrowsingSequence::MoveOutcome::Selected) {
            showFeedback(outcome == BrowsingSequence::MoveOutcome::Beginning
                             ? tr("Beginning of folder")
                             : tr("End of folder"));
            return;
        }
        markBrowsingTeachingComplete();
        displaySelectedImage(false);
    }

    void showFeedback(const QString &message)
    {
        if (!statusVisible_) {
            return;
        }
        statusIsFeedback_ = true;
        statusFade_->stop();
        statusOpacity_->setOpacity(1.0);
        statusDisplay_->setText(message);
        statusDisplay_->setMaximumWidth(qMax(1, qMin(440, surface_->width() - 40)));
        statusDisplay_->adjustSize();
        positionStatusDisplay();
        statusDisplay_->show();
        statusDisplay_->raise();
        statusTimer_->start(FeedbackVisibilityMilliseconds);
    }

    void showEmptyState()
    {
        loadingTimer_->stop();
        showPresentation(emptyState_, PresentationState::Empty);
        updateInformation();
    }

    void showDecodeError(const ImageLoading::DecodeFailure &failure)
    {
        animationTimer_->stop();
        errorExplanation_->setText(
            tr("This image could not be displayed\n%1")
                .arg(QFileInfo(failure.path).fileName()));
        errorDetails_->setText(
            failure.details.isEmpty() ? tr("The image decoder returned no pixels.")
                                      : failure.details);
        errorDetailsButton_->setChecked(false);
        showPresentation(errorState_, PresentationState::Error);
        updateInformation();
        setWindowTitle(tr("Flick — Error"));
    }

    void showLargeImageWarning(const ImageLoading::ConfirmationRequired &confirmation)
    {
        pendingLargeImageSize_ = confirmation.declaredSize;
        pendingLargeImagePath_ = confirmation.path;
        largeImageExplanation_->setText(
            tr("%1 declares %2 × %3 pixels and may require about %4 MB when decoded. "
               "Decode it anyway?")
                .arg(QFileInfo(confirmation.path).fileName())
                .arg(confirmation.declaredSize.width())
                .arg(confirmation.declaredSize.height())
                .arg(confirmation.estimatedAllocationBytes / (1024 * 1024)));
        showPresentation(largeImageWarning_, PresentationState::LargeImageConfirmation);
        updateInformation();
    }

    void showPresentation(QWidget *widget, const PresentationState state,
                          const bool animate = true)
    {
        presentationState_ = state;
        surfaceStack_->setCurrentWidget(widget);
        // Qt maps the platform's reduced-motion setting to a zero widget-animation duration.
        if (!animate || qEnvironmentVariableIsSet("FLICK_TEST_REDUCED_MOTION") ||
            style()->styleHint(QStyle::SH_Widget_Animation_Duration, nullptr, this) <= 0 ||
            !isVisible()) {
            return;
        }
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

    void hideDropOverlay()
    {
        dropOverlay_->hide();
    }

    QImage image_;
    std::unique_ptr<PlatformServices> platformServices_;
    ImageLoading::Loader imageLoader_;
    ImageCanvas *imageLabel_ = nullptr;
    QWidget *surface_ = nullptr;
    QStackedLayout *surfaceStack_ = nullptr;
    QScrollArea *viewport_ = nullptr;
    QTimer *animationTimer_ = nullptr;
    QFileSystemWatcher *directoryWatcher_ = nullptr;
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
    QSize pendingLargeImageSize_;
    QString pendingLargeImagePath_;
    QLabel *statusDisplay_ = nullptr;
    QTimer *statusTimer_ = nullptr;
    QGraphicsOpacityEffect *statusOpacity_ = nullptr;
    QPropertyAnimation *statusFade_ = nullptr;
    QWidget *dropOverlay_ = nullptr;
    QLabel *dropLabel_ = nullptr;
    PresentationState presentationState_ = PresentationState::Empty;
    BrowsingSequence browsingSequence_ = BrowsingSequence::explicitList({});
    QString pendingFilePickerPath_;
    QString pendingFeedback_;
    LoadedImage currentImage_;
    int currentFrame_ = 0;
    int completedLoops_ = 0;
    int pausedDelayMilliseconds_ = 0;
    bool animationPaused_ = false;
    bool animateDisplayedPresentation_ = true;
    WheelAction wheelAction_ = WheelAction::Navigate;
    double zoom_ = 1.0;
    int rotationQuarterTurns_ = 0;
    bool dragging_ = false;
    QPointF lastDragPosition_;
    QColor viewportBackground_{QStringLiteral("#181A1B")};
    bool statusVisible_ = true;
    bool statusIsFeedback_ = false;
    bool browsingTeachingComplete_ = false;
    bool fullscreenTeachingComplete_ = false;
    bool restoreWindowGeometry_ = false;
    QColorSpace displayColorSpace_{QColorSpace::SRgb};
    QList<QAction *> imageActions_;
    QString informationText_;
    QDialog *informationDialog_ = nullptr;
    QLabel *informationFacts_ = nullptr;
    QMenuBar *applicationMenuBar_ = nullptr;
    QMenu *contextMenu_ = nullptr;
    QDialog *settingsDialog_ = nullptr;
    QComboBox *settingsWheel_ = nullptr;
    QPushButton *settingsBackground_ = nullptr;
    QCheckBox *settingsStatus_ = nullptr;
    QSpinBox *settingsCache_ = nullptr;
    QCheckBox *settingsGeometry_ = nullptr;
    QDialogButtonBox *settingsButtons_ = nullptr;
    SettingsValues settingsOpeningValues_;
    QColor settingsDialogBackground_{QStringLiteral("#181A1B")};
#ifdef FLICK_ENABLE_TEST_HARNESS
    QHash<QString, int> decodeCounts_;
    bool failExternalActionsForTest_ = false;
#endif
};

#ifdef FLICK_ENABLE_TEST_HARNESS
void captureVisibleWindow(ViewerWindow &window)
{
    const QString screenshotPath = qEnvironmentVariable("FLICK_TEST_SCREENSHOT_FILE");
    if (screenshotPath.isEmpty()) {
        return;
    }
    window.grab().save(screenshotPath, "PNG");
}

void scheduleCapture(ViewerWindow &window, QObject &context, const bool waitUntilReady)
{
    auto capture = std::make_shared<std::function<void()>>();
    auto readyForCapture = std::make_shared<bool>(false);
    *capture = [&window, &context, waitUntilReady, capture, readyForCapture] {
        if (waitUntilReady && window.isLoading()) {
            QTimer::singleShot(10, &context, *capture);
            return;
        }
        if (waitUntilReady && !*readyForCapture) {
            *readyForCapture = true;
            QTimer::singleShot(0, &context, *capture);
            return;
        }
        captureVisibleWindow(window);
    };
    QTimer::singleShot(0, &context, *capture);
}
#endif
} // namespace

int main(int argc, char *argv[])
{
    FlickApplication application(argc, argv);
    QAccessible::installFactory(flickAccessibleInterface);
    QApplication::setApplicationName(QStringLiteral("Flick"));
    QApplication::setApplicationDisplayName(QStringLiteral("Flick"));
    QApplication::setApplicationVersion(QStringLiteral(FLICK_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("org.flick.Flick"));
    QApplication::setOrganizationName(QStringLiteral("Flick"));
#ifdef FLICK_ENABLE_TEST_HARNESS
    if (qEnvironmentVariableIsSet("FLICK_TEST_DARK_CHROME")) {
        QPalette palette = application.palette();
        palette.setColor(QPalette::Window, QColor(QStringLiteral("#2b2b2b")));
        palette.setColor(QPalette::WindowText, Qt::white);
        palette.setColor(QPalette::Base, QColor(QStringLiteral("#202020")));
        palette.setColor(QPalette::Text, Qt::white);
        palette.setColor(QPalette::Button, QColor(QStringLiteral("#353535")));
        palette.setColor(QPalette::ButtonText, Qt::white);
        application.setPalette(palette);
    }
#endif

    const QStringList arguments = application.arguments();
    const QString imagePath = arguments.size() > 1 ? arguments.at(1) : QString{};
#ifdef FLICK_ENABLE_TEST_HARNESS
    auto platformServices = std::make_unique<TestPlatformServices>();
    TestPlatformServices *testPlatformServices = platformServices.get();
#else
    auto platformServices = createPlatformServices();
#endif
    ViewerWindow window(imagePath, std::move(platformServices));
    application.setFileOpenHandler([&window](const QString &path) {
        window.openExternalFile(path);
    });
    window.show();
    window.setFocus();
#ifdef FLICK_ENABLE_TEST_HARNESS
    scheduleCapture(window, application, true);
    QSocketNotifier testCommands(STDIN_FILENO, QSocketNotifier::Read, &application);
    QObject::connect(
        &testCommands, &QSocketNotifier::activated, &application,
        [&application, &window, testPlatformServices] {
            char command[4096] = {};
            const auto bytesRead = ::read(STDIN_FILENO, command, sizeof(command));
            if (bytesRead <= 0) {
                return;
            }
            const QByteArray input(command, bytesRead);
            const bool captureImmediately = input.startsWith("Capture");
            if (input.startsWith("CacheBytes")) {
                fprintf(stdout, "%lld\n", static_cast<long long>(window.cacheBytes()));
                fflush(stdout);
                return;
            } else if (input.startsWith("UiResponsiveness")) {
                fprintf(stdout, "%d|%s\n", window.decodesInFlight() > 0 ? 1 : 0,
                        window.uiState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("SettingsState")) {
                fprintf(stdout, "%s\n", window.settingsState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("StoredSettingsState")) {
                fprintf(stdout, "%s\n", window.storedSettingsState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("SettingsDialogStructure")) {
                fprintf(stdout, "%s\n", window.settingsDialogStructure().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("SettingsDialogGeometry")) {
                fprintf(stdout, "%s\n", window.settingsDialogGeometry().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("SettingsDialogFocusOrder")) {
                fprintf(stdout, "%s\n", window.settingsDialogFocusOrder().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("LastPickerDirectory")) {
                fprintf(stdout, "%s\n",
                        QSettings().value(QStringLiteral("filePicker/lastDirectory"))
                            .toString().toUtf8().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("WindowGeometry")) {
                fprintf(stdout, "%s\n", window.windowGeometryState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("PresentationState")) {
                fprintf(stdout, "%s\n", window.presentationState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("SaveWindowGeometry")) {
                window.persistWindowGeometry();
                fprintf(stdout, "saved\n");
                fflush(stdout);
                return;
            } else if (input.startsWith("ApplySettings:")) {
                window.applyTestSettings(
                    QString::fromUtf8(input.mid(14).trimmed()).split(QLatin1Char(':')));
                return;
            } else if (input.startsWith("OpenSettings")) {
                if (QAction *settings =
                        window.findChild<QAction *>(QStringLiteral("settingsAction"))) {
                    settings->trigger();
                }
                return;
            } else if (input.startsWith("PreviewSettings:")) {
                window.previewTestSettings(
                    QString::fromUtf8(input.mid(16).trimmed()).split(QLatin1Char(':')));
                return;
            } else if (input.startsWith("ResetSettings")) {
                window.resetTestSettings();
                return;
            } else if (input.startsWith("ApplySettingsDialog")) {
                window.finishTestSettings(true);
                return;
            } else if (input.startsWith("CancelSettings")) {
                window.finishTestSettings(false);
                return;
            } else if (input.startsWith("DisplayProfileChanged:")) {
                testPlatformServices->setDisplayIccProfile(
                    QString::fromUtf8(input.mid(22).trimmed()));
                window.displayConfigurationChanged();
            } else if (input.startsWith("Resize:")) {
                const QList<QByteArray> size = input.mid(7).trimmed().split(':');
                if (size.size() == 2) {
                    window.resize(size.at(0).toInt(), size.at(1).toInt());
                }
                return;
            } else if (input.startsWith("Close")) {
                window.close();
                application.quit();
                return;
            } else if (input.startsWith("ViewState")) {
                fprintf(stdout, "%s\n", window.viewState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("UiState")) {
                fprintf(stdout, "%s\n", window.uiState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("FocusViewingSurface")) {
                window.focusViewingSurfaceForTest();
                return;
            } else if (input.startsWith("InformationState")) {
                fprintf(stdout, "%s\n", window.informationState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("InformationDialogState")) {
                fprintf(stdout, "%s\n", window.informationDialogState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("ClipboardText")) {
                fprintf(stdout, "%s\n", QApplication::clipboard()->text().toUtf8().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("ClipboardImageSize")) {
                const QSize size = QApplication::clipboard()->image().size();
                fprintf(stdout, "%dx%d\n", size.width(), size.height());
                fflush(stdout);
                return;
            } else if (input.startsWith("ContextActions")) {
                fprintf(stdout, "%s\n", window.contextActions().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("ContextMenuStructure")) {
                fprintf(stdout, "%s\n", window.contextMenuStructure().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("ApplicationMenuStructure")) {
                fprintf(stdout, "%s\n", window.applicationMenuStructure().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("CommandAvailability")) {
                fprintf(stdout, "%s\n", window.commandAvailability().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("FocusState")) {
                fprintf(stdout, "%s\n", window.focusState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("AccessibilityState")) {
                fprintf(stdout, "%s\n", window.accessibilityState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("RevealedPath")) {
                fprintf(stdout, "%s\n",
                        testPlatformServices->revealedPath().toUtf8().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("Feedback")) {
                fprintf(stdout, "%s\n", window.feedbackState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("ErrorState")) {
                fprintf(stdout, "%s\n", window.errorState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("LargeImageState")) {
                fprintf(stdout, "%s\n", window.largeImageState().constData());
                fflush(stdout);
                return;
            } else if (input.startsWith("ToggleDetails")) {
                if (auto *button = window.findChild<QToolButton *>()) {
                    button->toggle();
                }
            } else if (input.startsWith("ApproveLarge")) {
                window.findChild<QPushButton *>(QStringLiteral("approveLargeImage"))->click();
            } else if (input.startsWith("RejectLarge")) {
                window.findChild<QPushButton *>(QStringLiteral("rejectLargeImage"))->click();
            } else if (input.startsWith("FailExternalActions")) {
                window.failExternalActionsForTest();
                return;
            } else if (input.startsWith("DecodeCount:")) {
                const QString path = QString::fromUtf8(input.mid(12).trimmed());
                fprintf(stdout, "%d\n", window.decodeCount(path));
                fflush(stdout);
                return;
            } else if (input.startsWith("Drop:")) {
                const QList<QByteArray> encodedPaths = input.mid(5).trimmed().split('|');
                auto *mimeData = new QMimeData;
                QList<QUrl> urls;
                for (const QByteArray &encodedPath : encodedPaths) {
                    urls.append(QUrl::fromLocalFile(QString::fromUtf8(encodedPath)));
                }
                mimeData->setUrls(urls);
                QDragEnterEvent dragEvent(window.rect().center(), Qt::CopyAction, mimeData,
                                          Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(&window, &dragEvent);
                QDropEvent event(QPointF(window.rect().center()), Qt::CopyAction, mimeData,
                                 Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(&window, &event);
                delete mimeData;
            } else if (input.startsWith("BeginDrag:")) {
                auto *mimeData = new QMimeData;
                mimeData->setUrls({QUrl::fromLocalFile(
                    QString::fromUtf8(input.mid(10).trimmed()))});
                QDragEnterEvent event(window.rect().center(), Qt::CopyAction, mimeData,
                                      Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(&window, &event);
                delete mimeData;
            } else if (input.startsWith("LeaveDrag")) {
                QDragLeaveEvent event;
                QApplication::sendEvent(&window, &event);
            } else if (input.startsWith("SelectZoomWheelAction")) {
                if (auto *action =
                        window.findChild<QAction *>(QStringLiteral("wheelZoomAction"))) {
                    action->trigger();
                }
            } else if (input.startsWith("ContextMenu:")) {
                const QList<QByteArray> parts = input.trimmed().split(':');
                if (parts.size() == 3) {
                    QWidget *target =
                        window.findChild<QWidget *>(QStringLiteral("viewingSurface"));
                    const QPoint position(parts.at(1).toInt(), parts.at(2).toInt());
                    QContextMenuEvent event(QContextMenuEvent::Mouse, position,
                                            target->mapToGlobal(position));
                    QApplication::sendEvent(target, &event);
                }
            } else if (input.startsWith("MenuDown") || input.startsWith("MenuEnter")) {
                if (QWidget *menu = QApplication::activePopupWidget()) {
                    const int key =
                        input.startsWith("MenuDown") ? Qt::Key_Down : Qt::Key_Return;
                    QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
                    QApplication::sendEvent(menu, &event);
                }
            } else if (input.startsWith("Wheel:") || input.startsWith("CtrlWheel:")) {
                const QList<QByteArray> parts = input.trimmed().split(':');
                if (parts.size() == 4) {
                    const QPointF position(parts.at(1).toInt(), parts.at(2).toInt());
                    const Qt::KeyboardModifiers modifiers =
                        input.startsWith("CtrlWheel:") ? Qt::ControlModifier : Qt::NoModifier;
                    QWheelEvent event(position, position, QPoint(), QPoint(0, parts.at(3).toInt()),
                                      Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
                    QApplication::sendEvent(window.findChild<QScrollArea *>()->viewport(), &event);
                }
            } else if (input.startsWith("Drag:")) {
                const QList<QByteArray> parts = input.trimmed().split(':');
                if (parts.size() == 5) {
                    QWidget *target = window.findChild<QScrollArea *>()->viewport();
                    const QPointF start(parts.at(1).toInt(), parts.at(2).toInt());
                    const QPointF end(parts.at(3).toInt(), parts.at(4).toInt());
                    QMouseEvent press(QEvent::MouseButtonPress, start, start, start,
                                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(target, &press);
                    QMouseEvent move(QEvent::MouseMove, end, end, end, Qt::NoButton,
                                     Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(target, &move);
                    QMouseEvent release(QEvent::MouseButtonRelease, end, end, end,
                                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                    QApplication::sendEvent(target, &release);
                }
            } else if (input.startsWith("Move:") || input.startsWith("DoubleClick:")) {
                const QList<QByteArray> parts = input.trimmed().split(':');
                if (parts.size() == 3) {
                    QWidget *target = window.findChild<QScrollArea *>()->viewport();
                    const QPointF position(parts.at(1).toInt(), parts.at(2).toInt());
                    const QEvent::Type type = input.startsWith("DoubleClick:")
                                                  ? QEvent::MouseButtonDblClick
                                                  : QEvent::MouseMove;
                    const Qt::MouseButton button =
                        type == QEvent::MouseButtonDblClick ? Qt::LeftButton : Qt::NoButton;
                    QMouseEvent event(type, position, position, position, button, button,
                                      Qt::NoModifier);
                    QApplication::sendEvent(target, &event);
                }
            } else if (!input.startsWith("Capture")) {
                const bool ctrlO = input.startsWith("CtrlO");
                const bool copyImage = input.startsWith("CopyImage");
                const bool copyPath = input.startsWith("CopyPath");
                const bool reveal = input.startsWith("Reveal");
                const bool ctrlPlus = input.startsWith("CtrlPlus");
                const bool ctrlMinus = input.startsWith("CtrlMinus");
                const bool shift = input.startsWith("Shift");
                const int qtKey = ctrlO                         ? Qt::Key_O
                                  : copyImage                   ? Qt::Key_C
                                  : copyPath                    ? Qt::Key_C
                                  : reveal                      ? Qt::Key_R
                                  : ctrlPlus                    ? Qt::Key_Plus
                                  : ctrlMinus                   ? Qt::Key_Minus
                                  : input.startsWith("Information") ? Qt::Key_I
                                  : input.startsWith("Fit")     ? Qt::Key_F
                                  : input.startsWith("ActualSize") ? Qt::Key_1
                                  : input.startsWith("ShiftLeft") ? Qt::Key_Left
                                  : input.startsWith("ShiftRight") ? Qt::Key_Right
                                  : input.startsWith("ShiftUp") ? Qt::Key_Up
                                  : input.startsWith("ShiftDown") ? Qt::Key_Down
                                  : input.startsWith("RotateLeft") ? Qt::Key_L
                                  : input.startsWith("RotateRight") ? Qt::Key_R
                                  : input.startsWith("F11")     ? Qt::Key_F11
                                  : input.startsWith("Refresh") ? Qt::Key_F5
                                  : input.startsWith("Escape")  ? Qt::Key_Escape
                                  : input.startsWith("Left")    ? Qt::Key_Left
                                  : input.startsWith("Space")   ? Qt::Key_Space
                                                                : Qt::Key_Right;
                QKeyEvent event(QEvent::KeyPress, qtKey,
                                (ctrlO || copyImage || ctrlPlus || ctrlMinus)
                                    ? Qt::ControlModifier
                                : (copyPath || reveal)
                                    ? Qt::ControlModifier | Qt::ShiftModifier
                                : shift ? Qt::ShiftModifier
                                        : Qt::NoModifier);
                QWidget *target = QApplication::activePopupWidget();
                if (target == nullptr) {
                    target = QApplication::focusWidget();
                }
                QApplication::sendEvent(target != nullptr ? target : &window, &event);
            }
            scheduleCapture(window, application, !captureImmediately);
        });
#endif

    return application.exec();
}
