#include "app/ToolOptionsBar.h"

#include "app/AppState.h"
#include "app/Icons.h"
#include "app/tools/ToolManager.h"
#include "app/widgets/BarSlider.h"
#include "app/widgets/ChoiceButton.h"
#include "app/widgets/FlowLayout.h"
#include "app/widgets/FontChooser.h"
#include "core/CustomShapes.h"
#include "core/FillPattern.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDoubleValidator>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMenu>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

// Anchos de pincel predefinidos del desplegable de Paint.NET.
const QList<double>& brushPresets()
{
    static const QList<double> list = {1,  2,  3,  4,  5,  6,  7,  8,  9,   10,  11,  12,  13,  14,  15,  20,  25,
                                       30, 35, 40, 45, 50, 55, 60, 65, 70,  75,  80,  85,  90,  95,  100, 125, 150,
                                       175, 200, 225, 250, 275, 300, 350, 400, 450, 500, 750, 1000, 1500, 2000};
    return list;
}

const QList<double>& fontPresets()
{
    static const QList<double> list = {8, 9, 10, 11, 12, 14, 16, 18, 20, 22, 24, 26, 28, 36, 48, 72, 144, 288};
    return list;
}

// Siguiente valor de una lista de valores predefinidos (botones − y +).
double stepPreset(double value, int dir, const QList<double>& presets)
{
    if (dir > 0) {
        for (double p : presets)
            if (p > value + 1e-9)
                return p;
        return presets.isEmpty() ? value : presets.last();
    }
    for (auto it = presets.crbegin(); it != presets.crend(); ++it)
        if (*it < value - 1e-9)
            return *it;
    return presets.isEmpty() ? value : presets.first();
}

QString numberText(double v, int decimals)
{
    return decimals > 0 && v != std::floor(v) ? QLocale().toString(v, 'f', decimals) : QString::number(qint64(std::lround(v)));
}

} // namespace

// ---------------------------------------------------------------------------

AppToolSettings::AppToolSettings(QObject* parent)
    : ToolSettingsModel(parent)
{
    connect(AppState::instance(), &AppState::toolSettingsChanged, this, &ToolSettingsModel::changed);
}

// ---------------------------------------------------------------------------

QStringList ToolOptionsEditor::sectionNames()
{
    // Las secciones de Configuración > Herramientas de Paint.NET 5.
    return {QObject::tr("Pincel y relleno"),
            QObject::tr("Forma y estilo"),
            QObject::tr("Selección"),
            QObject::tr("Mover píxeles seleccionados"),
            QObject::tr("Texto"),
            QObject::tr("Degradado"),
            QObject::tr("Varita mágica, cubo de pintura y volver a colorear"),
            QObject::tr("Selector de color"),
            QObject::tr("Muestreo"),
            QObject::tr("Opciones")};
}

namespace {
enum Section { Brush, Shape, Select, Move, Text, Gradient, Flood, Picker, Sampling, Options };

// Valores del desplegable de formas a partir de los que van las personalizadas.
constexpr int kCustomShapeBase = 1000;
}

bool ToolOptionsEditor::groupApplies(const QString& key, const ToolSettings& s)
{
    if (key == QLatin1String("rectSize"))
        return s.rectMode != RectSelectMode::Normal;
    if (key == QLatin1String("corner"))
        return s.shape == ShapeType::RoundedRect || s.shape == ShapeType::RoundedRectCallout;
    return true;
}

QWidget* ToolOptionsEditor::label(const QString& text)
{
    auto* l = new QLabel(text, m_parent);
    l->setContentsMargins(4, 0, 2, 0);
    return l;
}

QToolButton* ToolOptionsEditor::textToggle(const QString& text, const QString& tip, const std::function<void(QFont&)>& style)
{
    // N, K, S y abc con su estilo, como los botones de Paint.NET en español.
    auto* b = new QToolButton(m_parent);
    b->setText(text);
    QFont f = b->font();
    style(f);
    b->setFont(f);
    b->setToolTip(tip);
    b->setCheckable(true);
    b->setAutoRaise(true);
    return b;
}

QWidget* ToolOptionsEditor::steppers(QWidget* inner, const std::function<void(int)>& step)
{
    auto* w = new QWidget(m_parent);
    auto* l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(1);
    auto button = [&](int dir, const QString& tip) {
        auto* b = new QToolButton(w);
        b->setIcon(Icons::stepper(dir > 0));
        b->setAutoRaise(true);
        b->setAutoRepeat(true);
        b->setToolTip(tip);
        b->setIconSize(QSize(12, 12));
        connect(b, &QToolButton::clicked, this, [step, dir] { step(dir); });
        return b;
    };
    l->addWidget(button(-1, tr("Reducir")));
    l->addWidget(inner);
    l->addWidget(button(+1, tr("Aumentar")));
    return w;
}

QComboBox* ToolOptionsEditor::numberCombo(const QList<double>& presets, double min, double max, int decimals,
                                          const std::function<void(double)>& set)
{
    // Como en Paint.NET: se escribe el número o se elige de la lista (sin flechas de caja
    // numérica).
    auto* c = new QComboBox(m_parent);
    c->setEditable(true);
    c->setInsertPolicy(QComboBox::NoInsert);
    for (double v : presets)
        c->addItem(numberText(v, decimals), v);
    auto* validator = new QDoubleValidator(min, max, decimals, c);
    validator->setNotation(QDoubleValidator::StandardNotation);
    c->lineEdit()->setValidator(validator);
    c->setMaxVisibleItems(16);
    c->setMinimumContentsLength(4);
    c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    auto apply = [c, min, max, set](const QString& text) {
        bool ok = false;
        const double v = QLocale().toDouble(text, &ok);
        const double w = ok ? v : text.toDouble(&ok);
        if (ok && w >= min && w <= max)
            set(w);
    };
    connect(c->lineEdit(), &QLineEdit::textEdited, this, apply);
    connect(c, &QComboBox::activated, this, [c, set](int i) { set(c->itemData(i).toDouble()); });
    return c;
}

void ToolOptionsEditor::add(const QString& key, quint32 option, int section, const QList<QWidget*>& widgets)
{
    QList<Item> items;
    for (QWidget* w : widgets)
        items.append({w, option});
    addItems(key, option, section, items);
}

void ToolOptionsEditor::addItems(const QString& key, quint32 option, int section, const QList<Item>& items)
{
    m_groups.append({key, option, section, items});
}

ToolOptionsEditor::ToolOptionsEditor(ToolSettingsModel* model, QWidget* parent)
    : QObject(parent)
    , m_model(model)
    , m_parent(parent)
{
    auto update = [model](auto f) { model->update(f); };
    auto buttonRow = [parent] {
        auto* box = new QWidget(parent);
        auto* l = new QHBoxLayout(box);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(0);
        return std::pair{box, l};
    };

    // --- Pincel y relleno -------------------------------------------------
    m_size = numberCombo(brushPresets(), 1, 2000, 0, [update](double v) { update([v](ToolSettings& s) { s.brushSize = v; }); });
    m_size->setToolTip(tr("Tamaño del pincel ([ y ] para cambiarlo)"));
    add(QStringLiteral("size"), Tool::OptBrushSize, Brush,
        {label(tr("Tamaño de pincel:")), steppers(m_size, [this, update](int dir) {
             const double v = stepPreset(m_model->tools().brushSize, dir, brushPresets());
             update([v](ToolSettings& s) { s.brushSize = v; });
         })});

    m_hardness = new BarSlider(0, 100, QStringLiteral("%"), parent);
    m_hardness->setToolTip(tr("Dureza del borde del pincel"));
    connect(m_hardness, &BarSlider::valueChanged, this, [update](int v) { update([v](ToolSettings& s) { s.hardness = v; }); });
    add(QStringLiteral("hardness"), Tool::OptHardness, Brush,
        {label(tr("Dureza:")), steppers(m_hardness, [this](int dir) { m_hardness->setValue(m_hardness->value() + dir); })});

    m_spacing = new BarSlider(1, 100, QStringLiteral("%"), parent);
    m_spacing->setToolTip(tr("Distancia entre toques del pincel, en % del ancho"));
    connect(m_spacing, &BarSlider::valueChanged, this, [update](int v) { update([v](ToolSettings& s) { s.spacing = v; }); });
    add(QStringLiteral("spacing"), Tool::OptSpacing, Brush,
        {label(tr("Espaciado:")), steppers(m_spacing, [this](int dir) { m_spacing->setValue(m_spacing->value() + dir); })});

    m_fill = new ChoiceButton(tr("Relleno"), parent);
    for (int i = 0; i < FillPattern::count(); ++i)
        m_fill->addChoice(Icons::fillPattern(i), FillPattern::name(i), i);
    m_fill->setShowText(true);
    m_fill->onChosen = [update](int v) { update([v](ToolSettings& s) { s.fill = v; }); };
    add(QStringLiteral("fill"), Tool::OptFill, Brush, {label(tr("Relleno:")), m_fill});

    // --- Forma y estilo ---------------------------------------------------
    m_shape = new ChoiceButton(tr("Forma (A / Mayús+A)"), parent);
    for (const auto& [group, shapes] : shapeGroups()) {
        m_shape->addSection(group);
        for (ShapeType t : shapes)
            m_shape->addChoice(Icons::shape(t), shapeName(t), int(t));
    }
    const QList<CustomShapes::Shape>& custom = CustomShapes::all();
    if (!custom.isEmpty())
        m_shape->addSection(tr("Formas personalizadas"));
    for (int i = 0; i < int(custom.size()); ++i)
        m_shape->addChoice(Icons::customShape(custom[i].path), custom[i].name, kCustomShapeBase + i);
    m_shape->setShowText(true);
    m_shape->onChosen = [update](int v) {
        update([v](ToolSettings& s) {
            if (v >= kCustomShapeBase) {
                s.shape = ShapeType::Custom;
                s.customShape = CustomShapes::all().value(v - kCustomShapeBase).file;
            } else {
                s.shape = ShapeType(v);
            }
        });
    };
    add(QStringLiteral("shape"), Tool::OptShape, Shape, {m_shape});

    m_shapeStyle = new ChoiceButton(tr("Tipo de dibujo"), parent);
    m_shapeStyle->addChoice(Icons::shapeStyle(ShapeStyle::Outline), tr("Dibujar el contorno de la forma"), int(ShapeStyle::Outline));
    m_shapeStyle->addChoice(Icons::shapeStyle(ShapeStyle::Fill), tr("Dibujar forma rellena"), int(ShapeStyle::Fill));
    m_shapeStyle->addChoice(Icons::shapeStyle(ShapeStyle::OutlineFill), tr("Dibuja una forma rellena con contorno"),
                            int(ShapeStyle::OutlineFill));
    m_shapeStyle->onChosen = [update](int v) { update([v](ToolSettings& s) { s.shapeStyle = ShapeStyle(v); }); };
    add(QStringLiteral("shapeStyle"), Tool::OptShapeStyle, Shape, {m_shapeStyle});

    m_cornerRadius = new QSpinBox(parent);
    m_cornerRadius->setRange(0, 1000);
    m_cornerRadius->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_cornerRadius->setToolTip(tr("Radio de las esquinas del rectángulo redondeado"));
    connect(m_cornerRadius, &QSpinBox::valueChanged, this,
            [update](int v) { update([v](ToolSettings& s) { s.cornerRadius = v; }); });
    add(QStringLiteral("corner"), Tool::OptCornerRadius, Shape,
        {label(tr("Radio:")),
         steppers(m_cornerRadius, [this](int dir) { m_cornerRadius->setValue(m_cornerRadius->value() + dir); })});

    // Tipo de curva: tres botones, como en Paint.NET.
    auto [curveBox, curveLayout] = buttonRow();
    m_curves = new QButtonGroup(this);
    const QList<std::pair<CurveType, QString>> curves{{CurveType::Straight, tr("Directo")},
                                                      {CurveType::Spline, tr("Curva segmentaria")},
                                                      {CurveType::Bezier, tr("Bézier")}};
    for (const auto& [type, name] : curves) {
        auto* b = new QToolButton(curveBox);
        b->setIcon(Icons::curveType(type));
        b->setToolTip(name);
        b->setCheckable(true);
        b->setAutoRaise(true);
        m_curves->addButton(b, int(type));
        curveLayout->addWidget(b);
    }
    connect(m_curves, &QButtonGroup::idClicked, this, [update](int id) { update([id](ToolSettings& s) { s.curveType = CurveType(id); }); });
    add(QStringLiteral("curve"), Tool::OptCurve, Shape, {curveBox});

    // Estilo: remate inicial, trazo y remate final (las formas solo usan el trazo).
    const QList<std::pair<LineCap, QString>> caps{{LineCap::Flat, tr("Línea")},
                                                  {LineCap::Arrow, tr("Flecha")},
                                                  {LineCap::ArrowFilled, tr("Flecha rellena")},
                                                  {LineCap::Round, tr("Redondeado")}};
    m_startCap = new ChoiceButton(tr("Inicio de la línea ( , )"), parent);
    m_endCap = new ChoiceButton(tr("Final de la línea ( / )"), parent);
    for (const auto& [cap, name] : caps) {
        m_startCap->addChoice(Icons::lineCap(cap, true), name, int(cap));
        m_endCap->addChoice(Icons::lineCap(cap, false), name, int(cap));
    }
    m_startCap->onChosen = [update](int v) { update([v](ToolSettings& s) { s.startCap = LineCap(v); }); };
    m_endCap->onChosen = [update](int v) { update([v](ToolSettings& s) { s.endCap = LineCap(v); }); };
    m_dash = new ChoiceButton(tr("Estilo de guion ( . )"), parent);
    const QList<std::pair<Qt::PenStyle, QString>> dashes{{Qt::SolidLine, tr("Sólido")},
                                                         {Qt::DashLine, tr("Guiones")},
                                                         {Qt::DotLine, tr("Punteado")},
                                                         {Qt::DashDotLine, tr("Guion, punto")},
                                                         {Qt::DashDotDotLine, tr("Guion, punto, punto")}};
    for (const auto& [style, name] : dashes)
        m_dash->addChoice(Icons::dash(style), name, int(style));
    m_dash->onChosen = [update](int v) { update([v](ToolSettings& s) { s.dash = Qt::PenStyle(v); }); };
    addItems(QStringLiteral("style"), Tool::OptDash, Shape,
        QList<Item>{{label(tr("Estilo:")), Tool::OptDash | Tool::OptLineCaps},
                    {m_startCap, Tool::OptLineCaps},
                    {m_dash, Tool::OptDash},
                    {m_endCap, Tool::OptLineCaps}});

    // --- Selección -------------------------------------------------------- Modo de
    // selección: cinco botones (Paint.NET 5), con las combinaciones de teclas.
    auto [modesBox, modesLayout] = buttonRow();
    m_selectionModes = new QButtonGroup(this);
    const QList<std::pair<SelectionMode, QString>> modes{
        {SelectionMode::Replace, tr("Reemplazar")},
        {SelectionMode::Union, tr("Sumar (unión) · Ctrl + clic izquierdo")},
        {SelectionMode::Exclude, tr("Excluir · Alt + clic izquierdo")},
        {SelectionMode::Intersect, tr("Intersecar · Alt + clic derecho")},
        {SelectionMode::Xor, tr("Invertir («xor») · Ctrl + clic derecho")}};
    for (const auto& [mode, name] : modes) {
        auto* b = new QToolButton(modesBox);
        b->setIcon(Icons::selectionMode(mode));
        b->setToolTip(tr("Modo de selección: %1").arg(name));
        b->setCheckable(true);
        b->setAutoRaise(true);
        m_selectionModes->addButton(b, int(mode));
        modesLayout->addWidget(b);
    }
    connect(m_selectionModes, &QButtonGroup::idClicked, this,
            [update](int id) { update([id](ToolSettings& s) { s.selectionMode = SelectionMode(id); }); });
    add(QStringLiteral("selMode"), Tool::OptSelectionMode, Select, {modesBox});

    m_rectMode = new ChoiceButton(tr("Modo de selección rectangular"), parent);
    m_rectMode->addChoice(Icons::rectSelectMode(RectSelectMode::Normal), tr("Cualquier tamaño"), int(RectSelectMode::Normal));
    m_rectMode->addChoice(Icons::rectSelectMode(RectSelectMode::FixedRatio), tr("Proporción"), int(RectSelectMode::FixedRatio));
    m_rectMode->addChoice(Icons::rectSelectMode(RectSelectMode::FixedSize), tr("Tamaño fijo"), int(RectSelectMode::FixedSize));
    m_rectMode->setShowText(true);
    m_rectMode->onChosen = [update](int v) { update([v](ToolSettings& s) { s.rectMode = RectSelectMode(v); }); };
    add(QStringLiteral("rectMode"), Tool::OptRectMode, Select, {m_rectMode});
    auto makeFixed = [this, parent](double ToolSettings::*field) {
        auto* box = new QDoubleSpinBox(parent);
        box->setRange(0.01, 65535);
        box->setDecimals(2);
        box->setButtonSymbols(QAbstractSpinBox::NoButtons);
        box->setMinimumWidth(64);
        connect(box, &QDoubleSpinBox::valueChanged, this, [this, field](double v) {
            m_model->update([field, v](ToolSettings& s) { s.*field = v; });
        });
        return box;
    };
    m_fixedWidth = makeFixed(&ToolSettings::fixedWidth);
    m_fixedHeight = makeFixed(&ToolSettings::fixedHeight);
    m_fixedWidth->setToolTip(tr("Ancho"));
    m_fixedHeight->setToolTip(tr("Alto"));
    add(QStringLiteral("rectSize"), Tool::OptRectMode, Select,
        {label(tr("Ancho:")), m_fixedWidth, label(tr("Alto:")), m_fixedHeight});

    m_afterSelect = new ChoiceButton(tr("Después de seleccionar"), parent);
    m_afterSelect->addChoice(Icons::afterSelect(AfterSelect::DoNotSwitch), tr("No cambiar de herramienta"),
                             int(AfterSelect::DoNotSwitch));
    m_afterSelect->addChoice(Icons::afterSelect(AfterSelect::MoveSelection), tr("Cambiar a Mover selección"),
                             int(AfterSelect::MoveSelection));
    m_afterSelect->addChoice(Icons::afterSelect(AfterSelect::MovePixels), tr("Cambiar a Mover píxeles seleccionados"),
                             int(AfterSelect::MovePixels));
    m_afterSelect->setShowText(true);
    m_afterSelect->onChosen = [update](int v) { update([v](ToolSettings& s) { s.afterSelect = AfterSelect(v); }); };
    add(QStringLiteral("afterSelect"), Tool::OptAfterSelect, Select, {label(tr("Después de seleccionar:")), m_afterSelect});

    // --- Mover píxeles seleccionados --------------------------------------
    m_interpolation = new ChoiceButton(tr("Muestreo"), parent);
    for (auto i : {Resample::Interpolation::NearestNeighbor, Resample::Interpolation::Bilinear,
                   Resample::Interpolation::Bicubic})
        m_interpolation->addChoice(QIcon(), Resample::name(i), int(i));
    m_interpolation->setShowText(true);
    m_interpolation->onChosen = [update](int v) {
        update([v](ToolSettings& s) { s.moveInterpolation = Resample::Interpolation(v); });
    };
    add(QStringLiteral("interp"), Tool::OptInterpolation, Move, {label(tr("Muestreo:")), m_interpolation});

    m_gamma = new ChoiceButton(tr("Gamma"), parent);
    m_gamma->addChoice(Icons::gamma(true), tr("Gamma corregido"), 1,
                       tr("El renderizado tendrá en cuenta la gamma. Se conservará la luminancia."));
    m_gamma->addChoice(Icons::gamma(false), tr("Ignorar gama"), 0,
                       tr("El renderizado ignorará gamma. La luminancia no se conservará correctamente."));
    m_gamma->onChosen = [update](int v) { update([v](ToolSettings& s) { s.moveGamma = v == 1; }); };
    add(QStringLiteral("gamma"), Tool::OptGamma, Move, {m_gamma});

    // --- Texto ------------------------------------------------------------
    m_font = new FontChooser(parent);
    m_font->setMaximumWidth(190);
    connect(m_font, &QFontComboBox::currentFontChanged, this, [update](const QFont& f) {
        const QString family = f.family();
        update([family](ToolSettings& s) { s.fontFamily = family; });
    });
    m_fontSize = numberCombo(fontPresets(), 1, 2000, 1, [update](double v) { update([v](ToolSettings& s) { s.fontSize = v; }); });
    m_fontSize->setToolTip(tr("Tamaño de letra"));
    m_fontMetric = new ChoiceButton(tr("Unidad del tamaño de letra"), parent);
    m_fontMetric->addChoice(Icons::fontMetric(FontMetric::Points), tr("Puntos (PPP de imagen)"), int(FontMetric::Points),
                            tr("La conversión del tamaño de letra a píxeles utiliza puntos estándar de la industria "
                               "definidos por los metadatos de resolución (PPP) de la imagen."));
    m_fontMetric->addChoice(Icons::fontMetric(FontMetric::Fixed96), tr("Fijo (96 PPP)"), int(FontMetric::Fixed96),
                            tr("La conversión del tamaño de letra a píxeles utiliza un valor fijo de 96 PPP."));
    m_fontMetric->onChosen = [update](int v) { update([v](ToolSettings& s) { s.fontMetric = FontMetric(v); }); };
    add(QStringLiteral("text.font"), Tool::OptText, Text,
        {label(tr("Tipo de letra:")), m_font, steppers(m_fontSize, [this, update](int dir) {
             const double v = stepPreset(m_model->tools().fontSize, dir, fontPresets());
             update([v](ToolSettings& s) { s.fontSize = v; });
         }),
         m_fontMetric});

    m_bold = textToggle(tr("N"), tr("Negrita"), [](QFont& f) { f.setBold(true); });
    m_italic = textToggle(tr("K"), tr("Cursiva"), [](QFont& f) { f.setItalic(true); });
    m_underline = textToggle(tr("S"), tr("Subrayado"), [](QFont& f) { f.setUnderline(true); });
    m_strikeout = textToggle(tr("abc"), tr("Tachado"), [](QFont& f) { f.setStrikeOut(true); });
    connect(m_bold, &QToolButton::toggled, this, [update](bool on) { update([on](ToolSettings& s) { s.bold = on; }); });
    connect(m_italic, &QToolButton::toggled, this, [update](bool on) { update([on](ToolSettings& s) { s.italic = on; }); });
    connect(m_underline, &QToolButton::toggled, this,
            [update](bool on) { update([on](ToolSettings& s) { s.underline = on; }); });
    connect(m_strikeout, &QToolButton::toggled, this,
            [update](bool on) { update([on](ToolSettings& s) { s.strikeout = on; }); });
    m_textRendering = new ChoiceButton(tr("Modo de representación del texto"), parent);
    m_textRendering->addChoice(QIcon(), tr("Suave"), int(TextRendering::Smooth));
    m_textRendering->addChoice(QIcon(), tr("Nítido (moderno)"), int(TextRendering::SharpModern));
    m_textRendering->addChoice(QIcon(), tr("Nítido (clásico)"), int(TextRendering::SharpClassic));
    m_textRendering->setShowText(true);
    m_textRendering->onChosen = [update](int v) { update([v](ToolSettings& s) { s.textRendering = TextRendering(v); }); };
    add(QStringLiteral("text.style"), Tool::OptText, Text, {m_bold, m_italic, m_underline, m_strikeout, m_textRendering});

    auto [alignBox, alignLayout] = buttonRow();
    m_align = new QButtonGroup(this);
    const QList<std::pair<TextAlign, QString>> aligns{{TextAlign::Left, tr("Alinear a la izquierda")},
                                                      {TextAlign::Center, tr("Centrar")},
                                                      {TextAlign::Right, tr("Alinear a la derecha")}};
    for (const auto& [a, name] : aligns) {
        auto* b = new QToolButton(alignBox);
        b->setIcon(Icons::textAlign(a));
        b->setToolTip(name);
        b->setCheckable(true);
        b->setAutoRaise(true);
        m_align->addButton(b, int(a));
        alignLayout->addWidget(b);
    }
    connect(m_align, &QButtonGroup::idClicked, this,
            [update](int id) { update([id](ToolSettings& s) { s.textAlign = TextAlign(id); }); });
    add(QStringLiteral("text.align"), Tool::OptText, Text, {alignBox});

    // --- Degradado --------------------------------------------------------
    auto [gradientBox, gradientLayout] = buttonRow();
    m_gradientTypes = new QButtonGroup(this);
    const QList<std::pair<GradientType, QString>> gradients{
        {GradientType::Linear, tr("Lineal")},
        {GradientType::Reflected, tr("Lineal (reflejado)")},
        {GradientType::Diamond, tr("Rombo")},
        {GradientType::Radial, tr("Radial")},
        {GradientType::Conical, tr("Cónico")},
        {GradientType::SpiralClockwise, tr("Espiral (en el sentido de las agujas del reloj)")},
        {GradientType::SpiralCounterClockwise, tr("Espiral (en sentido contrario a las agujas del reloj)")}};
    for (const auto& [type, name] : gradients) {
        auto* b = new QToolButton(gradientBox);
        b->setIcon(Icons::gradient(type));
        b->setToolTip(name);
        b->setCheckable(true);
        b->setAutoRaise(true);
        m_gradientTypes->addButton(b, int(type));
        gradientLayout->addWidget(b);
    }
    connect(m_gradientTypes, &QButtonGroup::idClicked, this,
            [update](int id) { update([id](ToolSettings& s) { s.gradient = GradientType(id); }); });
    add(QStringLiteral("gradient.types"), Tool::OptGradient, Gradient, {gradientBox});

    m_gradientMode = new ChoiceButton(tr("Modo"), parent);
    m_gradientMode->addChoice(Icons::theme({"edit-opacity", "transform-shear-left", "fill-color"}),
                              tr("Modo de transparencia"), 1);
    m_gradientMode->addChoice(Icons::theme({"color-management", "fill-color"}), tr("Modo de color"), 0);
    m_gradientMode->onChosen = [update](int v) { update([v](ToolSettings& s) { s.gradientTransparency = v == 1; }); };
    add(QStringLiteral("gradient.mode"), Tool::OptGradient, Gradient, {m_gradientMode});

    m_gradientRepeat = new ChoiceButton(tr("Repetición"), parent);
    m_gradientRepeat->addChoice(Icons::gradientRepeat(GradientRepeat::None), tr("Sin repetición"), int(GradientRepeat::None));
    m_gradientRepeat->addChoice(Icons::gradientRepeat(GradientRepeat::Wrapped), tr("Repetir envuelto"),
                                int(GradientRepeat::Wrapped));
    m_gradientRepeat->addChoice(Icons::gradientRepeat(GradientRepeat::Reflected), tr("Repetición reflejada"),
                                int(GradientRepeat::Reflected));
    m_gradientRepeat->setShowText(true);
    m_gradientRepeat->onChosen = [update](int v) { update([v](ToolSettings& s) { s.gradientRepeat = GradientRepeat(v); }); };
    add(QStringLiteral("gradient.repeat"), Tool::OptGradientRepeat, Gradient, {m_gradientRepeat});

    // --- Varita mágica, cubo de pintura y volver a colorear ---------------
    m_flood = new ChoiceButton(tr("Modo de saturación"), parent);
    m_flood->addChoice(Icons::floodMode(false), tr("Contiguo"), 0);
    m_flood->addChoice(Icons::floodMode(true), tr("Global"), 1);
    m_flood->onChosen = [update](int v) { update([v](ToolSettings& s) { s.floodGlobal = v == 1; }); };
    add(QStringLiteral("flood"), Tool::OptFloodMode, Flood, {label(tr("Modo de saturación")), m_flood});

    m_tolerance = new BarSlider(0, 100, QStringLiteral("%"), parent);
    connect(m_tolerance, &BarSlider::valueChanged, this, [update](int v) { update([v](ToolSettings& s) { s.tolerance = v; }); });
    add(QStringLiteral("tolerance"), Tool::OptTolerance, Flood,
        {label(tr("Tolerancia:")), steppers(m_tolerance, [this](int dir) { m_tolerance->setValue(m_tolerance->value() + dir); })});

    m_toleranceAlpha = new ChoiceButton(tr("Modo alfa de la tolerancia"), parent);
    m_toleranceAlpha->addChoice(Icons::toleranceAlpha(false), tr("Premultiplicado"), 0);
    m_toleranceAlpha->addChoice(Icons::toleranceAlpha(true), tr("Directo"), 1);
    m_toleranceAlpha->onChosen = [update](int v) { update([v](ToolSettings& s) { s.toleranceStraight = v == 1; }); };
    add(QStringLiteral("tolAlpha"), Tool::OptToleranceAlpha, Flood, {m_toleranceAlpha});

    // Volver a colorear: dos botones (una vez / color secundario), como Paint.NET.
    auto [recolorBox, recolorLayout] = buttonRow();
    m_recolorSampling = new QButtonGroup(this);
    const QList<std::tuple<RecolorSampling, QString, QString>> recolor{
        {RecolorSampling::Once, tr("Muestreo: una vez"),
         tr("Utiliza el color en el que haga clic en el comienzo de un trazo de pincel.")},
        {RecolorSampling::SecondaryColor, tr("Muestreo: Color secundario"),
         tr("Utiliza el Color secundario en la ventana de colores.")}};
    for (const auto& [mode, title, text] : recolor) {
        auto* b = new QToolButton(recolorBox);
        b->setIcon(Icons::recolorSampling(mode));
        b->setToolTip(QStringLiteral("<b>%1</b><br>%2").arg(title.toHtmlEscaped(), text.toHtmlEscaped()));
        b->setCheckable(true);
        b->setAutoRaise(true);
        m_recolorSampling->addButton(b, int(mode));
        recolorLayout->addWidget(b);
    }
    connect(m_recolorSampling, &QButtonGroup::idClicked, this,
            [update](int id) { update([id](ToolSettings& s) { s.recolorSampling = RecolorSampling(id); }); });
    add(QStringLiteral("recolor"), Tool::OptRecolorSampling, Flood, {recolorBox});

    // --- Selector de color ------------------------------------------------
    m_pickerAfter = new ChoiceButton(tr("Después de pulsar"), parent);
    m_pickerAfter->addChoice(Icons::theme({"color-picker"}), tr("No cambiar de herramienta"), int(PickerAfter::DoNotSwitch));
    m_pickerAfter->addChoice(Icons::theme({"edit-undo", "go-previous"}), tr("Cambiar a la herramienta anterior"),
                             int(PickerAfter::PreviousTool));
    m_pickerAfter->addChoice(Icons::theme({"draw-freehand"}), tr("Cambiar a la herramienta Lápiz"), int(PickerAfter::Pencil));
    m_pickerAfter->setShowText(true);
    m_pickerAfter->onChosen = [update](int v) { update([v](ToolSettings& s) { s.pickerAfter = PickerAfter(v); }); };
    add(QStringLiteral("picker.after"), Tool::OptPicker, Picker, {label(tr("Después de pulsar:")), m_pickerAfter});

    // --- Muestreo -----------------------------------------------------------
    m_sampling = new ChoiceButton(tr("Toma de muestras"), parent);
    m_sampling->addChoice(Icons::theme({"image-x-generic"}), tr("Imagen"), 1);
    m_sampling->addChoice(Icons::theme({"layer-visible-on", "view-list-details"}), tr("Capa"), 0);
    m_sampling->setShowText(true);
    m_sampling->onChosen = [update](int v) { update([v](ToolSettings& s) { s.sampleImage = v == 1; }); };
    m_pickerSampling = new ChoiceButton(tr("Toma de muestras"), parent);
    m_pickerSampling->addChoice(Icons::theme({"image-x-generic"}), tr("Imagen"), 1);
    m_pickerSampling->addChoice(Icons::theme({"layer-visible-on", "view-list-details"}), tr("Capa"), 0);
    m_pickerSampling->setShowText(true);
    m_pickerSampling->onChosen = [update](int v) { update([v](ToolSettings& s) { s.pickerSampleImage = v == 1; }); };
    m_pickerSize = new ChoiceButton(tr("Tamaño de la muestra"), parent);
    m_pickerSize->addChoice(Icons::sampleSize(1), tr("Píxel"), 1);
    for (int n : {3, 5, 11, 31, 51})
        m_pickerSize->addChoice(Icons::sampleSize(n), tr("%1 × %1 píxeles").arg(n), n);
    m_pickerSize->setShowText(true);
    m_pickerSize->onChosen = [update](int n) { update([n](ToolSettings& s) { s.pickerSize = n; }); };
    add(QStringLiteral("sampling"), Tool::OptSampling, Sampling, {label(tr("Toma de muestras:")), m_sampling});
    add(QStringLiteral("picker.sampling"), Tool::OptSampleSize, Picker,
        {label(tr("Toma de muestras:")), m_pickerSampling, m_pickerSize});

    // --- Opciones -----------------------------------------------------------
    m_smoothing = new ChoiceButton(tr("Trayectoria"), parent);
    m_smoothing->addChoice(Icons::smoothing(false), tr("Trayectoria no suavizada"), 0,
                           tr("El trazo de pincel utilizará los datos brutos que provienen del dispositivo de entrada."));
    m_smoothing->addChoice(Icons::smoothing(true), tr("Trayectoria suavizada"), 1,
                           tr("El trazo del pincel se suavizará utilizando una curva segmentaria Catmull-Rom centrípeta."));
    m_smoothing->onChosen = [update](int v) { update([v](ToolSettings& s) { s.smoothing = v == 1; }); };
    add(QStringLiteral("smoothing"), Tool::OptSmoothing, Options, {m_smoothing});

    m_pressure = new QToolButton(parent);
    m_pressure->setIcon(Icons::theme({"input-tablet", "draw-calligraphic"}));
    if (m_pressure->icon().isNull())
        m_pressure->setText(tr("Presión"));
    m_pressure->setToolTip(tr("La presión del lápiz de la tableta controla el tamaño del pincel"));
    m_pressure->setCheckable(true);
    m_pressure->setAutoRaise(true);
    connect(m_pressure, &QToolButton::toggled, this, [update](bool on) { update([on](ToolSettings& s) { s.pressure = on; }); });
    add(QStringLiteral("pressure"), Tool::OptPressure, Options, {m_pressure});

    m_antialias = new ChoiceButton(tr("Suavizado"), parent);
    m_antialias->addChoice(Icons::antialias(true), tr("Suavizado activado"), 1);
    m_antialias->addChoice(Icons::antialias(false), tr("Suavizado desactivado"), 0);
    m_antialias->onChosen = [update](int v) { update([v](ToolSettings& s) { s.antialias = v == 1; }); };
    add(QStringLiteral("aa"), Tool::OptAntialias, Options, {m_antialias});

    m_blend = new ChoiceButton(tr("Modo de fusión"), parent);
    for (BlendMode m : toolBlendModes())
        m_blend->addChoice(Icons::blendMode(), blendModeToolName(m), int(m));
    m_blend->setShowText(true);
    m_blend->onChosen = [update](int v) { update([v](ToolSettings& s) { s.blend = BlendMode(v); }); };
    add(QStringLiteral("blend"), Tool::OptBlend, Options, {m_blend});

    m_selectionQuality = new ChoiceButton(tr("Calidad de selección"), parent);
    m_selectionQuality->addChoice(Icons::selectionQuality(false), tr("Calidad de selección pixelada"), 0);
    m_selectionQuality->addChoice(Icons::selectionQuality(true), tr("Calidad de selección con antidentado"), 1);
    m_selectionQuality->onChosen = [update](int v) { update([v](ToolSettings& s) { s.selectionAntialias = v == 1; }); };
    add(QStringLiteral("selQuality"), Tool::OptSelectionQuality, Options, {m_selectionQuality});

    connect(model, &ToolSettingsModel::changed, this, &ToolOptionsEditor::sync);
    sync();
}

void ToolOptionsEditor::sync()
{
    const ToolSettings& s = m_model->tools();
    auto setNumber = [](QComboBox* c, double v, int decimals) {
        const QString text = numberText(v, decimals);
        if (c->lineEdit()->hasFocus() || c->currentText() == text)
            return;
        const QSignalBlocker b(c);
        c->setEditText(text);
    };
    setNumber(m_size, s.brushSize, 0);
    setNumber(m_fontSize, s.fontSize, 1);
    {
        const QSignalBlocker b1(m_hardness), b2(m_tolerance), b3(m_spacing), b4(m_cornerRadius), b5(m_fixedWidth),
            b6(m_fixedHeight);
        m_hardness->setValue(s.hardness);
        m_spacing->setValue(s.spacing);
        m_tolerance->setValue(s.tolerance);
        m_cornerRadius->setValue(s.cornerRadius);
        m_fixedWidth->setValue(s.fixedWidth);
        m_fixedHeight->setValue(s.fixedHeight);
    }
    {
        const QSignalBlocker b1(m_pressure), b2(m_bold), b3(m_italic), b4(m_underline), b5(m_strikeout);
        m_pressure->setChecked(s.pressure);
        m_bold->setChecked(s.bold);
        m_italic->setChecked(s.italic);
        m_underline->setChecked(s.underline);
        m_strikeout->setChecked(s.strikeout);
    }
    m_smoothing->setCurrent(s.smoothing ? 1 : 0);
    int shapeChoice = int(s.shape);
    if (s.shape == ShapeType::Custom) {
        const QList<CustomShapes::Shape>& custom = CustomShapes::all();
        for (int i = 0; i < int(custom.size()); ++i)
            if (custom[i].file == s.customShape)
                shapeChoice = kCustomShapeBase + i;
    }
    m_shape->setCurrent(shapeChoice);
    m_shapeStyle->setCurrent(int(s.shapeStyle));
    m_startCap->setCurrent(int(s.startCap));
    m_endCap->setCurrent(int(s.endCap));
    m_dash->setCurrent(int(s.dash));
    m_gradientMode->setCurrent(s.gradientTransparency ? 1 : 0);
    m_gradientRepeat->setCurrent(int(s.gradientRepeat));
    m_flood->setCurrent(s.floodGlobal ? 1 : 0);
    m_toleranceAlpha->setCurrent(s.toleranceStraight ? 1 : 0);
    m_antialias->setCurrent(s.antialias ? 1 : 0);
    m_selectionQuality->setCurrent(s.selectionAntialias ? 1 : 0);
    m_gamma->setCurrent(s.moveGamma ? 1 : 0);
    m_fontMetric->setCurrent(int(s.fontMetric));
    m_fill->setCurrent(s.fill);
    m_rectMode->setCurrent(int(s.rectMode));
    m_afterSelect->setCurrent(int(s.afterSelect));
    m_blend->setCurrent(int(s.blend));
    m_pickerSize->setCurrent(s.pickerSize);
    m_pickerAfter->setCurrent(int(s.pickerAfter));
    m_interpolation->setCurrent(int(s.moveInterpolation));
    m_textRendering->setCurrent(int(s.textRendering));
    m_sampling->setCurrent(s.sampleImage ? 1 : 0);
    m_pickerSampling->setCurrent(s.pickerSampleImage ? 1 : 0);
    if (!s.fontFamily.isEmpty() && m_font->currentFont().family() != s.fontFamily && !m_font->isEditingName()) {
        const QSignalBlocker b(m_font);
        m_font->setCurrentFont(QFont(s.fontFamily));
    }
    m_fixedWidth->setSuffix(s.rectMode == RectSelectMode::FixedSize ? tr(" px") : QString());
    m_fixedHeight->setSuffix(s.rectMode == RectSelectMode::FixedSize ? tr(" px") : QString());
    for (auto [group, id] : {std::pair{m_gradientTypes, int(s.gradient)}, std::pair{m_align, int(s.textAlign)},
                             std::pair{m_selectionModes, int(s.selectionMode)}, std::pair{m_curves, int(s.curveType)},
                             std::pair{m_recolorSampling, int(s.recolorSampling)}})
        if (QAbstractButton* b = group->button(id))
            b->setChecked(true);
}

// ---------------------------------------------------------------------------

QStringList ToolOptionsBar::order(ToolId id)
{
    // El orden de la barra de Paint.NET 5.
    auto list = [](const char* text) { return QString::fromLatin1(text).split(QLatin1Char(' '), Qt::SkipEmptyParts); };
    switch (id) {
    case ToolId::RectSelect: return list("selMode | rectMode rectSize | afterSelect | selQuality");
    case ToolId::EllipseSelect:
    case ToolId::LassoSelect: return list("selMode | afterSelect | selQuality");
    case ToolId::MagicWand:
        return list("selMode | flood | tolerance tolAlpha | sampling | afterSelect | selQuality");
    case ToolId::MoveSelected: return list("interp | gamma selQuality");
    case ToolId::MoveSelection: return list("selQuality");
    case ToolId::PaintBucket: return list("flood | fill | tolerance tolAlpha | sampling | aa blend selQuality");
    case ToolId::Gradient: return list("gradient.types | gradient.mode gradient.repeat | aa blend selQuality");
    case ToolId::Paintbrush: return list("size hardness spacing fill | smoothing pressure aa blend selQuality");
    case ToolId::Eraser: return list("size hardness spacing | smoothing pressure aa selQuality");
    case ToolId::Pencil: return list("blend selQuality");
    case ToolId::ColorPicker: return list("picker.sampling | picker.after | selQuality");
    case ToolId::CloneStamp: return list("size hardness spacing | smoothing pressure aa blend selQuality");
    case ToolId::Recolor:
        return list("size hardness spacing tolerance tolAlpha | recolor | smoothing pressure aa selQuality");
    case ToolId::Text: return list("text.font | text.style | text.align | aa blend selQuality");
    case ToolId::Line: return list("curve | size | style | fill | aa blend selQuality");
    case ToolId::Shapes: return list("shape shapeStyle corner | size | style | fill | aa blend selQuality");
    default: break;
    }
    return {};
}

ToolOptionsBar::ToolOptionsBar(ToolManager* manager, QWidget* parent)
    : QToolBar(tr("Opciones de herramienta"), parent)
    , m_manager(manager)
{
    setObjectName(QStringLiteral("ToolOptionsBar"));
    setIconSize(QSize(16, 16));
    setMovable(false);

    // Selector de herramienta con menú desplegable.
    auto* toolLabel = new QLabel(tr("Herramienta:"), this);
    toolLabel->setContentsMargins(4, 0, 2, 0);
    addWidget(toolLabel);
    m_toolButton = new QToolButton(this);
    m_toolButton->setPopupMode(QToolButton::InstantPopup);
    m_toolButton->setAutoRaise(true);
    auto* menu = new QMenu(m_toolButton);
    for (Tool* t : manager->tools()) {
        QAction* a = menu->addAction(t->icon(), t->name());
        a->setShortcut(t->shortcut());
        a->setShortcutVisibleInContextMenu(true);
        a->setShortcutContext(Qt::WidgetShortcut);
        connect(a, &QAction::triggered, this, [manager, t] { manager->select(t->id()); });
    }
    m_toolButton->setMenu(menu);
    addWidget(m_toolButton);

    m_editor = new ToolOptionsEditor(new AppToolSettings(this), this);
    for (const ToolOptionsEditor::Group& g : m_editor->groups()) {
        BarGroup bg;
        bg.key = g.key;
        bg.separator = addSeparator();
        for (const ToolOptionsEditor::Item& item : g.items)
            bg.items.append({addWidget(item.widget), item.mask});
        m_groups.append(bg);
    }

    // Acabado (formas, texto, rellenos editables...), como en Paint.NET.
    auto* finish = new QToolButton(this);
    finish->setIcon(Icons::theme({"dialog-ok-apply", "dialog-ok"}));
    finish->setText(tr("Acabado"));
    finish->setToolTip(tr("Aplicar lo que se está editando (Intro)"));
    finish->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    finish->setAutoRaise(true);
    connect(finish, &QToolButton::clicked, this, [this] {
        if (m_manager->current())
            m_manager->current()->finish();
    });
    m_finishSeparator = addSeparator();
    m_finish = addWidget(finish);

    // Altura fija: así el lienzo no salta al cambiar de herramienta.
    int tallest = 0;
    for (QAction* a : actions())
        if (QWidget* w = widgetForAction(a))
            tallest = std::max(tallest, w->sizeHint().height());
    setFixedHeight(tallest + 2 * (contentsMargins().top() + layout()->contentsMargins().top()) + 6);

    connect(manager, &ToolManager::toolChanged, this, &ToolOptionsBar::onToolChanged);
    connect(AppState::instance(), &AppState::toolSettingsChanged, this, &ToolOptionsBar::updateDependent);
    if (manager->current())
        onToolChanged(manager->current());
}

void ToolOptionsBar::onToolChanged(Tool* tool)
{
    m_toolButton->setIcon(tool->icon());
    m_toolButton->setToolTip(tool->name());
    const quint32 opts = tool->options();

    // Orden de Paint.NET para esta herramienta; los grupos que no figuran (o todos, si no hay
    // orden) van detrás con su separador.
    QStringList tokens = order(tool->id());
    for (const BarGroup& g : std::as_const(m_groups))
        if (!tokens.contains(g.key))
            tokens << QStringLiteral("|") << g.key;

    for (BarGroup& g : m_groups) {
        g.shown = false;
        g.separator->setVisible(false);
        for (const Shown& s : std::as_const(g.items))
            s.action->setVisible(false);
    }
    bool pendingSeparator = true;
    for (const QString& token : std::as_const(tokens)) {
        if (token == QLatin1String("|")) {
            pendingSeparator = true;
            continue;
        }
        auto it = std::find_if(m_groups.begin(), m_groups.end(), [&](const BarGroup& g) { return g.key == token; });
        if (it == m_groups.end())
            continue;
        QList<QAction*> visible;
        for (const Shown& s : std::as_const(it->items))
            if (opts & s.mask)
                visible << s.action;
        if (visible.isEmpty())
            continue;
        it->shown = true;
        it->separatorWanted = pendingSeparator;
        insertAction(nullptr, it->separator);
        it->separator->setVisible(pendingSeparator);
        pendingSeparator = false;
        for (const Shown& s : std::as_const(it->items)) {
            insertAction(nullptr, s.action);
            s.action->setVisible(opts & s.mask);
        }
    }
    insertAction(nullptr, m_finishSeparator);
    insertAction(nullptr, m_finish);
    m_finishSeparator->setVisible(opts & Tool::OptFinish);
    m_finish->setVisible(opts & Tool::OptFinish);
    updateDependent();
}

void ToolOptionsBar::updateDependent()
{
    // Grupos que dependen de otra opción (sin mover nada de sitio: así no se interrumpe un
    // arrastre en curso en otro control).
    const ToolSettings& s = AppState::instance()->tools();
    const quint32 opts = m_manager->current() ? m_manager->current()->options() : 0;
    for (const BarGroup& g : std::as_const(m_groups)) {
        if (!g.shown)
            continue;
        const bool applies = ToolOptionsEditor::groupApplies(g.key, s);
        for (const Shown& item : g.items)
            item.action->setVisible(applies && (opts & item.mask));
        g.separator->setVisible(applies && g.separatorWanted);
    }
}

// ---------------------------------------------------------------------------

ToolOptionsPanel::ToolOptionsPanel(ToolSettingsModel* model, QWidget* parent)
    : QWidget(parent)
{
    // Todas las opciones agrupadas como en Configuración > Herramientas de Paint.NET.
    auto* editor = new ToolOptionsEditor(model, this);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    const QStringList names = ToolOptionsEditor::sectionNames();
    QList<FlowLayout*> flows;
    for (const QString& name : names) {
        auto* header = new QWidget(this);
        auto* hl = new QHBoxLayout(header);
        hl->setContentsMargins(0, 8, 0, 0);
        auto* title = new QLabel(name, header);
        auto* line = new QFrame(header);
        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Sunken);
        hl->addWidget(title);
        hl->addWidget(line, 1);
        layout->addWidget(header);
        auto* body = new QWidget(this);
        auto* flow = new FlowLayout(body, 6, 4);
        flow->setContentsMargins(8, 2, 0, 0);
        layout->addWidget(body);
        flows.append(flow);
    }
    for (const ToolOptionsEditor::Group& g : editor->groups()) {
        FlowLayout* flow = flows.value(g.section, flows.last());
        QWidget* pendingLabel = nullptr;
        for (const ToolOptionsEditor::Item& item : g.items) {
            if (qobject_cast<QLabel*>(item.widget)) {
                pendingLabel = item.widget;
                continue;
            }
            if (!pendingLabel) {
                item.widget->setParent(this);
                flow->addWidget(item.widget);
                continue;
            }
            auto* box = new QWidget(this);
            auto* bl = new QHBoxLayout(box);
            bl->setContentsMargins(0, 0, 0, 0);
            bl->setSpacing(2);
            pendingLabel->setParent(box);
            item.widget->setParent(box);
            bl->addWidget(pendingLabel);
            bl->addWidget(item.widget);
            flow->addWidget(box);
            pendingLabel = nullptr;
        }
    }
    layout->addStretch();
}
