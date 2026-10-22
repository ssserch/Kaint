#pragma once

#include "app/widgets/ColorWidgets.h"

#include <QWidget>

class QComboBox;
class QHBoxLayout;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QToolButton;
class QVBoxLayout;

// Ventana Colores de Paint.NET: muestras principal/secundario, rueda de tono, brillo y paleta
// (32 colores; 96 en la vista ampliada) con "Añadir color" y el menú de paletas.
class ColorsPanel : public QWidget {
    Q_OBJECT
public:
    explicit ColorsPanel(QWidget* parent = nullptr);

    bool isExpanded() const;

signals:
    void layoutChanged();

private:
    QColor current() const;
    void setCurrent(const QColor& c);
    void refresh();
    void editWithDialog(bool secondary);
    void buildPaletteMenu();
    void updateAddIcon();
    void setExpanded(bool on);

    QHBoxLayout* m_topRow;
    QVBoxLayout* m_swatchColumn;
    QComboBox* m_target;
    QPushButton* m_more;
    ColorSwatches* m_swatches;
    ColorWheel* m_wheel;
    ColorSlider* m_value;
    QWidget* m_extended;
    QLineEdit* m_hex;
    QList<std::pair<ColorSlider*, QSpinBox*>> m_channels;
    QToolButton* m_addColor;
    QToolButton* m_paletteMenu;
    PaletteWidget* m_palette;
};
