#pragma once

#include "core/Document.h"

#include <QImage>
#include <QPointF>

#include <vector>

// Mask: solo la cobertura, sin tocar la capa (las herramientas de PhotoCraft que curan o quitan lo
// que se pinta encima).
enum class StrokeKind { Paint, Erase, Clone, Recolor, Mask };

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

    // Opciones avanzadas del pincel (Configuración → Herramientas), como en Ibis Paint y FireAlpaca.
    // Sin 'advanced', no cuentan.
    bool advanced = false;
    int tipShape = 0;            // 0 redonda, 1 cuadrada
    qreal angle = 0;             // grados
    qreal roundness = 1;         // 0.05..1: la punta, aplastada (elipse o rectángulo)
    qreal flow = 1;              // opacidad de cada toque
    qreal sizeJitter = 0;        // 0..1: cuánto puede encoger cada toque al azar
    qreal opacityJitter = 0;     // 0..1
    qreal angleJitter = 0;       // 0..1 (hasta media vuelta)
    qreal scatter = 0;           // 0..1: desplazamiento al azar, en diámetros
    qreal pressureGamma = 1;     // curva de la presión (<1 más sensible, >1 menos)
    qreal minSize = 0.05;        // con la presión, el tamaño más pequeño (fracción)
    bool pressureOpacity = false;  // la presión también controla la opacidad
    int stabilizer = 0;          // 0..20: corrección del trazo (cuanto más, más se queda atrás y más suave)
    // Entrada y salida (manga, como en Clip Studio e Ibis Paint): el trazo empieza y acaba en punta a lo
    // largo de estos píxeles. La salida se ve al soltar (el trazo se vuelve a dibujar).
    qreal taperIn = 0;
    qreal taperOut = 0;
    // Sin presión (ratón o dedo): la velocidad hace de presión (más rápido, más fino).
    bool speedPressure = false;

    // Simetría alrededor del centro de la imagen (como Ibis Paint): 0 ninguna, 1 espejo a izquierda y
    // derecha, 2 arriba y abajo, 3 las dos (cuatro copias), 4 radial ('symmetryCount' copias girando).
    int symmetry = 0;
    int symmetryCount = 6;
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
    // Formas rápidas: el trazo hecho hasta ahora se sustituye por esta línea quebrada (la forma
    // reconocida: línea, elipse, rectángulo, polígono), con la presión del último punto.
    void replaceWith(const QList<QPointF>& path);
    // Los sitios donde va un toque en 'p' con la simetría (el primero, 'p').
    QList<QPointF> symmetric(QPointF p) const;
    bool end(const QString& text, const QString& icon = {});
    QPointF lastPoint() const { return m_last; }

    // Por partes (los pinceles correctores, Quitar): termina el recorrido (la cobertura queda
    // completa) sin pasar al historial; después commit() o cancel().
    QRect finishPath();
    bool commit(const QString& text, const QString& icon = {});
    // Deja la capa como estaba antes del trazo, sin historial.
    void cancel();
    // 'source' (en formato de capa, del tamaño de 'r') sobre la capa de antes del trazo, a través
    // de la cobertura (y la selección), con el modo de fusión del trazo.
    void applyThroughCoverage(const QRect& r, const QImage& source);
    const QImage& before() const { return m_before; }
    const QImage& coverage() const { return m_mask; }
    QRect bounds() const { return m_total; }
    const StrokeParams& params() const { return m_params; }
    Document* document() const { return m_doc; }

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
        qreal distance = 0;  // recorrido hasta 'last' (para la entrada y la salida)
    };
    // Lo que encoge el toque a 'distance' del principio (entrada) y, con la longitud total conocida
    // (al terminar), del final (salida).
    qreal taperFactor(qreal distance) const;
    qreal m_strokeLength = -1;
    qreal m_speedPressure = 1;
    bool m_speedStarted = false;
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
    // Estabilizador: el punto suavizado y el último del ratón (al terminar, el trazo llega a él).
    QPointF m_stable;
    qreal m_stablePressure = 1;
    QPointF m_raw;
    qreal m_rawPressure = 1;
    bool m_stabilized = false;
    // La presión del toque que se dibuja (opacidad por presión).
    qreal m_dabPressure = 1;
    void lineToPoint(QPointF pos, qreal pressure);
    QRect dabAdvanced(QPointF c, qreal diameter);
    QRect dabAt(QPointF c, qreal diameter);
    QRect pixelLineAt(QPoint a, QPoint b);
    QPointF m_symCenter;
    qreal m_lastPressure = 1;
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
