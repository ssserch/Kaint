#include "app/tools/TextTool.h"

#include "app/AppState.h"
#include "app/Canvas.h"
#include "app/Icons.h"
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
        if (m_edit.isActive())
            render();
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
        render();
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
    render();
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
        render();
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
        render();
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
        render();
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

void TextTool::finish()
{
    if (!m_edit.isActive())
        return;
    m_preedit.clear();
    m_dragging = false;
    if (m_lines.join(QString()).trimmed().isEmpty())
        m_edit.cancel();
    else {
        render();
        m_edit.commit(name(), QStringLiteral("draw-text"));
    }
    m_lines.clear();
    m_blink.stop();
    setInputMethodEnabled(false);
    emit hintChanged(hint());
    repaint();
}

void TextTool::deactivate()
{
    finish();
    Tool::deactivate();
}
