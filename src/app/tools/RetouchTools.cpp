#include "app/tools/RetouchTools.h"

#include "app/AppState.h"
#include "app/Canvas.h"
#include "app/Icons.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/Pixels.h"
#include "core/Raster.h"

#include <QApplication>
#include <QKeyEvent>
#include <QLineF>
#include <QPainter>
#include <QPointer>

#include <climits>
#include <cmath>
#include <cstring>

using namespace retouch;

// ---------------------------------------------------------------------------------------------

struct RetouchJob::State {
    std::atomic_bool stop{false};
    bool ok = false;
    std::thread thread;
    Done done;
};

RetouchJob::~RetouchJob()
{
    cancel();
}

void RetouchJob::start(Work work, Done done)
{
    cancel();
    auto s = std::make_shared<State>();
    s->done = std::move(done);
    m_state = s;
    QPointer<RetouchJob> self(this);
    s->thread = std::thread([s, work = std::move(work), self] {
        bool ok = true;
        try {
            work(Stop(&s->stop));
        } catch (const Cancelled&) {
            ok = false;
        } catch (...) {
            ok = false;
        }
        s->ok = ok && !s->stop.load();
        QMetaObject::invokeMethod(
            qApp, [self, s] {
                if (self)
                    self->deliver(s);
            },
            Qt::QueuedConnection);
    });
    emit runningChanged(true);
}

void RetouchJob::deliver(const std::shared_ptr<State>& s)
{
    if (m_state != s)
        return;
    if (s->thread.joinable())
        s->thread.join();
    m_state.reset();
    emit runningChanged(false);
    if (s->done)
        s->done(s->ok);
}

void RetouchJob::cancel()
{
    if (!m_state)
        return;
    const std::shared_ptr<State> s = m_state;
    s->stop = true;
    if (s->thread.joinable())
        s->thread.join();
    m_state.reset();
    emit runningChanged(false);
}

void RetouchJob::wait()
{
    if (!m_state)
        return;
    const std::shared_ptr<State> s = m_state;
    if (s->thread.joinable())
        s->thread.join();
    deliver(s);
}

// ---------------------------------------------------------------------------------------------

namespace {

QImage sourceImage(Document* doc, bool image)
{
    QImage src = image ? doc->flattened() : (doc->activeLayer() ? doc->activeLayer()->image : QImage());
    if (!src.isNull() && src.format() != kLayerFormat)
        src = src.convertToFormat(kLayerFormat);
    return src;
}

Bytes pixelsOf(const QImage& src, const QRect& r)
{
    Bytes out(size_t(std::max(r.width(), 0)) * std::max(r.height(), 0) * 4, 0);
    const QRect inside = r.intersected(src.rect());
    for (int y = inside.top(); y <= inside.bottom(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(src.constScanLine(y));
        uint8_t* o = out.data() + (size_t(y - r.top()) * r.width() + (inside.left() - r.left())) * 4;
        for (int x = inside.left(); x <= inside.right(); ++x, o += 4) {
            const QRgb c = row[x];
            o[0] = uint8_t(qRed(c));
            o[1] = uint8_t(qGreen(c));
            o[2] = uint8_t(qBlue(c));
            o[3] = uint8_t(qAlpha(c));
        }
    }
    return out;
}

Box boxOf(const QRect& r)
{
    return {r.left(), r.top(), r.left() + r.width(), r.top() + r.height()};
}

QRect rectOf(const Box& b)
{
    return QRect(b.x0, b.y0, b.width(), b.height());
}

// La zona de 'mask' (Alpha8) con cobertura dentro de 'area'.
QRect coveredBounds(const QImage& mask, const QRect& area)
{
    int x0 = INT_MAX, y0 = INT_MAX, x1 = INT_MIN, y1 = INT_MIN;
    const QRect a = area.intersected(mask.rect());
    for (int y = a.top(); y <= a.bottom(); ++y) {
        const uchar* row = mask.constScanLine(y);
        for (int x = a.left(); x <= a.right(); ++x)
            if (row[x]) {
                x0 = std::min(x0, x);
                x1 = std::max(x1, x);
                y0 = std::min(y0, y);
                y1 = std::max(y1, y);
            }
    }
    return x0 > x1 ? QRect() : QRect(QPoint(x0, y0), QPoint(x1, y1));
}

Bytes coverageOf(const QImage& mask, const QRect& r)
{
    Bytes out(size_t(std::max(r.width(), 0)) * std::max(r.height(), 0), 0);
    const QRect inside = r.intersected(mask.rect());
    for (int y = inside.top(); y <= inside.bottom(); ++y)
        std::memcpy(out.data() + size_t(y - r.top()) * r.width() + (inside.left() - r.left()),
                    mask.constScanLine(y) + inside.left(), size_t(inside.width()));
    return out;
}

// La cobertura de 'mask' de colores (0..255) como un tinte sobre la imagen, para dibujarla.
QImage tintOf(const QImage& mask, const QRect& r, QColor color, int alpha)
{
    QImage out(r.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    const QRect inside = r.intersected(mask.rect());
    for (int y = inside.top(); y <= inside.bottom(); ++y) {
        const uchar* m = mask.constScanLine(y);
        QRgb* o = reinterpret_cast<QRgb*>(out.scanLine(y - r.top()));
        for (int x = inside.left(); x <= inside.right(); ++x)
            if (m[x]) {
                const int a = m[x] * alpha / 255;
                o[x - r.left()] = qPremultiply(qRgba(color.red(), color.green(), color.blue(), a));
            }
    }
    return out;
}

void drawImageOver(Canvas* canvas, QPainter& p, const QRect& r, const QImage& img)
{
    if (img.isNull() || r.isEmpty())
        return;
    p.save();
    p.setTransform(canvas->imageToViewTransform(), true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.drawImage(QRectF(r), img);
    p.restore();
}

QImage alphaScaled(QImage img, int opacity)
{
    if (opacity >= 255)
        return img;
    for (int y = 0; y < img.height(); ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x)
            row[x] = qRgba(qRed(row[x]), qGreen(row[x]), qBlue(row[x]), qAlpha(row[x]) * opacity / 255);
    }
    return img;
}

QString tooLargeText()
{
    return QObject::tr("La zona es demasiado grande (más de 16 millones de píxeles).");
}

} // namespace

Bytes retouchPixels(Document* doc, bool image, const QRect& r)
{
    return pixelsOf(sourceImage(doc, image), r);
}

QImage retouchImage(const Bytes& rgba, QSize size)
{
    QImage img(size, kLayerFormat);
    for (int y = 0; y < size.height(); ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(img.scanLine(y));
        const uint8_t* s = rgba.data() + size_t(y) * size.width() * 4;
        for (int x = 0; x < size.width(); ++x, s += 4)
            row[x] = qRgba(s[0], s[1], s[2], s[3]);
    }
    return img;
}

// ---------------------------------------------------------------------------------------------
// Pinceles correctores.

HealStrokeTool::HealStrokeTool(QObject* parent)
    : StrokeTool(parent)
    , m_job(new RetouchJob(this))
{
    connect(m_job, &RetouchJob::runningChanged, this, [this] {
        if (m_canvas)
            m_canvas->updateCursor();
    });
}

QCursor HealStrokeTool::cursor() const
{
    return m_job->running() ? QCursor(Qt::BusyCursor) : StrokeTool::cursor();
}

void HealStrokeTool::mousePress(const ToolEvent& e)
{
    if (m_job->running())
        return;
    StrokeTool::mousePress(e);
}

void HealStrokeTool::mouseMove(const ToolEvent& e)
{
    if (m_job->running())
        return;
    StrokeTool::mouseMove(e);
}

void HealStrokeTool::mouseRelease(const ToolEvent&)
{
    release(false);
}

void HealStrokeTool::finish()
{
    // Una curación a medias termina ya (otra herramienta, deshacer...).
    if (m_job->running())
        m_job->wait();
    else if (m_engine.isActive())
        release(true);
}

bool HealStrokeTool::keyPress(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape && m_engine.isActive()) {
        m_job->cancel();
        m_engine.cancel();
        repaint();
        return true;
    }
    return false;
}

void HealStrokeTool::release(bool wait)
{
    if (!m_engine.isActive() || m_job->running())
        return;
    m_engine.finishPath();
    std::optional<Plan> p = plan();
    if (!p) {
        m_engine.cancel();
        repaint();
        return;
    }
    auto result = std::make_shared<QImage>();
    const QRect rect = p->rect;
    m_job->start([run = std::move(p->run), result](const Stop& s) { *result = run(s); },
                 [this, rect, result](bool ok) {
                     if (!m_engine.isActive())
                         return;
                     if (ok && !result->isNull())
                         land(rect, *result);
                     else
                         m_engine.cancel();
                     repaint();
                 });
    if (wait)
        m_job->wait();
}

void HealStrokeTool::land(const QRect& rect, const QImage& healed)
{
    m_engine.applyThroughCoverage(rect, healed);
    m_engine.commit(name(), historyIcon());
}

QIcon HealingBrushTool::icon() const
{
    return Icons::tool(QStringLiteral("healingbrush"), Icons::theme({"edit-clone"}));
}

QString HealingBrushTool::hint() const
{
    return m_hasSource ? tr("Pinta sobre lo que quieras arreglar: al soltar, la textura del origen toma el color y la luz de "
                            "alrededor. Ctrl+clic: otro origen.")
                       : tr("Ctrl+clic para elegir de dónde tomar la textura y luego pinta sobre lo que quieras arreglar.");
}

bool HealingBrushTool::canStart(const ToolEvent& e)
{
    Document* doc = document();
    if (e.modifiers & Qt::ControlModifier) {
        m_source = e.pos;
        m_hasSource = true;
        m_hasOffset = false;
        emit hintChanged(hint());
        repaint();
        return false;
    }
    if (!m_hasSource) {
        // Sin origen no hay de dónde tomar la textura: se avisa junto al puntero (la barra de estado
        // sola pasaba desapercibida y parecía que la herramienta no hacía nada).
        const QString missing = tr("Primero Ctrl+clic para elegir de dónde tomar la textura.");
        emit hintChanged(missing);
        notice(missing);
        return false;
    }
    // Alineado: el desplazamiento se queda entre trazos; si no, cada trazo vuelve al origen.
    if (!m_hasOffset || !AppState::instance()->tools().healAligned) {
        const QPointF d = m_source - e.pos;
        m_offset = QPoint(int(std::round(d.x())), int(std::round(d.y())));
        m_hasOffset = true;
    }
    m_sample = AppState::instance()->tools().retouchSampleImage ? sourceImage(doc, true) : QImage();
    return true;
}

StrokeParams HealingBrushTool::params(const ToolEvent& e) const
{
    StrokeParams p = baseParams();
    p.kind = StrokeKind::Clone;
    p.blend = AppState::instance()->tools().blend;
    p.cloneOffset = m_offset;
    p.color = e.button == Qt::RightButton ? AppState::instance()->secondary() : AppState::instance()->primary();
    if (!m_sample.isNull())
        p.cloneSource = m_sample;
    return p;
}

void HealingBrushTool::mouseMove(const ToolEvent& e)
{
    m_hover = e.pos;
    HealStrokeTool::mouseMove(e);
    if (m_hasSource)
        repaint();
}

void HealingBrushTool::paintOverlay(QPainter& p)
{
    if (!m_hasSource)
        return;
    const QPointF src = m_hasOffset ? m_hover + QPointF(m_offset) : m_source;
    const QPointF v = canvas()->imageToView(src);
    const qreal r = std::max(4.0, AppState::instance()->tools().brushSize * canvas()->scale() / 2.0);
    p.setRenderHint(QPainter::Antialiasing);
    for (const QColor& c : {QColor(255, 255, 255, 200), QColor(0, 0, 0, 200)}) {
        p.setPen(QPen(c, 0, c == Qt::black ? Qt::DashLine : Qt::SolidLine));
        p.drawEllipse(v, r, r);
        p.drawLine(v - QPointF(6, 0), v + QPointF(6, 0));
        p.drawLine(v - QPointF(0, 6), v + QPointF(0, 6));
    }
}

std::optional<HealStrokeTool::Plan> HealingBrushTool::plan()
{
    const QImage& before = m_engine.before();
    const QRect covered = coveredBounds(m_engine.coverage(), m_engine.bounds());
    if (covered.isEmpty())
        return std::nullopt;
    // El trazo y dos píxeles alrededor (el borde de la solución), dentro de la capa.
    const QRect rect = covered.adjusted(-kHealPad, -kHealPad, kHealPad, kHealPad).intersected(before.rect());
    if ((long long)rect.width() * rect.height() > kMaxPixels) {
        emit hintChanged(tooLargeText());
        return std::nullopt;
    }
    const QImage& from = m_engine.params().cloneSource.isNull() ? before : m_engine.params().cloneSource;
    Heal heal;
    heal.w = rect.width();
    heal.h = rect.height();
    heal.src = pixelsOf(from, rect.translated(m_engine.params().cloneOffset));
    heal.dst = pixelsOf(m_sample.isNull() ? before : m_sample, rect);
    heal.cov = coverageOf(m_engine.coverage(), rect);
    const int opacity = m_engine.params().color.alpha();
    return Plan{rect, [heal = std::move(heal), opacity](const Stop& s) {
                    return alphaScaled(retouchImage(heal.run(s), QSize(heal.w, heal.h)), opacity);
                }};
}

QIcon SpotHealingTool::icon() const
{
    return Icons::tool(QStringLiteral("spothealing"), Icons::theme({"edit-clone"}));
}

QString SpotHealingTool::hint() const
{
    return tr("Pinta sobre una mancha: al soltar se rellena con lo de alrededor y se funde con su color.");
}

StrokeParams SpotHealingTool::params(const ToolEvent&) const
{
    StrokeParams p = baseParams();
    p.kind = StrokeKind::Mask;
    return p;
}

void SpotHealingTool::mousePress(const ToolEvent& e)
{
    HealStrokeTool::mousePress(e);
    repaint();
}

void SpotHealingTool::mouseMove(const ToolEvent& e)
{
    HealStrokeTool::mouseMove(e);
    if (m_engine.isActive())
        repaint();
}

void SpotHealingTool::paintOverlay(QPainter& p)
{
    // El trazo se ve como un rastro negro al 50 % (lo que se va a curar).
    if (!m_engine.isActive())
        return;
    const QRect r = m_engine.bounds().intersected(m_engine.coverage().rect());
    drawImageOver(canvas(), p, r, tintOf(m_engine.coverage(), r, Qt::black, 128));
}

std::optional<HealStrokeTool::Plan> SpotHealingTool::plan()
{
    const QImage& before = m_engine.before();
    const QRect covered = coveredBounds(m_engine.coverage(), m_engine.bounds());
    if (covered.isEmpty())
        return std::nullopt;
    const Box win = spotWindow(boxOf(covered), boxOf(before.rect()));
    if (win.area() > kMaxPixels) {
        emit hintChanged(tooLargeText());
        return std::nullopt;
    }
    const QRect rect = rectOf(win);
    const bool image = AppState::instance()->tools().retouchSampleImage;
    const Bytes img = pixelsOf(image ? sourceImage(m_engine.document(), true) : before, rect);
    const Bytes cov = coverageOf(m_engine.coverage(), rect);
    const int type = AppState::instance()->tools().spotType;
    const SpotKind kind = type == 1 ? SpotKind::CreateTexture : type == 2 ? SpotKind::ProximityMatch : SpotKind::ContentAware;
    const double size = m_engine.params().diameter;
    return Plan{rect, [img, cov, kind, size, w = rect.width(), h = rect.height()](const Stop& s) {
                    return retouchImage(spotHeal(w, h, img, cov, kind, size, s), QSize(w, h));
                }};
}

// ---------------------------------------------------------------------------------------------
// Quitar.

namespace {
constexpr uint64_t kRemoveSeed = 0x0052e30eull;
}

RemoveTool::RemoveTool(QObject* parent)
    : StrokeTool(parent)
    , m_job(new RetouchJob(this))
{
    connect(m_job, &RetouchJob::runningChanged, this, [this] {
        if (m_canvas)
            m_canvas->updateCursor();
    });
}

QIcon RemoveTool::icon() const
{
    return Icons::tool(QStringLiteral("remove"), Icons::theme({"edit-clear"}));
}

QString RemoveTool::hint() const
{
    if (!AppState::instance()->tools().removeEachStroke)
        return tr("Pinta encima o alrededor de lo que quieras quitar; Intro lo quita (Esc descarta los trazos).");
    return tr("Pinta encima o alrededor de lo que quieras quitar: al soltar se rellena con lo de alrededor.");
}

QCursor RemoveTool::cursor() const
{
    return m_job->running() ? QCursor(Qt::BusyCursor) : StrokeTool::cursor();
}

StrokeParams RemoveTool::params(const ToolEvent&) const
{
    StrokeParams p = baseParams();
    p.kind = StrokeKind::Mask;
    return p;
}

void RemoveTool::mousePress(const ToolEvent& e)
{
    if (m_job->running())
        return;
    StrokeTool::mousePress(e);
    repaint();
}

void RemoveTool::mouseMove(const ToolEvent& e)
{
    StrokeTool::mouseMove(e);
    if (m_engine.isActive())
        repaint();
}

void RemoveTool::collect()
{
    if (!m_engine.isActive())
        return;
    m_engine.finishPath();
    Document* doc = m_engine.document();
    if (m_pending.isNull() || m_pendingDoc != doc || m_pending.size() != doc->size()) {
        m_pending = QImage(doc->size(), QImage::Format_Alpha8);
        m_pending.fill(0);
        m_pendingRect = QRect();
        m_pendingDoc = doc;
    }
    const QImage& cov = m_engine.coverage();
    const QRect r = coveredBounds(cov, m_engine.bounds());
    for (int y = r.top(); y <= r.bottom() && !r.isEmpty(); ++y) {
        const uchar* s = cov.constScanLine(y);
        uchar* d = m_pending.scanLine(y);
        for (int x = r.left(); x <= r.right(); ++x)
            d[x] = std::max(d[x], s[x]);
    }
    m_pendingRect |= r;
    m_engine.cancel();
}

void RemoveTool::mouseRelease(const ToolEvent&)
{
    collect();
    if (AppState::instance()->tools().removeEachStroke)
        run(false);
    repaint();
}

void RemoveTool::drop()
{
    m_pending = QImage();
    m_pendingRect = QRect();
    m_pendingDoc = nullptr;
    repaint();
}

void RemoveTool::run(bool wait)
{
    Document* doc = m_pendingDoc;
    if (!doc || m_pendingRect.isEmpty() || m_job->running()) {
        drop();
        return;
    }
    if ((long long)m_pendingRect.width() * m_pendingRect.height() > kMaxPixels) {
        emit hintChanged(tr("La zona es demasiado grande para quitarla."));
        drop();
        return;
    }
    const Box win = removeWindow(boxOf(m_pendingRect), boxOf(QRect(QPoint(0, 0), doc->size())));
    if (win.empty()) {
        drop();
        return;
    }
    const QRect rect = rectOf(win);
    const bool image = AppState::instance()->tools().retouchSampleImage;
    const Bytes img = retouchPixels(doc, image, rect);
    const Bytes strokes = coverageOf(m_pending, rect);
    // La selección recorta el hueco.
    Bytes clip;
    if (!doc->selection().isEmpty())
        clip = coverageOf(doc->selection().mask(), rect);
    const LayerPtr layer = doc->activeLayerPtr();
    drop();
    auto out = std::make_shared<std::pair<Mask, Bytes>>();
    const int w = rect.width(), h = rect.height();
    QPointer<Document> target(doc);
    m_job->start(
        [img, strokes, clip, image, w, h, out](const Stop& s) {
            // El hueco: los trazos, lo que encierran y 2 píxeles más, dentro de la selección.
            Mask hole(strokes.size());
            for (size_t i = 0; i < hole.size(); ++i)
                hole[i] = strokes[i] > 0;
            closeLoops(hole, w, h);
            hole = dilate(hole, w, h, 2);
            if (!clip.empty())
                for (size_t i = 0; i < hole.size(); ++i)
                    hole[i] = hole[i] && clip[i] > 0;
            const bool any = std::any_of(hole.begin(), hole.end(), [](uint8_t v) { return v; });
            const bool all = std::all_of(hole.begin(), hole.end(), [](uint8_t v) { return v; });
            if (!any || all)
                return;
            // Una capa transparente no tiene nada que quitar ni de dónde rellenar.
            bool opaque = image;
            for (size_t i = 3; i < img.size() && !opaque; i += 4)
                opaque = img[i] != 0;
            if (!opaque)
                return;
            const Floats filled = removeFill(w, h, 4, toFloats(img), hole, kRemoveSeed, s);
            *out = {std::move(hole), toBytes(filled)};
        },
        [this, out, rect, target, layer](bool ok) {
            if (!ok || !target || out->first.empty() || target->indexOf(layer.get()) < 0)
                return;
            const QImage before = layer->image;
            layer->image.detach();
            for (int y = 0; y < rect.height(); ++y) {
                QRgb* row = reinterpret_cast<QRgb*>(layer->image.scanLine(y + rect.top()));
                for (int x = 0; x < rect.width(); ++x) {
                    const size_t i = size_t(y) * rect.width() + x;
                    if (!out->first[i])
                        continue;
                    const uint8_t* p = out->second.data() + i * 4;
                    row[x + rect.left()] = qRgba(p[0], p[1], p[2], p[3]);
                }
            }
            target->notifyPixelsChanged(layer.get(), rect);
            pushPixelPatch(target, layer, before, rect, name(), historyIcon());
        });
    if (wait)
        m_job->wait();
}

bool RemoveTool::keyPress(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) {
        if (m_job->running()) {
            m_job->cancel();
            return true;
        }
        if (m_engine.isActive()) {
            m_engine.cancel();
            repaint();
            return true;
        }
        if (!m_pending.isNull()) {
            drop();
            return true;
        }
        return false;
    }
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && !m_pending.isNull()) {
        collect();
        run(false);
        return true;
    }
    return false;
}

void RemoveTool::finish()
{
    collect();
    if (m_job->running())
        m_job->wait();
    else if (!m_pending.isNull())
        run(true);
}

void RemoveTool::deactivate()
{
    finish();
    Tool::deactivate();
}

void RemoveTool::paintOverlay(QPainter& p)
{
    // Los trazos se ven con un tinte rojo al 50 %.
    if (!m_pending.isNull())
        drawImageOver(canvas(), p, m_pendingRect, tintOf(m_pending, m_pendingRect, QColor(255, 0, 0), 128));
    if (m_engine.isActive()) {
        const QRect r = m_engine.bounds().intersected(m_engine.coverage().rect());
        drawImageOver(canvas(), p, r, tintOf(m_engine.coverage(), r, QColor(255, 0, 0), 128));
    }
}

// ---------------------------------------------------------------------------------------------
// Parche.

PatchTool::PatchTool(QObject* parent)
    : LassoSelectTool(parent)
    , m_job(new RetouchJob(this))
{
    connect(m_job, &RetouchJob::runningChanged, this, [this] {
        if (m_canvas)
            m_canvas->updateCursor();
    });
    m_previewTimer.setSingleShot(true);
    m_previewTimer.setInterval(40);
    connect(&m_previewTimer, &QTimer::timeout, this, &PatchTool::updatePreview);
}

QIcon PatchTool::icon() const
{
    return Icons::tool(QStringLiteral("patch"), Icons::theme({"edit-select-lasso"}));
}

QString PatchTool::hint() const
{
    const ToolSettings& s = AppState::instance()->tools();
    if (s.patchMode == 1)
        return tr("Selecciona una zona y arrástrala hasta lo que la rellenará según el contenido.");
    return s.patchDestination ? tr("Selecciona una zona y arrástrala hasta lo que quieras reparar con ella.")
                              : tr("Selecciona lo que quieras reparar y arrástralo hasta la textura que lo sustituirá.");
}

QCursor PatchTool::cursor() const
{
    if (m_job->running())
        return QCursor(Qt::BusyCursor);
    return m_dragging ? QCursor(Qt::SizeAllCursor) : LassoSelectTool::cursor();
}

QPoint PatchTool::offset() const
{
    const QPointF d = m_pointer - m_grab;
    return QPoint(int(std::round(d.x())), int(std::round(d.y())));
}

void PatchTool::mousePress(const ToolEvent& e)
{
    Document* doc = document();
    if (!doc || m_job->running())
        return;
    const Selection& sel = doc->selection();
    const QPoint px(int(std::floor(e.pos.x())), int(std::floor(e.pos.y())));
    // Un arrastre con el botón izquierdo y sin teclas dentro de la selección la parchea; los demás
    // son los del lazo.
    const bool plain = !(e.modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier));
    if (e.button == Qt::LeftButton && plain && !sel.isEmpty() && QRect(QPoint(0, 0), doc->size()).contains(px)
        && sel.coverage(px.x(), px.y()) > 0) {
        m_dragging = true;
        m_grab = e.pos;
        m_pointer = e.pos;
        m_preview = QImage();
        canvas()->updateCursor();
        return;
    }
    LassoSelectTool::mousePress(e);
}

void PatchTool::mouseMove(const ToolEvent& e)
{
    if (m_dragging) {
        m_pointer = e.pos;
        m_previewTimer.start();
        repaint();
        return;
    }
    LassoSelectTool::mouseMove(e);
}

void PatchTool::cancelDrag()
{
    m_dragging = false;
    m_preview = QImage();
    m_previewTimer.stop();
    if (m_canvas)
        m_canvas->updateCursor();
    repaint();
}

namespace {

struct PatchInput {
    PatchMode mode = PatchMode::Source;
    PatchPlan plan;
    int structure = 4;
    int color = 0;
    Bytes here;
    Bytes texture;
    Bytes coverage;
};

std::optional<PatchInput> patchInput(Document* doc, QPoint offset, QString* refusal)
{
    const ToolSettings& s = AppState::instance()->tools();
    PatchInput in;
    in.mode = s.patchMode == 1 ? PatchMode::ContentAware : s.patchDestination ? PatchMode::Destination : PatchMode::Source;
    in.structure = s.patchStructure;
    in.color = s.patchColor;
    const Selection& sel = doc->selection();
    if (sel.isEmpty())
        return std::nullopt;
    const QImage& mask = sel.mask();
    const QRect bounds = coveredBounds(mask, mask.rect());
    const Box canvas = boxOf(QRect(QPoint(0, 0), doc->size()));
    switch (planPatch(boxOf(bounds), canvas, {offset.x(), offset.y()}, in.mode, &in.plan)) {
    case PatchRefusal::None: break;
    case PatchRefusal::TooLarge:
        if (refusal)
            *refusal = QObject::tr("La selección es demasiado grande para el parche.");
        return std::nullopt;
    case PatchRefusal::NoOffset:
        if (refusal)
            *refusal = QObject::tr("Arrastra la selección hasta la zona de la que tomar.");
        return std::nullopt;
    case PatchRefusal::OffCanvas:
        if (refusal)
            *refusal = QObject::tr("El parche arrastrado tiene que quedar dentro de la imagen.");
        return std::nullopt;
    default: return std::nullopt;
    }
    const QRect rect = rectOf(in.plan.rect);
    const QImage src = sourceImage(doc, s.retouchSampleImage);
    in.here = pixelsOf(src, rect);
    in.texture = pixelsOf(src, rect.translated(in.plan.srcShift.first, in.plan.srcShift.second));
    in.coverage = coverageOf(mask, rect.translated(-in.plan.dstShift.first, -in.plan.dstShift.second));
    return in;
}

} // namespace

void PatchTool::updatePreview()
{
    m_preview = QImage();
    Document* doc = document();
    if (!m_dragging || !doc)
        return;
    const auto in = patchInput(doc, offset(), nullptr);
    if (!in)
        return;
    const QRect rect = rectOf(in->plan.rect);
    m_previewRect = rect;
    if (in->mode == PatchMode::ContentAware) {
        // La textura que entrará, donde está la selección.
        QImage img = retouchImage(in->texture, rect.size());
        for (int y = 0; y < rect.height(); ++y) {
            QRgb* row = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < rect.width(); ++x)
                if (in->coverage[size_t(y) * rect.width() + x] == 0)
                    row[x] = 0;
        }
        m_preview = img;
    } else {
        Heal heal;
        heal.w = rect.width();
        heal.h = rect.height();
        heal.src = in->texture;
        heal.dst = in->here;
        heal.cov = in->coverage;
        // La curación aproximada (la membrana más gruesa).
        QImage img = retouchImage(heal.runCoarse(patchPreviewStep(in->plan.rect, 4096)), rect.size());
        for (int y = 0; y < rect.height(); ++y) {
            QRgb* row = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < rect.width(); ++x) {
                const int c = heal.cov[size_t(y) * rect.width() + x];
                row[x] = qRgba(qRed(row[x]), qGreen(row[x]), qBlue(row[x]), qAlpha(row[x]) * c / 255);
            }
        }
        m_preview = img;
    }
    repaint();
}

void PatchTool::mouseRelease(const ToolEvent& e)
{
    if (!m_dragging) {
        LassoSelectTool::mouseRelease(e);
        return;
    }
    m_pointer = e.pos;
    const QPoint off = offset();
    cancelDrag();
    Document* doc = document();
    if (!doc)
        return;
    QString refusal;
    const auto in = patchInput(doc, off, &refusal);
    if (!in) {
        if (!refusal.isEmpty())
            emit hintChanged(refusal);
        return;
    }
    const QRect rect = rectOf(in->plan.rect);
    const LayerPtr layer = doc->activeLayerPtr();
    QPointer<Document> target(doc);
    auto result = std::make_shared<Bytes>();
    const PatchInput input = *in;
    m_job->start(
        [input, result](const Stop& s) {
            const int w = input.plan.rect.width(), h = input.plan.rect.height();
            if (input.mode == PatchMode::ContentAware) {
                Mask selected(input.coverage.size());
                for (size_t i = 0; i < selected.size(); ++i)
                    selected[i] = input.coverage[i] > 0;
                *result = toBytes(patchLand(w, h, toFloats(input.here), toFloats(input.texture), selected, input.structure,
                                            input.color, s));
            } else {
                Heal heal;
                heal.w = w;
                heal.h = h;
                heal.src = input.texture;
                heal.dst = input.here;
                heal.cov = input.coverage;
                *result = heal.run(s);
            }
        },
        [this, result, rect, target, layer, input](bool ok) {
            if (!ok || !target || result->empty() || target->indexOf(layer.get()) < 0)
                return;
            const QImage before = layer->image;
            layer->image.detach();
            const QImage healed = retouchImage(*result, rect.size());
            if (input.mode == PatchMode::ContentAware) {
                // Según el contenido: reemplaza los píxeles seleccionados.
                for (int y = 0; y < rect.height(); ++y) {
                    QRgb* row = reinterpret_cast<QRgb*>(layer->image.scanLine(y + rect.top()));
                    const QRgb* src = reinterpret_cast<const QRgb*>(healed.constScanLine(y));
                    for (int x = 0; x < rect.width(); ++x)
                        if (input.coverage[size_t(y) * rect.width() + x] > 0)
                            row[x + rect.left()] = src[x];
                }
            } else {
                // Normal: a través de la cobertura de la selección.
                QImage cov(rect.size(), QImage::Format_Alpha8);
                for (int y = 0; y < rect.height(); ++y)
                    std::memcpy(cov.scanLine(y), input.coverage.data() + size_t(y) * rect.width(), size_t(rect.width()));
                Raster::compositeRegion(layer->image, before, healed, rect, BlendMode::Normal, Selection(), cov);
            }
            target->notifyPixelsChanged(layer.get(), rect);
            pushPixelPatch(target, layer, before, rect, name(), QStringLiteral("patch"));
        });
}

bool PatchTool::keyPress(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) {
        if (m_job->running()) {
            m_job->cancel();
            return true;
        }
        if (m_dragging) {
            cancelDrag();
            return true;
        }
    }
    return false;
}

void PatchTool::finish()
{
    if (m_job->running())
        m_job->wait();
    if (m_dragging)
        cancelDrag();
}

void PatchTool::paintOverlay(QPainter& p)
{
    LassoSelectTool::paintOverlay(p);
    Document* doc = document();
    if (!m_dragging || !doc || doc->selection().isEmpty())
        return;
    drawImageOver(canvas(), p, m_previewRect, m_preview);
    // La selección arrastrada, con las hormigas de la selección.
    canvas()->drawSelectionAnts(p, doc->selection(), QTransform::fromTranslate(offset().x(), offset().y()));
}

// ---------------------------------------------------------------------------------------------
// Lazo magnético.

namespace {

constexpr qint64 kDoubleClickMs = 500;
constexpr double kDoubleClickDistance = 4.0;
constexpr double kCloseRadius = 8.0;
constexpr size_t kStepsPerFrame = 4;

double distance(const PointF& a, const PointF& b)
{
    return std::hypot(a.x - b.x, a.y - b.y);
}

// La polilínea 'p' hasta 'len' a lo largo (terminando en ese punto) y el punto, si queda más de un
// píxel después.
std::optional<std::pair<std::vector<PointF>, PointF>> cutAt(const std::vector<PointF>& p, double len)
{
    if (polylineLength(p) < len + 1.0)
        return std::nullopt;
    std::vector<PointF> head;
    double acc = 0.0;
    for (size_t i = 0; i + 1 < p.size(); ++i) {
        const PointF a = p[i], b = p[i + 1];
        head.push_back(a);
        const double d = distance(a, b);
        if (acc + d >= len && d > 0.0) {
            const double t = (len - acc) / d;
            const PointF point{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
            head.push_back(point);
            return std::pair<std::vector<PointF>, PointF>{std::move(head), point};
        }
        acc += d;
    }
    return std::nullopt;
}

} // namespace

MagneticLassoTool::MagneticLassoTool(QObject* parent)
    : SelectToolBase(parent)
{
    m_frame.setInterval(16);
    connect(&m_frame, &QTimer::timeout, this, [this] {
        if (m_pending.empty()) {
            m_frame.stop();
            return;
        }
        catchUp(kStepsPerFrame);
        show();
    });
}

QIcon MagneticLassoTool::icon() const
{
    return Icons::tool(QStringLiteral("magneticlasso"), Icons::theme({"edit-select-lasso"}));
}

QString MagneticLassoTool::hint() const
{
    if (!active())
        return tr("Haz clic en un borde y sigue el contorno: el borde de la selección se pega a él. [ y ]: anchura de "
                  "detección.");
    return tr("Clic: fijar un punto · Alt+clic: tramo recto · Retroceso: quitar el último punto · doble clic, clic en el "
              "inicio o Intro: cerrar · Esc: cancelar.");
}

MagneticSettings MagneticLassoTool::settings() const
{
    const ToolSettings& s = AppState::instance()->tools();
    return MagneticSettings::make(double(s.magneticWidth), float(s.magneticContrast) / 100.f);
}

double MagneticLassoTool::spacing() const
{
    // (8 + (100 − Frecuencia)·0,9) puntos de pantalla: 47 con la frecuencia de 57.
    const double f = std::clamp(double(AppState::instance()->tools().magneticFrequency), 0.0, 100.0);
    return (8.0 + (100.0 - f) * 0.9) / std::max(canvas() ? canvas()->scale() : 1.0, 1e-6);
}

PointF MagneticLassoTool::inside(P p) const
{
    const QSize s = document() ? document()->size() : QSize();
    return {std::clamp(p.x, 0.0, double(s.width())), std::clamp(p.y, 0.0, double(s.height()))};
}

namespace {

MagneticTracer::Fetch fetchFrom(const QImage& img)
{
    return [img](Box r) {
        std::vector<uint32_t> out;
        out.reserve(size_t(r.width()) * r.height());
        for (int y = r.y0; y < r.y1; ++y) {
            const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
            for (int x = r.x0; x < r.x1; ++x) {
                const QRgb c = row[x];
                out.push_back(uint32_t(qRed(c)) | uint32_t(qGreen(c)) << 8 | uint32_t(qBlue(c)) << 16 | uint32_t(qAlpha(c)) << 24);
            }
        }
        return out;
    };
}

} // namespace

PointF MagneticLassoTool::snap(P p)
{
    if (m_tracerImage.isNull())
        return p;
    return m_tracer.snap(fetchFrom(m_tracerImage), p, settings()).value_or(p);
}

std::vector<PointF> MagneticLassoTool::trace(P from, P to, const std::vector<P>& guide)
{
    std::vector<P> t;
    if (!m_tracerImage.isNull())
        t = m_tracer.trace(fetchFrom(m_tracerImage), from, to, guide, settings());
    if (t.empty())
        return {from, to};
    return t;
}

void MagneticLassoTool::mousePress(const ToolEvent& e)
{
    Document* doc = document();
    if (!doc || (e.button != Qt::LeftButton && e.button != Qt::RightButton) || m_pressed)
        return;
    const P p{e.pos.x(), e.pos.y()};
    m_pressed = true;
    m_pressPoint = p;
    m_altAtPress = e.modifiers & Qt::AltModifier;
    m_pressDragged = false;
    m_pointer = p;
    // Un clic actúa sobre el borde trazado hasta aquí.
    catchUp(SIZE_MAX);
    if (!active()) {
        // El primer punto de anclaje; las teclas del lazo eligen cómo se combina.
        begin(e);
        m_tracerLayer = doc->activeLayer();
        m_tracerImage = m_tracerLayer ? m_tracerLayer->image : QImage();
        if (!m_tracerImage.isNull() && m_tracerImage.format() != kLayerFormat)
            m_tracerImage = m_tracerImage.convertToFormat(kLayerFormat);
        m_tracer = MagneticTracer(Box{0, 0, doc->size().width(), doc->size().height()});
        const P start = snap(p);
        m_path = {start};
        m_anchors = {0};
        m_trail = {inside(p)};
        m_live.clear();
        m_lastPress.start();
        m_hasLastPress = true;
        m_lastPressPoint = p;
        m_opening = true;
        emit hintChanged(hint());
        show();
        return;
    }
    const double scale = std::max(canvas()->scale(), 1e-6);
    const bool dbl = m_hasLastPress && m_lastPress.elapsed() <= kDoubleClickMs
                  && distance(p, m_lastPressPoint) * scale <= kDoubleClickDistance;
    m_lastPress.start();
    m_hasLastPress = true;
    m_lastPressPoint = p;
    const bool alt = e.modifiers & Qt::AltModifier;
    if (dbl || nearStart(p)) {
        close(alt, !dbl);
        return;
    }
    if (alt) {
        // Alt+clic: un tramo recto hasta aquí; si se arrastra, a mano alzada.
        if (m_path.empty() || distance(m_path.back(), p) > 1e-9) {
            m_path.push_back(p);
            m_anchors.push_back(m_path.size() - 1);
        }
        m_live.clear();
        m_trail = {inside(p)};
        m_freehand = true;
        show();
        return;
    }
    // Un clic fija un punto en el borde junto al puntero.
    follow(p, false);
    fastenLive();
    show();
}

void MagneticLassoTool::queue(P p)
{
    const P last = m_pointer;
    m_pointer = p;
    if (last == p)
        return;
    const double step = std::max(spacing() / 2.0, 1.0);
    // Un salto largo se recorre a pasos, así se siguen fijando puntos por el camino.
    const int n = std::clamp(int(std::ceil(distance(last, p) / step)), 1, 256);
    for (int k = 1; k <= n; ++k) {
        const double t = double(k) / n;
        m_pending.push_back({last.x + (p.x - last.x) * t, last.y + (p.y - last.y) * t});
    }
    if (!m_frame.isActive())
        m_frame.start();
}

void MagneticLassoTool::catchUp(size_t budget)
{
    while (budget > 0 && !m_pending.empty()) {
        const P q = m_pending.front();
        m_pending.pop_front();
        if (!m_freehand) {
            follow(q, true);
            --budget;
            continue;
        }
        // Alt+arrastre: el camino del puntero es el borde.
        if (m_path.empty() || distance(m_path.back(), q) >= 0.5)
            m_path.push_back(q);
        m_trail = {inside(q)};
    }
}

void MagneticLassoTool::extendTrail(P q)
{
    const double gap = std::clamp(settings().width / 4.0, 1.0, 8.0);
    const size_t n = m_trail.size();
    if (n >= 2 && distance(m_trail[n - 2], q) < gap)
        m_trail[n - 1] = q;
    else
        m_trail.push_back(q);
}

void MagneticLassoTool::follow(P q0, bool fasten)
{
    const P q = inside(q0);
    extendTrail(q);
    const P target = snap(q);
    const double every = spacing();
    for (int i = 0; i < 64; ++i) {
        if (m_path.empty())
            return;
        const P from = m_path.back();
        std::vector<P> live = trace(from, target, m_trail);
        // La parte del borde a 'every' de su inicio ya no cambia al mover el puntero: se fija.
        const auto cut = fasten ? cutAt(live, every) : std::nullopt;
        if (!cut) {
            m_live = std::move(live);
            return;
        }
        m_path.insert(m_path.end(), cut->first.begin() + 1, cut->first.end());
        m_anchors.push_back(m_path.size() - 1);
        // Los bordes se buscan por el camino del puntero desde donde pasó junto al punto nuevo.
        size_t nearest = 0;
        double best = INFINITY;
        for (size_t k = 0; k < m_trail.size(); ++k) {
            const double d = distance(m_trail[k], cut->second);
            if (d < best) {
                best = d;
                nearest = k;
            }
        }
        std::vector<P> rest{cut->second};
        rest.insert(rest.end(), m_trail.begin() + long(std::min(nearest + 1, m_trail.size())), m_trail.end());
        m_trail = std::move(rest);
    }
}

void MagneticLassoTool::fastenLive()
{
    const size_t before = m_path.size();
    for (size_t i = 1; i < m_live.size(); ++i)
        if (m_path.empty() || distance(m_path.back(), m_live[i]) > 1e-9)
            m_path.push_back(m_live[i]);
    m_live.clear();
    if (m_path.size() > before)
        m_anchors.push_back(m_path.size() - 1);
    m_trail.clear();
    if (!m_path.empty())
        m_trail.push_back(m_path.back());
}

void MagneticLassoTool::endFreehand()
{
    const bool was = m_freehand;
    m_freehand = false;
    if (was && !m_anchors.empty() && m_anchors.back() + 1 < m_path.size())
        m_anchors.push_back(m_path.size() - 1);
}

bool MagneticLassoTool::nearStart(P p) const
{
    const double r = kCloseRadius / std::max(canvas()->scale(), 1e-6);
    return !m_path.empty() && distance(m_path.front(), p) <= r
        && polylineLength(m_path) + polylineLength(m_live) > 2.0 * r;
}

void MagneticLassoTool::close(bool straight, bool atStart)
{
    catchUp(SIZE_MAX);
    endFreehand();
    std::vector<P> guide;
    if (atStart) {
        m_live.clear();
        guide = std::move(m_trail);
        m_trail.clear();
    } else {
        fastenLive();
    }
    if (!straight && !m_path.empty()) {
        std::vector<P> closing = trace(m_path.back(), m_path.front(), guide);
        // El borde acaba donde empieza.
        if (!closing.empty())
            closing.pop_back();
        if (closing.size() > 1)
            m_path.insert(m_path.end(), closing.begin() + 1, closing.end());
    }
    commitBorder();
}

QPolygonF MagneticLassoTool::polygon() const
{
    QPolygonF poly;
    for (const P& q : m_path)
        poly << QPointF(q.x, q.y);
    for (size_t i = 1; i < m_live.size(); ++i)
        poly << QPointF(m_live[i].x, m_live[i].y);
    for (const P& q : m_pending)
        poly << QPointF(q.x, q.y);
    return poly;
}

void MagneticLassoTool::show()
{
    Document* doc = document();
    if (!doc || !active())
        return;
    const QPolygonF poly = polygon();
    if (poly.size() < 3) {
        repaint();
        return;
    }
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    path.addPolygon(poly);
    path.closeSubpath();
    path = clippedToCanvas(path, doc->size());
    preview(path.isEmpty() ? Selection()
                           : Selection::fromPath(path, doc->size(), AppState::instance()->tools().selectionAntialias));
}

void MagneticLassoTool::commitBorder()
{
    Document* doc = document();
    const QPolygonF poly = polygon();
    const SelectionMode mode = m_mode;
    m_path.clear();
    m_anchors.clear();
    m_live.clear();
    m_trail.clear();
    m_pending.clear();
    m_frame.stop();
    m_freehand = false;
    m_tracerImage = QImage();
    if (!doc || !m_active)
        return;
    if (poly.size() < 3) {
        doc->setSelection(m_before);
        m_active = false;
        m_shape = Selection();
        repaint();
        return;
    }
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    path.addPolygon(poly);
    path.closeSubpath();
    path = clippedToCanvas(path, doc->size());
    const Selection shape = path.isEmpty() ? Selection()
                                           : Selection::fromPath(path, doc->size(), AppState::instance()->tools().selectionAntialias);
    doc->setSelection(mode == SelectionMode::Replace ? shape : m_before.combined(shape, mode));
    commit(name(), QStringLiteral("magneticlasso"));
    emit hintChanged(hint());
    if (mode == SelectionMode::Replace && !doc->selection().isEmpty())
        emit selectionFinished();
    repaint();
}

void MagneticLassoTool::cancel()
{
    const bool was = active();
    m_path.clear();
    m_anchors.clear();
    m_live.clear();
    m_trail.clear();
    m_pending.clear();
    m_frame.stop();
    m_freehand = false;
    m_pressed = false;
    m_tracerImage = QImage();
    if (was && m_active && document())
        document()->setSelection(m_before);
    m_active = false;
    m_shape = Selection();
    emit hintChanged(hint());
    repaint();
}

void MagneticLassoTool::removeLastPoint()
{
    endFreehand();
    if (m_anchors.size() <= 1) {
        cancel();
        return;
    }
    m_anchors.pop_back();
    const size_t keep = m_anchors.empty() ? 1 : m_anchors.back() + 1;
    if (m_path.size() > keep)
        m_path.resize(keep);
    m_live.clear();
    m_trail.clear();
    if (!m_path.empty())
        m_trail.push_back(m_path.back());
    // El tramo vivo vuelve al puntero; solo se fija al moverlo, así Retroceso puede seguir quitando.
    follow(m_pointer, false);
}

void MagneticLassoTool::mouseMove(const ToolEvent& e)
{
    if (!active())
        return;
    const P p{e.pos.x(), e.pos.y()};
    const double far = kDoubleClickDistance / std::max(canvas()->scale(), 1e-6);
    if (m_pressed && distance(m_pressPoint, p) > far)
        m_pressDragged = true;
    queue(p);
    show();
}

void MagneticLassoTool::mouseRelease(const ToolEvent& e)
{
    if (!m_pressed)
        return;
    mouseMove(e);
    m_pressed = false;
    const bool opening = m_opening;
    m_opening = false;
    const bool dragged = m_pressDragged;
    m_pressDragged = false;
    if (dragged && active()) {
        // Tras arrastrar, al soltar se cierra el borde, como con el lazo (recto con Alt, si no se
        // pulsó ya al empezar).
        const bool straight = (e.modifiers & Qt::AltModifier) && !m_altAtPress;
        close(straight, false);
        return;
    }
    (void)opening;
    catchUp(SIZE_MAX);
    endFreehand();
    show();
}

bool MagneticLassoTool::keyPress(QKeyEvent* e)
{
    if (e->modifiers() & (Qt::ControlModifier | Qt::MetaModifier))
        return false;
    if (e->key() == Qt::Key_BracketLeft || e->key() == Qt::Key_BracketRight) {
        const int step = e->key() == Qt::Key_BracketLeft ? -1 : 1;
        AppState::instance()->updateTools([step](ToolSettings& s) { s.magneticWidth = std::clamp(s.magneticWidth + step, 1, 256); });
        return true;
    }
    if (!active())
        return false;
    switch (e->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter: close(e->modifiers() & Qt::AltModifier, false); return true;
    case Qt::Key_Escape: cancel(); return true;
    case Qt::Key_Backspace:
    case Qt::Key_Delete:
        catchUp(SIZE_MAX);
        removeLastPoint();
        show();
        return true;
    default: return false;
    }
}

void MagneticLassoTool::paintOverlay(QPainter& p)
{
    SelectToolBase::paintOverlay(p);
    if (!active())
        return;
    // El borde abierto (lo fijado y el tramo vivo) con las mismas hormigas que la selección: antes,
    // un punteado azul fijo encima de ellas, y el contorno se veía doble y cortado.
    QPainterPath open;
    open.addPolygon(polygon());
    canvas()->drawAntsPath(p, open);
}

void MagneticLassoTool::finish()
{
    cancel();
}

void MagneticLassoTool::deactivate()
{
    cancel();
    SelectToolBase::deactivate();
}

// ---------------------------------------------------------------------------------------------
// Selección de objetos.

ObjectSelectionTool::ObjectSelectionTool(QObject* parent)
    : SelectToolBase(parent)
    , m_job(new RetouchJob(this))
    , m_finder(new RetouchJob(this))
{
    connect(m_job, &RetouchJob::runningChanged, this, [this] {
        if (m_canvas)
            m_canvas->updateCursor();
    });
    m_hoverTimer.setSingleShot(true);
    m_hoverTimer.setInterval(150);
    connect(&m_hoverTimer, &QTimer::timeout, this, &ObjectSelectionTool::lookupHover);
}

QIcon ObjectSelectionTool::icon() const
{
    return Icons::tool(QStringLiteral("objectselection"), Icons::theme({"edit-select-all"}));
}

QString ObjectSelectionTool::hint() const
{
    return tr("Rodea un objeto con un recuadro, o haz clic en el que se resalta, para seleccionarlo.");
}

QCursor ObjectSelectionTool::cursor() const
{
    return m_job->running() ? QCursor(Qt::BusyCursor) : SelectToolBase::cursor();
}

void ObjectSelectionTool::activate(Canvas* c)
{
    SelectToolBase::activate(c);
    m_found.reset();
}

void ObjectSelectionTool::deactivate()
{
    m_hoverTimer.stop();
    m_finder->cancel();
    m_job->wait();
    m_found.reset();
    SelectToolBase::deactivate();
}

retouch::Sampler ObjectSelectionTool::sampler(bool* ok) const
{
    Document* doc = document();
    QImage img = doc ? sourceImage(doc, AppState::instance()->tools().retouchSampleImage) : QImage();
    if (ok)
        *ok = !img.isNull();
    return [img](Box r) {
        std::vector<std::array<float, 4>> out(size_t(r.width()) * r.height(), {0.f, 0.f, 0.f, 0.f});
        const QRect inside = QRect(r.x0, r.y0, r.width(), r.height()).intersected(img.rect());
        for (int y = inside.top(); y <= inside.bottom(); ++y) {
            const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
            for (int x = inside.left(); x <= inside.right(); ++x) {
                const QRgb c = row[x];
                out[size_t(y - r.y0) * r.width() + (x - r.x0)] = {float(qRed(c)) / 255.f, float(qGreen(c)) / 255.f,
                                                                  float(qBlue(c)) / 255.f, float(qAlpha(c)) / 255.f};
            }
        }
        return out;
    };
}

void ObjectSelectionTool::apply(const retouch::Region& region, SelectionMode mode, const Selection& before)
{
    Document* doc = document();
    if (!doc)
        return;
    // La cobertura de 128 o más, con bordes de píxel.
    QImage mask(doc->size(), QImage::Format_Alpha8);
    mask.fill(0);
    const int w = region.box.width();
    for (int y = region.box.y0; y < region.box.y1; ++y) {
        if (y < 0 || y >= mask.height())
            continue;
        uchar* row = mask.scanLine(y);
        for (int x = region.box.x0; x < region.box.x1; ++x)
            if (x >= 0 && x < mask.width() && region.mask[size_t(y - region.box.y0) * w + (x - region.box.x0)] >= 128)
                row[x] = 255;
    }
    const Selection shape = Selection::fromMask(mask);
    m_before = before;
    m_mode = mode;
    m_active = true;
    doc->setSelection(mode == SelectionMode::Replace ? shape : before.combined(shape, mode));
    commit(name(), QStringLiteral("objectselection"));
    if (mode == SelectionMode::Replace && !doc->selection().isEmpty())
        emit selectionFinished();
    repaint();
}

void ObjectSelectionTool::mousePress(const ToolEvent& e)
{
    if (!document() || m_job->running() || (e.button != Qt::LeftButton && e.button != Qt::RightButton))
        return;
    m_pressed = true;
    m_boxDragged = false;
    m_start = m_end = e.pos;
    m_pressMode = modeFor(e);
    m_hoverTimer.stop();
}

void ObjectSelectionTool::mouseMove(const ToolEvent& e)
{
    if (m_pressed) {
        m_end = e.pos;
        if (QLineF(canvas()->imageToView(m_start), canvas()->imageToView(m_end)).length() > 2)
            m_boxDragged = true;
        repaint();
        return;
    }
    m_hover = e.pos;
    if (AppState::instance()->tools().objectFinder && !m_job->running())
        m_hoverTimer.start();
    else if (m_found) {
        m_found.reset();
        repaint();
    }
}

void ObjectSelectionTool::mouseRelease(const ToolEvent& e)
{
    if (!m_pressed)
        return;
    m_pressed = false;
    m_end = e.pos;
    Document* doc = document();
    if (!doc)
        return;
    const SelectionMode mode = m_pressMode;
    const Selection before = doc->selection();
    const Box canvasBox{0, 0, doc->size().width(), doc->size().height()};
    bool ok = false;
    const retouch::Sampler sample = sampler(&ok);
    if (!ok)
        return;
    auto result = std::make_shared<std::optional<retouch::Region>>();
    std::function<void(const Stop&)> work;
    if (m_boxDragged) {
        // El recuadro, con los píxeles de los dos extremos dentro.
        const QPointF a = m_start, b = m_end;
        const int x0 = int(std::floor(std::min(a.x(), b.x()))), y0 = int(std::floor(std::min(a.y(), b.y())));
        const int x1 = int(std::floor(std::max(a.x(), b.x()))) + 1, y1 = int(std::floor(std::max(a.y(), b.y()))) + 1;
        const Box rect{x0, y0, x1, y1};
        work = [sample, rect, canvasBox, result](const Stop& s) {
            *result = objectInRect(sample, rect, canvasBox, kObjectAtWorkPx, s);
        };
    } else if (m_found && m_found->box.contains(int(std::floor(e.pos.x())), int(std::floor(e.pos.y())))) {
        // Un clic sobre el objeto resaltado lo selecciona.
        const retouch::Region found = *m_found;
        m_found.reset();
        apply(found, mode, before);
        return;
    } else {
        const float x = float(e.pos.x()), y = float(e.pos.y());
        work = [sample, canvasBox, x, y, result](const Stop& s) {
            *result = objectAt(sample, canvasBox, x, y, kObjectAtWorkPx, s);
        };
    }
    m_found.reset();
    repaint();
    m_job->start(std::move(work), [this, result, mode, before](bool done) {
        if (!done)
            return;
        if (!*result) {
            emit hintChanged(tr("No se encontró ningún objeto."));
            return;
        }
        apply(**result, mode, before);
    });
}

void ObjectSelectionTool::lookupHover()
{
    Document* doc = document();
    if (!doc || m_pressed || m_job->running() || !AppState::instance()->tools().objectFinder)
        return;
    const QPoint px(int(std::floor(m_hover.x())), int(std::floor(m_hover.y())));
    if (!QRect(QPoint(0, 0), doc->size()).contains(px)) {
        if (m_found) {
            m_found.reset();
            repaint();
        }
        return;
    }
    // El objeto resaltado sigue siéndolo mientras el puntero esté dentro.
    if (m_found && m_found->box.contains(px.x(), px.y())
        && m_found->mask[size_t(px.y() - m_found->box.y0) * m_found->box.width() + (px.x() - m_found->box.x0)] >= 128)
        return;
    bool ok = false;
    const retouch::Sampler sample = sampler(&ok);
    if (!ok)
        return;
    const Box canvasBox{0, 0, doc->size().width(), doc->size().height()};
    auto result = std::make_shared<std::optional<retouch::Region>>();
    const float x = float(m_hover.x()), y = float(m_hover.y());
    m_finder->start([sample, canvasBox, x, y, result](const Stop& s) { *result = objectAt(sample, canvasBox, x, y, kObjectAtWorkPx, s); },
                    [this, result](bool done) {
                        if (!done || m_pressed)
                            return;
                        m_found = *result;
                        m_foundOverlay = QImage();
                        m_foundOutline = QPainterPath();
                        if (m_found) {
                            const int w = m_found->box.width(), h = m_found->box.height();
                            QImage mask(w, h, QImage::Format_Alpha8);
                            for (int y = 0; y < h; ++y)
                                for (int x = 0; x < w; ++x)
                                    mask.scanLine(y)[x] = m_found->mask[size_t(y) * w + x] >= 128 ? 255 : 0;
                            m_foundOverlay = tintOf(mask, mask.rect(), QColor(40, 110, 230), 128);
                            m_foundOutline = Selection::smoothOutline(mask).translated(m_found->box.x0, m_found->box.y0);
                        }
                        repaint();
                    });
}

bool ObjectSelectionTool::keyPress(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) {
        if (m_job->running()) {
            m_job->cancel();
            return true;
        }
        if (m_pressed || m_found) {
            m_pressed = false;
            m_found.reset();
            repaint();
            return true;
        }
    }
    return false;
}

void ObjectSelectionTool::selectSubject()
{
    Document* doc = document();
    if (!doc || m_job->running())
        return;
    bool ok = false;
    const retouch::Sampler sample = sampler(&ok);
    if (!ok)
        return;
    const Box canvasBox{0, 0, doc->size().width(), doc->size().height()};
    const SelectionMode mode = AppState::instance()->tools().selectionMode;
    const Selection before = doc->selection();
    auto result = std::make_shared<std::optional<retouch::Region>>();
    m_found.reset();
    m_job->start([sample, canvasBox, result](const Stop& s) { *result = retouch::selectSubject(sample, canvasBox, kSubjectWorkPx, s); },
                 [this, result, mode, before](bool done) {
                     if (!done)
                         return;
                     if (!*result) {
                         emit hintChanged(tr("No se encontró un sujeto destacado."));
                         return;
                     }
                     apply(**result, mode, before);
                 });
}

void ObjectSelectionTool::paintOverlay(QPainter& p)
{
    SelectToolBase::paintOverlay(p);
    if (m_found && !m_pressed) {
        drawImageOver(canvas(), p, QRect(m_found->box.x0, m_found->box.y0, m_found->box.width(), m_found->box.height()),
                      m_foundOverlay);
        p.save();
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(40, 110, 230), 0));
        p.setBrush(Qt::NoBrush);
        p.drawPath(canvas()->imageToViewTransform().map(m_foundOutline));
        p.restore();
    }
    if (m_pressed && m_boxDragged) {
        const QRectF r = QRectF(canvas()->imageToView(m_start), canvas()->imageToView(m_end)).normalized();
        p.save();
        p.setPen(QPen(Qt::white, 0));
        p.drawRect(r);
        QPen dashes(QApplication::palette().color(QPalette::Highlight).darker(140), 0, Qt::CustomDashLine);
        dashes.setDashPattern({3, 3});
        p.setPen(dashes);
        p.drawRect(r);
        p.restore();
    }
}
