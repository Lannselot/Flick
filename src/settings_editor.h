// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QByteArray>
#include <QSize>
#include <QtTypes>

#include <functional>
#include <memory>
#include <optional>

class QWidget;

namespace Settings {

enum class WheelAction
{
    Navigate,
    Zoom
};

struct Values
{
    WheelAction wheelAction = WheelAction::Navigate;
    QColor background{QStringLiteral("#181A1B")};
    bool statusVisible = true;
    qsizetype cacheBudgetBytes = 512LL * 1024 * 1024;
    bool restoreWindowGeometry = false;
};

#ifdef FLICK_ENABLE_TEST_HARNESS
struct DialogSnapshot
{
    bool open = false;
    QSize size;
    bool structureMatchesContract = false;
    bool focusOrderMatchesContract = false;
    QString backgroundPickerTitle;
};
#endif

class Editor final
{
  public:
    using PreviewOperation = std::function<void(const Values &)>;

    explicit Editor(QWidget &parent);
    ~Editor();

    Editor(const Editor &) = delete;
    Editor &operator=(const Editor &) = delete;

    static Values defaults();
    static Values readAccepted();
    static QByteArray readWindowGeometry();
    static void persistAccepted(const Values &values);
    static void persistWheelAction(WheelAction action);
    static void persistWindowGeometry(const QByteArray &geometry, bool restorationEnabled);

    void open(const Values &opening, const Values &defaultValues, PreviewOperation preview);

#ifdef FLICK_ENABLE_TEST_HARNESS
    static QString settingsFilePathForTest();
    DialogSnapshot testSnapshot() const;
    void setTestValues(const Values &values);
    void resetForTest();
    void finishForTest(bool apply);
#endif

  private:
    class Implementation;
    std::unique_ptr<Implementation> implementation_;
};

} // namespace Settings
