#pragma once

#include <QImage>
#include <QLineF>
#include <QPainterPath>
#include <QRect>
#include <QTransform>
#include <QVector>

#include <memory>

enum class SelectionMode { Replace, Union, Exclude, Intersect, Xor };

// Selección del documento.
class Selection {
public:
    Selection() = default;

    static Selection fromPath(const QPainterPath& path, QSize canvas, bool antialias = true);
    static Selection fromRect(const QRect& rect, QSize canvas);
    static Selection fromMask(const QImage& mask);
    // Máscara con el contorno suavizado (escalones como diagonales), como el lazo con antidentado.
    static Selection fromMaskSmooth(const QImage& mask);
    // Ese contorno suavizado, en coordenadas de la imagen.
    static QPainterPath smoothOutline(const QImage& mask);

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

    // Geometría en el formato JSON de Paint.NET (Edición > Copiar selección): {"polygonList":
    // ["x1,y1,x2,y2,...", ...]}
    QString toJson() const;
    static Selection fromJson(const QString& json, QSize canvas, bool* ok = nullptr);

    Selection combined(const Selection& other, SelectionMode mode) const;
    // Con "smooth" en falso, girar o escalar una máscara no suaviza sus bordes.
    Selection transformed(const QTransform& transform, bool smooth = true) const;
    Selection inverted(QSize canvas) const;
    // Amplía (radio positivo) o reduce (negativo) la selección tantos píxeles, con esquinas
    // redondeadas como en GIMP.
    Selection grown(double radius) const;
    // Difumina el borde (desenfoque gaussiano de la máscara).
    Selection feathered(double radius) const;

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
