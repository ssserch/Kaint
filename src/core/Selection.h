#pragma once

#include <QImage>
#include <QLineF>
#include <QPainterPath>
#include <QRect>
#include <QTransform>
#include <QVector>

#include <memory>

enum class SelectionMode { Replace, Union, Exclude, Intersect, Xor };

// Selección del documento. Una selección vacía significa "sin selección":
// las herramientas y efectos actúan sobre todo el lienzo (como Paint.NET).
//
// Internamente puede ser vectorial (rectángulo, elipse, lazo: se guarda el
// QPainterPath para dibujar un contorno suave) o por máscara (varita mágica
// y combinaciones con ella). La máscara Alpha8 siempre está disponible y se
// calcula al construir, así que la clase es segura de leer desde hilos.
class Selection {
public:
    Selection() = default;

    static Selection fromPath(const QPainterPath& path, QSize canvas, bool antialias = true);
    static Selection fromRect(const QRect& rect, QSize canvas);
    static Selection fromMask(const QImage& mask);

    bool isEmpty() const { return !d; }
    bool isPathBased() const { return d && d->hasPath; }
    bool isRectangular() const;
    QSize canvasSize() const { return d ? d->mask.size() : QSize(); }

    const QPainterPath& path() const;
    const QImage& mask() const;
    QRect bounds() const { return d ? d->bounds : QRect(); }
    int coverage(int x, int y) const;

    // Contorno de una selección por máscara, como segmentos entre píxeles.
    const QVector<QLineF>& outline() const;

    // Geometría en el formato JSON de Paint.NET (Edición > Copiar selección):
    // {"polygonList": ["x1,y1,x2,y2,...", ...]}
    QString toJson() const;
    static Selection fromJson(const QString& json, QSize canvas, bool* ok = nullptr);

    Selection combined(const Selection& other, SelectionMode mode) const;
    Selection transformed(const QTransform& transform) const;
    Selection inverted(QSize canvas) const;

    bool operator==(const Selection& other) const { return d == other.d; }

private:
    struct Data {
        bool hasPath = false;
        QPainterPath path;
        QImage mask;
        QRect bounds;
        mutable QVector<QLineF> outline;
        mutable bool outlineValid = false;
    };
    std::shared_ptr<Data> d;
};
