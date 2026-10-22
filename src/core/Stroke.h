#pragma once

#include "core/Document.h"

#include <QImage>
#include <QPointF>

enum class StrokeKind { Paint, Erase, Clone, Recolor };

struct StrokeParams {
    StrokeKind kind = StrokeKind::Paint;
    qreal diameter = 10;
    qreal hardness = 0.75;     // 0..1
    qreal spacing = 0.15;      // fracción del diámetro entre toques
    bool antialias = true;
    bool pressureSize = true;  // la presión de la tableta modula el tamaño
    bool pixelExact = false;   // lápiz: píxeles enteros sin suavizado
    bool smoothing = false;    // suaviza el recorrido del puntero
    BlendMode blend = BlendMode::Normal;
    int fill = 0;              // FillPattern
    // Color del trazo. Su opacidad también regula la goma y el tampón, como en Paint.NET.
    QColor color = Qt::black;
    QColor background = Qt::white; // segundo color de las tramas / color a reemplazar (recolorear)
    QPoint cloneOffset;        // desplazamiento origen - destino del tampón
    QImage cloneSource;        // capa de origen del tampón (vacía = la misma capa)
    qreal tolerance = 0.5;     // recolorear
    bool toleranceStraight = false;
    bool recolorSampleOnce = false; // recolorear el color del primer clic
};

// Trazo con máscara de cobertura: cada "toque" del pincel guarda el máximo
// en una máscara Alpha8 y la capa se recalcula como original + color * máscara.
// Así un color semitransparente no se acumula en las zonas solapadas.
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
    QPointF m_target; // posición suavizada
    QPointF m_raw;    // última posición real del puntero
    qreal m_lastPressure = 1.0;
    qreal m_carry = 0; // distancia acumulada desde el último toque
    QRgb m_recolorTarget = 0; // color a reemplazar, sin premultiplicar
};
