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
    Ruler(Qt::Orientation orientation, Canvas* canvas, QWidget* parent);
    static constexpr int kThickness = 20;
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    Qt::Orientation m_orientation;
    Canvas* m_canvas;
};
