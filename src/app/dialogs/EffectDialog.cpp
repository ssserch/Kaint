#include "app/dialogs/EffectDialog.h"

#include "app/Icons.h"
#include "app/dialogs/DialogButtons.h"
#include "app/widgets/CurvesEditor.h"
#include "app/widgets/EffectControls.h"
#include "core/Raster.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSet>
#include <QSlider>
#include <QTabWidget>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVector3D>

#include <array>
#include <cmath>
#include <memory>

namespace {

// Título de un parámetro. Paint.NET 5 solo pone la línea al lado en los
// selectores de ángulo y de color.
QWidget* header(const QString& text, QWidget* parent, bool line = true)
{
    auto* w = new QWidget(parent);
    auto* l = new QHBoxLayout(w);
    l->setContentsMargins(0, 6, 0, 0);
    l->addWidget(new QLabel(text, w));
    if (line) {
        auto* rule = new QFrame(w);
        rule->setFrameShape(QFrame::HLine);
        rule->setFrameShadow(QFrame::Sunken);
        l->addWidget(rule, 1);
    } else {
        l->addStretch(1);
    }
    return w;
}

// Franja con un degradado bajo un deslizador (Matiz, Temperatura...).
class GradientStrip : public QWidget {
public:
    GradientStrip(const QList<QColor>& colors, QWidget* parent)
        : QWidget(parent)
        , m_colors(colors)
    {
        setFixedHeight(5);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        QLinearGradient g(rect().topLeft(), rect().topRight());
        for (int i = 0; i < m_colors.size(); ++i)
            g.setColorAt(m_colors.size() > 1 ? double(i) / (m_colors.size() - 1) : 0, m_colors[i]);
        // Los extremos del degradado coinciden con los del recorrido del deslizador.
        p.fillRect(rect().adjusted(6, 0, -6, 0), g);
    }

private:
    QList<QColor> m_colors;
};

QToolButton* resetButton(QWidget* parent)
{
    auto* b = new QToolButton(parent);
    b->setIcon(Icons::theme({"edit-undo", "view-refresh"}));
    b->setToolTip(QObject::tr("Restablecer"));
    b->setAutoRaise(true);
    return b;
}

QDoubleSpinBox* doubleBox(QWidget* parent, double min, double max, int decimals)
{
    auto* d = new QDoubleSpinBox(parent);
    d->setRange(min, max);
    d->setDecimals(decimals);
    d->setSingleStep(decimals > 0 ? std::pow(10.0, -decimals) * 10 : 1);
    d->setMinimumWidth(80);
    return d;
}

// Fila "deslizador + caja numérica + restablecer" para un valor real; 'changed'
// recibe el valor nuevo. Devuelve la caja (para fijar el valor desde fuera).
QDoubleSpinBox* sliderRow(QWidget* parent, QBoxLayout* into, double min, double max, int decimals, double def,
                          double current, const QString& tip, const std::function<void(double)>& changed)
{
    const double scale = std::pow(10.0, decimals);
    auto* row = new QHBoxLayout;
    auto* slider = new QSlider(Qt::Horizontal, parent);
    slider->setRange(int(std::round(min * scale)), int(std::round(max * scale)));
    slider->setToolTip(tip);
    auto* box = doubleBox(parent, min, max, decimals);
    box->setToolTip(tip);
    box->setValue(current);
    slider->setValue(int(std::round(current * scale)));
    auto* reset = resetButton(parent);
    QObject::connect(slider, &QSlider::valueChanged, box, [box, scale](int v) { box->setValue(v / scale); });
    QObject::connect(box, &QDoubleSpinBox::valueChanged, slider, [slider, scale, changed](double v) {
        const QSignalBlocker b(slider);
        slider->setValue(int(std::round(v * scale)));
        changed(v);
    });
    QObject::connect(reset, &QToolButton::clicked, box, [box, def] { box->setValue(def); });
    row->addWidget(slider, 1);
    row->addWidget(box);
    row->addWidget(reset);
    into->addLayout(row);
    return box;
}

} // namespace

EffectDialog::EffectDialog(const Effect& effect, const ParamValues& initial, const QImage& source, const QRect& roi,
                           QWidget* parent)
    : QDialog(parent)
    , m_values(initial)
    , m_params(effect.params())
{
    setWindowTitle(effect.name());
    setWindowIcon(effect.iconImage().isNull() ? Icons::theme(effect.iconName()) : QIcon(QPixmap::fromImage(effect.iconImage())));
    auto* root = new QVBoxLayout(this);

    QImage thumbnail;
    auto thumb = [&] {
        if (thumbnail.isNull() && !source.isNull())
            thumbnail = Raster::thumbnail(source.copy(roi.intersected(source.rect())), QSize(220, 160));
        return thumbnail;
    };

    // Casillas que van en el título de otro parámetro (no se muestran solas).
    QSet<QString> titleChecks;
    for (const EffectParam& p : m_params)
        if (!p.enableKey.isEmpty())
            titleChecks.insert(p.enableKey);
    // Pestañas (Nubes / Colores...), si algún parámetro las usa.
    QTabWidget* tabs = nullptr;
    QHash<QString, QVBoxLayout*> tabPages;
    for (const EffectParam& p : m_params) {
        if (p.tab.isEmpty() || tabPages.contains(p.tab))
            continue;
        if (!tabs) {
            tabs = new QTabWidget(this);
            root->addWidget(tabs, 1);
        }
        auto* page = new QWidget(tabs);
        auto* pageLayout = new QVBoxLayout(page);
        tabs->addTab(page, p.tab);
        tabPages.insert(p.tab, pageLayout);
    }

    for (const EffectParam& p : m_params) {
        const QString key = p.key;
        if (titleChecks.contains(key))
            continue;
        // Cada parámetro en su propio contenedor (para deshabilitarlo si es de solo lectura).
        auto* container = new QWidget(this);
        auto* layout = new QVBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);
        const bool stretch = p.type == EffectParam::Type::Curves || p.type == EffectParam::Type::Levels
                             || (p.type == EffectParam::Type::Text && p.multiline);
        QVBoxLayout* into = tabPages.value(p.tab, root);
        into->addWidget(container, stretch ? 1 : 0);
        m_containers.insert(key, container);
        // Título con casilla: el resto de controles va en un cuerpo que se
        // habilita con ella.
        bool titled = false;
        if (!p.enableKey.isEmpty()) {
            auto* check = new QCheckBox(p.label, container);
            check->setChecked(m_values.value(p.enableKey).toBool());
            const QString enableKey = p.enableKey;
            connect(check, &QCheckBox::toggled, this, [this, enableKey](bool on) { set(enableKey, on); });
            m_setters.insert(enableKey, [check](const QVariant& v) {
                const QSignalBlocker b(check);
                check->setChecked(v.toBool());
            });
            layout->addWidget(check);
            auto* body = new QWidget(container);
            auto* bodyLayout = new QVBoxLayout(body);
            bodyLayout->setContentsMargins(0, 0, 0, 0);
            layout->addWidget(body);
            m_bodies.insert(key, body);
            layout = bodyLayout;
            titled = true;
        }
        auto title = [&](bool line) {
            if (!titled && !p.label.isEmpty())
                layout->addWidget(header(p.label, this, line));
        };
        auto describe = [&] {
            if (p.description.isEmpty() || p.type == EffectParam::Type::Bool)
                return;
            auto* text = new QLabel(p.description, container);
            text->setWordWrap(true);
            layout->addWidget(text);
        };
        switch (p.type) {
        case EffectParam::Type::Int:
        case EffectParam::Type::Double: {
            const bool isDouble = p.type == EffectParam::Type::Double;
            const double scale = std::pow(10.0, p.decimals);
            title(false);
            describe();
            auto* row = new QHBoxLayout;
            auto* slider = new QSlider(Qt::Horizontal, this);
            slider->setRange(int(std::round(p.min * scale)), int(std::round(p.max * scale)));
            auto* reset = resetButton(this);
            const double current = m_values.value(key, p.def).toDouble();
            QAbstractSpinBox* spin;
            if (isDouble) {
                auto* d = doubleBox(this, p.min, p.max, p.decimals);
                d->setValue(current);
                spin = d;
                connect(d, &QDoubleSpinBox::valueChanged, this, [this, key, slider, scale](double v) {
                    const QSignalBlocker b(slider);
                    slider->setValue(int(std::round(v * scale)));
                    set(key, v);
                });
                connect(slider, &QSlider::valueChanged, d, [d, scale](int v) { d->setValue(v / scale); });
                connect(reset, &QToolButton::clicked, d, [d, def = p.def] { d->setValue(def); });
                m_setters.insert(key, [d, slider, scale](const QVariant& v) {
                    const QSignalBlocker b1(d), b2(slider);
                    d->setValue(v.toDouble());
                    slider->setValue(int(std::round(v.toDouble() * scale)));
                });
            } else {
                auto* i = new QSpinBox(this);
                i->setRange(int(p.min), int(p.max));
                i->setValue(int(current));
                i->setMinimumWidth(80);
                spin = i;
                connect(i, &QSpinBox::valueChanged, this, [this, key, slider](int v) {
                    const QSignalBlocker b(slider);
                    slider->setValue(v);
                    set(key, v);
                });
                connect(slider, &QSlider::valueChanged, i, &QSpinBox::setValue);
                connect(reset, &QToolButton::clicked, i, [i, def = p.def] { i->setValue(int(def)); });
                m_setters.insert(key, [i, slider](const QVariant& v) {
                    const QSignalBlocker b1(i), b2(slider);
                    i->setValue(v.toInt());
                    slider->setValue(v.toInt());
                });
            }
            slider->setValue(int(std::round(current * scale)));
            if (p.gradient.isEmpty()) {
                row->addWidget(slider, 1);
            } else {
                auto* column = new QVBoxLayout;
                column->setSpacing(0);
                column->addWidget(slider);
                column->addWidget(new GradientStrip(p.gradient, this));
                row->addLayout(column, 1);
            }
            row->addWidget(spin);
            row->addWidget(reset);
            layout->addLayout(row);
            break;
        }
        case EffectParam::Type::Bool: {
            // Como IndirectUI: con descripción, el nombre va de título y la descripción en la casilla.
            if (!p.description.isEmpty())
                title(false);
            auto* check = new QCheckBox(p.description.isEmpty() ? p.label : p.description, this);
            check->setChecked(m_values.value(key).toBool());
            connect(check, &QCheckBox::toggled, this, [this, key](bool on) { set(key, on); });
            m_setters.insert(key, [check](const QVariant& v) {
                const QSignalBlocker b(check);
                check->setChecked(v.toBool());
            });
            layout->addWidget(check);
            break;
        }
        case EffectParam::Type::Choice: {
            // Como en Paint.NET 5: "Etiqueta: [lista]" en una sola fila.
            auto* row = new QHBoxLayout;
            row->setContentsMargins(0, 6, 0, 0);
            auto* combo = new QComboBox(this);
            combo->addItems(p.choices);
            combo->setCurrentIndex(m_values.value(key).toInt());
            connect(combo, &QComboBox::currentIndexChanged, this, [this, key](int i) { set(key, i); });
            m_setters.insert(key, [combo](const QVariant& v) {
                const QSignalBlocker b(combo);
                combo->setCurrentIndex(v.toInt());
            });
            auto* label = new QLabel(p.label + QLatin1Char(':'), this);
            label->setBuddy(combo);
            row->addWidget(label);
            row->addWidget(combo);
            row->addStretch();
            layout->addLayout(row);
            break;
        }
        case EffectParam::Type::RollBall: {
            // Rotación y giro: globo 3D a la izquierda y sus tres valores a la derecha.
            title(true);
            auto* row = new QHBoxLayout;
            auto* ball = new RollBallControl(p.max, this);
            const QVector3D current = m_values.value(key).value<QVector3D>();
            ball->setValue(current);
            row->addWidget(ball);
            auto* sliders = new QVBoxLayout;
            auto boxes = std::make_shared<std::array<QDoubleSpinBox*, 3>>();
            auto syncing = std::make_shared<bool>(false); // el globo está moviendo los deslizadores
            auto update = [this, key, ball, boxes, syncing] {
                if (*syncing)
                    return;
                const QVector3D v(float((*boxes)[0]->value()), float((*boxes)[1]->value()), float((*boxes)[2]->value()));
                const QSignalBlocker b(ball);
                ball->setValue(v);
                set(key, v);
            };
            (*boxes)[0] = sliderRow(this, sliders, -180, 180, 2, 0, current.x(), tr("Ángulo"), [update](double) { update(); });
            (*boxes)[1] = sliderRow(this, sliders, -180, 180, 2, 0, current.y(), tr("Dirección de la inclinación"),
                                    [update](double) { update(); });
            (*boxes)[2] = sliderRow(this, sliders, -p.max, p.max, 2, 0, current.z(), tr("Inclinación"),
                                    [update](double) { update(); });
            connect(ball, &RollBallControl::valueChanged, this, [this, key, boxes, syncing](const QVector3D& v) {
                // Las cajas (y con ellas los deslizadores) siguen al globo.
                *syncing = true;
                for (int i = 0; i < 3; ++i)
                    (*boxes)[size_t(i)]->setValue(v[i]);
                *syncing = false;
                set(key, v);
            });
            row->addLayout(sliders, 1);
            layout->addLayout(row);
            break;
        }
        case EffectParam::Type::Seed: {
            // Como Paint.NET: solo el botón "Aleatorizar".
            describe();
            auto* button = new QPushButton(p.label, this);
            button->setToolTip(tr("Genera otro resultado aleatorio"));
            connect(button, &QPushButton::clicked, this,
                    [this, key] { set(key, int(QRandomGenerator::global()->bounded(256))); });
            auto* row = new QHBoxLayout;
            row->addWidget(button);
            row->addStretch();
            layout->addLayout(row);
            break;
        }
        case EffectParam::Type::Angle: {
            title(true);
            describe();
            auto* row = new QHBoxLayout;
            auto* chooser = new AngleChooser(p.min, p.max, this);
            auto* d = doubleBox(this, p.min, p.max, p.decimals);
            auto* reset = resetButton(this);
            const double current = m_values.value(key, p.def).toDouble();
            chooser->setValue(current);
            d->setValue(current);
            connect(chooser, &AngleChooser::valueChanged, d, &QDoubleSpinBox::setValue);
            connect(d, &QDoubleSpinBox::valueChanged, this, [this, key, chooser](double v) {
                const QSignalBlocker b(chooser);
                chooser->setValue(v);
                set(key, v);
            });
            connect(reset, &QToolButton::clicked, d, [d, def = p.def] { d->setValue(def); });
            row->addWidget(chooser);
            row->addStretch();
            row->addWidget(d);
            row->addWidget(reset);
            layout->addLayout(row);
            break;
        }
        case EffectParam::Type::Offset: {
            // Desplazamiento: miniatura con la cruz y dos deslizadores (X, Y) que la mueven.
            title(false);
            describe();
            auto* pan = new PanControl(this);
            pan->setImage(thumb());
            // La cruz trabaja en -1..1; los complementos pueden usar otro rango (píxeles...).
            const QRectF range(QPointF(p.min, p.minY), QPointF(p.max, p.maxY));
            auto toPan = [range](const QPointF& v) {
                return QPointF(range.width() > 0 ? (v.x() - range.left()) / range.width() * 2 - 1 : 0,
                               range.height() > 0 ? (v.y() - range.top()) / range.height() * 2 - 1 : 0);
            };
            auto fromPan = [range](const QPointF& v) {
                return QPointF(range.left() + (v.x() + 1) / 2 * range.width(), range.top() + (v.y() + 1) / 2 * range.height());
            };
            const QPointF current = m_values.value(key).toPointF();
            const QPointF def = p.defaultValue.isValid() ? p.defaultValue.toPointF() : QPointF();
            pan->setValue(toPan(current));
            auto* sliders = new QVBoxLayout;
            auto boxes = std::make_shared<std::array<QDoubleSpinBox*, 2>>();
            auto syncing = std::make_shared<bool>(false);
            auto changed = [this, key, pan, boxes, syncing, toPan] {
                if (*syncing)
                    return;
                const QPointF v((*boxes)[0]->value(), (*boxes)[1]->value());
                const QSignalBlocker b(pan);
                pan->setValue(toPan(v));
                set(key, v);
            };
            (*boxes)[0] = sliderRow(this, sliders, p.min, p.max, p.decimals, def.x(), current.x(), tr("X"),
                                    [changed](double) { changed(); });
            (*boxes)[1] = sliderRow(this, sliders, p.minY, p.maxY, p.decimals, def.y(), current.y(), tr("Y"),
                                    [changed](double) { changed(); });
            sliders->addStretch();
            connect(pan, &PanControl::valueChanged, this, [this, key, boxes, syncing, fromPan](const QPointF& panValue) {
                const QPointF v = fromPan(panValue);
                *syncing = true;
                (*boxes)[0]->setValue(v.x());
                (*boxes)[1]->setValue(v.y());
                *syncing = false;
                set(key, QPointF((*boxes)[0]->value(), (*boxes)[1]->value()));
            });
            m_setters.insert(key, [boxes, syncing, pan, toPan](const QVariant& v) {
                *syncing = true;
                (*boxes)[0]->setValue(v.toPointF().x());
                (*boxes)[1]->setValue(v.toPointF().y());
                *syncing = false;
                const QSignalBlocker b(pan);
                pan->setValue(toPan(v.toPointF()));
            });
            auto* row = new QHBoxLayout;
            row->addWidget(pan);
            row->addLayout(sliders, 1);
            layout->addLayout(row);
            break;
        }
        case EffectParam::Type::Color: {
            title(true);
            describe();
            auto* button = new ColorButton(this);
            button->setColor(m_values.value(key).value<QColor>());
            connect(button, &ColorButton::colorChanged, this, [this, key](const QColor& c) { set(key, c); });
            auto* row = new QHBoxLayout;
            row->addWidget(button);
            row->addStretch();
            layout->addLayout(row);
            break;
        }
        case EffectParam::Type::Curves: {
            auto* editor = new CurvesEditor(this);
            m_extraButtons << editor->dialogButtons();
            editor->setValue(m_values.value(key).value<CurvesValue>());
            connect(editor, &CurvesEditor::valueChanged, this,
                    [this, key, editor] { set(key, QVariant::fromValue(editor->value())); });
            layout->addWidget(editor, 1);
            break;
        }
        case EffectParam::Type::Levels: {
            auto* editor = new LevelsEditor(this);
            m_extraButtons << editor->dialogButtons();
            editor->setSource(source, roi);
            editor->setValue(m_values.value(key).value<LevelsValue>());
            connect(editor, &LevelsEditor::valueChanged, this,
                    [this, key, editor] { set(key, QVariant::fromValue(editor->value())); });
            layout->addWidget(editor, 1);
            break;
        }
        case EffectParam::Type::Text: {
            title(false);
            describe();
            const QString current = m_values.value(key).toString();
            const int maxLength = p.max > 0 ? int(p.max) : 0;
            if (p.multiline) {
                auto* edit = new QPlainTextEdit(current, this);
                connect(edit, &QPlainTextEdit::textChanged, this, [this, key, edit, maxLength] {
                    QString t = edit->toPlainText();
                    if (maxLength > 0 && t.size() > maxLength)
                        t.truncate(maxLength);
                    set(key, t);
                });
                m_setters.insert(key, [edit](const QVariant& v) {
                    const QSignalBlocker b(edit);
                    edit->setPlainText(v.toString());
                });
                layout->addWidget(edit, 1);
            } else {
                auto* edit = new QLineEdit(current, this);
                if (maxLength > 0)
                    edit->setMaxLength(maxLength);
                connect(edit, &QLineEdit::textChanged, this, [this, key](const QString& t) { set(key, t); });
                m_setters.insert(key, [edit](const QVariant& v) {
                    const QSignalBlocker b(edit);
                    edit->setText(v.toString());
                });
                layout->addWidget(edit);
            }
            break;
        }
        }
    }
    updateReadOnly();

    const QString note = effect.dialogNote();
    if (!note.isEmpty()) {
        auto* label = new QLabel(note, this);
        label->setWordWrap(true);
        root->addWidget(label);
    }
    root->addSpacing(8);
    auto* box = okCancelButtons(this);
    // Restablecer, Automático...: a la izquierda de Aceptar/Cancelar, como Paint.NET.
    for (QPushButton* b : std::as_const(m_extraButtons))
        box->addButton(b, QDialogButtonBox::ResetRole);
    const QString help = effect.helpText();
    if (!help.isEmpty()) {
        // Ayuda del complemento (WindowHelpContent de IndirectUI).
        auto* helpButton = box->addButton(QDialogButtonBox::Help);
        connect(helpButton, &QPushButton::clicked, this, [this, help, title = effect.name()] {
            QMessageBox m(QMessageBox::Information, title, help, QMessageBox::Close, this);
            m.setTextFormat(Qt::AutoText);
            m.exec();
        });
    }
    root->addWidget(box);
    setMinimumWidth(380);
}

void EffectDialog::updateReadOnly()
{
    for (const EffectParam& p : m_params) {
        if (QWidget* w = m_containers.value(p.key))
            w->setEnabled(!p.isReadOnly(m_values));
        if (QWidget* body = m_bodies.value(p.key))
            body->setEnabled(m_values.value(p.enableKey).toBool());
    }
}

void EffectDialog::set(const QString& key, const QVariant& value)
{
    m_values.insert(key, value);
    // Parámetros vinculados (p. ej. ancho y alto): si la casilla está
    // marcada, los demás del grupo toman el mismo valor.
    if (!m_linking) {
        m_linking = true;
        for (const EffectParam& p : m_params) {
            if (p.key != key || p.linkKey.isEmpty())
                continue;
            if (!m_values.value(p.linkKey).toBool())
                break;
            for (const EffectParam& other : m_params) {
                if (other.key == key || other.linkKey != p.linkKey)
                    continue;
                const QVariant v = other.type == EffectParam::Type::Int ? QVariant(std::clamp(value.toInt(), int(other.min), int(other.max)))
                                                                        : QVariant(std::clamp(value.toDouble(), other.min, other.max));
                m_values.insert(other.key, v);
                if (auto setter = m_setters.value(other.key))
                    setter(v);
            }
            break;
        }
        // Al marcar la casilla, todos toman el valor del primero.
        for (const EffectParam& p : m_params) {
            if (p.linkKey != key || !value.toBool())
                continue;
            for (const EffectParam& other : m_params)
                if (other.linkKey == key && other.key != p.key) {
                    const QVariant v = m_values.value(p.key);
                    m_values.insert(other.key, v);
                    if (auto setter = m_setters.value(other.key))
                        setter(v);
                }
            break;
        }
        m_linking = false;
    }
    updateReadOnly();
    emit valuesChanged(m_values);
}

// ---------------------------------------------------------------------------

EffectPreview::EffectPreview(Document* doc, LayerPtr layer, const Effect& effect, const EffectContext& ctx)
    : m_doc(doc)
    , m_layer(std::move(layer))
    , m_effect(effect)
    , m_ctx(ctx)
    , m_source(m_layer->image)
    , m_selection(doc->selection())
    , m_progress(std::make_shared<EffectProgress>())
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(40);
    connect(&m_debounce, &QTimer::timeout, this, &EffectPreview::start);
    m_progressTimer.setInterval(100);
    connect(&m_progressTimer, &QTimer::timeout, this, [this] { emit progress(m_progress->permille()); });
}

EffectPreview::~EffectPreview()
{
    stop();
    m_doc->clearPreview();
}

void EffectPreview::request(const ParamValues& values)
{
    m_pending = values;
    m_hasPending = true;
    if (m_running && m_cancel)
        m_cancel->store(true);
    m_debounce.start();
}

void EffectPreview::start()
{
    if (m_running || !m_hasPending)
        return;
    if (m_thread.joinable())
        m_thread.join();
    m_hasPending = false;
    m_running = true;
    auto cancel = std::make_shared<std::atomic_bool>(false);
    m_cancel = cancel;
    const ParamValues values = m_pending;
    const int generation = ++m_generation;
    EffectContext ctx = m_ctx;
    ctx.progress = m_progress.get();
    m_progressTimer.start();
    m_thread = std::thread([this, cancel, values, generation, src = m_source, sel = m_selection, ctx,
                            progress = m_progress, effect = &m_effect] {
        const QImage out = Effects::apply(*effect, src, sel, values, ctx, *cancel);
        const bool cancelled = cancel->load();
        QMetaObject::invokeMethod(
            this, [this, out, values, cancelled, generation] { onFinished(out, values, cancelled, generation); },
            Qt::QueuedConnection);
    });
}

void EffectPreview::stop()
{
    ++m_generation;
    if (m_cancel)
        m_cancel->store(true);
    if (m_thread.joinable())
        m_thread.join();
    m_running = false;
    m_progressTimer.stop();
}

void EffectPreview::onFinished(const QImage& result, const ParamValues& values, bool cancelled, int generation)
{
    if (generation != m_generation)
        return;
    m_running = false;
    if (m_thread.joinable())
        m_thread.join();
    if (!cancelled && !result.isNull()) {
        m_lastResult = result;
        m_lastValues = values;
        m_doc->setPreview(m_layer.get(), result);
    }
    if (m_hasPending && !m_debounce.isActive())
        start();
    if (!m_running) {
        m_progressTimer.stop();
        emit progress(-1);
    }
}

QImage EffectPreview::finalResult(const ParamValues& values, QWidget* parent)
{
    m_debounce.stop();
    if (!m_running && !m_hasPending && !m_lastResult.isNull() && m_lastValues == values)
        return m_lastResult;
    stop();
    m_hasPending = false;
    emit progress(-1);
    return applyEffectWithProgress(m_effect, m_source, m_selection, values, m_ctx, parent);
}

QImage applyEffectWithProgress(const Effect& effect, const QImage& src, const Selection& selection,
                               const ParamValues& values, EffectContext ctx, QWidget* parent)
{
    auto progress = std::make_shared<EffectProgress>();
    auto cancel = std::make_shared<std::atomic_bool>(false);
    auto done = std::make_shared<std::atomic_bool>(false);
    ctx.progress = progress.get();
    QImage result;
    std::thread worker([&, ctx] {
        result = Effects::apply(effect, src, selection, values, ctx, *cancel);
        done->store(true);
    });

    // Ventana de progreso solo si tarda más de medio segundo.
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QElapsedTimer clock;
    clock.start();
    std::unique_ptr<QProgressDialog> dialog;
    while (!done->load()) {
        if (!dialog && clock.elapsed() > 500) {
            QApplication::restoreOverrideCursor();
            dialog = std::make_unique<QProgressDialog>(QObject::tr("Aplicando «%1»…").arg(effect.name()),
                                                       QObject::tr("Cancelar"), 0, 1000, parent);
            dialog->setWindowTitle(effect.name());
            dialog->setWindowModality(Qt::WindowModal);
            dialog->setMinimumDuration(0);
            QObject::connect(dialog.get(), &QProgressDialog::canceled, [cancel] { cancel->store(true); });
            dialog->show();
        }
        if (dialog)
            dialog->setValue(progress->permille());
        // Sin ventana de progreso no se atiende al usuario (evita reentradas).
        QApplication::processEvents(dialog ? QEventLoop::AllEvents : QEventLoop::ExcludeUserInputEvents, 30);
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    worker.join();
    if (!dialog)
        QApplication::restoreOverrideCursor();
    return cancel->load() ? QImage() : result;
}
