#include "app/widgets/BrushAdvanced.h"

#include "app/AppState.h"
#include "app/Icons.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <cmath>

namespace {

template <typename F>
void updateTools(F f)
{
    AppState::instance()->updateTools(f);
}

} // namespace

AdvancedBrushPanel::AdvancedBrushPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 8, 10, 8);
    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    root->addLayout(form);

    auto section = [&](const QString& text) {
        auto* l = new QLabel(text, this);
        QFont f = l->font();
        f.setBold(true);
        l->setFont(f);
        form->addRow(l);
    };
    // Un deslizador con su número; 'get' y 'set' leen y cambian el ajuste.
    auto slider = [&](const QString& label, int min, int max, const QString& suffix, int ToolSettings::*member,
                      const QString& tip) {
        auto* row = new QWidget(this);
        auto* l = new QHBoxLayout(row);
        l->setContentsMargins(0, 0, 0, 0);
        auto* s = new QSlider(Qt::Horizontal, row);
        s->setRange(min, max);
        s->setMinimumWidth(150);
        auto* n = new QSpinBox(row);
        n->setRange(min, max);
        n->setSuffix(suffix);
        n->setKeyboardTracking(false);
        l->addWidget(s, 1);
        l->addWidget(n);
        row->setToolTip(tip);
        connect(s, &QSlider::valueChanged, n, &QSpinBox::setValue);
        connect(n, &QSpinBox::valueChanged, s, &QSlider::setValue);
        connect(n, &QSpinBox::valueChanged, this, [this, member](int v) {
            if (!m_syncing)
                updateTools([member, v](ToolSettings& t) { t.*member = v; });
        });
        m_syncers << [s, n, member] {
            const QSignalBlocker b1(s), b2(n);
            const int v = AppState::instance()->tools().*member;
            s->setValue(v);
            n->setValue(v);
        };
        form->addRow(label, row);
    };
    auto check = [&](const QString& label, bool ToolSettings::*member, const QString& tip) {
        auto* c = new QCheckBox(label, this);
        c->setToolTip(tip);
        connect(c, &QCheckBox::toggled, this, [this, member](bool on) {
            if (!m_syncing)
                updateTools([member, on](ToolSettings& t) { t.*member = on; });
        });
        m_syncers << [c, member] {
            const QSignalBlocker b(c);
            c->setChecked(AppState::instance()->tools().*member);
        };
        form->addRow(QString(), c);
    };

    section(tr("Punta"));
    auto* shape = new QComboBox(this);
    shape->addItem(tr("Redonda"), 0);
    shape->addItem(tr("Cuadrada"), 1);
    connect(shape, &QComboBox::activated, this, [this, shape](int i) {
        if (!m_syncing)
            updateTools([v = shape->itemData(i).toInt()](ToolSettings& t) { t.tipShape = v; });
    });
    m_syncers << [shape] {
        const QSignalBlocker b(shape);
        shape->setCurrentIndex(std::max(0, shape->findData(AppState::instance()->tools().tipShape)));
    };
    form->addRow(tr("Forma:"), shape);
    slider(tr("Ángulo:"), -180, 180, QStringLiteral("°"), &ToolSettings::tipAngle, tr("El giro de la punta"));
    slider(tr("Redondez:"), 5, 100, QStringLiteral(" %"), &ToolSettings::tipRoundness,
           tr("100 %: la punta entera; menos, aplastada (como una pluma o un rotulador de punta biselada)"));
    slider(tr("Flujo:"), 1, 100, QStringLiteral(" %"), &ToolSettings::flow,
           tr("La opacidad de cada toque: con menos, el color se acumula al repasar"));

    section(tr("Presión"));
    slider(tr("Curva:"), 20, 400, QStringLiteral(" %"), &ToolSettings::pressureCurve,
           tr("100 %: lineal; menos, responde antes (trazos gruesos con poca fuerza); más, hace falta más fuerza"));
    slider(tr("Tamaño mínimo:"), 1, 100, QStringLiteral(" %"), &ToolSettings::minSize, tr("El tamaño con la presión más leve"));
    check(tr("La presión también cambia la opacidad"), &ToolSettings::pressureOpacity, {});
    check(tr("Simular la presión con la velocidad (ratón o dedo)"), &ToolSettings::speedPressure,
          tr("Sin lápiz con presión: cuanto más rápido, más fino el trazo, como una pluma de tinta"));

    section(tr("Trazo"));
    slider(tr("Corrección:"), 0, 20, QString(), &ToolSettings::stabilizer,
           tr("Estabilizador (como la corrección de FireAlpaca): el trazo sigue al puntero con retraso y sale más suave. "
              "Útil para líneas de manga y para dibujar con el dedo"));
    slider(tr("Entrada:"), 0, 400, QStringLiteral(" px"), &ToolSettings::taperIn,
           tr("El trazo empieza en punta a lo largo de estos píxeles (líneas de manga)"));
    slider(tr("Salida:"), 0, 400, QStringLiteral(" px"), &ToolSettings::taperOut,
           tr("El trazo acaba en punta a lo largo de estos píxeles (se ve al soltar)"));

    section(tr("Variación al azar"));
    slider(tr("Tamaño:"), 0, 100, QStringLiteral(" %"), &ToolSettings::sizeJitter, {});
    slider(tr("Opacidad:"), 0, 100, QStringLiteral(" %"), &ToolSettings::opacityJitter, {});
    slider(tr("Ángulo:"), 0, 100, QStringLiteral(" %"), &ToolSettings::angleJitter, {});
    slider(tr("Dispersión:"), 0, 300, QStringLiteral(" %"), &ToolSettings::scatter, tr("Cuánto se separa cada toque del trazo"));

    section(tr("Lienzo"));
    check(tr("Deslizadores de tamaño y opacidad al lado del lienzo"), &ToolSettings::sideSliders,
          tr("Como en Ibis Paint: dos barras verticales en el borde izquierdo, cómodas con el dedo o el lápiz"));

    auto* reset = new QPushButton(Icons::theme({"edit-undo", "view-refresh"}), tr("Restablecer"), this);
    connect(reset, &QPushButton::clicked, this, [] {
        updateTools([](ToolSettings& t) {
            const ToolSettings d;
            t.tipShape = d.tipShape;
            t.tipAngle = d.tipAngle;
            t.tipRoundness = d.tipRoundness;
            t.flow = d.flow;
            t.sizeJitter = d.sizeJitter;
            t.opacityJitter = d.opacityJitter;
            t.angleJitter = d.angleJitter;
            t.scatter = d.scatter;
            t.pressureCurve = d.pressureCurve;
            t.minSize = d.minSize;
            t.pressureOpacity = d.pressureOpacity;
            t.stabilizer = d.stabilizer;
            t.taperIn = d.taperIn;
            t.taperOut = d.taperOut;
            t.speedPressure = d.speedPressure;
        });
    });
    root->addWidget(reset, 0, Qt::AlignRight);
    connect(AppState::instance(), &AppState::toolSettingsChanged, this, &AdvancedBrushPanel::sync);
    sync();
}

void AdvancedBrushPanel::sync()
{
    m_syncing = true;
    for (const auto& f : std::as_const(m_syncers))
        f();
    m_syncing = false;
}

AdvancedBrushButton::AdvancedBrushButton(QWidget* parent)
    : QToolButton(parent)
{
    setText(tr("Avanzado…"));
    setIcon(Icons::theme({"configure", "preferences-system", "document-properties"}));
    setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    setAutoRaise(true);
    setPopupMode(QToolButton::InstantPopup);
    setToolTip(tr("Opciones avanzadas del pincel: punta, presión, estabilizador, entrada y salida y variación al azar"));
    auto* menu = new QMenu(this);
    auto* action = new QWidgetAction(menu);
    action->setDefaultWidget(new AdvancedBrushPanel(menu));
    menu->addAction(action);
    setMenu(menu);
}

BrushSideSliders::BrushSideSliders(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("BrushSideSliders"));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral("#BrushSideSliders { background: rgba(30, 30, 34, 150); border-radius: 14px; }"
                                 "#BrushSideSliders QLabel { color: white; font-size: 8pt; }"));
    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(6, 10, 6, 10);
    l->setSpacing(4);
    auto make = [&](QSlider*& s, QLabel*& label, int min, int max, const QString& tip) {
        label = new QLabel(this);
        label->setAlignment(Qt::AlignCenter);
        s = new QSlider(Qt::Vertical, this);
        s->setRange(min, max);
        s->setToolTip(tip);
        s->setMinimumHeight(110);
        l->addWidget(label);
        l->addWidget(s, 1, Qt::AlignHCenter);
    };
    make(m_size, m_sizeLabel, 1, 400, tr("Tamaño del pincel"));
    l->addSpacing(10);
    make(m_opacity, m_opacityLabel, 1, 100, tr("Opacidad del pincel (flujo)"));
    connect(m_size, &QSlider::valueChanged, this, [this](int v) {
        // Escala que crece más deprisa arriba (pinceles pequeños precisos, grandes a mano).
        const double size = std::max(1.0, std::round(std::pow(v / 400.0, 2.0) * 1000.0));
        m_sizeLabel->setText(QString::number(size));
        if (!m_syncing)
            AppState::instance()->updateTools([size](ToolSettings& t) { t.brushSize = size; });
    });
    connect(m_opacity, &QSlider::valueChanged, this, [this](int v) {
        m_opacityLabel->setText(QStringLiteral("%1%").arg(v));
        if (!m_syncing)
            AppState::instance()->updateTools([v](ToolSettings& t) { t.flow = v; });
    });
    connect(AppState::instance(), &AppState::toolSettingsChanged, this, &BrushSideSliders::sync);
    setFixedWidth(44);
    sync();
}

void BrushSideSliders::sync()
{
    m_syncing = true;
    const ToolSettings& t = AppState::instance()->tools();
    m_size->setValue(int(std::round(std::sqrt(std::clamp(t.brushSize, 1.0, 1000.0) / 1000.0) * 400.0)));
    m_sizeLabel->setText(QString::number(t.brushSize, 'f', 0));
    m_opacity->setValue(t.flow);
    m_opacityLabel->setText(QStringLiteral("%1%").arg(t.flow));
    m_syncing = false;
    setVisible(t.advancedBrush && t.sideSliders);
}

void BrushSideSliders::place(const QRect& area)
{
    const int h = std::min(360, std::max(200, area.height() - 40));
    setGeometry(area.left() + 8, area.top() + (area.height() - h) / 2, width(), h);
    raise();
}
