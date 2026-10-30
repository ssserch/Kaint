#include "app/widgets/EffectControls.h"

#include "app/widgets/ColorWidgets.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QRadialGradient>
#include <QPainterPath>
#include <QFrame>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <array>
#include <cmath>

// ---------------------------------------------------------------------------
// AngleChooser
// ---------------------------------------------------------------------------

AngleChooser::AngleChooser(double min, double max, QWidget* parent)
    : QWidget(parent)
    , m_min(min)
    , m_max(max)
{
    setFixedSize(64, 64);
    setCursor(Qt::PointingHandCursor);
    setToolTip(tr("Arrastra para elegir el ángulo (Mayús: de 15 en 15°)"));
}

void AngleChooser::setValue(double degrees)
{
    degrees = std::clamp(degrees, m_min, m_max);
    if (qFuzzyCompare(degrees + 1000, m_value + 1000))
        return;
    m_value = degrees;
    update();
    emit valueChanged(m_value);
}

void AngleChooser::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(3, 3, -3, -3);
    p.setPen(QPen(palette().color(QPalette::Mid), 1));
    p.setBrush(palette().color(QPalette::Base));
    p.drawEllipse(r);
    const QPointF c = r.center();
    const double a = m_value * M_PI / 180.0;
    const QPointF tip = c + QPointF(std::cos(a), -std::sin(a)) * (r.width() / 2 - 2);
    p.setPen(QPen(palette().color(QPalette::Highlight), 2, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(c, tip);
    p.setBrush(palette().color(QPalette::Highlight));
    p.drawEllipse(tip, 3, 3);
    p.drawEllipse(c, 2, 2);
}

void AngleChooser::setFromPos(const QPointF& pos, bool snap)
{
    const QPointF d = pos - QRectF(rect()).center();
    if (d.isNull())
        return;
    double deg = std::atan2(-d.y(), d.x()) * 180.0 / M_PI;
    if (snap)
        deg = std::round(deg / 15.0) * 15.0;
    while (deg < m_min)
        deg += 360;
    while (deg > m_max)
        deg -= 360;
    setValue(deg);
}

void AngleChooser::mousePressEvent(QMouseEvent* e)
{
    setFromPos(e->position(), e->modifiers() & Qt::ShiftModifier);
}

void AngleChooser::mouseMoveEvent(QMouseEvent* e)
{
    if (e->buttons() & Qt::LeftButton)
        setFromPos(e->position(), e->modifiers() & Qt::ShiftModifier);
}

// ---------------------------------------------------------------------------
// PanControl
// ---------------------------------------------------------------------------

PanControl::PanControl(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(120, 90);
    setCursor(Qt::CrossCursor);
}

void PanControl::setImage(const QImage& thumbnail)
{
    m_image = thumbnail;
    update();
}

void PanControl::setValue(const QPointF& v)
{
    const QPointF c(std::clamp(v.x(), -1.0, 1.0), std::clamp(v.y(), -1.0, 1.0));
    if (c == m_value)
        return;
    m_value = c;
    update();
    emit valueChanged(m_value);
}

QRectF PanControl::imageRect() const
{
    const QSizeF avail = QSizeF(size()) - QSizeF(4, 4);
    QSizeF s = m_image.isNull() ? avail : QSizeF(m_image.size()).scaled(avail, Qt::KeepAspectRatio);
    return QRectF(QPointF((width() - s.width()) / 2, (height() - s.height()) / 2), s);
}

void PanControl::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const QRectF r = imageRect();
    paintCheckerboard(p, r);
    if (!m_image.isNull()) {
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(r, m_image);
    }
    p.setPen(palette().color(QPalette::Mid));
    p.setBrush(Qt::NoBrush);
    p.drawRect(r);
    const QPointF c(r.center().x() + m_value.x() * r.width() / 2, r.center().y() + m_value.y() * r.height() / 2);
    p.setRenderHint(QPainter::Antialiasing);
    for (const QColor& col : {QColor(255, 255, 255), QColor(0, 0, 0)}) {
        const qreal w = col == Qt::white ? 3 : 1;
        p.setPen(QPen(col, w));
        p.drawLine(c - QPointF(9, 0), c - QPointF(3, 0));
        p.drawLine(c + QPointF(3, 0), c + QPointF(9, 0));
        p.drawLine(c - QPointF(0, 9), c - QPointF(0, 3));
        p.drawLine(c + QPointF(0, 3), c + QPointF(0, 9));
        p.drawEllipse(c, 6, 6);
    }
}

void PanControl::setFromPos(const QPointF& pos)
{
    const QRectF r = imageRect();
    setValue(QPointF((pos.x() - r.center().x()) / (r.width() / 2), (pos.y() - r.center().y()) / (r.height() / 2)));
}

void PanControl::mousePressEvent(QMouseEvent* e)
{
    setFromPos(e->position());
}

void PanControl::mouseMoveEvent(QMouseEvent* e)
{
    if (e->buttons() & Qt::LeftButton)
        setFromPos(e->position());
}

// ---------------------------------------------------------------------------
// ColorButton
// ---------------------------------------------------------------------------

ColorButton::ColorButton(QWidget* parent)
    : QWidget(parent)
{
    setCursor(Qt::PointingHandCursor);
    setMinimumSize(48, 24);
    setToolTip(tr("Haz clic para elegir el color"));
}

void ColorButton::setColor(const QColor& c)
{
    if (c == m_color)
        return;
    m_color = c;
    update();
    emit colorChanged(c);
}

void ColorButton::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const QRectF r = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
    paintCheckerboard(p, r);
    p.fillRect(r, m_color);
    p.setPen(QPen(palette().color(QPalette::Mid), 1));
    p.drawRect(r);
}

void ColorButton::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return;
    const QColor c = QColorDialog::getColor(m_color, this, tr("Elegir color"), QColorDialog::ShowAlphaChannel);
    if (c.isValid())
        setColor(c);
}

// ---------------------------------------------------------------------------
// Histogramas y niveles
// ---------------------------------------------------------------------------

HistogramView::HistogramView(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(160, 60);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void HistogramView::setImage(const QImage& image, const QRect& rect)
{
    for (auto& h : m_hist)
        h.fill(0);
    const QRect r = rect.intersected(image.rect());
    const bool pm = image.format() == QImage::Format_ARGB32_Premultiplied;
    for (int y = r.top(); y <= r.bottom(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = r.left(); x <= r.right(); ++x) {
            if (qAlpha(row[x]) == 0)
                continue;
            const QRgb u = pm ? qUnpremultiply(row[x]) : row[x];
            ++m_hist[0][size_t(qRed(u))];
            ++m_hist[1][size_t(qGreen(u))];
            ++m_hist[2][size_t(qBlue(u))];
        }
    }
    update();
}

void HistogramView::setChannels(const std::array<bool, 3>& shown)
{
    m_shown = shown;
    update();
}

void HistogramView::setVertical(bool vertical, bool towardsRight)
{
    m_vertical = vertical;
    m_towardsRight = towardsRight;
    setMinimumSize(vertical ? QSize(60, 160) : QSize(160, 60));
    setSizePolicy(vertical ? QSizePolicy::Preferred : QSizePolicy::Expanding,
                  vertical ? QSizePolicy::Expanding : QSizePolicy::Preferred);
    updateGeometry();
    update();
}

void HistogramView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.fillRect(r, palette().color(QPalette::Base));
    qint64 peak = 1;
    for (int c = 0; c < 3; ++c)
        if (m_shown[size_t(c)])
            for (qint64 v : m_hist[size_t(c)])
                peak = std::max(peak, v);
    static const QColor colors[3] = {QColor(230, 40, 40, 120), QColor(40, 190, 40, 120), QColor(40, 80, 240, 120)};
    p.setRenderHint(QPainter::Antialiasing);
    // Sobre un fondo oscuro, "multiplicar" apenas deja ver los canales: ahí se aclara.
    const bool dark = palette().color(QPalette::Base).lightness() < 128;
    p.setCompositionMode(dark ? QPainter::CompositionMode_Screen : QPainter::CompositionMode_Multiply);
    for (int c = 0; c < 3; ++c) {
        if (!m_shown[size_t(c)])
            continue;
        QPainterPath path;
        auto level = [&](int i) { return std::sqrt(double(m_hist[size_t(c)][size_t(i)]) / double(peak)); };
        if (!m_vertical) {
            path.moveTo(r.bottomLeft());
            for (int i = 0; i < 256; ++i)
                path.lineTo(r.left() + r.width() * i / 255.0, r.bottom() - level(i) * r.height());
            path.lineTo(r.bottomRight());
        } else {
            const double base = m_towardsRight ? r.left() : r.right();
            const double dir = m_towardsRight ? 1 : -1;
            path.moveTo(base, r.bottom());
            for (int i = 0; i < 256; ++i)
                path.lineTo(base + dir * level(i) * r.width(), r.bottom() - r.height() * i / 255.0);
            path.lineTo(base, r.top());
        }
        path.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(colors[c]);
        p.drawPath(path);
    }
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    p.setPen(palette().color(QPalette::Mid));
    p.setBrush(Qt::NoBrush);
    p.drawRect(r);
}

LevelsGradient::LevelsGradient(int handles, QWidget* parent)
    : QWidget(parent)
{
    for (int i = 0; i < handles; ++i)
        m_values.append(handles == 1 ? 128 : 255.0 * i / (handles - 1));
    setMinimumSize(160, 24);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void LevelsGradient::setValues(const QList<double>& values)
{
    m_values = values;
    update();
}

void LevelsGradient::setTint(const QColor& c)
{
    m_tint = c;
    update();
}

void LevelsGradient::setVertical(bool vertical)
{
    m_vertical = vertical;
    setMinimumSize(vertical ? QSize(26, 160) : QSize(160, 24));
    setSizePolicy(vertical ? QSizePolicy::Fixed : QSizePolicy::Expanding,
                  vertical ? QSizePolicy::Expanding : QSizePolicy::Fixed);
    updateGeometry();
    update();
}

QRectF LevelsGradient::barRect() const
{
    if (m_vertical)
        return QRectF(7, 6, width() - 14, height() - 12);
    return QRectF(6, 2, width() - 12, height() - 12);
}

double LevelsGradient::xFor(double v) const
{
    const QRectF b = barRect();
    if (m_vertical)
        return b.bottom() - b.height() * v / 255.0;
    return b.left() + b.width() * v / 255.0;
}

double LevelsGradient::valueAt(const QPointF& pos) const
{
    const QRectF b = barRect();
    const double t = m_vertical ? (b.bottom() - pos.y()) / b.height() : (pos.x() - b.left()) / b.width();
    return std::clamp(t * 255.0, 0.0, 255.0);
}

void LevelsGradient::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const QRectF b = barRect();
    QLinearGradient g = m_vertical ? QLinearGradient(b.bottomLeft(), b.topLeft()) : QLinearGradient(b.topLeft(), b.topRight());
    g.setColorAt(0, Qt::black);
    g.setColorAt(1, m_tint);
    p.fillRect(b, g);
    p.setPen(palette().color(QPalette::Mid));
    p.drawRect(b);
    p.setRenderHint(QPainter::Antialiasing);
    for (int i = 0; i < m_values.size(); ++i) {
        const double x = xFor(m_values[i]);
        const double t = m_values.size() == 1 ? 0.5 : double(i) / (m_values.size() - 1);
        p.setPen(QPen(palette().color(QPalette::WindowText), 1));
        p.setBrush(QColor::fromRgbF(float(t), float(t), float(t)));
        if (m_vertical) {
            p.drawPolygon(QPolygonF{QPointF(b.left() - 1, x), QPointF(0.5, x - 5), QPointF(0.5, x + 5)});
            p.drawPolygon(QPolygonF{QPointF(b.right() + 1, x), QPointF(width() - 0.5, x - 5), QPointF(width() - 0.5, x + 5)});
        } else {
            p.drawPolygon(QPolygonF{QPointF(x, b.bottom() + 1), QPointF(x - 6, height() - 1.0), QPointF(x + 6, height() - 1.0)});
        }
    }
}

void LevelsGradient::mousePressEvent(QMouseEvent* e)
{
    double best = 1e9;
    for (int i = 0; i < m_values.size(); ++i) {
        const double pos = m_vertical ? e->position().y() : e->position().x();
        const double d = std::abs(xFor(m_values[i]) - pos);
        if (d < best) {
            best = d;
            m_drag = i;
        }
    }
    mouseMoveEvent(e);
}

void LevelsGradient::mouseMoveEvent(QMouseEvent* e)
{
    if (m_drag < 0)
        return;
    double v = valueAt(e->position());
    if (m_drag > 0)
        v = std::max(v, m_values[m_drag - 1] + 1);
    if (m_drag < m_values.size() - 1)
        v = std::min(v, m_values[m_drag + 1] - 1);
    m_values[m_drag] = v;
    update();
    emit handleMoved(m_drag, v);
}

void LevelsGradient::mouseReleaseEvent(QMouseEvent*)
{
    m_drag = -1;
}

LevelsEditor::LevelsEditor(QWidget* parent)
    : QWidget(parent)
{
    m_inHist = new HistogramView(this);
    m_outHist = new HistogramView(this);
    m_inBar = new LevelsGradient(2, this);
    m_outBar = new LevelsGradient(3, this);
    auto spin = [this](int def) {
        auto* s = new QSpinBox(this);
        s->setRange(0, 255);
        s->setValue(def);
        return s;
    };
    m_inLow = spin(0);
    m_inHigh = spin(255);
    m_outLow = spin(0);
    m_outHigh = spin(255);
    m_gamma = new QDoubleSpinBox(this);
    m_gamma->setRange(0.1, 10);
    m_gamma->setDecimals(2);
    m_gamma->setSingleStep(0.1);
    m_gamma->setValue(1);

    // Disposición vertical de Paint.NET 5: histograma de entrada | entrada | salida |
    // histograma de salida, con el blanco arriba y el negro abajo.
    m_inHist->setVertical(true, false);
    m_outHist->setVertical(true, true);
    m_inBar->setVertical(true);
    m_outBar->setVertical(true);
    for (QFrame*& sw : m_swatches) {
        sw = new QFrame(this);
        sw->setFrameShape(QFrame::Box);
        sw->setFixedSize(44, 20);
        sw->setAutoFillBackground(true);
    }
    auto title = [this](const QString& text) {
        auto* w = new QWidget(this);
        auto* l = new QHBoxLayout(w);
        l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(new QLabel(text, w));
        auto* rule = new QFrame(w);
        rule->setFrameShape(QFrame::HLine);
        rule->setFrameShadow(QFrame::Sunken);
        l->addWidget(rule, 1);
        return w;
    };
    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(6);
    grid->addWidget(title(tr("Histograma de entrada")), 0, 0);
    grid->addWidget(title(tr("Entrada")), 0, 1, 1, 2);
    grid->addWidget(title(tr("Salida")), 0, 3, 1, 2);
    grid->addWidget(title(tr("Histograma de salida")), 0, 5);
    grid->addWidget(m_inHist, 1, 0, 7, 1);
    grid->addWidget(m_inHigh, 1, 1);
    grid->addWidget(m_swatches[0], 2, 1);
    grid->addWidget(m_swatches[1], 6, 1);
    grid->addWidget(m_inLow, 7, 1);
    grid->addWidget(m_inBar, 1, 2, 7, 1);
    grid->addWidget(m_outBar, 1, 3, 7, 1);
    grid->addWidget(m_outHigh, 1, 4);
    grid->addWidget(m_swatches[2], 2, 4);
    grid->addWidget(m_gamma, 3, 4);
    grid->addWidget(m_swatches[3], 4, 4);
    grid->addWidget(m_swatches[4], 6, 4);
    grid->addWidget(m_outLow, 7, 4);
    grid->addWidget(m_outHist, 1, 5, 7, 1);
    grid->setRowStretch(5, 1);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(5, 1);

    // R, V, A: los canales que se ajustan (en español, como Paint.NET).
    auto* checks = new QHBoxLayout;
    checks->addStretch();
    const QString names[3] = {tr("R"), tr("V"), tr("A")};
    const QString tips[3] = {tr("Rojo"), tr("Verde"), tr("Azul")};
    for (int c = 0; c < 3; ++c) {
        m_checks[size_t(c)] = new QCheckBox(names[c], this);
        m_checks[size_t(c)]->setToolTip(tips[c]);
        m_checks[size_t(c)]->setChecked(true);
        checks->addWidget(m_checks[size_t(c)]);
        connect(m_checks[size_t(c)], &QCheckBox::toggled, this, [this] { syncControls(); });
    }
    checks->addStretch();
    grid->addLayout(checks, 8, 0, 1, 6);

    m_auto = new QPushButton(tr("Automático"), this);
    m_auto->setToolTip(tr("Ajusta los niveles según el histograma de la imagen"));
    m_reset = new QPushButton(tr("Restablecer"), this);
    auto* autoButton = m_auto;
    auto* reset = m_reset;

    connect(m_inLow, &QSpinBox::valueChanged, this, [this](int v) { edit([v](LevelsValue& l, int c) { l.inLow[size_t(c)] = v; }); });
    connect(m_inHigh, &QSpinBox::valueChanged, this, [this](int v) { edit([v](LevelsValue& l, int c) { l.inHigh[size_t(c)] = v; }); });
    connect(m_outLow, &QSpinBox::valueChanged, this, [this](int v) { edit([v](LevelsValue& l, int c) { l.outLow[size_t(c)] = v; }); });
    connect(m_outHigh, &QSpinBox::valueChanged, this, [this](int v) { edit([v](LevelsValue& l, int c) { l.outHigh[size_t(c)] = v; }); });
    connect(m_gamma, &QDoubleSpinBox::valueChanged, this, [this](double v) { edit([v](LevelsValue& l, int c) { l.gamma[size_t(c)] = v; }); });
    connect(m_inBar, &LevelsGradient::handleMoved, this, [this](int i, double v) {
        (i == 0 ? m_inLow : m_inHigh)->setValue(int(std::lround(v)));
    });
    connect(m_outBar, &LevelsGradient::handleMoved, this, [this](int i, double v) {
        if (i == 0)
            m_outLow->setValue(int(std::lround(v)));
        else if (i == 2)
            m_outHigh->setValue(int(std::lround(v)));
        else {
            const double lo = m_outLow->value(), hi = m_outHigh->value();
            const double t = std::clamp((v - lo) / std::max(1.0, hi - lo), 0.001, 0.999);
            m_gamma->setValue(std::clamp(std::log(t) / std::log(0.5), 0.1, 10.0));
        }
    });
    connect(autoButton, &QPushButton::clicked, this, [this] {
        if (m_source.isNull())
            return;
        setValue(LevelsValue::automatic(m_source, m_rect));
        emit valueChanged();
    });
    connect(reset, &QPushButton::clicked, this, [this] {
        setValue(LevelsValue());
        emit valueChanged();
    });
}

void LevelsEditor::setSource(const QImage& image, const QRect& rect)
{
    m_source = image;
    m_rect = rect;
    m_inHist->setImage(image, rect);
    refreshOutputHistogram();
}

void LevelsEditor::setValue(const LevelsValue& v)
{
    m_value = v;
    syncControls();
    refreshOutputHistogram();
}

QList<int> LevelsEditor::channels() const
{
    QList<int> list;
    for (int c = 0; c < 3; ++c)
        if (m_checks[size_t(c)]->isChecked())
            list << c;
    return list;
}

template <typename F>
void LevelsEditor::edit(F f)
{
    if (m_syncing)
        return;
    for (int c : channels())
        f(m_value, c);
    syncControls();
    refreshOutputHistogram();
    emit valueChanged();
}

void LevelsEditor::syncControls()
{
    const QList<int> chans = channels();
    auto avg = [&](const auto& arr) {
        if (chans.isEmpty())
            return double(arr[0]);
        double s = 0;
        for (int c : chans)
            s += arr[size_t(c)];
        return s / chans.size();
    };
    m_syncing = true;
    m_inLow->setValue(int(std::lround(avg(m_value.inLow))));
    m_inHigh->setValue(int(std::lround(avg(m_value.inHigh))));
    m_outLow->setValue(int(std::lround(avg(m_value.outLow))));
    m_outHigh->setValue(int(std::lround(avg(m_value.outHigh))));
    m_gamma->setValue(avg(m_value.gamma));
    const double lo = m_outLow->value(), hi = m_outHigh->value();
    m_inBar->setValues({double(m_inLow->value()), double(m_inHigh->value())});
    m_outBar->setValues({lo, lo + (hi - lo) * std::pow(0.5, m_gamma->value()), hi});
    m_syncing = false;
    const QColor tint(m_checks[0]->isChecked() ? 255 : 0, m_checks[1]->isChecked() ? 255 : 0, m_checks[2]->isChecked() ? 255 : 0);
    m_inBar->setTint(tint);
    m_outBar->setTint(tint);
    const std::array<bool, 3> shown{m_checks[0]->isChecked(), m_checks[1]->isChecked(), m_checks[2]->isChecked()};
    m_inHist->setChannels(shown);
    m_outHist->setChannels(shown);
    auto swatch = [&](QFrame* f, int v) {
        QPalette pal = f->palette();
        pal.setColor(QPalette::Window, QColor(shown[0] ? v : 0, shown[1] ? v : 0, shown[2] ? v : 0));
        f->setPalette(pal);
    };
    swatch(m_swatches[0], m_inHigh->value());
    swatch(m_swatches[1], m_inLow->value());
    swatch(m_swatches[2], m_outHigh->value());
    swatch(m_swatches[3], int(std::lround(lo + (hi - lo) * std::pow(0.5, m_gamma->value()))));
    swatch(m_swatches[4], m_outLow->value());
}

void LevelsEditor::refreshOutputHistogram()
{
    if (m_source.isNull())
        return;
    QImage small = m_source.copy(m_rect.intersected(m_source.rect()));
    if (small.width() > 256 || small.height() > 256)
        small = small.scaled(256, 256, Qt::KeepAspectRatio, Qt::FastTransformation);
    small = small.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const auto r = m_value.lut(0), g = m_value.lut(1), b = m_value.lut(2);
    for (int y = 0; y < small.height(); ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(small.scanLine(y));
        for (int x = 0; x < small.width(); ++x) {
            const QRgb u = qUnpremultiply(row[x]);
            row[x] = qPremultiply(qRgba(r[size_t(qRed(u))], g[size_t(qGreen(u))], b[size_t(qBlue(u))], qAlpha(u)));
        }
    }
    m_outHist->setImage(small, small.rect());
}

// ---------------------------------------------------------------------------

RollBallControl::RollBallControl(double maxTilt, QWidget* parent)
    : QWidget(parent)
    , m_maxTilt(maxTilt)
{
    setMinimumSize(80, 80);
    setCursor(Qt::OpenHandCursor);
    setToolTip(tr("Arrastra para inclinar; con el botón derecho (o Mayús) para girar"));
}

void RollBallControl::setValue(const QVector3D& v)
{
    m_value = v;
    update();
}

QPointF RollBallControl::center() const
{
    return QRectF(rect()).center();
}

qreal RollBallControl::radius() const
{
    return std::min(width(), height()) / 2.0 - 3;
}

void RollBallControl::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPointF c = center();
    const qreal r = radius();
    const QPalette pal = palette();
    QRadialGradient shade(c - QPointF(r * 0.35, r * 0.35), r * 1.4);
    shade.setColorAt(0, pal.color(QPalette::Midlight));
    shade.setColorAt(1, pal.color(QPalette::Mid));
    p.setPen(QPen(pal.color(QPalette::Dark), 1));
    p.setBrush(shade);
    p.drawEllipse(c, r, r);

    // Rotación del globo: la misma que aplica Giro y zum a la imagen (girar, inclinar hacia la
    // dirección). Se mira desde el polo (+z).
    const double deg = M_PI / 180.0;
    const double a = m_value.x() * deg, dir = m_value.y() * deg, tilt = m_value.z() * deg;
    auto rotZ = [](double& x, double& y, double t) {
        const double nx = x * std::cos(t) - y * std::sin(t);
        y = x * std::sin(t) + y * std::cos(t);
        x = nx;
    };
    auto transform = [&](double x, double y, double z) {
        rotZ(x, y, a);
        rotZ(x, y, -dir);
        const double ny = y * std::cos(tilt) - z * std::sin(tilt);
        z = y * std::sin(tilt) + z * std::cos(tilt);
        y = ny;
        rotZ(x, y, dir);
        return std::array<double, 3>{x, y, z};
    };
    const QColor line = pal.color(QPalette::Text);
    QColor front = line;
    front.setAlpha(200);
    QColor back = line;
    back.setAlpha(40);
    auto segment = [&](const std::array<double, 3>& u, const std::array<double, 3>& v) {
        const bool visible = u[2] + v[2] > 0;
        p.setPen(QPen(visible ? front : back, visible ? 1.0 : 0.8));
        p.drawLine(c + QPointF(u[0], u[1]) * r, c + QPointF(v[0], v[1]) * r);
    };
    for (int polar = 15; polar <= 90; polar += 15) {
        const double t = polar * deg;
        std::array<double, 3> prev{};
        for (int i = 0; i <= 48; ++i) {
            const double ph = i * 2 * M_PI / 48;
            const auto q = transform(std::sin(t) * std::cos(ph), std::sin(t) * std::sin(ph), std::cos(t));
            if (i > 0)
                segment(prev, q);
            prev = q;
        }
    }
    for (int m = 0; m < 12; ++m) {
        const double ph = m * 2 * M_PI / 12;
        std::array<double, 3> prev{};
        for (int i = 0; i <= 12; ++i) {
            const double t = i * (M_PI / 2) / 12;
            const auto q = transform(std::sin(t) * std::cos(ph), std::sin(t) * std::sin(ph), std::cos(t));
            if (i > 0)
                segment(prev, q);
            prev = q;
        }
    }
    const auto pole = transform(0, 0, 1);
    p.setPen(QPen(pal.color(QPalette::Highlight), 2));
    p.setBrush(pal.color(QPalette::Highlight));
    p.drawEllipse(c + QPointF(pole[0], pole[1]) * r, 2.5, 2.5);
}

void RollBallControl::mousePressEvent(QMouseEvent* e)
{
    m_rotating = e->button() == Qt::RightButton || (e->modifiers() & Qt::ShiftModifier);
    m_last = e->position();
    setCursor(Qt::ClosedHandCursor);
    if (!m_rotating)
        dragTo(e->position());
}

void RollBallControl::mouseMoveEvent(QMouseEvent* e)
{
    if (!(e->buttons() & (Qt::LeftButton | Qt::RightButton)))
        return;
    if (m_rotating) {
        const QPointF c = center();
        const double before = std::atan2(m_last.y() - c.y(), m_last.x() - c.x());
        const double now = std::atan2(e->position().y() - c.y(), e->position().x() - c.x());
        double angle = m_value.x() + (now - before) * 180.0 / M_PI;
        while (angle > 180)
            angle -= 360;
        while (angle < -180)
            angle += 360;
        m_last = e->position();
        m_value.setX(float(angle));
        update();
        emit valueChanged(m_value);
        return;
    }
    dragTo(e->position());
}

void RollBallControl::dragTo(const QPointF& pos)
{
    // La distancia al centro es la inclinación y el ángulo, su dirección: el polo del globo
    // (el centro de la imagen) queda bajo el puntero.
    const QPointF v = (pos - center()) / radius();
    const double len = std::min(1.0, std::hypot(v.x(), v.y()));
    const double tilt = std::min(m_maxTilt, std::asin(len) * 180.0 / M_PI);
    double direction = len > 1e-6 ? std::atan2(v.y(), v.x()) * 180.0 / M_PI + 90.0 : m_value.y();
    if (direction > 180)
        direction -= 360;
    m_value.setY(float(direction));
    m_value.setZ(float(tilt));
    update();
    emit valueChanged(m_value);
}
