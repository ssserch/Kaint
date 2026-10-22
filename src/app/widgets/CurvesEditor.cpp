#include "app/widgets/CurvesEditor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

CurveGraph::CurveGraph(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(200, 200);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCursor(Qt::CrossCursor);
    setMouseTracking(true);
}

void CurveGraph::setValue(const CurvesValue& v)
{
    m_value = v;
    update();
}

void CurveGraph::setChannels(int channel, const QList<int>& followers)
{
    m_channel = std::clamp(channel, 0, 3);
    m_followers = followers;
    update();
}

void CurveGraph::propagate()
{
    for (int c : m_followers)
        if (c != m_channel)
            m_value.points[size_t(c)] = m_value.points[size_t(m_channel)];
}

QRectF CurveGraph::area() const
{
    const qreal side = std::min(width(), height()) - 10;
    return QRectF(5, 5, side, side);
}

QPointF CurveGraph::toWidget(const QPointF& v) const
{
    const QRectF a = area();
    return QPointF(a.left() + v.x() / 255.0 * a.width(), a.bottom() - v.y() / 255.0 * a.height());
}

QPointF CurveGraph::toValue(const QPointF& w) const
{
    const QRectF a = area();
    return QPointF(std::clamp((w.x() - a.left()) / a.width() * 255.0, 0.0, 255.0),
                   std::clamp((a.bottom() - w.y()) / a.height() * 255.0, 0.0, 255.0));
}

int CurveGraph::hit(const QPointF& w) const
{
    const auto& pts = m_value.points[size_t(m_channel)];
    for (int i = 0; i < pts.size(); ++i)
        if (QLineF(toWidget(pts[i]), w).length() <= 7)
            return i;
    return -1;
}

void CurveGraph::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const QRectF a = area();
    p.fillRect(a, palette().color(QPalette::Base));
    p.setPen(QPen(palette().color(QPalette::Mid), 0, Qt::DotLine));
    for (int i = 1; i < 4; ++i) {
        p.drawLine(QPointF(a.left() + a.width() * i / 4, a.top()), QPointF(a.left() + a.width() * i / 4, a.bottom()));
        p.drawLine(QPointF(a.left(), a.top() + a.height() * i / 4), QPointF(a.right(), a.top() + a.height() * i / 4));
    }
    p.drawLine(a.bottomLeft(), a.topRight());
    p.setPen(palette().color(QPalette::Mid));
    p.drawRect(a);

    p.setRenderHint(QPainter::Antialiasing);
    static const QColor channelColors[4] = {QColor(), QColor(220, 40, 40), QColor(40, 170, 40), QColor(40, 80, 230)};
    auto drawCurve = [&](int channel, bool active) {
        const auto lut = m_value.lut(channel);
        QPainterPath path;
        for (int i = 0; i < 256; ++i) {
            const QPointF pt = toWidget(QPointF(i, lut[i]));
            if (i == 0)
                path.moveTo(pt);
            else
                path.lineTo(pt);
        }
        QColor c = channel == 0 ? palette().color(QPalette::Text) : channelColors[channel];
        if (!active)
            c.setAlpha(90);
        p.setPen(QPen(c, active ? 2 : 1));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    };
    if (m_value.mode == CurvesValue::Rgb) {
        for (int c = 1; c < 4; ++c)
            if (c != m_channel && !m_followers.contains(c))
                drawCurve(c, false);
        for (int c : m_followers)
            if (c != m_channel)
                drawCurve(c, true);
    }
    drawCurve(m_channel, true);

    p.setPen(QPen(palette().color(QPalette::Text), 1));
    p.setBrush(palette().color(QPalette::Base));
    for (const QPointF& pt : m_value.points[size_t(m_channel)])
        p.drawRect(QRectF(toWidget(pt) - QPointF(4, 4), QSizeF(8, 8)));
}

void CurveGraph::mousePressEvent(QMouseEvent* e)
{
    auto& pts = m_value.points[size_t(m_channel)];
    const int index = hit(e->position());
    if (e->button() == Qt::RightButton) {
        if (index >= 0 && pts.size() > 2) {
            pts.removeAt(index);
            propagate();
            update();
            emit edited();
        }
        return;
    }
    if (e->button() != Qt::LeftButton)
        return;
    if (index >= 0) {
        m_drag = index;
        return;
    }
    const QPointF v = toValue(e->position());
    pts.append(v);
    std::sort(pts.begin(), pts.end(), [](const QPointF& a, const QPointF& b) { return a.x() < b.x(); });
    m_drag = int(pts.indexOf(v));
    propagate();
    update();
    emit edited();
}

void CurveGraph::mouseMoveEvent(QMouseEvent* e)
{
    emit hovered(toValue(e->position()), area().contains(e->position()));
    if (m_drag < 0)
        return;
    auto& pts = m_value.points[size_t(m_channel)];
    QPointF v = toValue(e->position());
    // Cada punto queda entre sus vecinos para que la curva siga siendo una función.
    const qreal lo = m_drag > 0 ? pts[m_drag - 1].x() + 1 : 0;
    const qreal hi = m_drag < pts.size() - 1 ? pts[m_drag + 1].x() - 1 : 255;
    v.setX(std::clamp(v.x(), lo, hi));
    pts[m_drag] = v;
    propagate();
    update();
    emit edited();
}

void CurveGraph::mouseReleaseEvent(QMouseEvent*)
{
    m_drag = -1;
}

// ---------------------------------------------------------------------------

CurvesEditor::CurvesEditor(QWidget* parent)
    : QWidget(parent)
{
    m_mode = new QComboBox(this);
    m_mode->addItems({tr("Luminosidad"), tr("RGB")});
    m_red = new QCheckBox(tr("Rojo"), this);
    m_green = new QCheckBox(tr("Verde"), this);
    m_blue = new QCheckBox(tr("Azul"), this);
    for (QCheckBox* c : {m_red, m_green, m_blue})
        c->setChecked(true);
    m_graph = new CurveGraph(this);
    m_coords = new QLabel(this);
    m_reset = new QPushButton(tr("Restablecer"), this);

    // Como Paint.NET 5: título, lista, gráfico, casillas y la sugerencia.
    auto* title = new QHBoxLayout;
    title->addWidget(new QLabel(tr("Mapa de transferencia"), this));
    auto* rule = new QFrame(this);
    rule->setFrameShape(QFrame::HLine);
    rule->setFrameShadow(QFrame::Sunken);
    title->addWidget(rule, 1);

    auto* modeRow = new QHBoxLayout;
    modeRow->addWidget(m_mode);
    modeRow->addStretch();

    auto* checks = new QHBoxLayout;
    checks->addWidget(m_red);
    checks->addWidget(m_green);
    checks->addWidget(m_blue);
    checks->addStretch();
    checks->addWidget(m_coords);

    auto* help = new QLabel(tr("Sugerencia: pulse con el botón secundario del ratón para quitar puntos de control."), this);
    help->setWordWrap(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(title);
    layout->addLayout(modeRow);
    layout->addWidget(m_graph, 1);
    layout->addLayout(checks);
    layout->addWidget(help);

    connect(m_mode, &QComboBox::currentIndexChanged, this, [this](int mode) {
        CurvesValue v = m_graph->value();
        v.mode = mode;
        m_graph->setValue(v);
        updateChannel();
        emit valueChanged();
    });
    for (QCheckBox* c : {m_red, m_green, m_blue})
        connect(c, &QCheckBox::toggled, this, &CurvesEditor::updateChannel);
    connect(m_graph, &CurveGraph::edited, this, &CurvesEditor::valueChanged);
    connect(m_graph, &CurveGraph::hovered, this, [this](const QPointF& v, bool inside) {
        m_coords->setText(inside ? QStringLiteral("(%1, %2)").arg(qRound(v.x())).arg(qRound(v.y())) : QString());
    });
    connect(m_reset, &QPushButton::clicked, this, [this] {
        CurvesValue v;
        v.mode = m_mode->currentIndex();
        m_graph->setValue(v);
        emit valueChanged();
    });
    updateChannel();
}

void CurvesEditor::updateChannel()
{
    const bool rgb = m_mode->currentIndex() == CurvesValue::Rgb;
    for (QCheckBox* c : {m_red, m_green, m_blue})
        c->setVisible(rgb);
    if (!rgb) {
        m_graph->setChannels(0, {});
        return;
    }
    QList<int> checked;
    if (m_red->isChecked())
        checked << 1;
    if (m_green->isChecked())
        checked << 2;
    if (m_blue->isChecked())
        checked << 3;
    // Sin ninguna casilla marcada no se edita nada útil: se muestra el rojo.
    m_graph->setChannels(checked.isEmpty() ? 1 : checked.first(), checked);
}

void CurvesEditor::setValue(const CurvesValue& v)
{
    m_graph->setValue(v);
    const QSignalBlocker b(m_mode);
    m_mode->setCurrentIndex(v.mode);
    updateChannel();
}

CurvesValue CurvesEditor::value() const
{
    return m_graph->value();
}
