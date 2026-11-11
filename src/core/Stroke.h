#pragma once

#include "core/Document.h"

#include <QImage>
#include <QPointF>

#include <vector>

enum class StrokeKind { Paint, Erase, Clone, Recolor };

struct StrokeParams {
    StrokeKind kind = StrokeKind::Paint;
    qreal diameter = 10;
    qreal hardness = 0.75;
    qreal spacing = 0.15;
    bool antialias = true;
    bool pressureSize = true;
    bool pixelExact = false;
    bool smoothing = false;
    BlendMode blend = BlendMode::Normal;
    int fill = 0;
    QColor color = Qt::black;
    QColor background = Qt::white;
    QPoint cloneOffset;
    QImage cloneSource;
    qreal tolerance = 0.5;
    bool toleranceStraight = false;
    bool recolorSampleOnce = false;
};

// Trazo con máscara de cobertura, como el pincel de Paint.NET: cada toque del pincel se acumula
// en una máscara Alpha8 ("encima": donde se solapan, el borde suave se va llenando) y la capa se
// recalcula como original + color * máscara, así un color translúcido no se oscurece dentro del
// mismo trazo. El borde del toque es un disco desenfocado en luz lineal según la dureza; con la
// trayectoria suavizada, el recorrido pasa por los puntos del ratón con una curva (Catmull-Rom
// centrípeta) y el último tramo se vuelve a dibujar hasta que llega el siguiente punto.
class StrokeEngine {
public:
    void begin(Document* doc, LayerPtr layer, const StrokeParams& params);
    bool isActive() const { return m_doc != nullptr; }
    const LayerPtr& layer() const { return m_layer; }
    void moveTo(QPointF pos, qreal pressure);
    void lineTo(QPointF pos, qreal pressure);
    bool end(const QString& text, const QString& icon = {});
    QPointF lastPoint() const { return m_last; }

private:
    struct Point {
        QPointF pos;
        qreal pressure = 1;
    };
    // Dónde va el siguiente toque a lo largo del recorrido.
    struct Walker {
        bool started = false;
        Point last;
        qreal next = 0;
    };
    void buildProfile();
    float profile(qreal u) const;
    qreal diameterFor(qreal pressure) const;
    qreal spacingFor(qreal diameter) const;
    // Puntos del tramo de m_points[i] a m_points[i + 1] (con la curva, si está suavizado).
    void segment(int i, QList<Point>& out) const;
    QRect walk(Walker& walker, const QList<Point>& path);
    QRect reach(const QList<Point>& path) const;
    QRect restoreWet();
    QRect drawWet();
    QRect dab(QPointF center, qreal diameter);
    QRect pixelLine(QPoint a, QPoint b);
    void composite(const QRect& r);

    Document* m_doc = nullptr;
    LayerPtr m_layer;
    StrokeParams m_params;
    QImage m_before;
    QImage m_mask;
    QRect m_total;
    QPointF m_last;
    QList<Point> m_points;
    int m_drySegments = 0;
    Walker m_walker;
    // El tramo provisional (el último): su zona de la máscara antes de dibujarlo.
    QRect m_wetRect;
    QImage m_wetBackup;
    // Perfil del toque en radios del pincel: cobertura de 0 a m_extent, el disco y su desenfoque.
    std::vector<float> m_profile;
    qreal m_extent = 1;
    qreal m_edge = 1;
    qreal m_sigma = 0;
    QRgb m_recolorTarget = 0;
};
