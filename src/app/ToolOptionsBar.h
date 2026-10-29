#pragma once

#include "app/AppState.h"
#include "app/tools/Tool.h"

#include <QToolBar>

#include <functional>

class BarSlider;
class ChoiceButton;
class FontChooser;
class QButtonGroup;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;
class QToolButton;
class Tool;
class ToolManager;

// Opciones de herramienta que editan los controles: las actuales (AppState, barra de opciones)
// o una copia aparte (los valores predeterminados de Configuración > Herramientas).
class ToolSettingsModel : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual const ToolSettings& tools() const = 0;
    virtual void update(const std::function<void(ToolSettings&)>& f) = 0;

signals:
    void changed();
};

// Las opciones actuales de la aplicación.
class AppToolSettings : public ToolSettingsModel {
    Q_OBJECT
public:
    explicit AppToolSettings(QObject* parent = nullptr);
    const ToolSettings& tools() const override { return AppState::instance()->tools(); }
    void update(const std::function<void(ToolSettings&)>& f) override { AppState::instance()->updateTools(f); }
};

// Una copia propia de las opciones.
class LocalToolSettings : public ToolSettingsModel {
    Q_OBJECT
public:
    explicit LocalToolSettings(const ToolSettings& initial, QObject* parent = nullptr)
        : ToolSettingsModel(parent)
        , m_tools(initial)
    {
    }
    const ToolSettings& tools() const override { return m_tools; }
    void update(const std::function<void(ToolSettings&)>& f) override
    {
        f(m_tools);
        emit changed();
    }
    void set(const ToolSettings& s)
    {
        m_tools = s;
        emit changed();
    }

private:
    ToolSettings m_tools;
};

// Crea los controles de todas las opciones de herramienta con los textos de la barra de
// Paint.NET 5 y las secciones de su Configuración > Herramientas (Pincel y relleno, Forma y
// estilo, Selección...).
class ToolOptionsEditor : public QObject {
    Q_OBJECT
public:
    struct Item {
        QWidget* widget;
        quint64 mask;
    };
    struct Group {
        QString key;
        quint64 option;
        int section;
        QList<Item> items;
    };

    ToolOptionsEditor(ToolSettingsModel* model, QWidget* parent);
    static QStringList sectionNames();
    const QList<Group>& groups() const { return m_groups; }
    ToolSettingsModel* model() const { return m_model; }
    static bool groupApplies(const QString& key, const ToolSettings& s);

private:
    void sync();
    // Las formas personalizadas al final del selector de formas.
    void addCustomShapes();
    QWidget* label(const QString& text);
    QToolButton* textToggle(const QString& text, const QString& tip, const std::function<void(QFont&)>& style);
    QWidget* steppers(QWidget* inner, const std::function<void(int)>& step);
    QComboBox* numberCombo(const QList<double>& presets, double min, double max, int decimals,
                           const std::function<void(double)>& set);
    void add(const QString& key, quint64 option, int section, const QList<QWidget*>& widgets);
    void addItems(const QString& key, quint64 option, int section, const QList<Item>& items);

    ToolSettingsModel* m_model;
    QWidget* m_parent;
    QList<Group> m_groups;

    QButtonGroup* m_selectionModes;
    ChoiceButton* m_rectMode;
    QDoubleSpinBox* m_fixedWidth;
    QDoubleSpinBox* m_fixedHeight;
    QLabel* m_fixedWidthUnit;
    QLabel* m_fixedHeightUnit;
    ChoiceButton* m_afterSelect;
    QComboBox* m_size;
    BarSlider* m_hardness;
    BarSlider* m_spacing;
    ChoiceButton* m_smoothing;
    ChoiceButton* m_shape;
    ChoiceButton* m_shapeStyle;
    QSpinBox* m_cornerRadius;
    QButtonGroup* m_curves;
    ChoiceButton* m_startCap;
    ChoiceButton* m_endCap;
    ChoiceButton* m_dash;
    QButtonGroup* m_gradientTypes;
    ChoiceButton* m_gradientMode;
    ChoiceButton* m_gradientRepeat;
    FontChooser* m_font;
    QComboBox* m_fontSize;
    ChoiceButton* m_fontMetric;
    QToolButton* m_bold;
    QToolButton* m_italic;
    QToolButton* m_underline;
    QToolButton* m_strikeout;
    QButtonGroup* m_align;
    ChoiceButton* m_textRendering;
    BarSlider* m_tolerance;
    ChoiceButton* m_toleranceAlpha;
    ChoiceButton* m_accumulate;
    ChoiceButton* m_flood;
    ChoiceButton* m_sampling;
    ChoiceButton* m_pickerSampling;
    ChoiceButton* m_pickerSize;
    ChoiceButton* m_pickerAfter;
    QButtonGroup* m_recolorSampling;
    ChoiceButton* m_interpolation;
    ChoiceButton* m_gamma;
    ChoiceButton* m_antialias;
    ChoiceButton* m_moveQuality = nullptr;
    ChoiceButton* m_selectionQuality;
    QToolButton* m_pressure;
    ChoiceButton* m_blend;
    ChoiceButton* m_fill;
};

// Segunda fila de la barra de herramientas: "Herramienta: [icono]" y las opciones que use la
// herramienta activa, como en Paint.NET, con el botón "Acabado" al final.
class ToolOptionsBar : public QToolBar {
    Q_OBJECT
public:
    explicit ToolOptionsBar(ToolManager* manager, QWidget* parent = nullptr);

    // Orden de los grupos de una herramienta en la barra de Paint.NET 5 ("|" = separador).
    // Vacío: el orden de las secciones.
    static QStringList order(ToolId id);

private:
    void onToolChanged(Tool* tool);
    void updateDependent();

    ToolManager* m_manager;
    ToolOptionsEditor* m_editor;
    QToolButton* m_toolButton;
    struct Shown {
        QAction* action;
        quint64 mask;
    };
    struct BarGroup {
        QString key;
        QAction* separator;
        QList<Shown> items;
        bool shown = false;
        bool separatorWanted = false;
    };
    QList<BarGroup> m_groups;
    QAction* m_finishSeparator = nullptr;
    QAction* m_finish = nullptr;
};

// Todas las opciones, por secciones (Configuración > Herramientas).
class ToolOptionsPanel : public QWidget {
    Q_OBJECT
public:
    explicit ToolOptionsPanel(ToolSettingsModel* model, QWidget* parent = nullptr);
};
