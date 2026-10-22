#pragma once

#include "core/Document.h"

#include <QImage>
#include <QPointF>

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

// Trazo con máscara de cobertura: cada "toque" del pincel guarda el máximo en una máscara
// Alpha8 y la capa se recalcula como original + color * máscara.
class StrokeEngine {
public:
    void begin(Document* doc, LayerPtr layer, const StrokeParams& params);
    bool isActive() const { return m_doc != nullptr; }
    void moveTo(QPointF pos, qreal pressure);
    void lineTo(QPointF pos, qreal pressure);
    bool end(const QString& text, const QString& icon = {});
    QPointF lastPoint() const { return m_last; }

private:
    void segmentTo(QPointF pos, qreal pressure);
    QRect dab(QPointF center, qreal radius);
    QRect pixelLine(QPoint a, QPoint b);
    void composite(const QRect& r);
    qreal radiusFor(qreal pressure) const;

    Document* m_doc = nullptr;
    LayerPtr m_layer;
    StrokeParams m_params;
    QImage m_before;
    QImage m_mask;
    QRect m_total;
    QPointF m_last;
    QPointF m_target;
    QPointF m_raw;
    qreal m_lastPressure = 1.0;
    qreal m_carry = 0;
    QRgb m_recolorTarget = 0;
};
