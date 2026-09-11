// SPDX-License-Identifier: GPL-3.0-or-later

#include "settings_editor.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

#include <optional>
#include <utility>

namespace Settings {
namespace {

QString wheelActionName(const WheelAction action)
{
    return action == WheelAction::Zoom ? QStringLiteral("zoom") : QStringLiteral("navigate");
}

WheelAction wheelActionFromName(const QString &name)
{
    return name == QStringLiteral("zoom") ? WheelAction::Zoom : WheelAction::Navigate;
}

} // namespace

class Editor::Implementation final
{
  public:
    explicit Implementation(QWidget &parent) : parent_(parent) {}

    Values controlsValues() const
    {
        return {wheelActionFromName(wheel_->currentData().toString()),
                background_, status_->isChecked(),
                static_cast<qsizetype>(cache_->value()) * 1024 * 1024,
                geometry_->isChecked()};
    }

    void setControls(const Values &values)
    {
        background_ = values.background;
        wheel_->setCurrentIndex(values.wheelAction == WheelAction::Zoom ? 1 : 0);
        backgroundButton_->setText(values.background.name());
        status_->setChecked(values.statusVisible);
        cache_->setValue(static_cast<int>(values.cacheBudgetBytes / (1024 * 1024)));
        geometry_->setChecked(values.restoreWindowGeometry);
        preview_(values);
    }

    QWidget &parent_;
    QDialog *dialog_ = nullptr;
    QComboBox *wheel_ = nullptr;
    QPushButton *backgroundButton_ = nullptr;
    QCheckBox *status_ = nullptr;
    QSpinBox *cache_ = nullptr;
    QCheckBox *geometry_ = nullptr;
    QDialogButtonBox *buttons_ = nullptr;
    Values opening_;
    Values defaults_;
    QColor background_{QStringLiteral("#181A1B")};
    PreviewOperation preview_;
};

Editor::Editor(QWidget &parent) : implementation_(std::make_unique<Implementation>(parent)) {}
Editor::~Editor()
{
    if (implementation_->dialog_ != nullptr) {
        QObject::disconnect(implementation_->dialog_, nullptr, nullptr, nullptr);
        delete implementation_->dialog_;
    }
}

Values Editor::defaults()
{
    return {};
}

Values Editor::readAccepted()
{
    QSettings settings;
    Values values;
    values.wheelAction = wheelActionFromName(
        settings.value(QStringLiteral("view/wheelAction"), QStringLiteral("navigate")).toString());
    values.background = QColor(
        settings.value(QStringLiteral("view/background"), values.background.name()).toString());
    if (!values.background.isValid()) {
        values.background = defaults().background;
    }
    values.statusVisible =
        settings.value(QStringLiteral("view/statusVisible"), values.statusVisible).toBool();
    values.cacheBudgetBytes =
        settings.value(QStringLiteral("cache/budgetBytes"), values.cacheBudgetBytes).toLongLong();
    values.restoreWindowGeometry =
        settings.value(QStringLiteral("window/restoreGeometry"), values.restoreWindowGeometry)
            .toBool();
    return values;
}

QByteArray Editor::readWindowGeometry()
{
    return QSettings().value(QStringLiteral("window/geometry")).toByteArray();
}

void Editor::persistAccepted(const Values &values)
{
    QSettings settings;
    settings.setValue(QStringLiteral("view/wheelAction"), wheelActionName(values.wheelAction));
    settings.setValue(QStringLiteral("view/background"), values.background.name());
    settings.setValue(QStringLiteral("view/statusVisible"), values.statusVisible);
    settings.setValue(QStringLiteral("cache/budgetBytes"), values.cacheBudgetBytes);
    settings.setValue(QStringLiteral("window/restoreGeometry"), values.restoreWindowGeometry);
    if (!values.restoreWindowGeometry) {
        settings.remove(QStringLiteral("window/geometry"));
    }
    settings.sync();
}

void Editor::persistWheelAction(const WheelAction action)
{
    QSettings().setValue(QStringLiteral("view/wheelAction"), wheelActionName(action));
}

void Editor::persistWindowGeometry(const QByteArray &geometry, const bool restorationEnabled)
{
    QSettings settings;
    if (restorationEnabled) {
        settings.setValue(QStringLiteral("window/geometry"), geometry);
    } else {
        settings.remove(QStringLiteral("window/geometry"));
    }
    settings.sync();
}

void Editor::open(const Values &opening, const Values &defaultValues, PreviewOperation preview)
{
    auto &state = *implementation_;
    if (state.dialog_ != nullptr) {
        state.dialog_->raise();
        state.dialog_->activateWindow();
        return;
    }
    state.opening_ = opening;
    state.defaults_ = defaultValues;
    state.preview_ = std::move(preview);
    state.dialog_ = new QDialog(&state.parent_);
    state.dialog_->setAttribute(Qt::WA_DeleteOnClose);
    state.dialog_->setWindowTitle(QDialog::tr("Settings"));
    state.dialog_->setWindowModality(Qt::WindowModal);
    state.dialog_->setMinimumWidth(380);
    auto *layout = new QVBoxLayout(state.dialog_);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->setSpacing(4);

    auto *navigation = new QGroupBox(QDialog::tr("Navigation"), state.dialog_);
    auto *navigationLayout = new QFormLayout(navigation);
    navigationLayout->setContentsMargins(8, 12, 8, 6);
    navigationLayout->setVerticalSpacing(4);
    state.wheel_ = new QComboBox(navigation);
    state.wheel_->setAccessibleName(QDialog::tr("Mouse wheel action"));
    state.wheel_->addItem(QDialog::tr("Navigate images"), QStringLiteral("navigate"));
    state.wheel_->addItem(QDialog::tr("Zoom image"), QStringLiteral("zoom"));
    navigationLayout->addRow(QDialog::tr("Mouse wheel:"), state.wheel_);
    layout->addWidget(navigation);

    auto *appearance = new QGroupBox(QDialog::tr("Appearance"), state.dialog_);
    auto *appearanceLayout = new QFormLayout(appearance);
    appearanceLayout->setContentsMargins(8, 12, 8, 6);
    appearanceLayout->setVerticalSpacing(4);
    state.backgroundButton_ = new QPushButton(appearance);
    state.backgroundButton_->setAccessibleName(QDialog::tr("Viewing surface background"));
    state.status_ = new QCheckBox(QDialog::tr("Show status overlay"), appearance);
    state.status_->setAccessibleName(QDialog::tr("Show status overlay"));
    appearanceLayout->addRow(QDialog::tr("Viewing surface background:"), state.backgroundButton_);
    appearanceLayout->addRow(QString{}, state.status_);
    layout->addWidget(appearance);

    auto *performance = new QGroupBox(QDialog::tr("Performance & Window"), state.dialog_);
    auto *performanceLayout = new QFormLayout(performance);
    performanceLayout->setContentsMargins(8, 12, 8, 6);
    performanceLayout->setVerticalSpacing(4);
    state.cache_ = new QSpinBox(performance);
    state.cache_->setAccessibleName(QDialog::tr("Decoded cache budget"));
    state.cache_->setRange(1, 16384);
    state.cache_->setSuffix(QDialog::tr(" MB"));
    state.geometry_ =
        new QCheckBox(QDialog::tr("Restore window size and position"), performance);
    state.geometry_->setAccessibleName(QDialog::tr("Restore window size and position"));
    performanceLayout->addRow(QDialog::tr("Decoded cache budget:"), state.cache_);
    performanceLayout->addRow(QString{}, state.geometry_);
    layout->addWidget(performance);

    state.buttons_ = new QDialogButtonBox(QDialogButtonBox::Reset | QDialogButtonBox::Cancel |
                                              QDialogButtonBox::Apply,
                                          state.dialog_);
    state.buttons_->button(QDialogButtonBox::Reset)->setText(QDialog::tr("Reset Defaults"));
    layout->addWidget(state.buttons_);

    const auto previewControls = [&state] { state.preview_(state.controlsValues()); };
    QObject::connect(state.wheel_, &QComboBox::currentIndexChanged, state.dialog_, previewControls);
    QObject::connect(state.status_, &QCheckBox::toggled, state.dialog_, previewControls);
    QObject::connect(state.cache_, &QSpinBox::valueChanged, state.dialog_, previewControls);
    QObject::connect(state.geometry_, &QCheckBox::toggled, state.dialog_, previewControls);
    QObject::connect(state.backgroundButton_, &QPushButton::clicked, state.dialog_, [&state] {
        const QColor selected = QColorDialog::getColor(
            state.background_, state.dialog_, QDialog::tr("Viewing Surface Background"));
        if (selected.isValid()) {
            state.background_ = selected;
            state.backgroundButton_->setText(selected.name());
            state.preview_(state.controlsValues());
        }
    });
    QObject::connect(state.buttons_->button(QDialogButtonBox::Reset), &QPushButton::clicked,
                     state.dialog_, [&state] { state.setControls(state.defaults_); });
    QObject::connect(state.buttons_->button(QDialogButtonBox::Apply), &QPushButton::clicked,
                     state.dialog_, [&state] {
                         Editor::persistAccepted(state.controlsValues());
                         state.dialog_->accept();
                     });
    QObject::connect(state.buttons_, &QDialogButtonBox::rejected, state.dialog_, &QDialog::reject);
    QObject::connect(state.dialog_, &QDialog::rejected, state.dialog_,
                     [&state] { state.preview_(state.opening_); });
    QObject::connect(state.dialog_, &QDialog::finished, &state.parent_, [&state] {
        state.dialog_ = nullptr;
        state.wheel_ = nullptr;
        state.backgroundButton_ = nullptr;
        state.status_ = nullptr;
        state.cache_ = nullptr;
        state.geometry_ = nullptr;
        state.buttons_ = nullptr;
        state.parent_.activateWindow();
        state.parent_.setFocus(Qt::OtherFocusReason);
    });
    state.setControls(state.opening_);
    state.dialog_->open();
}

#ifdef FLICK_ENABLE_TEST_HARNESS
QString Editor::settingsFilePathForTest()
{
    return QSettings().fileName();
}

DialogSnapshot Editor::testSnapshot() const
{
    const auto &state = *implementation_;
    DialogSnapshot snapshot;
    snapshot.backgroundPickerTitle = QDialog::tr("Viewing Surface Background");
    if (state.dialog_ == nullptr) {
        return snapshot;
    }
    snapshot.open = true;
    snapshot.size = state.dialog_->size();
    QList<QStringList> groups;
    for (const QGroupBox *group : state.dialog_->findChildren<QGroupBox *>()) {
        QStringList groupContents{group->title()};
        for (const QWidget *child :
             group->findChildren<QWidget *>(QString{}, Qt::FindDirectChildrenOnly)) {
            if (!child->accessibleName().isEmpty()) {
                groupContents.append(child->accessibleName());
            }
        }
        groups.append(groupContents);
    }
    const QStringList buttons = {state.buttons_->button(QDialogButtonBox::Reset)->text(),
                                 state.buttons_->button(QDialogButtonBox::Cancel)->text(),
                                 state.buttons_->button(QDialogButtonBox::Apply)->text()};
    snapshot.structureMatchesContract =
        groups == QList<QStringList>{{QDialog::tr("Navigation"), QDialog::tr("Mouse wheel action")},
                                     {QDialog::tr("Appearance"), QDialog::tr("Viewing surface background"),
                                      QDialog::tr("Show status overlay")},
                                     {QDialog::tr("Performance & Window"), QDialog::tr("Decoded cache budget"),
                                      QDialog::tr("Restore window size and position")}} &&
        buttons == QStringList{QDialog::tr("Reset Defaults"), QDialog::tr("Cancel"),
                               QDialog::tr("Apply")};
    QStringList focusOrder;
    const QWidget *widget = state.wheel_;
    do {
        if (widget->focusPolicy() != Qt::NoFocus) {
            QString name = widget->accessibleName();
            if (name.isEmpty()) {
                if (const auto *button = qobject_cast<const QAbstractButton *>(widget)) {
                    name = button->text();
                }
            }
            if (!name.isEmpty()) {
                focusOrder.append(name);
            }
        }
        widget = widget->nextInFocusChain();
    } while (widget != state.wheel_ && widget != nullptr);
    const QStringList requiredFocusOrder = {
        QDialog::tr("Mouse wheel action"), QDialog::tr("Viewing surface background"),
        QDialog::tr("Show status overlay"), QDialog::tr("Decoded cache budget"),
        QDialog::tr("Restore window size and position"), QDialog::tr("Reset Defaults"),
        QDialog::tr("Cancel"), QDialog::tr("Apply")};
    qsizetype previousPosition = -1;
    snapshot.focusOrderMatchesContract = true;
    for (const QString &name : requiredFocusOrder) {
        const qsizetype position = focusOrder.indexOf(name);
        if (position <= previousPosition) {
            snapshot.focusOrderMatchesContract = false;
            break;
        }
        previousPosition = position;
    }
    return snapshot;
}

void Editor::setTestValues(const Values &values)
{
    if (implementation_->dialog_ != nullptr) {
        implementation_->setControls(values);
    }
}

void Editor::resetForTest()
{
    if (implementation_->buttons_ != nullptr) {
        implementation_->buttons_->button(QDialogButtonBox::Reset)->click();
    }
}

void Editor::finishForTest(const bool apply)
{
    if (implementation_->buttons_ != nullptr) {
        implementation_->buttons_->button(apply ? QDialogButtonBox::Apply
                                                : QDialogButtonBox::Cancel)
            ->click();
    }
}
#endif

} // namespace Settings
