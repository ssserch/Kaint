#pragma once

#include <QWidget>

class Canvas;

enum class Unit { Pixels, Inches, Centimeters };
QString unitSuffix(Unit unit);
double pixelsPerUnit(Unit unit, double dpi);
// Formatea una medida en píxeles con la unidad indicada.
QString formatLength(double pixels, Unit unit, double dpi);

// Regla horizontal o vertical junto al lienzo: marca la posición del cursor y resalta los
// límites de la selección, en la unidad de medida actual.
class Ruler : public QWidget {
    Q_OBJECT
public:
    // Según el tema (oscura como la de paint.js con los temas oscuros y del color de la barra de
    // herramientas con los claros, como en Paint.NET), siempre del color de la barra o siempre
    // oscura con números claros. Los valores son los que se guardan en la configuración.
    enum class Style { Auto = 0, ToolBar = 1, PaintJs = 2 };

    Ruler(Qt::Orientation orientation, Canvas* canvas, QWidget* parent);
    static constexpr int kThickness = 20;
    QSize sizeHint() const override;

    static void setStyle(Style style);
    static Style style();
    // Si se dibuja oscura, como la de paint.js (con Auto, según el tema).
    static bool dark();
    // Fondo de las reglas (también el de la esquina entre las dos).
    static QColor background();

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    Qt::Orientation m_orientation;
    Canvas* m_canvas;
};
