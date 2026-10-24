#pragma once

#include "core/BlendMode.h"
#include "core/Resample.h"
#include "core/Selection.h"

#include <QColor>
#include <QHash>
#include <QIcon>
#include <QObject>
#include <QPainterPath>
#include <QStringList>

enum class GradientType { Linear, Reflected, Diamond, Radial, Conical, SpiralClockwise, SpiralCounterClockwise };
enum class GradientRepeat { None, Wrapped, Reflected };

// Las 29 formas de la herramienta Formas de Paint.NET.
enum class ShapeType {
    Rectangle, RoundedRect, Ellipse, Diamond, Trapezoid, Parallelogram, Triangle, RightTriangle,
    Pentagon, Hexagon, Heptagon, Octagon, Star3, Star4, Star5, Star6,
    Arrow, NotchedArrow, PentagonArrow, ChevronArrow,
    RectCallout, RoundedRectCallout, EllipseCallout, CloudCallout,
    LightningBolt, CheckMark, Multiply, Gear, Heart,
    Custom,
};
enum class ShapeStyle { Outline, Fill, OutlineFill };
enum class CurveType { Straight, Spline, Bezier };
enum class LineCap { Flat, Round, Arrow, ArrowFilled };
enum class RectSelectMode { Normal, FixedRatio, FixedSize };
enum class TextAlign { Left, Center, Right };
enum class FontMetric { Points, Fixed96 };
// Modos de renderizado del texto: suave (contorno) o nítido (con hinting).
enum class TextRendering { Smooth, SharpModern, SharpClassic };
enum class PickerAfter { DoNotSwitch, PreviousTool, Pencil };
enum class RecolorSampling { Once, SecondaryColor };
// Herramientas de selección: qué hacer al terminar una selección (por defecto nada, como
// Paint.NET).
enum class AfterSelect { DoNotSwitch, MoveSelection, MovePixels };

// Opciones de las herramientas, compartidas entre todas ellas igual que en Paint.NET (el ancho
// del pincel es el mismo para pincel, goma, formas...).
struct ToolSettings {
    double brushSize = 10;
    int hardness = 75;
    int spacing = 15;
    bool smoothing = true;
    bool antialias = true;
    bool selectionAntialias = false;
    bool moveSelectionAntialias = false;
    bool pressure = true;
    BlendMode blend = BlendMode::Normal;
    int fill = 0;
    int tolerance = 50;
    bool toleranceStraight = false;
    bool floodGlobal = false;
    bool sampleImage = false;
    // Cubo de pintura: rellenar varias veces seguidas acumula los bordes suavizados.
    bool fillAccumulate = true;
    bool pickerSampleImage = false;
    SelectionMode selectionMode = SelectionMode::Replace;
    RectSelectMode rectMode = RectSelectMode::Normal;
    double fixedWidth = 4;
    double fixedHeight = 3;
    GradientType gradient = GradientType::Linear;
    GradientRepeat gradientRepeat = GradientRepeat::None;
    bool gradientTransparency = false;
    ShapeType shape = ShapeType::Rectangle;
    QString customShape;
    ShapeStyle shapeStyle = ShapeStyle::Outline;
    int cornerRadius = 20;
    Qt::PenStyle dash = Qt::SolidLine;
    CurveType curveType = CurveType::Spline;
    LineCap startCap = LineCap::Flat;
    LineCap endCap = LineCap::Flat;
    QString fontFamily;
    double fontSize = 12;
    FontMetric fontMetric = FontMetric::Points;
    TextRendering textRendering = TextRendering::Smooth;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikeout = false;
    TextAlign textAlign = TextAlign::Left;
    int pickerSize = 1;
    PickerAfter pickerAfter = PickerAfter::DoNotSwitch;
    RecolorSampling recolorSampling = RecolorSampling::SecondaryColor;
    Resample::Interpolation moveInterpolation = Resample::Interpolation::Bicubic;
    bool moveGamma = true;
    AfterSelect afterSelect = AfterSelect::DoNotSwitch;
};

// Preferencias de Configuración que usan las herramientas (las pone MainWindow al aplicar la
// configuración).
struct ToolPreferences {
    // Al arrastrar con las herramientas Mover se ocultan el contorno y los tiradores (con Mover
    // selección, solo los tiradores); queda el icono de mover, como en Paint.NET.
    bool hideBoxWhileDragging = true;
    // Los píxeles que se pegan o se mueven reemplazan a los de debajo, también donde son
    // transparentes (como en Paint.NET); si no, se mezclan con ellos.
    bool replacePixels = true;
};

class AppState : public QObject {
    Q_OBJECT
public:
    static AppState* instance();

    QColor primary() const { return m_primary; }
    QColor secondary() const { return m_secondary; }
    void setPrimary(const QColor& c);
    void setSecondary(const QColor& c);
    void swapColors();
    void resetColors();
    bool secondaryActive() const { return m_secondaryActive; }
    void setSecondaryActive(bool secondary);
    QColor activeColor() const { return m_secondaryActive ? m_secondary : m_primary; }
    void setActiveColor(const QColor& c) { m_secondaryActive ? setSecondary(c) : setPrimary(c); }
    void setInactiveColor(const QColor& c) { m_secondaryActive ? setPrimary(c) : setSecondary(c); }

    QList<QColor> palette() const { return m_palette; }
    void setPalette(const QList<QColor>& colors);
    void setPaletteColor(int index, const QColor& c);

    const ToolPreferences& preferences() const { return m_preferences; }
    void setPreferences(const ToolPreferences& preferences) { m_preferences = preferences; }

    const ToolSettings& tools() const { return m_tools; }
    template <typename F>
    void updateTools(F f)
    {
        f(m_tools);
        emit toolSettingsChanged();
    }
    void resetTools();
    ToolSettings toolDefaults() const;
    void setToolDefaults(const ToolSettings& defaults);

    QStringList recentFiles() const { return m_recent; }
    void addRecentFile(const QString& path, const QImage& thumbnail = QImage());
    void clearRecentFiles();
    QIcon recentThumbnail(const QString& path);

    void load();
    void save() const;

signals:
    void colorsChanged();
    void activeSlotChanged();
    void paletteChanged();
    void toolSettingsChanged();
    void recentFilesChanged();

private:
    AppState();
    QColor m_primary = Qt::black;
    QColor m_secondary = Qt::white;
    bool m_secondaryActive = false;
    QList<QColor> m_palette;
    ToolSettings m_tools;
    ToolPreferences m_preferences;
    QStringList m_recent;
    QHash<QString, QIcon> m_thumbs;
};

// Contornos de las formas disponibles, inscritos en 'r'.
QPainterPath shapePath(ShapeType type, const QRectF& r, double cornerRadius = 20);
QString shapeName(ShapeType type);
const QList<ShapeType>& allShapes();
// Grupos del menú de formas (Básicas, Polígonos y estrellas, Flechas, Bocadillos, Símbolos).
QList<std::pair<QString, QList<ShapeType>>> shapeGroups();
// La forma elegida en la barra (integrada o personalizada) y su nombre. Si el archivo de la
// personalizada ya no está, se usa el rectángulo.
QPainterPath toolShapePath(const ToolSettings& s, const QRectF& r);
QString toolShapeName(const ToolSettings& s);
// A / Mayús+A: forma siguiente o anterior, también entre las personalizadas.
void stepShape(ToolSettings& s, int dir);
