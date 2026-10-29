#pragma once

#include "app/Workspace.h"
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
// (32 colores; 96 en la vista ampliada) con "Añadir color" y el menú de paletas. Como en
// Paint.NET, solo cambia de tamaño manteniendo la proporción, y "Más" conserva la escala.
class ColorsPanel : public QWidget, public PanelContent {
    Q_OBJECT
public:
    explicit ColorsPanel(QWidget* parent = nullptr);

    bool isExpanded() const;
    // Tamaño actual respecto al recomendado de la vista (compacta o ampliada).
    double scale() const;
    QSize sizeForScale(double scale) const;

    QSize fitSize(QSize proposed, Qt::Orientations changed) const override;
    QVariant layoutState() const override { return scale(); }
    QSize sizeForLayout(const QVariant& state) const override;

signals:
    // La vista cambió (Más/Menos): el panel debe tomar sizeForScale(scale).
    void layoutChanged(double scale);

private:
    QColor current() const;
    void setCurrent(const QColor& c);
    void refresh();
    void editWithDialog(bool secondary);
    void buildPaletteMenu();
    void updateAddIcon();
    void setExpanded(bool on);
    void applyLook();
    double minimumScale() const;

    QHBoxLayout* m_topRow;
    QVBoxLayout* m_swatchColumn;
    QVBoxLayout* m_wheelColumn;
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
