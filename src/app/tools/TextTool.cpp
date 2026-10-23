#include "app/tools/TextTool.h"

#include "app/AppState.h"
#include "app/Canvas.h"
#include "app/Icons.h"
#include "app/tools/EditableSteps.h"
#include "app/tools/Handles.h"
#include "app/tools/ShapeTools.h"
#include "core/Raster.h"

#include <QClipboard>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QInputMethod>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QUndoStack>

#include <cmath>

namespace {

bool isWordChar(QChar c) { return c.isLetterOrNumber() || c == QLatin1Char('_'); }

// Posición del siguiente/anterior límite de palabra en la línea.
int wordRight(const QString& s, int col)
{
    const int n = int(s.size());
    while (col < n && !isWordChar(s[col]))
        ++col;
    while (col < n && isWordChar(s[col]))
        ++col;
    return col;
}

int wordLeft(const QString& s, int col)
{
    while (col > 0 && !isWordChar(s[col - 1]))
        --col;
    while (col > 0 && isWordChar(s[col - 1]))
        --col;
    return col;
}

// Texto, posición y opciones con los que se escribió.
struct TextState : ToolStepState {
    QPointF pos;
    QStringList lines;
};

// Paso del texto ("Texto", "Editar texto"): es el paso en vivo mientras se escribe y, al
// registrarlo, guarda el estado y la capa de antes para seguir editando al volver a él.
class TextStepCommand : public QUndoCommand, public EditableStep, public HistoryStep {
public:
    ToolId tool() const override { return ToolId::Text; }
    void set(LayerPtr layer, std::shared_ptr<const TextState> state, const QImage& base, const QRect& area)
    {
        m_layer = std::move(layer);
        m_state = std::move(state);
        m_base = base;
        m_area = area;
    }
    bool editable() const { return m_state && !m_base.isNull(); }
    const LayerPtr& layer() const { return m_layer; }
    const TextState& state() const { return *m_state; }
    const QImage& base() const { return m_base; }
    QRect area() const { return m_area; }
    qint64 memoryBytes() const override { return 0; }
    void releaseMemory() override
    {
        m_base = QImage();
        m_released = true;
    }

private:
    LayerPtr m_layer;
    std::shared_ptr<const TextState> m_state;
    QImage m_base;
    QRect m_area;
};

} // namespace

TextTool::TextTool(QObject* parent)
    : Tool(parent)
{
    m_blink.setInterval(530);
    connect(&m_blink, &QTimer::timeout, this, [this] {
        m_caretOn = !m_caretOn;
        repaint();
    });
    auto refresh = [this] {
        if (m_edit.isActive() && !m_restoring)
            changed();
    };
    connect(AppState::instance(), &AppState::toolSettingsChanged, this, refresh);
    connect(AppState::instance(), &AppState::colorsChanged, this, refresh);
}

QIcon TextTool::icon() const { return Icons::tool(QStringLiteral("text"), Icons::theme({"draw-text", "tool-text"})); }

QString TextTool::hint() const
{
    return m_edit.isActive()
             ? tr("Escribe el texto. Flechas, Inicio y Fin mueven el cursor; arrastra el tirador o con el botón "
                  "derecho para moverlo. Esc o Terminar: aplicar.")
             : tr("Haz clic donde quieras empezar a escribir con el color principal.");
}

QFont TextTool::font() const
{
    const ToolSettings& s = AppState::instance()->tools();
    QFont f = s.fontFamily.isEmpty() ? QGuiApplication::font() : QFont(s.fontFamily);
    const double dpi = (s.fontMetric == FontMetric::Points && document()) ? document()->dpi() : 96.0;
    f.setPixelSize(std::max(1, int(std::lround(s.fontSize * dpi / 72.0))));
    f.setBold(s.bold);
    f.setItalic(s.italic);
    f.setUnderline(s.underline);
    f.setStrikeOut(s.strikeout);
    f.setStyleStrategy(s.antialias ? QFont::PreferAntialias : QFont::NoAntialias);
    switch (s.textRendering) {
    case TextRendering::Smooth: f.setHintingPreference(QFont::PreferNoHinting); break;
    case TextRendering::SharpModern: f.setHintingPreference(QFont::PreferVerticalHinting); break;
    case TextRendering::SharpClassic: f.setHintingPreference(QFont::PreferFullHinting); break;
    }
    return f;
}

void TextTool::setInputMethodEnabled(bool on)
{
    if (!canvas())
        return;
    canvas()->setAttribute(Qt::WA_InputMethodEnabled, on);
    QGuiApplication::inputMethod()->update(Qt::ImEnabled | Qt::ImCursorRectangle);
}

QStringList TextTool::displayLines() const
{
    QStringList lines = m_lines;
    if (!m_preedit.isEmpty())
        lines[m_line].insert(m_col, m_preedit);
    return lines;
}

qreal TextTool::lineLeft(const QFontMetricsF& fm, const QString& line) const
{
    const qreal w = fm.horizontalAdvance(line);
    switch (AppState::instance()->tools().textAlign) {
    case TextAlign::Left: return m_pos.x();
    case TextAlign::Center: return m_pos.x() - std::round(w / 2);
    case TextAlign::Right: return m_pos.x() - w;
    }
    return m_pos.x();
}

QPoint TextTool::positionAt(const QPointF& p) const
{
    const QFontMetricsF fm(font());
    const int line = std::clamp(int(std::floor((p.y() - m_pos.y()) / fm.lineSpacing())), 0, int(m_lines.size()) - 1);
    const QString& s = m_lines[line];
    const qreal x = p.x() - lineLeft(fm, s);
    int best = 0;
    qreal bestDist = std::abs(x);
    for (int c = 1; c <= s.size(); ++c) {
        const qreal d = std::abs(fm.horizontalAdvance(s.left(c)) - x);
        if (d < bestDist) {
            bestDist = d;
            best = c;
        }
    }
    return QPoint(best, line);
}

void TextTool::mousePress(const ToolEvent& e)
{
    Document* doc = document();
    if (!doc || !doc->activeLayer())
        return;
    if (m_edit.isActive()) {
        if (e.button == Qt::RightButton || Handles::hit(e.viewPos, moveIcon(), 11)) {
            m_dragging = true;
            m_grab = e.pos;
            return;
        }
        const qreal margin = QFontMetricsF(font()).height();
        if (e.button == Qt::LeftButton && m_bounds.adjusted(-margin, -margin, margin, margin).contains(e.pos)) {
            const QPoint at = positionAt(e.pos);
            m_col = at.x();
            m_line = at.y();
            touch();
            return;
        }
    }
    if (e.button != Qt::LeftButton)
        return;
    finish();
    m_edit.begin(doc, doc->activeLayerPtr());
    m_resumed = false;
    m_pos = QPointF(std::round(e.pos.x()), std::round(e.pos.y()));
    m_lines = QStringList{QString()};
    m_line = m_col = 0;
    m_preedit.clear();
    m_caretOn = true;
    m_blink.start();
    setInputMethodEnabled(true);
    render();
    emit hintChanged(hint());
}

void TextTool::mouseMove(const ToolEvent& e)
{
    if (m_dragging) {
        m_pos += e.pos - m_grab;
        m_grab = e.pos;
        render();
        return;
    }
    if (canvas())
        canvas()->setToolCursor(m_edit.isActive() && Handles::hit(e.viewPos, moveIcon(), 11) ? QCursor(Qt::SizeAllCursor)
                                                                                                : cursor());
}

void TextTool::mouseRelease(const ToolEvent&)
{
    if (m_dragging) {
        m_dragging = false;
        m_pos = QPointF(std::round(m_pos.x()), std::round(m_pos.y()));
        changed();
    }
}

void TextTool::insertText(const QString& text)
{
    if (!m_edit.isActive() || text.isEmpty())
        return;
    QString t = text;
    t.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    t.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    t.replace(QLatin1Char('\t'), QLatin1String("    "));
    const QStringList parts = t.split(QLatin1Char('\n'));
    QString& cur = m_lines[m_line];
    const QString tail = cur.mid(m_col);
    cur.truncate(m_col);
    cur += parts.first();
    for (int i = 1; i < parts.size(); ++i)
        m_lines.insert(++m_line, parts[i]);
    m_col = int(m_lines[m_line].size());
    m_lines[m_line] += tail;
    changed();
}

bool TextTool::keyPress(QKeyEvent* e)
{
    if (!m_edit.isActive())
        return false;
    const bool ctrl = e->modifiers() & Qt::ControlModifier;
    QString& cur = m_lines[m_line];
    switch (e->key()) {
    case Qt::Key_Escape:
        finish();
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (ctrl) {
            finish();
            return true;
        }
        m_lines.insert(m_line + 1, cur.mid(m_col));
        m_lines[m_line].truncate(m_col);
        ++m_line;
        m_col = 0;
        changed();
        return true;
    case Qt::Key_Backspace:
        if (m_col > 0) {
            int from = ctrl ? wordLeft(cur, m_col) : m_col - 1;
            if (!ctrl && from > 0 && cur[from].isLowSurrogate())
                --from;
            cur.remove(from, m_col - from);
            m_col = from;
        } else if (m_line > 0) {
            m_col = int(m_lines[m_line - 1].size());
            m_lines[m_line - 1] += cur;
            m_lines.removeAt(m_line--);
        }
        changed();
        return true;
    case Qt::Key_Delete:
        if (m_col < cur.size()) {
            int to = ctrl ? wordRight(cur, m_col) : m_col + 1;
            if (!ctrl && to < cur.size() && cur[to].isLowSurrogate())
                ++to;
            cur.remove(m_col, to - m_col);
        } else if (m_line + 1 < m_lines.size()) {
            cur += m_lines[m_line + 1];
            m_lines.removeAt(m_line + 1);
        }
        changed();
        return true;
    case Qt::Key_Left:
        if (m_col > 0)
            m_col = ctrl ? wordLeft(cur, m_col) : m_col - (m_col > 1 && cur[m_col - 1].isLowSurrogate() ? 2 : 1);
        else if (m_line > 0)
            m_col = int(m_lines[--m_line].size());
        touch();
        return true;
    case Qt::Key_Right:
        if (m_col < cur.size())
            m_col = ctrl ? wordRight(cur, m_col) : m_col + (cur[m_col].isHighSurrogate() ? 2 : 1);
        else if (m_line + 1 < m_lines.size()) {
            ++m_line;
            m_col = 0;
        }
        touch();
        return true;
    case Qt::Key_Up:
    case Qt::Key_Down: {
        const int target = m_line + (e->key() == Qt::Key_Up ? -1 : 1);
        if (target >= 0 && target < m_lines.size()) {
            const QRectF c = caretRect();
            const QPoint at = positionAt(QPointF(c.left(), c.top() + (target - m_line + 0.5) * c.height()));
            m_line = target;
            m_col = at.x();
        }
        touch();
        return true;
    }
    case Qt::Key_Home:
        if (ctrl)
            m_line = 0;
        m_col = 0;
        touch();
        return true;
    case Qt::Key_End:
        if (ctrl)
            m_line = int(m_lines.size()) - 1;
        m_col = int(m_lines[m_line].size());
        touch();
        return true;
    case Qt::Key_Tab:
        return true;
    case Qt::Key_V:
        if (ctrl) {
            insertText(QGuiApplication::clipboard()->text());
            return true;
        }
        break;
    default:
        break;
    }
    const QString t = e->text();
    if (!t.isEmpty() && !(e->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)) && t.front().isPrint()) {
        insertText(t);
        return true;
    }
    return false;
}

void TextTool::inputMethod(QInputMethodEvent* e)
{
    if (!m_edit.isActive())
        return;
    m_preedit = e->preeditString();
    if (!e->commitString().isEmpty())
        insertText(e->commitString());
    else
        render();
}

void TextTool::touch()
{
    m_caretOn = true;
    m_blink.start();
    ensureCaretVisible();
    QGuiApplication::inputMethod()->update(Qt::ImCursorRectangle);
    repaint();
}

void TextTool::render()
{
    const ToolSettings& s = AppState::instance()->tools();
    const QFont f = font();
    const QFontMetricsF fm(f);
    const QStringList lines = displayLines();
    const qreal ls = fm.lineSpacing();

    QRectF bounds(m_pos, QSizeF(1, ls));
    for (int i = 0; i < lines.size(); ++i) {
        const qreal left = lineLeft(fm, lines[i]);
        bounds |= fm.boundingRect(lines[i]).translated(left, m_pos.y() + fm.ascent() + i * ls);
        bounds |= QRectF(left, m_pos.y() + i * ls, fm.horizontalAdvance(lines[i]), ls);
    }
    m_bounds = bounds;

    const AppState* app = AppState::instance();
    renderVector(m_edit, bounds.adjusted(-2, -2, 2, 2).toAlignedRect(), [&](QPainter& p, bool coverage) {
        p.setRenderHint(QPainter::TextAntialiasing, s.antialias);
        p.setFont(f);
        p.setPen(QPen(coverage ? QBrush(Qt::black) : Raster::patternBrush(0, app->primary(), app->secondary()), 1));
        for (int i = 0; i < lines.size(); ++i)
            p.drawText(QPointF(lineLeft(fm, lines[i]), m_pos.y() + fm.ascent() + i * ls), lines[i]);
    });
    touch();
}

QRectF TextTool::caretRect() const
{
    const QFontMetricsF fm(font());
    const QStringList lines = displayLines();
    const QString& line = lines[m_line];
    const qreal x = lineLeft(fm, line) + fm.horizontalAdvance(line.left(m_col + int(m_preedit.size())));
    return QRectF(x, m_pos.y() + m_line * fm.lineSpacing(), 1, fm.lineSpacing());
}

QRectF TextTool::inputCursorRect() const
{
    if (!m_edit.isActive() || !canvas())
        return {};
    return canvas()->imageToView(caretRect());
}

QPointF TextTool::moveIcon() const
{
    // Debajo y a la derecha del final del texto, como el tirador de Paint.NET.
    return Handles::moveIconPos(canvas()->imageToView(m_bounds));
}

void TextTool::ensureCaretVisible()
{
    if (!canvas())
        return;
    const QRectF c = canvas()->imageToView(caretRect()).adjusted(-20, -20, 20, 20);
    const QRectF view = canvas()->viewport()->rect();
    QPointF d;
    if (c.right() > view.right())
        d.rx() = c.right() - view.right();
    else if (c.left() < view.left())
        d.rx() = c.left() - view.left();
    if (c.bottom() > view.bottom())
        d.ry() = c.bottom() - view.bottom();
    else if (c.top() < view.top())
        d.ry() = c.top() - view.top();
    if (!d.isNull())
        canvas()->scrollByView(d);
}

void TextTool::paintOverlay(QPainter& p)
{
    if (!m_edit.isActive())
        return;
    const QRectF box = canvas()->imageToView(m_bounds.adjusted(-3, -3, 3, 3));
    p.setPen(QPen(QColor(0, 120, 215), 0, Qt::DashLine));
    p.setBrush(Qt::NoBrush);
    p.drawRect(box);
    if (m_caretOn) {
        const QRectF c = canvas()->imageToView(caretRect());
        p.fillRect(QRectF(c.left(), c.top(), std::max(1.5, canvas()->scale()), c.height()), QColor(0, 120, 215));
    }
    Handles::draw(p, moveIcon(), Handles::Shape::Move);
}

void TextTool::changed()
{
    // Lo escrito aparece en el historial al momento ("Editar texto" si se retomó).
    render();
    if ((m_lines.join(QString()).isEmpty() && !m_resumed) || m_edit.liveStep())
        return;
    m_pushing = true;
    m_edit.showInHistory(m_resumed ? tr("Editar texto") : name(), QStringLiteral("draw-text"), new TextStepCommand);
    m_pushing = false;
}

bool TextTool::commitLive()
{
    // Registra el paso en vivo con el estado para retomarlo; la edición sigue abierta desde él.
    Document* doc = m_edit.document();
    const LayerPtr layer = m_edit.layer();
    const QImage base = m_edit.before();
    const QRect area = m_edit.touchedArea();
    QString text = m_resumed ? tr("Editar texto") : name();
    if (auto* step = dynamic_cast<TextStepCommand*>(m_edit.liveStep())) {
        auto st = std::make_shared<TextState>();
        st->settings = AppState::instance()->tools();
        st->primary = AppState::instance()->primary();
        st->secondary = AppState::instance()->secondary();
        st->pos = m_pos;
        st->lines = m_lines;
        step->set(layer, st, base, area);
        text = step->text();
    }
    // Sin paso en vivo (retomado sin cambios) solo se registra lo que haya cambiado.
    m_pushing = true;
    const bool done = m_edit.commit(text, QStringLiteral("draw-text"));
    m_pushing = false;
    m_edit.resume(doc, layer, base, area);
    m_resumed = true;
    return done;
}

void TextTool::leave()
{
    m_edit.detach();
    m_lines.clear();
    m_preedit.clear();
    m_dragging = false;
    m_resumed = false;
    m_blink.stop();
    setInputMethodEnabled(false);
    emit hintChanged(hint());
    repaint();
}

void TextTool::finish()
{
    if (!m_edit.isActive())
        return;
    m_preedit.clear();
    m_dragging = false;
    if (m_lines.join(QString()).trimmed().isEmpty() && !m_resumed) {
        m_edit.cancel();
    } else {
        render();
        Document* doc = m_edit.document();
        if (commitLive()) {
            const QString text = tr("Finalizar texto");
            HistoryIcons::registerIcon(text, QStringLiteral("draw-text"));
            m_pushing = true;
            doc->undoStack()->push(new QUndoCommand(text));
            m_pushing = false;
        }
    }
    leave();
}

void TextTool::beforeHistoryJump()
{
    // Lo escrito se registra; el historial decide si se retoma.
    if (!m_edit.liveStep())
        return;
    m_preedit.clear();
    render();
    commitLive();
}

void TextTool::historyChanged()
{
    if (m_pushing)
        return;
    Document* doc = document();
    const QUndoStack* stack = doc ? doc->undoStack() : nullptr;
    const auto* step = stack && stack->index() > 0 ? dynamic_cast<const TextStepCommand*>(stack->command(stack->index() - 1))
                                                   : nullptr;
    if (!step || !step->editable()) {
        if (m_edit.isActive())
            leave();
        return;
    }
    // Se vuelve a un texto: su fuente, opciones y colores (que pasan a ser los actuales), y el
    // cursor al final para seguir escribiendo.
    const TextState& st = step->state();
    m_restoring = true;
    AppState::instance()->updateTools([&st](ToolSettings& s) {
        const ToolSettings& o = st.settings;
        s.fontFamily = o.fontFamily;
        s.fontSize = o.fontSize;
        s.bold = o.bold;
        s.italic = o.italic;
        s.underline = o.underline;
        s.strikeout = o.strikeout;
        s.textAlign = o.textAlign;
        s.fontMetric = o.fontMetric;
        s.textRendering = o.textRendering;
        s.antialias = o.antialias;
        s.blend = o.blend;
    });
    AppState::instance()->setPrimary(st.primary);
    AppState::instance()->setSecondary(st.secondary);
    m_restoring = false;
    m_edit.resume(doc, step->layer(), step->base(), step->area());
    m_pos = st.pos;
    m_lines = st.lines.isEmpty() ? QStringList{QString()} : st.lines;
    m_line = int(m_lines.size()) - 1;
    m_col = int(m_lines[m_line].size());
    m_preedit.clear();
    m_dragging = false;
    m_resumed = true;
    m_caretOn = true;
    m_blink.start();
    setInputMethodEnabled(true);
    render();
    emit hintChanged(hint());
}

void TextTool::deactivate()
{
    finish();
    Tool::deactivate();
}
