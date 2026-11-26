#include "app/MainWindow.h"

#include "app/Theme.h"

#include "app/Animations.h"
#include "app/AppState.h"
#include "app/Canvas.h"
#include "app/collab/CollabController.h"
#include "app/collab/CollabProtocol.h"
#include "app/Icons.h"
#include "app/Recovery.h"
#include "app/Shortcuts.h"
#include "app/UserFonts.h"
#include "app/ToolOptionsBar.h"
#include "app/WindowFrame.h"
#include "app/Customization.h"
#include "app/Workspace.h"
#include "app/DiscordPresence.h"
#include "app/IconPacks.h"
#include "app/dialogs/AboutDialog.h"
#include "app/dialogs/DialogExec.h"
#include "app/dialogs/EffectDialog.h"
#include "app/dialogs/GifFramesDialog.h"
#include "app/GifEditor.h"
#include "app/GifExport.h"
#include "app/widgets/BrushAdvanced.h"
#include "app/dialogs/ImageDialogs.h"
#include "app/dialogs/SaveAllDialog.h"
#include "app/widgets/BarSlider.h"
#include "app/dialogs/SaveConfigDialog.h"
#include "app/dialogs/TaskDialog.h"
#include "app/dialogs/WelcomeDialog.h"
#include "app/panels/ColorsPanel.h"
#include "app/panels/HistoryPanel.h"
#include "app/panels/LayersPanel.h"
#include "app/panels/ToolsPanel.h"
#include "app/tools/MoveTools.h"
#include "app/tools/ToolManager.h"
#include "app/widgets/FontChooser.h"
#include "app/widgets/SliderBehavior.h"
#include "core/Commands.h"
#include "core/CustomShapes.h"
#include "core/Document.h"
#include "core/ImageIO.h"
#include "core/Ora.h"
#include "core/Raster.h"
#include "core/plugins/CraftPlugins.h"
#include "core/plugins/PaintJs.h"
#include "core/plugins/PdnBridge.h"
#include "core/plugins/PluginManager.h"

#include <QActionGroup>
#include <QApplication>
#include <QBuffer>
#include <QElapsedTimer>
#include <QTabWidget>
#include <QCollator>
#include <QClipboard>
#include <QCloseEvent>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDataStream>
#include <QDateTime>
#include <QDragEnterEvent>
#include <QEventLoop>
#include <QPointingDevice>
#include <QWheelEvent>
#include <QCursor>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImageReader>
#include <QColorDialog>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QColorTransform>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QLinearGradient>
#include <QPainter>
#include <QPointer>
#include <QPrintPreviewDialog>
#include <QPrintPreviewWidget>
#include <QPrinter>
#ifdef Q_OS_WIN
#include <QPrinterInfo>
#endif
#include <QProcess>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QScrollBar>
#include <QWindow>
#include <QSettings>
#include <QStackedWidget>
#include <QSlider>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStylePainter>
#include <QTabBar>
#include <QToolBar>
#include <QToolButton>
#include <QUndoGroup>
#include <QUrl>

#include <atomic>
#include <cmath>
#include <thread>

namespace {

QImage flipped(const QImage& img, bool horizontal)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    return img.flipped(horizontal ? Qt::Horizontal : Qt::Vertical);
#else
    return img.mirrored(horizontal, !horizontal);
#endif
}

// El deslizador de zoom es logarítmico entre 1 % y 6400 %.
constexpr double kZoomMin = 0.01;
constexpr double kZoomMax = 64.0;

int zoomToSlider(qreal z)
{
    return int(std::round(std::log(z / kZoomMin) / std::log(kZoomMax / kZoomMin) * 1000));
}

qreal sliderToZoom(int v)
{
    return kZoomMin * std::pow(kZoomMax / kZoomMin, v / 1000.0);
}

// "v1.2.10" > "1.2.9": compara los números de versión uno a uno.
bool versionNewer(const QString& candidate, const QString& current)
{
    auto parts = [](QString v) {
        v.remove(QRegularExpression(QStringLiteral("^[^0-9]*")));
        QList<int> out;
        for (const QString& p : v.split(QRegularExpression(QStringLiteral("[^0-9]+")), Qt::SkipEmptyParts))
            out << p.toInt();
        return out;
    };
    const QList<int> a = parts(candidate), b = parts(current);
    for (int i = 0; i < std::max(a.size(), b.size()); ++i) {
        const int x = a.value(i), y = b.value(i);
        if (x != y)
            return x > y;
    }
    return false;
}

// Huella de una imagen (tamaño y una rejilla de píxeles): reconoce en el portapapeles lo que
// copió Kaint aunque vuelva convertido.
quint64 imageSignature(const QImage& source)
{
    if (source.isNull())
        return 0;
    const QImage image = source.convertToFormat(QImage::Format_ARGB32);
    quint64 h = quint64(image.width()) * 1000003u + quint64(image.height());
    for (int gy = 0; gy < 16; ++gy)
        for (int gx = 0; gx < 16; ++gx) {
            const int x = (image.width() - 1) * gx / 15;
            const int y = (image.height() - 1) * gy / 15;
            h = (h ^ image.pixel(x, y)) * 1099511628211ull;
        }
    return h;
}

// La forma de lo copiado (lo que no es un rectángulo: varita mágica, lazo, elipse...), junto a la
// imagen en el portapapeles, como hace Paint.NET: al pegar en Kaint (en esta imagen, en otra o en
// otra ventana de Kaint) la selección vuelve a tener esa forma y no el rectángulo con lo
// transparente de alrededor.
const QString kSelectionShapeMime = QStringLiteral("application/x-kaint-selection-shape");

QByteArray encodeSelectionShape(const Selection& shape, QSize imageSize)
{
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_0);
    out << quint32(0x4b53454c) << quint32(1) << imageSize << shape.isPathBased();
    if (shape.isPathBased())
        out << shape.path();
    else
        out << shape.mask();
    return bytes;
}

Selection decodeSelectionShape(const QByteArray& bytes, QSize imageSize)
{
    QDataStream in(bytes);
    in.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0, version = 0;
    QSize size;
    bool hasPath = false;
    in >> magic >> version >> size >> hasPath;
    if (in.status() != QDataStream::Ok || magic != 0x4b53454c || version != 1 || size != imageSize)
        return {};
    if (hasPath) {
        QPainterPath path;
        in >> path;
        return in.status() == QDataStream::Ok ? Selection::fromPath(path, imageSize) : Selection();
    }
    QImage mask;
    in >> mask;
    if (in.status() != QDataStream::Ok || mask.size() != imageSize)
        return {};
    return Selection::fromMask(mask);
}

// Lo que tiene píxeles de una imagen que viene de fuera, si alrededor es transparente (un PNG
// recortado, una pegatina...); vacía si es opaca entera.
Selection shapeFromAlpha(const QImage& image)
{
    const QImage src = image.convertToFormat(QImage::Format_ARGB32);
    QImage mask(src.size(), QImage::Format_Alpha8);
    bool clear = false, any = false;
    for (int y = 0; y < src.height(); ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(src.constScanLine(y));
        uchar* m = mask.scanLine(y);
        for (int x = 0; x < src.width(); ++x) {
            const bool has = qAlpha(row[x]) != 0;
            m[x] = has ? 255 : 0;
            any = any || has;
            clear = clear || !has;
        }
    }
    return clear && any ? Selection::fromMask(mask) : Selection();
}

// Tira de imágenes: no pide más ancho que el de sus pestañas (QTabBar reserva sitio para las
// flechas de desplazamiento aunque no hagan falta), así la flecha de la lista queda pegada a la
// última miniatura. Las miniaturas las dibuja la tira, y la X de cerrar va en la esquina del
// recuadro de la imagen actual y de la que tiene el puntero, como en Paint.NET. Se reordenan
// arrastrando.
class ImageStrip : public QTabBar {
public:
    explicit ImageStrip(QWidget* parent)
        : QTabBar(parent)
    {
        setMouseTracking(true);
        // La X aparece y desaparece con un fundido de 200 ms (como en paint.js).
        m_closeTimer.setInterval(16);
        connect(&m_closeTimer, &QTimer::timeout, this, &ImageStrip::stepCloseFade);
        connect(&m_scrollTimer, &QTimer::timeout, this, &ImageStrip::stepScroll);
        connect(&m_shiftTimer, &QTimer::timeout, this, &ImageStrip::stepShift);
        // Los botones de desplazamiento de QTabBar: las flechas las dibuja y las atiende la tira. Con
        // algunos temas (hojas de estilo) salían con su ancho encima de la flecha derecha, con otro
        // recuadro y otra flecha al pasar el puntero o pulsar: no se pintan y el ratón los atraviesa.
        for (QToolButton* b : findChildren<QToolButton*>(Qt::FindDirectChildrenOnly)) {
            b->setAttribute(Qt::WA_TransparentForMouseEvents);
            b->setFocusPolicy(Qt::NoFocus);
            b->installEventFilter(this);
        }
        connect(this, &QTabBar::currentChanged, this, &ImageStrip::fadeCloseButtons);
        connect(this, &QTabBar::tabMoved, this, [this](int, int to) {
            clearRange();
            fixGroupAfterMove(to);
            resetCloseButtons();
        });
    }

    // --- Grupos de imágenes ---------------------------------------------------------------------
    // Ctrl+clic (o Mayús+clic) en otra imagen elige desde la actual hasta ella; el «+» que aparece
    // encima las agrupa. Un grupo lleva un nombre y el color de su borde, y se contrae a su primera
    // imagen (ahorra sitio en la tira) o se expande con un clic en su nombre.
    struct Group {
        QString name;
        QColor color;
        bool collapsed = false;
    };
    int groupOf(int index) const { return index >= 0 && index < count() ? marks(index) >> kGroupShift : 0; }
    bool hasGroup(int id) const { return m_groups.contains(id); }
    Group group(int id) const { return m_groups.value(id); }
    QList<int> membersOf(int id) const
    {
        QList<int> out;
        for (int i = 0; id > 0 && i < count(); ++i)
            if (groupOf(i) == id)
                out << i;
        return out;
    }
    bool hasRange() const { return m_rangeA >= 0 && m_rangeB >= 0 && m_rangeA != m_rangeB && m_rangeA < count() && m_rangeB < count(); }
    bool inRange(int index) const
    {
        return hasRange() && index >= std::min(m_rangeA, m_rangeB) && index <= std::max(m_rangeA, m_rangeB);
    }
    void clearRange()
    {
        if (m_rangeA < 0 && m_rangeB < 0)
            return;
        m_rangeA = m_rangeB = -1;
        m_hoverPlus = false;
        update();
    }
    // Agrupa las imágenes elegidas (las de otro grupo pasan a este). Devuelve el grupo nuevo.
    int groupRange(const QString& name)
    {
        if (!hasRange())
            return 0;
        static const QColor palette[] = {QColor(0xe7, 0x4c, 0x3c), QColor(0xf3, 0x9c, 0x12), QColor(0x27, 0xae, 0x60),
                                         QColor(0x29, 0x80, 0xb9), QColor(0x8e, 0x44, 0xad), QColor(0xe9, 0x1e, 0x63),
                                         QColor(0x16, 0xa0, 0x85), QColor(0x7f, 0x8c, 0x8d)};
        const int id = m_nextGroup++;
        m_groups.insert(id, Group{name, palette[(id - 1) % std::size(palette)], false});
        for (int i = std::min(m_rangeA, m_rangeB); i <= std::max(m_rangeA, m_rangeB); ++i)
            setGroupOf(i, id);
        clearRange();
        cleanupGroups();
        return id;
    }
    void setGroupName(int id, const QString& name)
    {
        if (m_groups.contains(id)) {
            m_groups[id].name = name;
            update();
        }
    }
    void setGroupColor(int id, const QColor& color)
    {
        if (m_groups.contains(id) && color.isValid()) {
            m_groups[id].color = color;
            update();
        }
    }
    void setCollapsed(int id, bool collapsed)
    {
        if (!m_groups.contains(id) || m_groups[id].collapsed == collapsed)
            return;
        m_groups[id].collapsed = collapsed;
        applyGroupVisibility();
    }
    void ungroup(int id)
    {
        for (int i : membersOf(id))
            setGroupOf(i, 0);
        m_groups.remove(id);
        applyGroupVisibility();
    }
    void removeFromGroup(int index)
    {
        setGroupOf(index, 0);
        cleanupGroups();
    }
    // Si la imagen está dentro de un grupo contraído, el grupo se expande (para verla).
    void revealTab(int index)
    {
        const int id = groupOf(index);
        if (id > 0 && m_groups.value(id).collapsed && !isTabVisible(index))
            setCollapsed(id, false);
    }
    // Al elegir otra imagen con un clic normal, se olvida la selección de varias.
    std::function<void()> onGroupRequested;

    bool eventFilter(QObject* watched, QEvent* e) override
    {
        if (e->type() == QEvent::Paint && qobject_cast<QToolButton*>(watched) && watched->parent() == this)
            return true;
        return QTabBar::eventFilter(watched, e);
    }

    QSize minimumSizeHint() const override
    {
        const QSize min = QTabBar::minimumSizeHint();
        return QSize(std::min(min.width(), sizeHint().width()), min.height());
    }

    void setFullHeight(bool on)
    {
        if (m_fullHeight == on)
            return;
        m_fullHeight = on;
        setIconSize(iconSize());
        updateGeometry();
        thumbBoxChanged();
    }

    // Tamaño máximo de las miniaturas. Como en Paint.NET, el recuadro es igual para todas las
    // imágenes y lo que cambia es la miniatura dentro de él: a lo alto de toda la fila, un hueco
    // cuadrado (una imagen apaisada deja un poco de recuadro vacío arriba; una alargada llega
    // hasta arriba).
    QSize thumbBox() const
    {
        if (!m_fullHeight)
            return iconSize();
        const int h = std::max(iconSize().height(), height() - 2 * kPadY - kTopGap);
        // Casi cuadrado, como paint.rs: una imagen apaisada deja hueco arriba.
        return QSize(std::max(iconSize().width(), h * 26 / 25), h);
    }
    // Se llama cuando cambia thumbBox(): las miniaturas se vuelven a crear con el tamaño nuevo.
    std::function<void()> onThumbBoxChanged;

    // Miniatura de una pestaña, centrada en su hueco. Con el recuadro a lo alto de toda la fila, el
    // hueco va abajo (como en Paint.NET: la miniatura junto a la barra de herramientas y el recuadro
    // subiendo por encima de ella).
    QRect thumbRect(int index) const
    {
        const QRect tab = tabRect(index);
        const QSize box = thumbBox();
        const QSize thumb = tabIcon(index).actualSize(box).boundedTo(box);
        const QRect slot = m_fullHeight ? tab.adjusted(kPadX, kPadY + kTopGap, -kPadX, -kPadY)
                                        : QRect(tab.left() + kPadX, tab.top() + std::max(kPadY, tab.height() - kPadY - box.height()),
                                                box.width(), box.height());
        // A lo alto de toda la fila, abajo del recuadro (como paint.rs y Paint.NET); si no, centrada.
        const int y = m_fullHeight ? slot.bottom() + 1 - thumb.height() : slot.top() + (slot.height() - thumb.height()) / 2;
        return QRect(slot.left() + (slot.width() - thumb.width()) / 2, y, thumb.width(), thumb.height());
    }

    // Siempre en la esquina de arriba a la derecha del recuadro de la pestaña (dentro de su
    // margen), aunque la miniatura sea estrecha o muy baja.
    QRect closeRect(int index) const
    {
        const QRect tab = tabRect(index);
        return QRect(tab.right() - 2 - kClose, tab.top() + 4, kClose, kClose);
    }

    // Punto de "sin guardar": siempre en la esquina de arriba a la izquierda, frente a la X.
    QRect unsavedRect(int index) const
    {
        const QRect tab = tabRect(index);
        return QRect(tab.left() + 2, tab.top() + 4, kDot, kDot);
    }
    void setUnsaved(int index, bool unsaved)
    {
        setMarks(index, (marks(index) & ~kUnsaved) | (unsaved ? kUnsaved : 0));
    }
    // Imagen de la sesión colaborativa: la pestaña amarilla, las personas en lugar de la estrella
    // y, debajo, la corona (0 no, 1 sin guardar, 2 guardada) y el equipo (0 no, 1 sin copia,
    // 2 copia atrasada, 3 copia al día).
    void setCollab(int index, bool collab, int crown, int computer)
    {
        int m = marks(index) & (kUnsaved | kGroupMask);
        if (collab)
            m |= kCollab | (crown << 2) | (computer << 4);
        setMarks(index, m);
    }

    // Marcas debajo de la de arriba a la izquierda (la corona y el equipo).
    QRect markRect(int index, int row) const { return unsavedRect(index).translated(0, row * (kDot + 3)); }

protected:
    // Al abrir o cerrar una imagen, un arrastre a medias se olvida y los desplazamientos de las
    // pestañas también (eran de otros índices: una pestaña se quedaba encima de otra, corrida, hasta
    // volver a arrastrar alguna).
    void tabInserted(int index) override
    {
        QTabBar::tabInserted(index);
        resetDrag();
        clearRange();
        resetCloseButtons();
    }
    void tabRemoved(int index) override
    {
        QTabBar::tabRemoved(index);
        resetDrag();
        clearRange();
        cleanupGroups();
        resetCloseButtons();
    }
    void resetDrag()
    {
        m_dragging = false;
        m_dragTab = -1;
        m_pressedTab = -1;
        m_shift.clear();
        m_shiftTimer.stop();
        update();
    }
    int firstVisible() const
    {
        for (int i = 0; i < count(); ++i)
            if (isTabVisible(i))
                return i;
        return -1;
    }
    int lastVisible() const
    {
        for (int i = count() - 1; i >= 0; --i)
            if (isTabVisible(i))
                return i;
        return -1;
    }

    QSize tabSizeHint(int index) const override
    {
        Q_UNUSED(index);
        const QSize base = iconSize() + QSize(2 * kPadX, 2 * kPadY);
        // A lo alto de toda la fila de arriba: el recuadro llega arriba del todo (el mismo para
        // todas las imágenes).
        return m_fullHeight ? QSize(thumbBox().width() + 2 * kPadX, std::max(base.height(), height())) : base;
    }

    // Las flechas de los bordes (como en Paint.NET), solo hacia donde quedan imágenes cortadas. Las
    // calcula la tira (las de QTabBar se quedaban activas al llegar al final: la de la derecha tapaba
    // la X de la última imagen y no se podía cerrar, y la de la izquierda a veces no hacía nada).
    QRect arrowRect(bool right) const
    {
        return QRect(right ? width() - kArrow : 0, 0, kArrow, height());
    }
    bool arrowShown(bool right) const
    {
        const int first = firstVisible(), last = lastVisible();
        if (first < 0)
            return false;
        return right ? tabRect(last).right() > width() - 1 : tabRect(first).left() < 0;
    }
    // Un clic en una flecha: la imagen siguiente hacia ese lado queda entera a la vista (junto a la
    // flecha o, si es la última, en el borde), con una animación corta.
    void scrollOneImage(bool right)
    {
        const int n = count();
        int delta = 0;
        if (!right) {
            for (int i = n - 1; i >= 0; --i) {
                const QRect r = tabRect(i);
                if (isTabVisible(i) && r.left() < kArrow) {
                    delta = i == firstVisible() ? -r.left() : kArrow - r.left();
                    break;
                }
            }
        } else {
            for (int i = 0; i < n; ++i) {
                const QRect r = tabRect(i);
                if (isTabVisible(i) && r.right() > width() - 1 - kArrow) {
                    delta = i == lastVisible() ? width() - 1 - r.right() : width() - 1 - kArrow - r.right();
                    break;
                }
            }
        }
        if (delta == 0)
            return;
        if (!Animations::enabled()) {
            scrollPixels(delta);
            update();
            return;
        }
        // Seguido, en el mismo sentido, se suma; en el otro, se olvida lo que quedaba.
        if ((delta > 0) != (m_scrollPending > 0))
            m_scrollPending = 0;
        m_scrollPending += delta;
        if (!m_scrollTimer.isActive()) {
            m_scrollTimer.setInterval(10);
            m_scrollTimer.start();
        }
    }
    void paintScrollArrows(QPainter& p)
    {
        for (bool right : {false, true}) {
            if (!arrowShown(right))
                continue;
            const QRect r = arrowRect(right);
            const bool hot = m_hoverArrow == int(right);
            // Al pasar el puntero, la columna de la flecha resaltada (como en Paint.NET).
            if (hot) {
                QColor fill = palette().color(QPalette::Highlight);
                fill.setAlpha(m_arrowDown == int(right) ? 150 : 95);
                p.save();
                p.setRenderHint(QPainter::Antialiasing, false);
                p.fillRect(r, fill);
                p.setPen(palette().color(QPalette::Highlight));
                p.setBrush(Qt::NoBrush);
                p.drawRect(r.adjusted(0, 0, -1, -1));
                p.restore();
            }
            // Siempre la doble flecha blanca con borde oscuro (se lee sobre cualquier miniatura): antes,
            // con el estilo de Kaint, al pasar el puntero se cambiaba por otro icono más pequeño.
            const QPointF c = QRectF(r).center();
            QPainterPath chevrons;
            const qreal dir = right ? 1 : -1;
            for (qreal x : {-2.5, 2.5}) {
                chevrons.moveTo(c.x() + x - dir * 2.5, c.y() - 4.5);
                chevrons.lineTo(c.x() + x + dir * 2.0, c.y());
                chevrons.lineTo(c.x() + x - dir * 2.5, c.y() + 4.5);
            }
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor(0, 0, 0, 150), 3.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPath(chevrons);
            p.setPen(QPen(QColor(255, 255, 255, hot ? 255 : 225), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPath(chevrons);
        }
    }

    // --- Arrastrar una miniatura (como en Paint.NET): sigue al puntero y las demás se apartan ---
    // Desplazamiento en pantalla de cada pestaña respecto a su sitio (se anima hacia 0, o hacia el
    // hueco que deja la que se arrastra).
    int shiftOf(int index) const { return index >= 0 && index < m_shift.size() ? int(std::lround(m_shift[index])) : 0; }
    int slotWidth() const
    {
        const int first = firstVisible();
        if (first < 0)
            return 0;
        for (int i = first + 1; i < count(); ++i)
            if (isTabVisible(i))
                return tabRect(i).left() - tabRect(first).left();
        return tabRect(first).width();
    }
    // Dónde se dibuja la miniatura arrastrada: sigue al puntero, pero como en Paint.NET no pasa de
    // la primera ni de la última imagen (antes seguía de largo y el borde de la tira la cortaba).
    int dragLeft() const
    {
        const int first = firstVisible(), last = lastVisible();
        if (first < 0 || m_dragTab < 0)
            return m_dragX - m_grab;
        const int w = tabRect(m_dragTab).width();
        const int lo = std::max(0, tabRect(first).left());
        const int hi = std::max(lo, std::min(width() - w, tabRect(last).left()));
        return std::clamp(m_dragX - m_grab, lo, hi);
    }
    // Dónde quedaría la miniatura arrastrada: el sitio cuyo centro está más cerca del suyo.
    int dropIndex() const
    {
        const int w = tabRect(m_dragTab).width();
        const int center = dragLeft() + w / 2;
        int best = m_dragTab, bestDistance = INT_MAX;
        for (int i = 0; i < count(); ++i) {
            if (!isTabVisible(i))
                continue;
            const int d = std::abs(tabRect(i).center().x() - center);
            if (d < bestDistance) {
                bestDistance = d;
                best = i;
            }
        }
        return best;
    }
    qreal shiftTarget(int index) const
    {
        if (!m_dragging)
            return 0;
        if (index == m_dragTab)
            return dragLeft() - tabRect(index).left();
        const int to = dropIndex();
        const int w = slotWidth();
        if (m_dragTab < to && index > m_dragTab && index <= to)
            return -w;
        if (m_dragTab > to && index >= to && index < m_dragTab)
            return w;
        return 0;
    }
    void startShiftAnimation()
    {
        if (m_shift.size() != count())
            m_shift.resize(count(), 0.0);
        if (!m_shiftTimer.isActive()) {
            m_shiftClock.start();
            m_shiftTimer.start(16);
        }
        stepShift();
    }
    void stepShift()
    {
        if (m_shift.size() != count())
            m_shift.resize(count(), 0.0);
        // Con el puntero en un borde, la tira se desplaza sola para llegar a las que no se ven.
        if (m_dragging) {
            if (m_dragX < kArrow * 2 && arrowShown(false))
                scrollPixels(10);
            else if (m_dragX > width() - kArrow * 2 && arrowShown(true))
                scrollPixels(-10);
        }
        const qreal elapsed = std::max<qreal>(1, m_shiftClock.restart());
        // Las que se apartan llegan en unos 120 ms (más rápido al principio).
        const qreal k = Animations::enabled() ? 1.0 - std::pow(0.001, elapsed / 360.0) : 1.0;
        bool moving = false;
        for (int i = 0; i < count(); ++i) {
            const qreal target = shiftTarget(i);
            qreal& v = m_shift[i];
            v = (m_dragging && i == m_dragTab) ? target : v + (target - v) * k;
            if (std::abs(target - v) < 0.5)
                v = target;
            moving = moving || v != target || target != 0;
        }
        if (!moving && !m_dragging) {
            m_shiftTimer.stop();
            m_shift.clear();
        }
        update();
    }
    // Suelta la miniatura: pasa a su sitio nuevo (moveTab) y, desde donde estaba en pantalla, todas
    // van a su lugar sin saltos.
    void finishDrag()
    {
        const int from = m_dragTab;
        int to = dropIndex();
        // Detrás de un grupo contraído: después de todas sus imágenes (no entre la que se ve y las
        // ocultas).
        if (to > from) {
            const int g = groupOf(to);
            if (g > 0 && m_groups.value(g).collapsed) {
                const QList<int> members = membersOf(g);
                if (!members.isEmpty())
                    to = members.last();
            }
        }
        QList<int> visual;
        for (int i = 0; i < count(); ++i)
            visual << tabRect(i).left() + (i == from ? dragLeft() - tabRect(i).left() : shiftOf(i));
        m_dragging = false;
        m_dragTab = -1;
        if (from != to)
            moveTab(from, to);
        m_shift = QList<qreal>(count(), 0.0);
        for (int i = 0; i < count() && i < visual.size(); ++i) {
            int j = i;
            if (i == from)
                j = to;
            else if (from < to && i > from && i <= to)
                j = i - 1;
            else if (from > to && i >= to && i < from)
                j = i + 1;
            m_shift[j] = visual[i] - tabRect(j).left();
        }
        startShiftAnimation();
    }

    // La rueda del ratón desplaza la tira (como arrastrándola), no cambia de imagen: Qt ya lo
    // hace así con los touchpads (desplazamiento en píxeles), así que la rueda se le pasa como uno.
    void wheelEvent(QWheelEvent* e) override
    {
        const QPoint pixel = e->pixelDelta();
        const QPoint angle = e->angleDelta();
        // Vertical u horizontal, lo que mueva más (la rueda normal es vertical).
        // El touchpad, tal cual (ya es suave); la rueda, 180 px por paso, repartidos en una
        // animación corta (se suman si se gira seguido).
        if (!pixel.isNull()) {
            scrollPixels(std::abs(pixel.y()) > std::abs(pixel.x()) ? pixel.y() : pixel.x());
            e->accept();
            return;
        }
        const int delta = (std::abs(angle.y()) > std::abs(angle.x()) ? angle.y() : angle.x()) * 3 / 2;
        if (delta == 0) {
            e->ignore();
            return;
        }
        // Al cambiar de sentido, lo que quedaba se olvida.
        if ((delta > 0) != (m_scrollPending > 0))
            m_scrollPending = 0;
        m_scrollPending += delta;
        if (!m_scrollTimer.isActive()) {
            m_scrollTimer.setInterval(10);
            m_scrollTimer.start();
        }
        e->accept();
    }
    void scrollPixels(int delta)
    {
        static const QPointingDevice* const kPixelWheel = new QPointingDevice(
            QStringLiteral("kaint-strip-wheel"), 0x6b61696e74, QInputDevice::DeviceType::TouchPad, QPointingDevice::PointerType::Generic,
            QInputDevice::Capability::Scroll | QInputDevice::Capability::PixelScroll, 1, 0);
        const QPointF pos = rect().center();
        QWheelEvent pixelWheel(pos, mapToGlobal(pos), QPoint(delta, 0), QPoint(delta, 0), Qt::NoButton, Qt::NoModifier,
                               Qt::NoScrollPhase, false, Qt::MouseEventSynthesizedByApplication, kPixelWheel);
        QTabBar::wheelEvent(&pixelWheel);
    }
    void stepScroll()
    {
        // Cada paso, una parte de lo que falta (empieza rápido y frena al final); en un borde, para.
        int step = m_scrollPending / 4;
        if (step == 0)
            step = m_scrollPending;
        m_scrollPending -= step;
        scrollPixels(step);
        if (m_scrollPending == 0 || !arrowShown(step < 0)) {
            m_scrollPending = 0;
            m_scrollTimer.stop();
        }
    }

    void resizeEvent(QResizeEvent* e) override
    {
        QTabBar::resizeEvent(e);
        // Las pestañas se recalculan con la altura nueva.
        if (m_fullHeight && e->oldSize().height() != e->size().height()) {
            setIconSize(iconSize());
            thumbBoxChanged();
        }
    }
    void thumbBoxChanged()
    {
        // Una vez, al terminar (al cambiar de tamaño llegan varios seguidos).
        if (onThumbBoxChanged && !m_thumbPending) {
            m_thumbPending = true;
            QTimer::singleShot(0, this, [this] {
                m_thumbPending = false;
                if (onThumbBoxChanged)
                    onThumbBoxChanged();
            });
        }
    }
    QSize minimumTabSizeHint(int index) const override { return tabSizeHint(index); }

    void initStyleOption(QStyleOptionTab* option, int index) const override
    {
        QTabBar::initStyleOption(option, index);
        option->icon = QIcon();
        option->text.clear();
    }

    void paintEvent(QPaintEvent* e) override
    {
        Q_UNUSED(e);
        // Todo en una capa: las pestañas (en su sitio de pantalla mientras se arrastra una), sus
        // miniaturas y marcas; hacia donde quedan imágenes, la capa se desvanece en el borde (como en
        // Paint.NET) y encima van las flechas.
        const qreal dpr = devicePixelRatioF();
        QImage layer((QSizeF(size()) * dpr).toSize(), QImage::Format_ARGB32_Premultiplied);
        layer.setDevicePixelRatio(dpr);
        layer.fill(Qt::transparent);
        {
            QPainter lp(&layer);
            QList<int> order;
            for (int i = 0; i < count(); ++i)
                if (i != currentIndex() && i != m_dragTab)
                    order << i;
            if (currentIndex() >= 0 && currentIndex() != m_dragTab)
                order << currentIndex();
            if (m_dragTab >= 0 && m_dragTab < count())
                order << m_dragTab;
            // Los marcos de los grupos, debajo de la que se arrastra.
            paintGroups(lp);
            for (int i : std::as_const(order)) {
                if (!isTabVisible(i))
                    continue;
                const int dx = shiftOf(i);
                const QRect tab = tabRect(i).translated(dx, 0);
                if (tab.right() < 0 || tab.left() > width())
                    continue;
                paintTab(lp, i, dx);
            }
            paintGroupLabels(lp);
        }
        for (bool right : {false, true}) {
            if (!arrowShown(right))
                continue;
            QPainter fp(&layer);
            fp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            const QRect r = arrowRect(right);
            const int fade = kArrow + 22;
            QLinearGradient g(right ? QPointF(width(), 0) : QPointF(0, 0), right ? QPointF(width() - fade, 0) : QPointF(fade, 0));
            g.setColorAt(0, QColor(0, 0, 0, 40));
            g.setColorAt(qreal(r.width()) / fade, QColor(0, 0, 0, 120));
            g.setColorAt(1, QColor(0, 0, 0, 255));
            fp.fillRect(right ? QRect(width() - fade, 0, fade, height()) : QRect(0, 0, fade, height()), g);
        }
        QPainter p(this);
        p.drawImage(QPoint(0, 0), layer);
        p.setRenderHint(QPainter::Antialiasing);
        paintScrollArrows(p);
        paintPlus(p);
    }

    // El marco de cada grupo alrededor de sus imágenes (las que se ven), de su color.
    QRect groupFrame(int id) const
    {
        QRect frame;
        for (int i : membersOf(id))
            if (isTabVisible(i) && i != m_dragTab)
                frame = frame.united(tabRect(i).translated(shiftOf(i), 0));
        return frame;
    }
    void paintGroups(QPainter& p)
    {
        p.save();
        p.setRenderHint(QPainter::Antialiasing);
        for (auto it = m_groups.cbegin(); it != m_groups.cend(); ++it) {
            const QRect frame = groupFrame(it.key());
            if (frame.isEmpty())
                continue;
            QColor fill = it->color;
            fill.setAlpha(38);
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
            p.drawRoundedRect(QRectF(frame).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
            // Contraído: otra hoja asomando detrás (hay más imágenes dentro).
            if (it->collapsed) {
                p.setPen(QPen(it->color, 1.5));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(QRectF(frame).adjusted(3.5, 1.5, -0.5, -3.5), 4, 4);
            }
        }
        p.restore();
    }
    // Encima de las miniaturas: el borde y el nombre del grupo (un clic contrae o expande).
    void paintGroupLabels(QPainter& p)
    {
        m_groupChips.clear();
        p.save();
        p.setRenderHint(QPainter::Antialiasing);
        QFont f = font();
        f.setBold(true);
        f.setPointSizeF(std::max(6.5, f.pointSizeF() * 0.85));
        p.setFont(f);
        const QFontMetrics fm(f);
        for (auto it = m_groups.cbegin(); it != m_groups.cend(); ++it) {
            const QRect frame = groupFrame(it.key());
            if (frame.isEmpty())
                continue;
            p.setPen(QPen(it->color, 2));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(frame).adjusted(1, 1, -1, -1), 4, 4);
            QString text = it->name;
            if (it->collapsed)
                text += QStringLiteral(" (%1)").arg(membersOf(it.key()).size());
            const int h = fm.height() + 2;
            const int left = frame.left() + 3 + kDot + 3;
            // A lo ancho del grupo (sin tapar la X de la última), como mucho 150 px.
            const int maxW = std::max(24, std::min(150, frame.right() - left - kClose - 6));
            const int w = std::min(maxW, fm.horizontalAdvance(text) + 18);
            const QRect chip(left, frame.top() + 3, w, h);
            p.setPen(Qt::NoPen);
            p.setBrush(it->color);
            p.drawRoundedRect(QRectF(chip), h / 2.0, h / 2.0);
            const QColor ink = qGray(it->color.rgb()) > 160 ? Qt::black : Qt::white;
            p.setPen(ink);
            p.drawText(chip.adjusted(6, 0, -12, 0), Qt::AlignVCenter | Qt::AlignLeft, fm.elidedText(text, Qt::ElideRight, chip.width() - 18));
            // La flecha: hacia la derecha contraído, hacia abajo expandido.
            const QPointF c(chip.right() - 7, chip.center().y() + 0.5);
            QPolygonF arrow = it->collapsed ? QPolygonF({c + QPointF(-2, -3.5), c + QPointF(2, 0), c + QPointF(-2, 3.5)})
                                            : QPolygonF({c + QPointF(-3.5, -2), c + QPointF(3.5, -2), c + QPointF(0, 2.5)});
            p.setBrush(ink);
            p.setPen(Qt::NoPen);
            p.drawPolygon(arrow);
            m_groupChips.append({chip, it.key()});
        }
        // Las elegidas con Ctrl+clic, con el azul de la selección.
        if (hasRange()) {
            QRect frame;
            for (int i = std::min(m_rangeA, m_rangeB); i <= std::max(m_rangeA, m_rangeB); ++i)
                if (isTabVisible(i))
                    frame = frame.united(tabRect(i).translated(shiftOf(i), 0));
            QColor fill = palette().color(QPalette::Highlight);
            fill.setAlpha(45);
            p.setBrush(fill);
            p.setPen(QPen(palette().color(QPalette::Highlight), 2, Qt::DashLine));
            p.drawRoundedRect(QRectF(frame).adjusted(1, 1, -1, -1), 4, 4);
        }
        p.restore();
    }
    // El «+» que agrupa las elegidas: arriba, en el centro de la del medio.
    QRect plusRect() const
    {
        if (!hasRange() || m_dragging)
            return {};
        QList<int> shown;
        for (int i = std::min(m_rangeA, m_rangeB); i <= std::max(m_rangeA, m_rangeB); ++i)
            if (isTabVisible(i))
                shown << i;
        if (shown.isEmpty())
            return {};
        const QRect mid = tabRect(shown[shown.size() / 2]);
        return QRect(mid.center().x() - 11, mid.top() + 3, 22, 22);
    }
    void paintPlus(QPainter& p)
    {
        const QRect r = plusRect();
        if (r.isEmpty())
            return;
        p.save();
        QColor fill = palette().color(QPalette::Highlight);
        if (m_hoverPlus)
            fill = fill.lighter(125);
        p.setPen(QPen(QColor(255, 255, 255, 230), 1.5));
        p.setBrush(fill);
        p.drawEllipse(QRectF(r).adjusted(0.75, 0.75, -0.75, -0.75));
        p.setPen(QPen(Qt::white, 2, Qt::SolidLine, Qt::RoundCap));
        const QPointF c = QRectF(r).center();
        p.drawLine(c + QPointF(-5, 0), c + QPointF(5, 0));
        p.drawLine(c + QPointF(0, -5), c + QPointF(0, 5));
        p.restore();
    }
    void setGroupOf(int index, int id)
    {
        setMarks(index, (marks(index) & ~kGroupMask) | (id << kGroupShift));
    }
    // Sin grupos de una sola imagen; las ocultas, solo las que no son la primera de uno contraído.
    void cleanupGroups()
    {
        for (auto it = m_groups.begin(); it != m_groups.end();) {
            const QList<int> members = membersOf(it.key());
            if (members.size() < 2) {
                for (int i : members)
                    setGroupOf(i, 0);
                it = m_groups.erase(it);
            } else {
                ++it;
            }
        }
        applyGroupVisibility();
    }
    void applyGroupVisibility()
    {
        QHash<int, bool> seen;
        for (int i = 0; i < count(); ++i) {
            const int id = groupOf(i);
            bool visible = true;
            if (id > 0 && m_groups.contains(id)) {
                visible = !m_groups[id].collapsed || !seen.value(id);
                seen.insert(id, true);
            } else if (id > 0) {
                setGroupOf(i, 0);
            }
            if (isTabVisible(i) != visible)
                setTabVisible(i, visible);
        }
        update();
    }
    // Una imagen que se lleva entre dos del mismo grupo entra en él; una que sale de su grupo
    // (sin ninguna de él al lado) lo deja.
    void fixGroupAfterMove(int to)
    {
        if (to < 0 || to >= count())
            return;
        const int own = groupOf(to);
        const int left = to > 0 ? groupOf(to - 1) : 0;
        const int right = to + 1 < count() ? groupOf(to + 1) : 0;
        if (left > 0 && left == right)
            setGroupOf(to, left);
        else if (own > 0 && own != left && own != right)
            setGroupOf(to, 0);
        cleanupGroups();
    }

    // Una pestaña con su recuadro (el del estilo), la miniatura, las marcas y la X, 'dx' píxeles a
    // un lado de su sitio.
    void paintTab(QPainter& p, int i, int dx)
    {
        QStyleOptionTab opt;
        initStyleOption(&opt, i);
        opt.rect = tabRect(i).translated(dx, 0);
        if (i == m_dragTab)
            opt.state |= QStyle::State_Selected;
        p.save();
        style()->drawControl(QStyle::CE_TabBarTab, &opt, &p, this);
        p.restore();
        const int m = marks(i);
        // Las imágenes de la sesión colaborativa, en amarillo.
        if (m & kCollab) {
            const QRectF r = QRectF(opt.rect).adjusted(1.5, 1.5, -1.5, 0);
            p.fillRect(r, QColor(255, 206, 40, i == currentIndex() ? 150 : 105));
            p.setPen(QPen(QColor(226, 168, 0), 2));
            p.setBrush(Qt::NoBrush);
            p.drawRect(r.adjusted(0.5, 0.5, -0.5, -1));
        }
        // Paint.NET: dentro del borde azul de la imagen actual (y de la que tiene el puntero), una
        // línea oscura muy tenue que lo separa del relleno.
        if (Theme::paintNetLook() && !Theme::active().classic && !Theme::active().xp
            && (i == currentIndex() || i == m_hoverTab || i == m_dragTab) && !(m & kCollab)) {
            p.setPen(QPen(QColor(0, 0, 0, 48), 1));
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRectF(opt.rect).adjusted(2.5, 1.5, -2.5, -1.5));
        }
        tabIcon(i).paint(&p, thumbRect(i).translated(dx, 0));
        p.save();
        p.setRenderHint(QPainter::Antialiasing);
        if (m & kCollab) {
            Icons::paintCollabMark(p, QRectF(unsavedRect(i).translated(dx, 0)));
            int row = 1;
            if (const int crown = (m >> 2) & 3)
                Icons::paintCrownMark(p, QRectF(markRect(i, row++).translated(dx, 0)), Icons::collabMarkColor(crown));
            if (const int computer = (m >> 4) & 3)
                Icons::paintComputerMark(p, QRectF(markRect(i, row++).translated(dx, 0)), Icons::collabMarkColor(computer - 1));
        } else if (m & kUnsaved) {
            Icons::paintUnsavedStar(p, QRectF(unsavedRect(i).translated(dx, 0)));
        }
        // La X (no mientras se arrastra).
        const qreal level = m_dragging ? 0.0 : closeLevel(i);
        if (level > 0) {
            p.setOpacity(level);
            const QRectF r = QRectF(closeRect(i).translated(dx, 0)).adjusted(0.5, 0.5, -0.5, -0.5);
            const bool hot = i == m_hoverTab && m_hoverClose;
            p.setPen(QPen(QColor(150, 30, 30), 1));
            p.setBrush(hot ? QColor(240, 80, 70) : QColor(215, 60, 55));
            p.drawRoundedRect(r, 2, 2);
            p.setPen(QPen(Qt::white, 1.6, Qt::SolidLine, Qt::RoundCap));
            const QRectF x = r.adjusted(4, 4, -4, -4);
            p.drawLine(x.topLeft(), x.bottomRight());
            p.drawLine(x.topRight(), x.bottomLeft());
        }
        p.restore();
    }

    void mousePressEvent(QMouseEvent* e) override
    {
        const QPoint pos = e->position().toPoint();
        // Un arrastre que no terminó (se perdió la suelta del botón, p. ej. al cambiar de ventana):
        // la miniatura se quedaba corrida encima de otra.
        if (m_dragging)
            finishDrag();
        if (e->button() == Qt::LeftButton && plusRect().contains(pos)) {
            if (onGroupRequested)
                onGroupRequested();
            e->accept();
            return;
        }
        if (e->button() == Qt::LeftButton)
            for (const auto& [chip, id] : std::as_const(m_groupChips))
                if (chip.contains(pos)) {
                    setCollapsed(id, !m_groups.value(id).collapsed);
                    e->accept();
                    return;
                }
        // Ctrl+clic (o Mayús+clic): desde la imagen actual hasta esta, para agruparlas.
        if (e->button() == Qt::LeftButton && (e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) {
            const int i = tabAt(pos);
            if (i >= 0 && currentIndex() >= 0 && i != currentIndex()) {
                m_rangeA = currentIndex();
                m_rangeB = i;
                update();
                e->accept();
                return;
            }
        }
        if (e->button() == Qt::LeftButton)
            clearRange();
        // Las flechas de los bordes: una imagen hacia ese lado.
        for (bool right : {false, true})
            if (e->button() == Qt::LeftButton && arrowShown(right) && arrowRect(right).contains(pos)) {
                m_arrowDown = int(right);
                scrollOneImage(right);
                update();
                e->accept();
                return;
            }
        const int i = tabAt(pos);
        if (e->button() == Qt::LeftButton && i >= 0 && closeTarget(i) > 0 && closeRect(i).contains(pos)) {
            m_closeTab = i;
            e->accept();
            return;
        }
        m_pressedTab = e->button() == Qt::LeftButton ? i : -1;
        m_pressPos = pos;
        QTabBar::mousePressEvent(e);
    }

    void mouseMoveEvent(QMouseEvent* e) override
    {
        const QPoint pos = e->position().toPoint();
        if (m_dragging && !(e->buttons() & Qt::LeftButton)) {
            m_pressedTab = -1;
            finishDrag();
        }
        const bool overPlus = plusRect().contains(pos);
        if (overPlus != m_hoverPlus) {
            m_hoverPlus = overPlus;
            setToolTip(overPlus ? QCoreApplication::translate("MainWindow", "Agrupar las imágenes elegidas") : QString());
            update();
        }
        // Las de un grupo contraído no se arrastran (se separarían de las suyas, que están ocultas).
        const int pressedGroup = groupOf(m_pressedTab);
        if (pressedGroup > 0 && m_groups.value(pressedGroup).collapsed)
            m_pressedTab = -1;
        // Arrastrar una miniatura: va con el puntero y las demás se apartan para dejarle sitio.
        if (m_pressedTab >= 0 && m_pressedTab < count() && (e->buttons() & Qt::LeftButton)) {
            if (!m_dragging && (pos - m_pressPos).manhattanLength() >= QApplication::startDragDistance()) {
                m_dragging = true;
                m_dragTab = m_pressedTab;
                m_grab = m_pressPos.x() - tabRect(m_dragTab).left();
                m_hoverClose = false;
                setToolTip(QString());
            }
            if (m_dragging) {
                m_dragX = pos.x();
                startShiftAnimation();
                e->accept();
                return;
            }
        }
        int arrow = -1;
        for (bool right : {false, true})
            if (arrowShown(right) && arrowRect(right).contains(pos))
                arrow = int(right);
        if (arrow != m_hoverArrow) {
            m_hoverArrow = arrow;
            update();
        }
        const int hover = arrow >= 0 ? -1 : tabAt(pos);
        const bool onClose = hover >= 0 && closeRect(hover).contains(pos);
        if (hover != m_hoverTab || onClose != m_hoverClose) {
            m_hoverTab = hover;
            m_hoverClose = onClose;
            setToolTip(onClose ? QCoreApplication::translate("MainWindow", "Cerrar") : QString());
            fadeCloseButtons();
        }
        QTabBar::mouseMoveEvent(e);
    }

    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (m_arrowDown >= 0) {
            m_arrowDown = -1;
            update();
            e->accept();
            return;
        }
        if (m_closeTab >= 0) {
            const int i = m_closeTab;
            m_closeTab = -1;
            if (closeRect(i).contains(e->position().toPoint()))
                emit tabCloseRequested(i);
            e->accept();
            return;
        }
        m_pressedTab = -1;
        if (m_dragging) {
            m_dragX = int(e->position().x());
            finishDrag();
        }
        QTabBar::mouseReleaseEvent(e);
        update();
    }

    void leaveEvent(QEvent* e) override
    {
        m_hoverTab = -1;
        m_hoverClose = false;
        m_hoverArrow = -1;
        m_hoverPlus = false;
        fadeCloseButtons();
        QTabBar::leaveEvent(e);
    }
    // Si la ventana deja de estar activa a mitad de un arrastre (Alt+Tab, otra ventana encima), la
    // suelta no llega: la miniatura se deja en su sitio nuevo.
    void changeEvent(QEvent* e) override
    {
        if (e->type() == QEvent::ActivationChange && m_dragging && !(QGuiApplication::mouseButtons() & Qt::LeftButton)) {
            m_pressedTab = -1;
            finishDrag();
        }
        QTabBar::changeEvent(e);
    }
    void hideEvent(QHideEvent* e) override
    {
        if (m_dragging) {
            m_pressedTab = -1;
            finishDrag();
        }
        QTabBar::hideEvent(e);
    }

private:
    // La X se ve en la imagen actual y en la que tiene el puntero (con el aspecto de Paint.NET,
    // solo en la que tiene el puntero).
    qreal closeTarget(int index) const
    {
        return (index == currentIndex() && !Theme::paintNetLook()) || index == m_hoverTab ? 1.0 : 0.0;
    }
    qreal closeLevel(int index) const { return m_closeLevel.value(index, closeTarget(index)); }

    void resetCloseButtons()
    {
        m_closeLevel.clear();
        m_closeTimer.stop();
        m_closeShown.clear();
        for (int i = 0; i < count(); ++i)
            if (closeTarget(i) > 0)
                m_closeShown.insert(i);
        update();
    }

    void fadeCloseButtons()
    {
        if (!Animations::enabled()) {
            resetCloseButtons();
            return;
        }
        for (int i = 0; i < count(); ++i)
            if (!m_closeLevel.contains(i))
                m_closeLevel.insert(i, m_closeShown.contains(i) ? 1.0 : 0.0);
        m_closeClock.start();
        m_closeTimer.start();
        stepCloseFade();
    }

    void stepCloseFade()
    {
        const qreal step = std::max<qreal>(m_closeClock.restart(), 1) / Animations::kCloseFade;
        bool moving = false;
        for (int i = 0; i < count(); ++i) {
            const qreal target = closeTarget(i);
            qreal level = m_closeLevel.value(i, target);
            level = level < target ? std::min(target, level + step) : std::max(target, level - step);
            m_closeLevel.insert(i, level);
            moving = moving || level != target;
        }
        if (!moving)
            resetCloseButtons();
        update();
    }

    int marks(int index) const { return tabData(index).toInt(); }
    void setMarks(int index, int m)
    {
        if (marks(index) == m)
            return;
        setTabData(index, m);
        update(tabRect(index));
    }

    static constexpr int kUnsaved = 1;
    static constexpr int kCollab = 2;
    // El grupo de la imagen va en las marcas, de este bit en adelante (0: ninguno).
    static constexpr int kGroupShift = 8;
    static constexpr int kGroupMask = ~0xff;
    QHash<int, Group> m_groups;
    int m_nextGroup = 1;
    int m_rangeA = -1;
    int m_rangeB = -1;
    bool m_hoverPlus = false;
    QList<std::pair<QRect, int>> m_groupChips;
    static constexpr int kClose = 14;
    static constexpr int kDot = 12;
    // Ancho de las flechas de los bordes (el mismo hueco que reserva el estilo).
    static constexpr int kArrow = 16;
    // Margen, borde y relleno de la pestaña (los de la hoja de estilo) alrededor de la miniatura.
    static constexpr int kPadX = 6;
    static constexpr int kPadY = 5;
    // A lo alto de toda la fila: algo de recuadro libre arriba (la miniatura, algo más pequeña).
    static constexpr int kTopGap = 0;
    int m_hoverTab = -1;
    bool m_hoverClose = false;
    bool m_fullHeight = false;
    bool m_thumbPending = false;
    int m_scrollPending = 0;
    QTimer m_scrollTimer;
    int m_closeTab = -1;
    int m_pressedTab = -1;
    QPoint m_pressPos;
    // Flecha con el puntero encima y pulsada (-1 ninguna, 0 izquierda, 1 derecha).
    int m_hoverArrow = -1;
    int m_arrowDown = -1;
    // La miniatura que se arrastra, dónde está el puntero y por dónde se agarró.
    bool m_dragging = false;
    int m_dragTab = -1;
    int m_dragX = 0;
    int m_grab = 0;
    QList<qreal> m_shift;
    QTimer m_shiftTimer;
    QElapsedTimer m_shiftClock;
    // Opacidad de la X de cada pestaña mientras cambia y las que se veían antes del cambio.
    QHash<int, qreal> m_closeLevel;
    QSet<int> m_closeShown;
    QTimer m_closeTimer;
    QElapsedTimer m_closeClock;
};

// Entre los botones de las ventanas y Configuración: un hueco y, con el aspecto de Paint.NET, la
// línea vertical que los separa en el suyo.
class CornerSeparator : public QWidget {
public:
    explicit CornerSeparator(QWidget* parent)
        : QWidget(parent)
    {
        setFixedWidth(9);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        if (!Theme::paintNetLook())
            return;
        QPainter p(this);
        QColor c = palette().color(QPalette::WindowText);
        c.setAlpha(90);
        p.setPen(c);
        p.drawLine(width() / 2, 4, width() / 2, height() - 5);
    }
};

// Fila superior al estilo de Paint.NET 5: menús con la barra principal debajo, la tira de
// imágenes a lo alto de las dos filas y los botones de las ventanas a la derecha. La franja de
// los menús lleva el color de la barra de título y la de abajo el de las barras.
class TopRow : public QWidget {
public:
    using QWidget::QWidget;

    void setMenuBar(QWidget* menu) { m_menu = menu; }

    static QColor bandColor()
    {
        const QColor title = Theme::titleBarColor();
        if (!title.isValid())
            return Theme::active().menuBar;
        // Con el color de énfasis, Windows pinta la barra de título inactiva sin él al pasar a otro
        // programa: la franja se apaga con ella (con una ventana de Kaint delante sigue activa).
        return qApp->applicationState() == Qt::ApplicationActive ? title : Theme::inactiveTitleBarColor();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        const int split = m_menu ? m_menu->geometry().bottom() + 1 : height() / 2;
        // Windows 7 con la barra de título simulada: su fila se deja sin pintar y se ve el cristal de
        // Aero del marco de la ventana, seguido como en Windows 7.
        const int top = m_caption && WindowFrame::aeroTitle() ? m_caption->geometry().bottom() + 1 : 0;
        // Con el cristal (sin color propio de la barra), la fila del título y la franja de los menús
        // son del cristal del marco, con sus mismos reflejos: antes la franja era de un color liso y,
        // con un color de ventana oscuro, se veía un bloque plano entre los lados claros del marco.
        const bool glass = top > 0 && !Theme::titleBarColor().isValid();
        // Extendido (Configuración): el cristal sigue por la franja de los menús; si no, como el borde
        // de una ventana de Windows 7, solo en la barra de título.
        const bool extended = glass && WindowFrame::aeroExtended();
        const QRect below = rect().adjusted(0, extended ? split : top, 0, 0);
        // Azul: bajo los menús empieza el degradado de casi blanco a azul claro que sigue en la
        // barra de opciones (la barra principal es transparente encima de él).
        if (Theme::active().blue) {
            QLinearGradient g(QPointF(0, split), QPointF(0, height()));
            g.setColorAt(0, QColor(0xfb, 0xfd, 0xff));
            g.setColorAt(1, QColor(0xeb, 0xf1, 0xf9));
            p.fillRect(below, g);
        } else {
            p.fillRect(below, Theme::active().toolBar);
        }
        if (glass)
            WindowFrame::paintMainGlass(p, QRect(0, 0, width(), extended ? split : top), this);
        if (!extended)
            p.fillRect(QRect(0, top, width(), split - top), bandColor());
        // Las líneas del marco de Windows 7 que este cristal tapaba (como en Kaint 1.1.8): la blanca
        // de dentro del contorno, arriba, y bajo la barra de título el borde del contenido, blanco y
        // por dentro oscuro.
        if (glass) {
            p.save();
            if (!WindowFrame::isMaximized(window())) {
                p.setPen(QColor(255, 255, 255, 170));
                p.drawLine(0, 0, width(), 0);
            }
            if (!extended) {
                p.setPen(QColor(255, 255, 255, 153));
                p.drawLine(0, top - 2, width(), top - 2);
                p.setPen(QColor(0, 0, 0, window()->isActiveWindow() ? 179 : 110));
                p.drawLine(0, top - 1, width(), top - 1);
            }
            p.restore();
        }
        // Con el tema Windows 98 y la barra de título simulada, su fila es la de Windows 98
        // (degradado azul) y debajo sigue la franja gris de los menús.
        if (m_caption && WindowFrame::classicTitle())
            WindowFrame::paintClassicTitle(p, QRect(0, 0, width(), m_caption->geometry().bottom() + 1), window()->isActiveWindow());
    }

    // Los botones de la ventana a la derecha van pegados a la esquina de arriba a la derecha (no en
    // el diseño, que solo les guarda el sitio): como en Windows, llevar el puntero a la esquina de la
    // pantalla con la ventana maximizada lo deja sobre el de cerrar.
    void resizeEvent(QResizeEvent* e) override
    {
        QWidget::resizeEvent(e);
        if (m_onResize)
            m_onResize();
    }

public:
    std::function<void()> m_onResize;

    void setCaption(QWidget* caption)
    {
        m_caption = caption;
        // Los huecos de la franja mueven la ventana (en Windows, como su barra de título).
        setProperty("kaintCaption", caption != nullptr);
        if (caption)
            WindowFrame::watchCaption(this);
    }

protected:
    // Como en Windows, la ventana se mueve arrastrando cualquier hueco de la barra de título
    // simulada y de la franja de los menús, y un doble clic la maximiza o la restaura.
    void mousePressEvent(QMouseEvent* e) override
    {
        if (m_caption && e->button() == Qt::LeftButton && window()->windowHandle()) {
            window()->windowHandle()->startSystemMove();
            return;
        }
        QWidget::mousePressEvent(e);
    }

    void mouseDoubleClickEvent(QMouseEvent* e) override
    {
        if (m_caption && e->button() == Qt::LeftButton) {
            WindowFrame::toggleMaximized(window());
            return;
        }
        QWidget::mouseDoubleClickEvent(e);
    }

    bool event(QEvent* e) override
    {
        // La fila del título se apaga con la ventana inactiva.
        if (e->type() == QEvent::Polish && m_caption && !m_watching) {
            window()->installEventFilter(this);
            m_watching = true;
        }
        return QWidget::event(e);
    }

    bool eventFilter(QObject* watched, QEvent* e) override
    {
        if (watched == window() && e->type() == QEvent::ActivationChange)
            update();
        return false;
    }

private:
    QWidget* m_menu = nullptr;
    QWidget* m_caption = nullptr;
    bool m_watching = false;
};

// Menús de efectos (Ajustes, Efectos y sus submenús), como los de Paint.NET: estrechos (tan anchos
// como pide lo que tienen), con filas de 24 px y una columna para los iconos separada del texto
// por una línea. Los efectos de los complementos llevan a la derecha el icono de los complementos,
// la pieza de puzle verde (se quita en Configuración > Complementos), en el margen derecho.
class EffectMenu : public QMenu {
public:
    explicit EffectMenu(const QString& title, QWidget* parent = nullptr)
        : QMenu(title, parent)
    {
        // Sin ningún icono, Qt no deja la columna: el texto empieza después de la línea igualmente.
        connect(this, &QMenu::aboutToShow, this, [this] {
            const QList<QAction*> list = actions();
            const bool icons = std::any_of(list.cbegin(), list.cend(),
                                           [](const QAction* a) { return a->isVisible() && !a->icon().isNull(); });
            const QString style = QStringLiteral("QMenu::item { padding: 3px %1px 3px %2px; } QMenu::separator { margin-left: %3px; }")
                                      .arg(kRightPad)
                                      .arg(icons ? kTextPad : kGutter + 8)
                                      .arg(kGutter + 2);
            if (styleSheet() != style)
                setStyleSheet(style);
        });
    }

    static bool& showPluginIcons()
    {
        static bool show = true;
        return show;
    }

    static void markPlugin(QAction* action) { action->setProperty("kaintPlugin", true); }

protected:
    void paintEvent(QPaintEvent* e) override
    {
        QMenu::paintEvent(e);
        QPainter p(this);
        // La línea entre la columna de los iconos y el texto (menos en la fila resaltada).
        QColor line = palette().color(QPalette::WindowText);
        line.setAlpha(48);
        for (QAction* a : actions()) {
            if (!a->isVisible() || (a == activeAction() && !a->isSeparator() && a->isEnabled()))
                continue;
            const QRect r = actionGeometry(a);
            if (r.intersects(e->rect()))
                p.fillRect(QRect(r.left() + kGutter, r.top(), 1, r.height()), line);
        }
        if (!showPluginIcons())
            return;
        const QIcon icon = Icons::theme({"settings-plugins", "preferences-plugin", "application-x-addon"});
        if (icon.isNull())
            return;
        for (QAction* a : actions()) {
            if (!a->isVisible() || !a->property("kaintPlugin").toBool())
                continue;
            const QRect r = actionGeometry(a);
            if (!r.intersects(e->rect()))
                continue;
            const QRect box(r.right() - kIcon - 5, r.top() + (r.height() - kIcon) / 2, kIcon, kIcon);
            icon.paint(&p, box, Qt::AlignCenter, a->isEnabled() ? QIcon::Normal : QIcon::Disabled);
        }
    }

private:
    static constexpr int kIcon = 16;
    // La columna de los iconos (hasta la línea), lo que deja Qt delante del icono y el margen
    // derecho (la flecha de los submenús o la pieza de los complementos).
    static constexpr int kGutter = 23;
    static constexpr int kTextPad = 6;
    static constexpr int kRightPad = 30;
};

// Flecha de la lista de imágenes: una franja de la altura de la tira con el chevrón arriba,
// como en Paint.NET.
class ImageListButton : public QToolButton {
public:
    using QToolButton::QToolButton;

    QSize sizeHint() const override { return QSize(16, 32); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QStylePainter p(this);
        QStyleOptionToolButton opt;
        initStyleOption(&opt);
        opt.icon = QIcon();
        opt.text.clear();
        opt.arrowType = Qt::NoArrow;
        opt.features &= ~QStyleOptionToolButton::HasMenu;
        p.drawComplexControl(QStyle::CC_ToolButton, opt);
        // Tres puntos y, debajo, una flecha hacia abajo, centrados en el botón.
        p.setRenderHint(QPainter::Antialiasing);
        const QColor band = TopRow::bandColor();
        const QColor color = isDown() || underMouse() ? palette().color(QPalette::ButtonText)
                                                      : (band.lightness() < 128 ? QColor(Qt::white) : QColor(Qt::black));
        p.setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const qreal cx = width() / 2.0;
        const qreal cy = height() / 2.0;
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        for (int i = -1; i <= 1; ++i)
            p.drawEllipse(QPointF(cx + i * 3.5, cy - 4), 1.1, 1.1);
        // La flecha, celeste.
        p.setPen(QPen(QColor(0x5c, 0xc8, 0xff), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(QPolygonF{QPointF(cx - 4, cy), QPointF(cx, cy + 4), QPointF(cx + 4, cy)});
    }
};

QString zoomText(qreal z)
{
    // Con hasta dos decimales cuando no es un número entero (66,67 %), como Paint.NET 5.2.
    const qreal pct = z * 100;
    if (std::abs(pct - std::round(pct)) < 0.005)
        return QString::number(std::round(pct)) + QStringLiteral(" %");
    QString text = QString::number(pct, 'f', 2);
    while (text.endsWith(QLatin1Char('0')))
        text.chop(1);
    if (text.endsWith(QLatin1Char('.')))
        text.chop(1);
    return text + QStringLiteral(" %");
}

// La imagen de un archivo para agregarla como capa: también de los formatos con capas (.pdn, .ora,
// los de los complementos), acoplada.
QImage readLayerImage(const QString& path, QString* error)
{
    QImage image = ImageIO::readImage(path, error);
    if (!image.isNull())
        return image;
    QString loadError;
    if (const std::unique_ptr<Document> doc(ImageIO::load(path, &loadError)); doc)
        return doc->flattened();
    return {};
}

} // namespace

MainWindow::MainWindow()
    : m_canvas(new Canvas)
    , m_undoGroup(new QUndoGroup(this))
{
    setWindowIcon(Icons::app());
    setAcceptDrops(true);
    SliderBehavior::install();
    UserFonts::loadAll();
    m_lastDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);

    m_tools = new ToolManager(m_canvas, this);
    m_workspace = new Workspace(m_canvas, this);
    // Pincel avanzado: los deslizadores de tamaño y opacidad sobre el borde izquierdo del lienzo.
    {
        auto* sliders = new BrushSideSliders(m_canvas);
        struct Follow : QObject {
            BrushSideSliders* sliders;
            Follow(QObject* parent, BrushSideSliders* s) : QObject(parent), sliders(s) {}
            bool eventFilter(QObject* watched, QEvent* e) override
            {
                if (e->type() == QEvent::Resize || e->type() == QEvent::Show)
                    sliders->place(static_cast<QWidget*>(watched)->rect());
                return false;
            }
        };
        m_canvas->installEventFilter(new Follow(m_canvas, sliders));
    }
    // Modo colaborativo: la imagen que llega al unirse se abre en una pestaña.
    m_collab = new CollabController(this, m_canvas, m_tools);
    m_collab->setDocumentProvider([this] { return m_doc; });
    connect(m_collab, &CollabController::documentReady, this, [this](Document* doc) { addDocument(doc); });
    connect(m_collab, &CollabController::message, this, [this](const QString& text) { statusBar()->showMessage(text, 6000); });
    connect(m_collab, &CollabController::changed, this, &MainWindow::updateCollabActions);
    connect(m_collab, &CollabController::tabStateChanged, this, [this](Document* doc) {
        if (m_docs.contains(doc))
            updateTab(doc);
    });
    // Volver a un paso del historial de la sesión (el anfitrión, o el permiso de un invitado).
    m_collab->setHistoryJumper([this](Document* doc, int index) {
        if (!m_docs.contains(doc) || index < 0 || index > doc->undoStack()->count())
            return;
        if (doc == m_doc)
            prepareHistoryJump();
        doc->undoStack()->setIndex(index);
    });
    m_central = new QStackedWidget(this);
    m_central->addWidget(m_workspace);
    m_gifEditor = new GifEditor(m_central);
    m_central->addWidget(m_gifEditor);
    connect(m_gifEditor, &GifEditor::exportRequested, this, [this](const QString& format) {
        if (format == QLatin1String("gif")) {
            saveAs(m_doc, QStringLiteral("gif"));
            return;
        }
        const GifExport::Format f = format == QLatin1String("apng")   ? GifExport::Format::Apng
                                    : format == QLatin1String("webp") ? GifExport::Format::WebP
                                    : format == QLatin1String("mp4")  ? GifExport::Format::Mp4
                                                                      : GifExport::Format::WebM;
        const QString path = GifExport::exportAnimation(this, m_doc, f, m_lastDir);
        if (!path.isEmpty()) {
            m_lastDir = QFileInfo(path).absolutePath();
            statusBar()->showMessage(tr("Exportado: %1").arg(QDir::toNativeSeparators(path)), 5000);
        }
    });
    connect(m_gifEditor, &GifEditor::imageEditorRequested, this, [this] { toggleGifEditor(); });
    connect(m_gifEditor, &GifEditor::undoRequested, this, &MainWindow::undo);
    connect(m_gifEditor, &GifEditor::redoRequested, this, &MainWindow::redo);
    connect(m_gifEditor, &GifEditor::editFrameRequested, this, [this](int layer) {
        if (!m_doc)
            return;
        finishTool();
        m_doc->setActiveIndex(layer);
        m_gifDocs.remove(m_doc);
        updateCentralView();
    });
    setCentralWidget(m_central);
    // Linux con el estilo de Paint.NET: sin la barra de título del escritorio, con la de Windows 10.
    WindowFrame::decorate(this, false);

    createTopRow();
    createActions();
    createMenus();
    createToolBars();
    createPanels();
    createStatusBar();
    Shortcuts::registerActions(m_act);
    // Otro paquete de iconos: todo se vuelve a pintar con los suyos.
    connect(IconPacks::notifier(), &IconPacks::Notifier::changed, this, [this] {
        for (const auto& [label, icon] : std::as_const(m_statusIcons))
            label->setPixmap(icon.pixmap(16, 16));
        refreshHint();
        for (QWidget* w : QApplication::allWidgets())
            w->update();
        m_canvas->updateCursor();
    });

    connect(m_tools, &ToolManager::toolChanged, this, [this](Tool* t) {
        showToolHint(t->hint());
        if (m_layers)
            m_layers->setLinkingEnabled(t->id() == ToolId::MoveSelected);
    });
    connect(AppState::instance(), &AppState::colorsChanged, this, &MainWindow::updateToolIconColors);
    // La miniatura del cursor de Formas sigue a la forma elegida.
    const auto shapesCursor = [this] {
        if (m_tools->current() && m_tools->current()->id() == ToolId::Shapes)
            m_canvas->updateCursor();
    };
    connect(AppState::instance(), &AppState::toolSettingsChanged, this, shapesCursor);
    connect(AppState::instance(), &AppState::customShapesChanged, this, shapesCursor);
    for (Tool* t : m_tools->tools())
        connect(t, &Tool::hintChanged, this, [this, t](const QString& h) {
            if (m_tools->current() == t)
                showToolHint(h);
        });
    connect(m_canvas, &Canvas::deselectRequested, this, &MainWindow::deselect);
    connect(m_canvas, &Canvas::tabletEraserChanged, this, [this](bool eraser) {
        if (!m_settings.tabletEraser || !m_tools->current())
            return;
        if (eraser && m_tools->current()->id() != ToolId::Eraser) {
            m_toolBeforeEraser = int(m_tools->current()->id());
            m_tools->select(ToolId::Eraser);
        } else if (!eraser && m_toolBeforeEraser >= 0) {
            if (m_tools->current()->id() == ToolId::Eraser)
                m_tools->select(ToolId(m_toolBeforeEraser));
            m_toolBeforeEraser = -1;
        }
    });

    m_settings = AppSettings::load();
    // Las herramientas de PhotoCraft antes de elegir la del arranque (puede ser una de ellas).
    m_tools->setExtrasEnabled(m_settings.photocraftExtras);
    QSettings ts;
    const int lastTool = ts.value(QStringLiteral("tools/current"), int(ToolId::Paintbrush)).toInt();
    // Por defecto, el pincel (en Configuración > Herramientas se puede elegir otra o la última usada).
    m_tools->select(ToolId(m_settings.startTool >= 0 ? m_settings.startTool : lastTool));
    if (!m_tools->current())
        m_tools->select(ToolId::Paintbrush);

    connect(m_undoGroup, &QUndoGroup::canUndoChanged, this, &MainWindow::updateActions);
    connect(m_undoGroup, &QUndoGroup::canRedoChanged, this, &MainWindow::updateActions);
    m_historyTimer.setSingleShot(true);
    m_historyTimer.setInterval(500);
    connect(&m_historyTimer, &QTimer::timeout, this, &MainWindow::checkHistoryMemory);
    connect(m_undoGroup, &QUndoGroup::indexChanged, this, [this] { m_historyTimer.start(); });
    connect(m_undoGroup, &QUndoGroup::indexChanged, this, &MainWindow::syncLayerWithHistory);
    connect(m_undoGroup, &QUndoGroup::indexChanged, this, &MainWindow::syncToolWithHistory);
    connect(AppState::instance(), &AppState::recentFilesChanged, this, &MainWindow::rebuildRecentMenu);
    m_discord = new DiscordPresence(this);
    m_presenceTimer.setInterval(2000);
    connect(&m_presenceTimer, &QTimer::timeout, this, &MainWindow::updatePresence);
    m_presenceTimer.start();
    connect(m_collab, &CollabController::changed, this, &MainWindow::updatePresence);
    // Discord y el modo colaborativo: una invitación aceptada abre «Unirse» con el código (después
    // se pide la contraseña o se espera al anfitrión, como siempre).
    connect(m_discord, &DiscordPresence::joinInvite, this,
            [this](const QString& code) { m_collab->joinInvite(code, QStringLiteral("Discord")); }, Qt::QueuedConnection);
    connect(m_discord, &DiscordPresence::joinRequest, this, [this](const QString& userId, const QString& userName) {
        auto* box = new QMessageBox(QMessageBox::Question, tr("Discord"),
                                    tr("«%1» pide unirse a tu sesión colaborativa desde Discord.").arg(userName),
                                    QMessageBox::NoButton, this);
        box->setInformativeText(tr("Si aceptas, Discord le da el código de la sesión. Aun así pasará por el acceso de la "
                                   "sesión: la contraseña o tu aprobación."));
        box->addButton(tr("Aceptar"), QMessageBox::AcceptRole);
        box->addButton(tr("Rechazar"), QMessageBox::RejectRole);
        box->setAttribute(Qt::WA_DeleteOnClose);
        connect(box, &QMessageBox::finished, this, [this, box, userId] {
            const bool ok = box->buttonRole(box->clickedButton()) == QMessageBox::AcceptRole;
            m_discord->replyJoinRequest(userId, ok);
        });
        box->open();
    }, Qt::QueuedConnection);

    connect(&m_autosaveTimer, &QTimer::timeout, this, &MainWindow::autosave);
    m_thumbTimer.setSingleShot(true);
    m_thumbTimer.setInterval(400);
    connect(&m_thumbTimer, &QTimer::timeout, this, [this] {
        for (Document* d : std::as_const(m_thumbDirty))
            updateTab(d);
        m_thumbDirty.clear();
    });

    QSettings s;
    resize(1280, 820);
    restoreGeometry(s.value(QStringLiteral("window/geometry")).toByteArray());
    m_act[QStringLiteral("pixelGrid")]->setChecked(s.value(QStringLiteral("view/pixelGrid"), false).toBool());
    applySettings();
    applyThemeLayout();
    connect(Theme::notifier(), &Theme::Notifier::changed, this, &MainWindow::applyThemeLayout);
    // Al pasar a otro programa (o volver) con un diálogo de Kaint delante, la barra de título de
    // la principal cambia con él.
    connect(qApp, &QGuiApplication::applicationStateChanged, this, &MainWindow::syncTitleBarActivation);
    ensurePolished();
    keepOpaque();

    rebuildRecentMenu();
    updateActions();
    updateTitle();
}

MainWindow::~MainWindow()
{
    // Una sesión colaborativa que siga abierta (si se cierra sin pasar por closeEvent) termina
    // ahora, mientras la ventana existe: sus avisos ya no llegan a una ventana a medio destruir.
    if (m_collab) {
        m_collab->disconnect(this);
        delete m_collab;
        m_collab = nullptr;
    }
    // El lienzo deja de observar los documentos antes de destruirlos.
    m_canvas->setDocument(nullptr);
    m_canvas->setTool(nullptr);
}

// ---------------------------------------------------------------------------
// Acciones y menús
// ---------------------------------------------------------------------------

QAction* MainWindow::makeAction(const QString& text, std::initializer_list<const char*> icons, const QKeySequence& key,
                                std::function<void()> fn, QList<QAction*>* group)
{
    auto* a = new QAction(Icons::theme(icons), text, this);
    if (!key.isEmpty())
        a->setShortcut(key);
    connect(a, &QAction::triggered, this, [fn = std::move(fn)] { fn(); });
    if (group)
        group->append(a);
    return a;
}

void MainWindow::createActions()
{
    auto& A = m_act;
    QList<QAction*>* doc = &m_docActions;
    QList<QAction*>* sel = &m_selActions;

    // Archivo (textos y atajos del menú Archivo de Paint.NET 5)
    A["new"] = makeAction(tr("&Nuevo..."), {"document-new"}, QKeySequence::New, [this] { newImage(); });
    A["open"] = makeAction(tr("&Abrir..."), {"document-open"}, QKeySequence::Open, [this] { open(); });
    A["acquire"] = makeAction(tr("Desde &escáner..."), {"scanner", "document-scan"}, {}, [this] { acquireFromScanner(); });
    A["clearRecent"] = makeAction(tr("&Borrar la lista"), {"edit-clear-history", "edit-delete"}, {},
                                  [] { AppState::instance()->clearRecentFiles(); });
    A["save"] = makeAction(tr("&Guardar"), {"document-save"}, QKeySequence::Save, [this] { save(m_doc); }, doc);
    A["saveAs"] = makeAction(tr("Guardar &como..."), {"document-save-as"}, QKeySequence(QStringLiteral("Ctrl+Shift+S")),
                             [this] { saveAs(m_doc); }, doc);
    A["saveAll"] = makeAction(tr("Guardar &todo"), {"document-save-all"}, QKeySequence(QStringLiteral("Ctrl+Alt+S")),
                              [this] { saveAll(); }, doc);
    A["saveAllAs"] = makeAction(tr("Guardar to&dos..."), {"document-save-all", "document-save-as"}, {},
                                [this] { saveAllAs(); }, doc);
    A["print"] = makeAction(tr("&Imprimir..."), {"document-print"}, QKeySequence::Print, [this] { print(); }, doc);
    // Editor de GIF: la imagen actual como una animación en una línea de tiempo (cada capa, un
    // fotograma), con una interfaz de editor de vídeo; se vuelve con la misma opción.
    A["gifEditor"] = makeAction(tr("Editor de &GIF"), {"video-x-generic", "image-gif", "media-playback-start"}, {},
                                [this] { toggleGifEditor(); }, doc);
    A["gifEditor"]->setCheckable(true);
    A["gifEditor"]->setToolTip(tr("Edita esta imagen como un GIF animado, con una línea de tiempo (vuelve a elegirlo para "
                                  "volver al editor de imágenes)"));
    // Colaboración: por P2P (la predeterminada) o en la red local, cada una con su configuración.
    A["collabHost"] = makeAction(tr("&Hospedar la imagen actual"), {"collab"}, {}, [this] { m_collab->host(m_doc, Collab::Mode::P2P); });
    A["collabJoin"] = makeAction(tr("&Unirse con un código..."), {"network-connect", "collab"}, {},
                                 [this] { m_collab->showJoinDialog(Collab::Mode::P2P); });
    A["collabSettingsP2P"] = makeAction(tr("&Configuración P2P..."), {"configure", "settings-configure"}, {},
                                        [this] { showSettings(SettingsDialog::Page::Collaboration, QStringLiteral("collabP2PSection")); });
    A["collabHostLan"] = makeAction(tr("&Hospedar la imagen actual"), {"collab"}, {}, [this] { m_collab->host(m_doc, Collab::Mode::Lan); });
    A["collabJoinLan"] = makeAction(tr("&Unirse a una sesión..."), {"network-connect", "collab"}, {},
                                    [this] { m_collab->showJoinDialog(Collab::Mode::Lan); });
    A["collabSettingsLan"] = makeAction(tr("C&onfiguración LAN..."), {"configure", "settings-configure"}, {},
                                        [this] { showSettings(SettingsDialog::Page::Collaboration, QStringLiteral("collabLanSection")); });
    A["collabWindow"] = makeAction(tr("&Ventana de la sesión..."), {"collab"}, {}, [this] { m_collab->showSessionWindow(); });
    A["collabChat"] = makeAction(tr("C&hat de la sesión..."), {"dialog-messages", "im-message-new", "mail-message"}, {},
                                 [this] { m_collab->showChat(); });
    A["collabHistory"] = makeAction(tr("&Historial de la sesión..."), {"collab"}, {}, [this] { m_collab->showSessionHistory(); });
    A["collabHistory"]->setToolTip(tr("El historial del anfitrión: volver a una versión anterior para todos"));
    A["collabHistory"]->setVisible(false);
    A["collabLeave"] = makeAction(tr("&Salir de la sesión"), {"network-disconnect", "application-exit"}, {},
                                  [this] { m_collab->leave(true); });
    A["collabSettings"] = makeAction(tr("&Configuración de colaboración..."), {"configure", "settings-configure"}, {},
                                     [this] { showSettings(SettingsDialog::Page::Collaboration); });
    A["close"] = makeAction(tr("&Cerrar"), {"document-close"}, QKeySequence(QStringLiteral("Ctrl+W")),
                            [this] { closeDocument(m_doc); }, doc);
    A["close"]->setShortcuts({QKeySequence(QStringLiteral("Ctrl+W")), QKeySequence(QStringLiteral("Ctrl+F4"))});
    A["closeAll"] = makeAction(tr("Cerrar t&odas"), {"document-close"}, QKeySequence(QStringLiteral("Ctrl+Shift+W")),
                               [this] { closeAll(); }, doc);
    A["closeOthers"] = makeAction(tr("Cerrar todas &menos esta"), {"document-close"},
                                  QKeySequence(QStringLiteral("Ctrl+Alt+W")), [this] { closeAll(m_doc); }, doc);
    A["quit"] = makeAction(tr("&Salir"), {"application-exit"}, QKeySequence(QStringLiteral("Ctrl+Q")), [this] { close(); });

    // Edición
    A["undo"] = makeAction(tr("&Deshacer"), {"edit-undo"}, QKeySequence::Undo, [this] { undo(); });
    A["redo"] = makeAction(tr("&Rehacer"), {"edit-redo"}, QKeySequence(), [this] { redo(); });
    A["redo"]->setShortcut(QKeySequence(QStringLiteral("Ctrl+Y")));
    // Como en Paint.NET, Cortar solo se activa con algo seleccionado.
    A["cut"] = makeAction(tr("Cor&tar"), {"edit-cut"}, QKeySequence::Cut, [this] { cut(); }, sel);
    A["cut"]->setShortcuts({QKeySequence(QStringLiteral("Ctrl+X")), QKeySequence(QStringLiteral("Shift+Del"))});
    A["copy"] = makeAction(tr("&Copiar"), {"edit-copy"}, QKeySequence::Copy, [this] { copy(false); }, doc);
    A["copy"]->setShortcuts({QKeySequence(QStringLiteral("Ctrl+C")), QKeySequence(QStringLiteral("Ctrl+Ins"))});
    A["copyMerged"] = makeAction(tr("Copiar ac&oplado"), {"edit-copy-merged", "edit-copy"}, QKeySequence(QStringLiteral("Ctrl+Shift+C")),
                                 [this] { copy(true); }, doc);
    A["paste"] = makeAction(tr("&Pegar"), {"edit-paste"}, QKeySequence::Paste, [this] { paste(); });
    A["paste"]->setShortcuts({QKeySequence(QStringLiteral("Ctrl+V")), QKeySequence(QStringLiteral("Shift+Ins"))});
    A["pasteLayer"] = makeAction(tr("Pegar en capa &nueva"), {"edit-paste-new-layer", "edit-paste-in-place", "edit-paste"},
                                 QKeySequence(QStringLiteral("Ctrl+Shift+V")), [this] { pasteIntoNewLayer(); }, doc);
    A["pasteImage"] = makeAction(tr("Pegar en una &imagen nueva"), {"edit-paste-new-image", "window-new", "edit-paste"},
                                 QKeySequence(QStringLiteral("Ctrl+Alt+V")), [this] { pasteIntoNewImage(); });
    A["copySelection"] = makeAction(tr("Copiar &selección"), {"edit-copy-selection", "edit-copy"},
                                    QKeySequence(QStringLiteral("Ctrl+Alt+Shift+C")), [this] { copySelection(); }, sel);
    const QList<std::tuple<SelectionMode, QString, const char*>> pasteModes{
        {SelectionMode::Replace, tr("&Reemplazar"), "Ctrl+Alt+Shift+V"},
        {SelectionMode::Union, tr("&Sumar (unión)"), ""},
        {SelectionMode::Exclude, tr("&Excluir"), ""},
        {SelectionMode::Intersect, tr("&Intersecar"), ""},
        {SelectionMode::Xor, tr("In&vertir («xor»)"), ""}};
    for (const auto& [mode, name, key] : pasteModes) {
        const QString id = QStringLiteral("pasteSelection%1").arg(int(mode));
        A[id] = makeAction(name, {}, QKeySequence(QString::fromLatin1(key)), [this, m = mode] { pasteSelection(m); }, doc);
        A[id]->setIcon(Icons::selectionMode(mode));
    }
    A["eraseSel"] = makeAction(tr("&Borrar la selección"), {"edit-erase-selection", "edit-delete"}, QKeySequence(Qt::Key_Delete),
                               [this] { eraseSelection(tr("Borrar la selección")); }, sel);
    A["fillSel"] = makeAction(tr("&Rellenar la selección"), {"edit-fill-selection", "fill-color"}, QKeySequence(Qt::Key_Backspace),
                              [this] { fillSelection(); }, doc);
    A["invertSel"] = makeAction(tr("&Invertir la selección"), {"edit-select-invert"}, QKeySequence(QStringLiteral("Ctrl+I")),
                                [this] { invertSelection(); }, sel);
    A["selectAll"] = makeAction(tr("Seleccionar &todo"), {"edit-select-all"}, QKeySequence::SelectAll,
                                [this] { selectAll(); }, doc);
    A["growSel"] = makeAction(tr("A&mpliar la selección..."), {"zoom-in", "list-add"}, {},
                              [this] { reshapeSelection(SelectionReshape::Grow); }, sel);
    A["shrinkSel"] = makeAction(tr("&Reducir la selección..."), {"zoom-out", "list-remove"}, {},
                                [this] { reshapeSelection(SelectionReshape::Shrink); }, sel);
    A["featherSel"] = makeAction(tr("&Difuminar la selección..."), {"blurfx", "draw-brush"}, {},
                                 [this] { reshapeSelection(SelectionReshape::Feather); }, sel);
    A["deselect"] = makeAction(tr("&Anular la selección"), {"edit-select-none"}, QKeySequence(QStringLiteral("Ctrl+D")),
                               [this] { deselect(); }, sel);
    // Crear formas propias: se dibuja la figura (con el lazo, o con el pincel y la varita mágica) y su
    // contorno queda en Formas personalizadas.
    A["selectionShape"] = makeAction(tr("Guardar la selección como &forma..."), {"draw-geometry", "shapes", "draw-polygon"}, {},
                                     [this] { saveSelectionAsShape(); }, sel);

    // Ver
    A["zoomIn"] = makeAction(tr("&Acercar"), {"zoom-in"}, QKeySequence::ZoomIn, [this] { m_canvas->zoomIn(); }, doc);
    A["zoomIn"]->setShortcuts({QKeySequence::ZoomIn, QKeySequence(QStringLiteral("Ctrl+="))});
    A["zoomOut"] = makeAction(tr("A&lejar"), {"zoom-out"}, QKeySequence::ZoomOut, [this] { m_canvas->zoomOut(); }, doc);
    A["zoomFit"] = makeAction(tr("&Zum a ventana"), {"zoom-fit-best"}, QKeySequence(QStringLiteral("Ctrl+B")),
                              [this] { m_canvas->toggleZoomToWindow(); }, doc);
    A["zoomSel"] = makeAction(tr("Zum a &selección"), {"zoom-select", "zoom-fit-selection"},
                              QKeySequence(QStringLiteral("Ctrl+Shift+B")),
                              [this] { m_canvas->zoomToRect(QRectF(m_doc->selection().bounds())); }, sel);
    // Girar la vista (solo se ve girada, como en Kaint.APK); también con Alt + rueda.
    A["rotateViewLeft"] = makeAction(tr("Girar la vista a la iz&quierda"), {"object-rotate-left", "rotate-left"},
                                     QKeySequence(QStringLiteral("Ctrl+Alt+Left")),
                                     [this] { m_canvas->setViewRotation(m_canvas->viewRotation() - 15); }, doc);
    A["rotateViewRight"] = makeAction(tr("Girar la vista a la de&recha"), {"object-rotate-right", "rotate-right"},
                                      QKeySequence(QStringLiteral("Ctrl+Alt+Right")),
                                      [this] { m_canvas->setViewRotation(m_canvas->viewRotation() + 15); }, doc);
    A["rotateViewReset"] = makeAction(tr("Vista sin &girar"), {"transform-rotate", "view-refresh"},
                                      QKeySequence(QStringLiteral("Ctrl+Alt+Up")), [this] { m_canvas->setViewRotation(0); }, doc);
    A["actualSize"] = makeAction(tr("&Tamaño real"), {"zoom-original"}, QKeySequence(QStringLiteral("Ctrl+0")),
                                 [this] { m_canvas->actualSize(); }, doc);
    A["actualSize"]->setShortcuts({QKeySequence(QStringLiteral("Ctrl+0")), QKeySequence(QStringLiteral("Ctrl+Shift+A"))});
    A["pixelGrid"] = makeAction(tr("Cuadrícula de &píxeles"), {"view-grid", "grid-rectangular"},
                                QKeySequence(Qt::CTRL | Qt::Key_Apostrophe), [] {});
    A["pixelGrid"]->setCheckable(true);
    connect(A["pixelGrid"], &QAction::toggled, m_canvas, &Canvas::setPixelGrid);
    A["rulers"] = makeAction(tr("&Reglas"), {"measure", "kruler"}, QKeySequence(QStringLiteral("Ctrl+Alt+R")), [] {});
    A["rulers"]->setCheckable(true);
    connect(A["rulers"], &QAction::toggled, this, [this](bool on) {
        m_canvas->setRulersVisible(on);
        m_settings.rulers = on;
        m_settings.save();
    });
    auto* unitGroup = new QActionGroup(this);
    const QStringList unitNames{tr("&Píxeles"), tr("P&ulgadas"), tr("&Centímetros")};
    for (int u = 0; u < 3; ++u) {
        const QString id = QStringLiteral("unit%1").arg(u);
        A[id] = makeAction(unitNames[u], {}, {}, [this, u] { setUnit(u); });
        A[id]->setCheckable(true);
        unitGroup->addAction(A[id]);
    }
    A["fullscreen"] = makeAction(tr("&Pantalla completa"), {"view-fullscreen"}, QKeySequence::FullScreen, [this] {
        setWindowState(windowState() ^ Qt::WindowFullScreen);
    });
    addAction(A["fullscreen"]);

    // Imagen
    A["crop"] = makeAction(tr("&Recortar según la selección"), {"transform-crop"}, QKeySequence(QStringLiteral("Ctrl+Shift+X")),
                           [this] { cropToSelection(); }, sel);
    A["resize"] = makeAction(tr("Cambiar el &tamaño..."), {"transform-scale", "image-resize-symbolic"},
                             QKeySequence(QStringLiteral("Ctrl+R")), [this] { resizeImage(); }, doc);
    A["canvasSize"] = makeAction(tr("Tamaño del &lienzo..."), {"transform-crop-and-resize"},
                                 QKeySequence(QStringLiteral("Ctrl+Shift+R")), [this] { resizeCanvas(); }, doc);
    A["flipH"] = makeAction(tr("Voltear &horizontalmente"), {"object-flip-horizontal"}, {}, [this] { flipImage(true); }, doc);
    A["flipV"] = makeAction(tr("Voltear &verticalmente"), {"object-flip-vertical"}, {}, [this] { flipImage(false); }, doc);
    A["rotCW"] = makeAction(tr("Girar 90° en sentido &horario"), {"object-rotate-right"}, QKeySequence(QStringLiteral("Ctrl+H")),
                            [this] { rotateImage(90); }, doc);
    A["rotCCW"] = makeAction(tr("Girar 90° en sentido &antihorario"), {"object-rotate-left"},
                             QKeySequence(QStringLiteral("Ctrl+G")), [this] { rotateImage(270); }, doc);
    A["rot180"] = makeAction(tr("Girar &180°"), {"image-rotate-180", "object-rotate-right"}, {}, [this] { rotateImage(180); }, doc);
    A["colorProfile"] = makeAction(tr("&Perfil de colores..."), {"color-profile", "color-management"}, {},
                                   [this] { colorProfile(); }, doc);
    A["animation"] = makeAction(tr("A&nimación..."), {"media-playback-start", "video-x-generic"}, {},
                                [this] { editAnimation(); }, doc);
    A["flatten"] = makeAction(tr("&Acoplar"), {"image-flatten", "merge"}, QKeySequence(QStringLiteral("Ctrl+Shift+F")),
                              [this] { flatten(); }, doc);

    // Capas
    A["addLayer"] = makeAction(tr("Agregar &nueva capa"), {"layer-new"}, QKeySequence(QStringLiteral("Ctrl+Shift+N")),
                               [this] { addLayer(); }, doc);
    A["deleteLayer"] = makeAction(tr("&Eliminar la capa"), {"layer-delete"}, QKeySequence(QStringLiteral("Ctrl+Shift+Del")),
                                  [this] { deleteLayer(); });
    A["duplicateLayer"] = makeAction(tr("&Duplicar la capa"), {"layer-duplicate"}, QKeySequence(QStringLiteral("Ctrl+Shift+D")),
                                     [this] { duplicateLayer(); }, doc);
    A["mergeDown"] = makeAction(tr("&Combinar capa con la inferior"), {"layer-merge-down", "merge", "layer-bottom"},
                                QKeySequence(QStringLiteral("Ctrl+M")), [this] { mergeLayerDown(); });
    A["toggleLayer"] = makeAction(tr("Alternar &visibilidad de capa"), {"layer-visible-on"}, QKeySequence(QStringLiteral("Ctrl+,")),
                                  [this] { if (m_doc) toggleLayerVisibility(m_doc->activeIndex()); }, doc);
    // Alpha-Lock (como en Ibis Paint, FireAlpaca o Krita): pintar solo cambia el color de lo que ya
    // hay en la capa, no su transparencia.
    A["alphaLock"] = makeAction(tr("&Bloquear la transparencia"), {"object-locked", "lock", "transparency"}, {},
                                [this] { toggleAlphaLock(); }, doc);
    A["alphaLock"]->setCheckable(true);
    A["alphaLock"]->setToolTip(tr("Bloquear la transparencia de la capa (Alpha-Lock): al pintar, rellenar o aplicar efectos "
                                  "solo cambia el color de lo que ya hay; lo transparente sigue transparente."));
    A["importLayer"] = makeAction(tr("&Importar desde archivo..."), {"layer-import", "document-import", "insert-image"}, {},
                                  [this] { importLayer(); }, doc);
    A["flipLayerH"] = makeAction(tr("Voltear &horizontalmente"), {"layer-flip-horizontal", "object-flip-horizontal"}, {},
                                 [this] { flipLayer(true); }, doc);
    A["flipLayerV"] = makeAction(tr("Voltear &verticalmente"), {"layer-flip-vertical", "object-flip-vertical"}, {},
                                 [this] { flipLayer(false); }, doc);
    A["rotateLayer180"] = makeAction(tr("Girar &180°"), {"layer-rotate-180", "object-rotate-right"}, {}, [this] { rotateLayer180(); }, doc);
    A["rotateZoom"] = makeAction(tr("&Giro y zum..."), {"layer-rotate-zoom", "transform-rotate", "object-rotate-right"},
                                 QKeySequence(QStringLiteral("Ctrl+Shift+Z")), [this] { rotateZoomLayer(); }, doc);
    A["layerTop"] = makeAction(tr("Ir a capa &superior"), {"layer-go-top", "go-top"}, QKeySequence(QStringLiteral("Ctrl+Alt+PgUp")),
                               [this] { if (m_doc) selectLayer(m_doc->layerCount() - 1); }, doc);
    A["layerAbove"] = makeAction(tr("Ir a capa de &encima"), {"layer-go-up", "go-up"}, QKeySequence(QStringLiteral("Alt+PgUp")),
                                 [this] { if (m_doc) selectLayer(m_doc->activeIndex() + 1); }, doc);
    A["layerBelow"] = makeAction(tr("Ir a capa de de&bajo"), {"layer-go-down", "go-down"}, QKeySequence(QStringLiteral("Alt+PgDown")),
                                 [this] { if (m_doc) selectLayer(m_doc->activeIndex() - 1); }, doc);
    A["layerBottom"] = makeAction(tr("Ir a capa in&ferior"), {"layer-go-bottom", "go-bottom"}, QKeySequence(QStringLiteral("Ctrl+Alt+PgDown")),
                                  [this] { selectLayer(0); }, doc);
    A["layerToTop"] = makeAction(tr("Enviar capa al &principio"), {"layer-top", "go-top"}, {},
                                 [this] { if (m_doc) moveLayerTo(m_doc->activeIndex(), m_doc->layerCount() - 1); });
    A["layerUp"] = makeAction(tr("&Subir capa"), {"layer-raise"}, {}, [this] { moveLayer(+1); });
    A["layerDown"] = makeAction(tr("&Bajar capa"), {"layer-lower"}, {}, [this] { moveLayer(-1); });
    A["layerToBottom"] = makeAction(tr("Enviar capa al &final"), {"layer-bottom", "go-bottom"}, {},
                                    [this] { if (m_doc) moveLayerTo(m_doc->activeIndex(), 0); });
    A["layerProps"] = makeAction(tr("&Propiedades de la capa..."), {"layer-properties", "document-properties", "configure"},
                                 QKeySequence(Qt::Key_F4), [this] { layerProperties(); }, doc);

    // Imágenes abiertas y paneles: sin menú, como en Paint.NET (atajos, la tira de imágenes y
    // los botones de la esquina superior derecha).
    A["nextImage"] = makeAction(tr("Imagen &siguiente"), {"go-next"}, QKeySequence(QStringLiteral("Ctrl+Tab")),
                                [this] { switchImage(+1); }, doc);
    A["nextImage"]->setShortcuts({QKeySequence(QStringLiteral("Ctrl+Tab")), QKeySequence(QStringLiteral("Ctrl+PgDown"))});
    A["prevImage"] = makeAction(tr("Imagen &anterior"), {"go-previous"}, QKeySequence(QStringLiteral("Ctrl+Shift+Tab")),
                                [this] { switchImage(-1); }, doc);
    A["prevImage"]->setShortcuts({QKeySequence(QStringLiteral("Ctrl+Shift+Tab")), QKeySequence(QStringLiteral("Ctrl+PgUp"))});
    A["tabLeft"] = makeAction(tr("Mover la imagen a la izquierda"), {}, QKeySequence(QStringLiteral("Ctrl+Shift+PgUp")),
                              [this] { moveImageTab(-1); }, doc);
    A["tabRight"] = makeAction(tr("Mover la imagen a la derecha"), {}, QKeySequence(QStringLiteral("Ctrl+Shift+PgDown")),
                               [this] { moveImageTab(+1); }, doc);
    addActions({A["nextImage"], A["prevImage"], A["tabLeft"], A["tabRight"]});
    A["resetPanels"] = makeAction(tr("&Restablecer la posición de las ventanas"), {"view-restore"}, {},
                                  [this] { m_workspace->resetPanels(); });
    A["translucent"] = makeAction(tr("Ventanas &translúcidas"), {}, {}, [] {});
    A["translucent"]->setCheckable(true);
    connect(A["translucent"], &QAction::toggled, this, [this](bool on) {
        m_workspace->setTranslucentPanels(on);
        m_settings.translucentPanels = on;
        m_settings.save();
    });
    for (int n = 1; n <= 9; ++n) {
        auto* a = new QAction(this);
        a->setShortcuts({QKeySequence(QStringLiteral("Alt+%1").arg(n)), QKeySequence(QStringLiteral("Ctrl+%1").arg(n))});
        connect(a, &QAction::triggered, this, [this, n] {
            if (Document* d = m_docs.value(n == 9 ? int(m_docs.size()) - 1 : n - 1))
                setCurrentDocument(d);
        });
        addAction(a);
    }
    A["settings"] = makeAction(tr("&Configuración..."), {"configure", "settings-configure"}, QKeySequence(QStringLiteral("Alt+X")),
                               [this] { showSettings(); });
    addAction(A["settings"]);
    A["plugins"] = makeAction(tr("&Complementos..."), {"plugins", "preferences-plugin", "application-x-addon"}, {},
                              [this] { showSettings(SettingsDialog::Page::Plugins); });

    // Ayuda (botón "?" de la esquina), como la de Paint.NET con los enlaces de Kaint.
    auto openUrl = [](const QString& url) { return [url] { QDesktopServices::openUrl(QUrl(url)); }; };
    A["website"] = makeAction(tr("Sitio web de &Kaint"), {"help-website", "internet-services", "applications-internet", "globe"}, {},
                              openUrl(QStringLiteral(KAINT_REPOSITORY)));
    A["docs"] = makeAction(tr("&Documentación"), {"help-docs", "help-contents"}, QKeySequence(Qt::Key_F1),
                           openUrl(QStringLiteral("https://www.getpaint.net/doc/latest/index.html")));
    A["donate"] = makeAction(tr("&Donar"), {"face-smile", "emblem-favorite"}, {},
                             openUrl(QStringLiteral("https://ko-fi.com/opposition")));
    A["forum"] = makeAction(tr("&Foro"), {"help-forum", "im-user", "system-users"}, {},
                            openUrl(QStringLiteral("https://forums.getpaint.net/")));
    A["tutorials"] = makeAction(tr("&Tutoriales"), {"help-tutorials", "help-contents"}, {},
                                openUrl(QStringLiteral("https://forums.getpaint.net/forum/20-publishing-only/")));
    {
        QFont italic = A["donate"]->font();
        italic.setItalic(true);
        A["donate"]->setFont(italic);
    }
    A["pluginsSite"] = makeAction(tr("&Complementos"), {"plugins", "preferences-plugin", "application-x-addon"}, {},
                                  openUrl(QStringLiteral("https://www.getpaint.net/redirect/plugins_hm.html")));
    A["feedback"] = makeAction(tr("&Enviar comentarios o informe de errores..."), {"help-feedback", "mail-message-new", "mail-send"}, {},
                               openUrl(QStringLiteral(KAINT_REPOSITORY "/issues")));
    A["about"] = makeAction(tr("&Acerca de"), {"help-about"}, {}, [this] {
        AboutDialog dlg(this);
        dlg.exec();
    });

    // Atajos de herramientas (una letra) y otros atajos sin menú.
    for (const QKeySequence& key : m_tools->shortcuts()) {
        auto* a = new QAction(this);
        a->setShortcut(key);
        connect(a, &QAction::triggered, this, [this, key] { m_tools->activateShortcut(key); });
        auto* back = new QAction(this);
        back->setShortcut(QKeySequence(Qt::SHIFT | key[0].key()));
        connect(back, &QAction::triggered, this, [this, key] { m_tools->activateShortcut(key, true); });
        addActions({a, back});
        // En el editor de GIF no hay herramientas: sus letras no cambian nada a escondidas.
        m_imageOnlyActions << a << back;
    }
    auto* swap = new QAction(tr("Intercambiar colores"), this);
    swap->setShortcut(QKeySequence(Qt::Key_X));
    connect(swap, &QAction::triggered, AppState::instance(), &AppState::swapColors);
    addAction(swap);
    auto* activeSlot = new QAction(tr("Cambiar la ranura de color activa"), this);
    activeSlot->setShortcut(QKeySequence(Qt::Key_C));
    connect(activeSlot, &QAction::triggered, this,
            [] { AppState::instance()->setSecondaryActive(!AppState::instance()->secondaryActive()); });
    addAction(activeSlot);
    auto brushStep = [this](int delta) {
        AppState::instance()->updateTools([delta](ToolSettings& s) { s.brushSize = std::clamp(s.brushSize + delta, 1.0, 2000.0); });
        m_canvas->updateOverlay();
    };
    const QList<std::pair<QKeySequence, int>> steps{{QKeySequence(Qt::Key_BracketLeft), -1},
                                                    {QKeySequence(Qt::Key_BracketRight), +1},
                                                    {QKeySequence(Qt::CTRL | Qt::Key_BracketLeft), -5},
                                                    {QKeySequence(Qt::CTRL | Qt::Key_BracketRight), +5}};
    for (const auto& [key, delta] : steps) {
        auto* a = new QAction(this);
        a->setShortcut(key);
        connect(a, &QAction::triggered, this, [brushStep, d = delta] { brushStep(d); });
        addAction(a);
    }
}

void MainWindow::createMenus()
{
    auto& A = m_act;
    QMenuBar* bar = m_menuBar;
    QMenu* file = bar->addMenu(tr("&Archivo"));
    file->addActions({A["new"], A["open"]});
    m_recentMenu = file->addMenu(Icons::theme({"document-open-recent"}), tr("Abrir &recientes"));
    QMenu* acquire = file->addMenu(Icons::theme({"scanner", "document-import"}), tr("A&dquirir"));
    acquire->addAction(A["acquire"]);
    file->addSeparator();
    file->addActions({A["save"], A["saveAs"], A["saveAll"], A["saveAllAs"]});
    file->addSeparator();
    file->addAction(A["gifEditor"]);
    file->addSeparator();
    file->addAction(A["print"]);
    file->addSeparator();
    // Modo colaborativo: editar la imagen con otras personas de la red local.
    QMenu* collab = file->addMenu(Icons::theme(QStringLiteral("collab")), tr("C&olaboración"));
    QMenu* p2p = collab->addMenu(Icons::theme({"network-wireless", "internet-services", "collab"}), tr("&P2P (por Internet)"));
    p2p->addActions({A["collabHost"], A["collabJoin"]});
    p2p->addSeparator();
    p2p->addAction(A["collabSettingsP2P"]);
    QMenu* lan = collab->addMenu(Icons::theme({"network-wired", "network-workgroup", "collab"}), tr("&Red local (LAN)"));
    lan->addActions({A["collabHostLan"], A["collabJoinLan"]});
    lan->addSeparator();
    lan->addAction(A["collabSettingsLan"]);
    collab->addSeparator();
    collab->addActions({A["collabWindow"], A["collabChat"], A["collabHistory"], A["collabLeave"]});
    collab->addSeparator();
    collab->addAction(A["collabSettings"]);
    file->addSeparator();
    file->addAction(A["close"]);
    file->addSeparator();
    file->addAction(A["quit"]);
    addActions({A["closeAll"], A["closeOthers"]});

    QMenu* edit = bar->addMenu(tr("&Edición"));
    edit->addActions({A["undo"], A["redo"]});
    edit->addSeparator();
    edit->addActions({A["cut"], A["copy"], A["copyMerged"], A["paste"], A["pasteLayer"], A["pasteImage"]});
    edit->addSeparator();
    edit->addAction(A["copySelection"]);
    QMenu* pasteSel = edit->addMenu(Icons::theme({"edit-paste-selection", "edit-paste"}), tr("Pegar s&elección"));
    for (int m : {int(SelectionMode::Replace), int(SelectionMode::Union), int(SelectionMode::Exclude),
                  int(SelectionMode::Intersect), int(SelectionMode::Xor)})
        pasteSel->addAction(A[QStringLiteral("pasteSelection%1").arg(m)]);
    m_docActions.append(pasteSel->menuAction());
    edit->addSeparator();
    edit->addActions({A["eraseSel"], A["fillSel"], A["invertSel"], A["selectAll"], A["deselect"]});
    edit->addSeparator();
    edit->addActions({A["growSel"], A["shrinkSel"], A["featherSel"], A["selectionShape"]});

    QMenu* view = bar->addMenu(tr("&Ver"));
    view->addActions({A["zoomIn"], A["zoomOut"], A["zoomFit"], A["zoomSel"], A["actualSize"]});
    view->addSeparator();
    view->addActions({A["rotateViewLeft"], A["rotateViewRight"], A["rotateViewReset"]});
    view->addSeparator();
    view->addActions({A["pixelGrid"], A["rulers"]});
    view->addSeparator();
    view->addActions({A["unit0"], A["unit1"], A["unit2"]});

    QMenu* image = bar->addMenu(tr("&Imagen"));
    image->addActions({A["crop"], A["resize"], A["canvasSize"]});
    image->addSeparator();
    image->addActions({A["flipH"], A["flipV"]});
    image->addSeparator();
    image->addActions({A["rotCW"], A["rotCCW"], A["rot180"]});
    image->addSeparator();
    image->addAction(A["colorProfile"]);
    image->addAction(A["animation"]);
    image->addSeparator();
    image->addAction(A["flatten"]);

    QMenu* layers = bar->addMenu(tr("&Capas"));
    layers->addActions({A["addLayer"], A["deleteLayer"], A["duplicateLayer"], A["mergeDown"], A["toggleLayer"], A["alphaLock"]});
    layers->addSeparator();
    layers->addAction(A["importLayer"]);
    layers->addSeparator();
    layers->addActions({A["flipLayerH"], A["flipLayerV"], A["rotateLayer180"], A["rotateZoom"]});
    layers->addSeparator();
    layers->addActions({A["layerTop"], A["layerAbove"], A["layerBelow"], A["layerBottom"]});
    layers->addSeparator();
    layers->addActions({A["layerToTop"], A["layerUp"], A["layerDown"], A["layerToBottom"]});
    layers->addSeparator();
    layers->addAction(A["layerProps"]);

    m_adjustmentsMenu = new EffectMenu(tr("A&justes"), bar);
    bar->addMenu(m_adjustmentsMenu);
    m_effectsMenu = new EffectMenu(tr("Efe&ctos"), bar);
    bar->addMenu(m_effectsMenu);
    A["repeatEffect"] = new QAction(tr("Repetir el último efecto"), this);
    A["repeatEffect"]->setShortcut(QKeySequence(QStringLiteral("Ctrl+F")));
    connect(A["repeatEffect"], &QAction::triggered, this, [this] { runEffect(m_lastEffect, true); });
    addAction(A["repeatEffect"]);
    rebuildEffectMenus();
}

void MainWindow::rebuildEffectMenus()
{
    // Ajustes y Efectos con los efectos integrados y los de los complementos activados. Como
    // en Paint.NET, cada submenú está en orden alfabético.
    for (QAction* a : std::as_const(m_effectActions)) {
        m_docActions.removeAll(a);
        a->deleteLater();
    }
    m_effectActions.clear();
    for (QMenu* sub : std::as_const(m_effectSubmenus))
        sub->deleteLater();
    m_effectSubmenus.clear();
    if (m_lastEffect && !Effects::all().contains(m_lastEffect)) {
        m_lastEffect = nullptr;
        m_act["repeatEffect"]->setText(tr("Repetir el último efecto"));
        m_act["repeatEffect"]->setIcon(QIcon());
    }
    m_adjustmentsMenu->clear();
    m_effectsMenu->clear();
    // Arriba, como en Paint.NET: repetir el último efecto (Ctrl+F, solo después de usar uno) y
    // los complementos (solo si hay alguno activado). Sin ninguno de los dos, el separador no se
    // ve (QMenu oculta los separadores del principio).
    m_effectsMenu->addAction(m_act["repeatEffect"]);
    m_effectsMenu->addAction(m_act["plugins"]);
    m_effectsMenu->addSeparator();
    bool anyPlugin = false;
    for (const Effect* e : Effects::all())
        anyPlugin = anyPlugin || !e->pluginName().isEmpty();
    m_act["plugins"]->setVisible(anyPlugin);

    // Configuración > Personalización: efectos ocultos y movidos a otro menú o submenú.
    const Customization* custom = Customization::instance();
    QList<const Effect*> effects;
    for (const Effect* e : Effects::all())
        if (!e->hidden() && !custom->isHidden(e->id()))
            effects.append(e);
    const QCollator collator = [] {
        QCollator c;
        c.setCaseSensitivity(Qt::CaseInsensitive);
        return c;
    }();
    std::stable_sort(effects.begin(), effects.end(),
                     [&](const Effect* a, const Effect* b) { return collator.compare(a->name(), b->name()) < 0; });
    QStringList categories;
    for (const Effect* e : std::as_const(effects)) {
        const Customization::Placement place = custom->placement(e);
        if (!place.adjustment && !place.submenu.isEmpty() && !categories.contains(place.submenu))
            categories.append(place.submenu);
    }
    std::sort(categories.begin(), categories.end(), [&](const QString& a, const QString& b) { return collator.compare(a, b) < 0; });
    QHash<QString, QMenu*> submenus;
    for (const QString& cat : std::as_const(categories)) {
        QMenu* sub = new EffectMenu(cat, m_effectsMenu);
        m_effectsMenu->addMenu(sub);
        submenus.insert(cat, sub);
        m_effectSubmenus.append(sub);
    }
    for (const Effect* e : std::as_const(effects)) {
        const Customization::Placement place = custom->placement(e);
        QMenu* menu = place.adjustment ? m_adjustmentsMenu : submenus.value(place.submenu, m_effectsMenu);
        QIcon icon = Icons::theme(Icons::effectIcon(e->id(), e->iconName()));
        if (icon.isNull() && !e->iconImage().isNull())
            icon = QIcon(QPixmap::fromImage(e->iconImage()));
        const bool hasDialog = !e->params().isEmpty() || e->hasCustomDialog();
        QAction* a = menu->addAction(icon, e->name() + (hasDialog ? QStringLiteral("...") : QString()));
        a->setShortcut(e->shortcut());
        if (!e->pluginName().isEmpty()) {
            a->setToolTip(tr("%1 (complemento %2)").arg(e->name(), e->pluginName()));
            EffectMenu::markPlugin(a);
        } else if (!e->origin().isEmpty()) {
            // Las funciones de PhotoCraft y las que siguen complementos de BoltBait llevan la misma
            // pieza de puzle.
            a->setToolTip(tr("%1 (de %2)").arg(e->name(), e->origin()));
            EffectMenu::markPlugin(a);
        }
        connect(a, &QAction::triggered, this, [this, e] { runEffect(e, false); });
        m_docActions.append(a);
        m_effectActions.append(a);
    }
    updateActions();
}

void MainWindow::createToolBars()
{
    auto& A = m_act;
    // La barra principal va en la fila superior, bajo los menús (como en Paint.NET).
    auto* main = new QToolBar(tr("Principal"), menuWidget());
    main->setObjectName(QStringLiteral("MainToolBar"));
    main->setMovable(false);
    main->setIconSize(QSize(16, 16));
    main->addActions({A["new"], A["open"], A["save"]});
    // Guardar lleva una flecha con las opciones de guardado (Guardar como, Guardar todos...).
    if (auto* saveButton = qobject_cast<QToolButton*>(main->widgetForAction(A["save"]))) {
        auto* saveMenu = new QMenu(saveButton);
        saveMenu->addActions({A["save"], A["saveAs"]});
        saveMenu->addSeparator();
        saveMenu->addActions({A["saveAll"], A["saveAllAs"]});
        saveButton->setMenu(saveMenu);
        saveButton->setPopupMode(QToolButton::MenuButtonPopup);
    }
    main->addSeparator();
    main->addAction(A["print"]);
    main->addSeparator();
    // De Cortar a Anular la selección, la barra lleva iconos como los de Paint.NET 4 (los menús
    // siguen con los PNG): botones de acciones propias que hacen lo mismo que las del menú y se
    // activan con ellas.
    auto barAction = [main](QAction* a, const char* icon) {
        auto* b = new QAction(Icons::theme({icon}), a->text(), main);
        QObject::connect(b, &QAction::triggered, a, &QAction::trigger);
        auto sync = [a, b] {
            b->setText(a->text());
            b->setToolTip(a->toolTip());
            b->setEnabled(a->isEnabled());
        };
        sync();
        QObject::connect(a, &QAction::changed, b, sync);
        return b;
    };
    main->addActions({barAction(A["cut"], "toolbar-cut"), barAction(A["copy"], "toolbar-copy"),
                      barAction(A["paste"], "toolbar-paste")});
    main->addSeparator();
    main->addActions({barAction(A["crop"], "toolbar-crop"), barAction(A["deselect"], "toolbar-deselect")});
    main->addSeparator();
    main->addActions({A["undo"], A["redo"]});
    main->addSeparator();
    main->addActions({A["pixelGrid"], A["rulers"]});
    // Con muchas imágenes, la tira se acorta (con sus flechas); los menús y la barra, no.
    main->setSizePolicy(QSizePolicy::Fixed, main->sizePolicy().verticalPolicy());
    m_topLayout->addWidget(main, m_menuRow + 1, 0, Qt::AlignLeft | Qt::AlignTop);

    m_options = new ToolOptionsBar(m_tools, this);
    addToolBar(m_options);
    for (FontChooser* font : m_options->findChildren<FontChooser*>())
        connect(font, &FontChooser::chosen, m_canvas, qOverload<>(&QWidget::setFocus));
}

void MainWindow::createTopRow()
{
    // Fila superior de Paint.NET 5: menús y, debajo, la barra principal (la pone
    // createToolBars); a la derecha, la tira de imágenes a lo alto de las dos filas y los
    // botones de las ventanas, Configuración y Ayuda.
    auto* top = new TopRow(this);
    top->setObjectName(QStringLiteral("TopRow"));
    m_topLayout = new QGridLayout(top);
    m_topLayout->setContentsMargins(0, 0, 4, 0);
    m_topLayout->setHorizontalSpacing(6);
    m_topLayout->setVerticalSpacing(0);
    m_menuBar = new QMenuBar(top);
    // Dentro de la ventana aunque Plasma tenga menú global: si no, se iba con la fila entera.
    m_menuBar->setNativeMenuBar(false);
    m_menuBar->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    top->setMenuBar(m_menuBar);
    // Linux con el estilo de Paint.NET: arriba, la barra de título simulada de Windows 10 (icono,
    // título y botones de la ventana); los menús y la barra principal bajan una fila.
    if (WindowFrame::isDecorated(this)) {
        m_menuRow = 1;
        m_captionTitle = new CaptionTitle(top);
        m_topLayout->addWidget(m_captionTitle, 0, 0);
        m_captionButtons = new CaptionButtons(top);
        top->setCaption(m_captionTitle);
        placeCaptionButtons();
    }
    m_menuBar->setSizePolicy(QSizePolicy::Fixed, m_menuBar->sizePolicy().verticalPolicy());
    m_topLayout->addWidget(m_menuBar, m_menuRow, 0, Qt::AlignLeft | Qt::AlignBottom);
    m_topLayout->setRowStretch(m_menuRow, 1);

    // Tira de imágenes abiertas con miniaturas.
    m_tabs = new ImageStrip(top);
    m_tabs->setObjectName(QStringLiteral("ImageStrip"));
    m_tabs->setDocumentMode(true);
    m_tabs->setMovable(false);
    m_tabs->setExpanding(false);
    m_tabs->setDrawBase(false);
    m_tabs->setIconSize(QSize(64, 48));
    static_cast<ImageStrip*>(m_tabs)->onThumbBoxChanged = [this] {
        for (Document* doc : std::as_const(m_docs))
            updateTab(doc);
    };
    static_cast<ImageStrip*>(m_tabs)->onGroupRequested = [this] {
        static int groups = 0;
        static_cast<ImageStrip*>(m_tabs)->groupRange(tr("Grupo %1").arg(++groups));
    };
    m_tabs->setUsesScrollButtons(true);
    m_tabs->setElideMode(Qt::ElideRight);
    // Solo lo que ocupan las miniaturas: la flecha de la lista va justo después de la última y
    // llega a la derecha cuando la tira se llena.
    m_tabs->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    m_topLayout->addWidget(m_tabs, 0, 1, m_menuRow + 2, 1, Qt::AlignBottom);
    m_tabsColumnRows = m_menuRow + 2;
    connect(m_tabs, &QTabBar::currentChanged, this, [this](int i) { setCurrentDocument(m_docs.value(i)); });
    connect(m_tabs, &QTabBar::tabCloseRequested, this, [this](int i) { closeDocument(m_docs.value(i)); });
    connect(m_tabs, &QTabBar::tabMoved, this, [this](int from, int to) { m_docs.move(from, to); });
    m_tabs->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tabs, &QTabBar::customContextMenuRequested, this, &MainWindow::showTabMenu);
    m_tabs->installEventFilter(this);

    // Lista de todas las imágenes (la flecha del final de la tira).
    m_listButton = new ImageListButton(top);
    m_listButton->setObjectName(QStringLiteral("ImageListButton"));
    m_listButton->setAutoRaise(true);
    m_listButton->setToolTip(tr("Imágenes abiertas"));
    m_listButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    connect(m_listButton, &QToolButton::clicked, this, &MainWindow::showImageList);
    m_topLayout->addWidget(m_listButton, m_menuRow, 2, 2, 1);
    m_topLayout->setColumnStretch(3, 1);

    m_cornerButtons = new QWidget(top);
    m_cornerButtons->setObjectName(QStringLiteral("CornerButtons"));
    m_cornerLayout = new QHBoxLayout(m_cornerButtons);
    m_cornerLayout->setContentsMargins(0, 0, m_captionButtons ? 4 : 0, 0);
    m_cornerLayout->setSpacing(0);
    m_topLayout->addWidget(m_cornerButtons, m_menuRow, 4, Qt::AlignRight | Qt::AlignVCenter);
    m_cornerButtons->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_cornerButtons, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(this);
        menu.addActions({m_act["resetPanels"], m_act["translucent"]});
        menu.exec(m_cornerButtons->mapToGlobal(pos));
    });
    // Breeze pide menuBar() al pulir la ventana y Qt cambiaría esta fila por una barra de menús
    // vacía (se perdían los menús y la barra principal con el tema de KDE): se pule antes.
    ensurePolished();
    setMenuWidget(top);
}

void MainWindow::placeCaptionButtons()
{
    // A la derecha de la fila de la barra de título o, a la izquierda, encima del principio del
    // título (que deja su sitio).
    if (!m_captionButtons)
        return;
    m_captionRow = CaptionButtons::rowHeight(this);
    m_topLayout->removeWidget(m_captionButtons);
    auto* top = static_cast<TopRow*>(m_topLayout->parentWidget());
    if (!m_captionSpacer) {
        m_captionSpacer = new QWidget(top);
        m_captionSpacer->setAttribute(Qt::WA_TransparentForMouseEvents);
        // Con otro estilo de botones cambian de tamaño.
        connect(Theme::notifier(), &Theme::Notifier::changed, m_captionSpacer, [top] {
            if (top->m_onResize)
                top->m_onResize();
        });
    }
    m_topLayout->removeWidget(m_captionSpacer);
    if (WindowFrame::buttonsOnLeft()) {
        m_captionSpacer->hide();
        top->m_onResize = nullptr;
        m_topLayout->addWidget(m_captionButtons, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);
    } else {
        // El diseño guarda su sitio (con el margen derecho de la fila) y los botones van encima,
        // hasta el borde: la esquina de arriba a la derecha es el botón de cerrar.
        const QSize size = m_captionButtons->sizeHint();
        m_captionSpacer->setFixedSize(size);
        m_captionSpacer->show();
        m_topLayout->addWidget(m_captionSpacer, 0, 4, Qt::AlignRight | Qt::AlignTop);
        top->m_onResize = [this, top] {
            if (!m_captionButtons || WindowFrame::buttonsOnLeft())
                return;
            const QSize size = m_captionButtons->sizeHint();
            m_captionSpacer->setFixedSize(size);
            const int extra = m_topLayout->contentsMargins().right();
            m_captionButtons->setGeometry(top->width() - size.width() - extra, 0, size.width() + extra, size.height());
            m_captionButtons->raise();
        };
        top->m_onResize();
    }
    m_captionButtons->raise();
    m_captionButtons->updateGeometry();
    if (m_captionTitle)
        m_captionTitle->update();
}

void MainWindow::updateCaptionRow()
{
    // Maximizada (o a pantalla completa), la fila de la barra de título es más baja, como las
    // ventanas de Windows y Paint.NET: el título, los botones y su hueco toman el alto nuevo.
    if (!m_captionButtons || CaptionButtons::rowHeight(this) == m_captionRow)
        return;
    if (m_captionTitle)
        m_captionTitle->updateGeometry();
    placeCaptionButtons();
}

void MainWindow::resizeEvent(QResizeEvent* e)
{
    QMainWindow::resizeEvent(e);
    // Windows maximiza y restaura (Aero Snap, arrastrar la barra de título) sin que Qt cambie el
    // estado de la ventana: se mira con su tamaño, ya colocada.
    if (m_captionButtons && CaptionButtons::rowHeight(this) != m_captionRow)
        QTimer::singleShot(0, this, &MainWindow::updateCaptionRow);
    // Al terminar de cambiar de tamaño (el gestor de ventanas maximiza o restaura en varios pasos y
    // con animación), la fila de arriba se repinta entera.
    if (!m_topRepaint) {
        m_topRepaint = new QTimer(this);
        m_topRepaint->setSingleShot(true);
        m_topRepaint->setInterval(150);
        connect(m_topRepaint, &QTimer::timeout, this, &MainWindow::repaintTopRow);
    }
    m_topRepaint->start();
}

void MainWindow::repaintTopRow()
{
    // Arrastrando la barra para maximizar o restaurar, el sistema se queda con el ratón (Qt no
    // recibe la suelta): los botones podían quedar como si el puntero siguiera encima o pulsados.
    if (m_cornerButtons) {
        const QPoint cursor = QCursor::pos();
        for (QToolButton* b : m_cornerButtons->findChildren<QToolButton*>()) {
            const bool under = b->isVisible() && b->rect().contains(b->mapFromGlobal(cursor));
            b->setAttribute(Qt::WA_UnderMouse, under);
            if (b->isDown() && !(QGuiApplication::mouseButtons() & Qt::LeftButton))
                b->setDown(false);
            b->style()->unpolish(b);
            b->style()->polish(b);
            b->update();
        }
    }
    // Al maximizar o restaurar arrastrando la ventana, el marco cambia (esquinas redondeadas,
    // transparencia) y los botones de las ventanas, con su fondo translúcido, se quedaban a medio
    // pintar (el relleno acumulado o solo los bordes): se repinta toda la parte de arriba.
    if (QWidget* top = menuWidget()) {
        top->update();
        for (QWidget* w : top->findChildren<QWidget*>())
            w->update();
    }
}

void MainWindow::createPanels()
{
    using Corner = FloatingPanel::Corner;
    auto* toolsWidget = new ToolsPanel(m_tools);
    m_toolsPanel = m_workspace->addPanel(QStringLiteral("tools"), tr("Herramientas"), toolsWidget, Corner::TopLeft,
                                         QPoint(8, 8), QSize());

    m_history = new HistoryPanel(m_undoGroup, m_act["undo"], m_act["redo"]);
    connect(m_history, &HistoryPanel::aboutToJump, this, &MainWindow::prepareHistoryJump);
    // Cada editor con su historial: el de las imágenes sin los pasos del editor de GIF, y el del
    // editor de GIF (una pestaña junto a sus medios) solo con ellos.
    m_history->setFilter(HistoryPanel::Filter::Image);
    m_gifHistory = new HistoryPanel(m_undoGroup, m_act["undo"], m_act["redo"]);
    m_gifHistory->setFilter(HistoryPanel::Filter::Gif);
    connect(m_gifHistory, &HistoryPanel::aboutToJump, this, &MainWindow::prepareHistoryJump);
    m_gifEditor->setHistoryPanel(m_gifHistory);
    // Solo en el modo colaborativo: el historial del anfitrión.
    m_history->setCollabAction(m_act["collabHistory"]);
    m_historyPanel = m_workspace->addPanel(QStringLiteral("history"), tr("Historial"), m_history, Corner::TopRight,
                                           QPoint(8, 8), QSize(210, 240));

    m_act["alphaLock"]->setObjectName(QStringLiteral("alphaLock"));
    m_act["layerProps"]->setObjectName(QStringLiteral("layerProps"));
    m_layers = new LayersPanel({m_act["addLayer"], m_act["deleteLayer"], m_act["duplicateLayer"], m_act["mergeDown"],
                                m_act["layerUp"], m_act["layerDown"], m_act["alphaLock"], m_act["layerProps"]});
    connect(m_layers, &LayersPanel::visibilityToggled, this, &MainWindow::toggleLayerVisibility);
    connect(m_layers, &LayersPanel::propertiesRequested, this, &MainWindow::layerProperties);
    m_layersPanel = m_workspace->addPanel(QStringLiteral("layers"), tr("Capas"), m_layers, Corner::BottomRight,
                                          QPoint(8, 8), QSize(230, 260));

    m_colors = new ColorsPanel;
    m_colorsPanel = m_workspace->addPanel(QStringLiteral("colors"), tr("Colores"), m_colors, Corner::BottomLeft,
                                          QPoint(8, 8), QSize(230, 330));
    connect(m_colors, &ColorsPanel::layoutChanged, this, [this](double scale) {
        m_colors->layout()->invalidate();
        m_colors->layout()->activate();
        m_colorsPanel->resize(m_colors->sizeForScale(scale) + (m_colorsPanel->size() - m_colors->size()));
        m_colorsPanel->reposition();
    });

    // Botones de las ventanas en la esquina superior derecha, como Paint.NET.
    // Con el aspecto de Paint.NET, sus iconos (applyThemeLayout): el martillo, el reloj, las hojas y la
    // rueda, con el contorno blanco grueso.
    const QList<std::tuple<FloatingPanel*, QString, QIcon, Qt::Key, const char*>> panels{
        {m_toolsPanel, tr("Herramientas"), Icons::theme({"window-tools", "tools", "draw-brush"}), Qt::Key_F5, "corner-tools"},
        {m_historyPanel, tr("Historial"), Icons::theme({"window-history", "view-history"}), Qt::Key_F6, "corner-history"},
        {m_layersPanel, tr("Capas"), Icons::theme({"window-layers", "layer-visible-on", "layer-new"}), Qt::Key_F7, "corner-layers"},
        {m_colorsPanel, tr("Colores"), Icons::theme({"window-colors", "color-management", "color-picker"}), Qt::Key_F8,
         "corner-colors"},
    };
    for (const auto& [panel, name, icon, key, pdnIcon] : panels) {
        QAction* a = makeAction(name, {}, QKeySequence(key), [] {});
        a->setIcon(icon);
        a->setProperty("kaintIcon", QVariant::fromValue(icon));
        a->setProperty("pdnIcon", QString::fromLatin1(pdnIcon));
        a->setCheckable(true);
        a->setChecked(!panel->isHidden());
        a->setToolTip(QStringLiteral("%1 (%2)").arg(name, QKeySequence(key).toString(QKeySequence::NativeText)));
        connect(a, &QAction::toggled, panel, [panel](bool on) {
            if (on)
                panel->showAnimated();
            else
                panel->hide();
        });
        connect(panel, &FloatingPanel::closed, a, [a] { a->setChecked(false); });
        addAction(a);
        auto* b = new QToolButton(m_cornerButtons);
        b->setDefaultAction(a);
        b->setAutoRaise(true);
        b->setIconSize(QSize(16, 16));
        m_cornerLayout->addWidget(b);
    }
    // El engranaje con su propio icono (no el de la acción, que también va en el menú Utilidades).
    auto* gear = new QToolButton(m_cornerButtons);
    gear->setIcon(m_act["settings"]->icon());
    gear->setToolTip(m_act["settings"]->toolTip());
    gear->setProperty("kaintIcon", QVariant::fromValue(m_act["settings"]->icon()));
    gear->setProperty("pdnIcon", QStringLiteral("corner-settings"));
    connect(gear, &QToolButton::clicked, m_act["settings"], &QAction::trigger);
    gear->setAutoRaise(true);
    gear->setIconSize(QSize(16, 16));
    // Entre las ventanas y Configuración: un hueco o, con el aspecto de Paint.NET, su línea.
    m_cornerLayout->addWidget(new CornerSeparator(m_cornerButtons));
    m_cornerLayout->addWidget(gear);
    auto* help = new QToolButton(m_cornerButtons);
    help->setIcon(Icons::theme({"help-contents", "help-about"}));
    help->setProperty("kaintIcon", QVariant::fromValue(help->icon()));
    help->setProperty("pdnIcon", QStringLiteral("corner-help"));
    help->setToolTip(tr("Ayuda"));
    help->setAutoRaise(true);
    help->setIconSize(QSize(16, 16));
    help->setPopupMode(QToolButton::InstantPopup);
    auto* helpMenu = new QMenu(help);
    // Como la de paint.js: documentación y web, y después donar, foro, tutoriales, complementos y
    // comentarios.
    helpMenu->addActions({m_act["docs"], m_act["website"]});
    helpMenu->addSeparator();
    helpMenu->addActions({m_act["donate"], m_act["forum"], m_act["tutorials"], m_act["pluginsSite"], m_act["feedback"]});
    helpMenu->addSeparator();
    helpMenu->addAction(m_act["about"]);
    addAction(m_act["docs"]);
    help->setMenu(helpMenu);
    m_cornerLayout->addWidget(help);

    connect(m_layers, &LayersPanel::soloRequested, this, &MainWindow::soloLayer);
    connect(m_layers, &LayersPanel::moveRequested, this, &MainWindow::moveLayerTo);
    connect(PluginManager::instance(), &PluginManager::changed, this, &MainWindow::rebuildEffectMenus);
    connect(Customization::instance(), &Customization::effectMenusChanged, this, &MainWindow::rebuildEffectMenus);
}

void MainWindow::createStatusBar()
{
    // Barra de estado de Paint.NET 5: icono y ayuda de la herramienta, tamaño de la selección,
    // posición del cursor, unidades, zum y su deslizador.
    QStatusBar* bar = statusBar();
    m_hintIcon = new QLabel(bar);
    m_hintIcon->setFixedSize(16, 16);
    m_hintIcon->setContentsMargins(0, 0, 0, 0);
    bar->addWidget(m_hintIcon);
    m_hint = new QLabel(bar);
    m_hint->setTextFormat(Qt::PlainText);
    m_hint->setMinimumWidth(100);
    m_hint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    bar->addWidget(m_hint, 1);

    auto item = [this, bar](std::initializer_list<const char*> icons, const QString& tip) {
        auto* w = new QWidget(bar);
        auto* l = new QHBoxLayout(w);
        l->setContentsMargins(6, 0, 6, 0);
        l->setSpacing(4);
        auto* icon = new QLabel(w);
        icon->setPixmap(Icons::theme(icons).pixmap(16, 16));
        m_statusIcons.append({icon, Icons::theme(icons)});
        auto* text = new QLabel(w);
        text->setMinimumWidth(70);
        l->addWidget(icon);
        l->addWidget(text);
        w->setToolTip(tip);
        bar->addPermanentWidget(w);
        return text;
    };
    m_progress = new QProgressBar(bar);
    m_progress->setRange(0, 1000);
    m_progress->setFixedWidth(120);
    m_progress->setTextVisible(false);
    m_progress->hide();
    bar->addPermanentWidget(m_progress);
    bar->addPermanentWidget(m_collab->statusWidget());
    m_selLabel = item({"status-selection", "select-rectangular"}, tr("Tamaño de la selección"));
    m_posLabel = item({"status-cursor", "tool-pointer", "edit-select"}, tr("Posición del cursor"));

    // Unidades: botón compacto ("px", "in", "cm") con menú.
    m_unitButton = new QToolButton(bar);
    m_unitButton->setAutoRaise(true);
    m_unitButton->setPopupMode(QToolButton::InstantPopup);
    m_unitButton->setToolTip(tr("Unidades de medida"));
    m_unitButton->setFocusPolicy(Qt::NoFocus);
    auto* unitMenu = new QMenu(m_unitButton);
    unitMenu->addActions({m_act["unit0"], m_act["unit1"], m_act["unit2"]});
    m_unitButton->setMenu(unitMenu);
    bar->addPermanentWidget(m_unitButton);

    m_zoomBox = new QComboBox(bar);
    m_zoomBox->setObjectName(QStringLiteral("ZoomBox"));
    m_zoomBox->setEditable(true);
    m_zoomBox->setInsertPolicy(QComboBox::NoInsert);
    m_zoomBox->setMinimumWidth(90);
    m_zoomBox->setToolTip(tr("Zum"));
    m_zoomBox->addItem(tr("Ventana"));
    const auto& presets = Canvas::zoomPresets();
    for (auto it = presets.crbegin(); it != presets.crend(); ++it)
        m_zoomBox->addItem(zoomText(*it), *it);
    auto* fit = new QToolButton(bar);
    fit->setDefaultAction(m_act["zoomFit"]);
    fit->setAutoRaise(true);
    auto* zoomOut = new QToolButton(bar);
    zoomOut->setDefaultAction(m_act["zoomOut"]);
    zoomOut->setAutoRaise(true);
    m_zoomSlider = new QSlider(Qt::Horizontal, bar);
    m_zoomSlider->setRange(0, 1000);
    m_zoomSlider->setFixedWidth(130);
    m_zoomSlider->setToolTip(tr("Zum"));
    auto* zoomIn = new QToolButton(bar);
    zoomIn->setDefaultAction(m_act["zoomIn"]);
    zoomIn->setAutoRaise(true);
    for (QWidget* w : {static_cast<QWidget*>(m_zoomBox), static_cast<QWidget*>(fit), static_cast<QWidget*>(zoomOut),
                       static_cast<QWidget*>(m_zoomSlider), static_cast<QWidget*>(zoomIn)})
        bar->addPermanentWidget(w);
    for (QToolButton* b : {fit, zoomOut, zoomIn})
        b->setIconSize(QSize(16, 16));

    connect(m_zoomBox, &QComboBox::activated, this, [this](int i) {
        if (i == 0)
            m_canvas->zoomToWindow();
        else
            m_canvas->setZoom(m_zoomBox->itemData(i).toDouble());
        m_canvas->setFocus();
    });
    connect(m_zoomBox->lineEdit(), &QLineEdit::editingFinished, this, [this] {
        QString t = m_zoomBox->currentText();
        t.remove(QLatin1Char('%'));
        bool ok = false;
        const double v = QLocale().toDouble(t.trimmed(), &ok);
        const double v2 = ok ? v : t.trimmed().toDouble(&ok);
        if (ok && v2 > 0)
            m_canvas->setZoom(v2 / 100.0);
        else
            m_zoomBox->setEditText(zoomText(m_canvas->zoom()));
    });
    connect(m_zoomSlider, &QSlider::valueChanged, this, [this](int v) { m_canvas->setZoom(sliderToZoom(v)); });
    connect(m_canvas, &Canvas::unitRequested, this, [this](int u) { setUnit(u); });
    connect(m_canvas, &Canvas::zoomChanged, this, [this](qreal z) {
        const QSignalBlocker b1(m_zoomSlider), b2(m_zoomBox);
        m_zoomSlider->setValue(zoomToSlider(z));
        m_zoomBox->setEditText(zoomText(z));
    });
    // Como en Paint.NET: con la imagen como la deja «Zum a ventana» (y centrada), el botón muestra la
    // imagen enmarcada en vez de las flechas.
    connect(m_canvas, &Canvas::viewChanged, this, [this] {
        const bool atWindow = m_canvas->atWindowZoom();
        QAction* fit = m_act["zoomFit"];
        if (fit->property("kaintAtWindow").isValid() && fit->property("kaintAtWindow").toBool() == atWindow)
            return;
        fit->setProperty("kaintAtWindow", atWindow);
        fit->setIcon(atWindow ? Icons::theme({"zoom-original"}) : Icons::theme({"zoom-fit-best"}));
    });
    connect(m_canvas, &Canvas::cursorMoved, this, [this](QPoint p, bool inside) {
        // Como en Paint.NET, al salir del lienzo (o de Kaint) se queda la última posición.
        if (!m_doc) {
            m_posLabel->clear();
            return;
        }
        if (!inside)
            return;
        const Unit u = m_canvas->unit();
        m_posLabel->setText(QStringLiteral("%1, %2").arg(formatLength(p.x(), u, m_doc->dpi()),
                                                         formatLength(p.y(), u, m_doc->dpi())));
    });
}

void MainWindow::rebuildRecentMenu()
{
    // Como Paint.NET: número, nombre y miniatura de cada archivo, y al final "Borrar la
    // lista".
    m_recentMenu->clear();
    const QStringList files = AppState::instance()->recentFiles();
    int n = 0;
    // La carpeta de cada uno, a la derecha (como Paint.NET 5.2, que la pone debajo): sin pasar el
    // puntero por encima ya se distinguen dos archivos con el mismo nombre.
    const QFontMetrics metrics(m_recentMenu->font());
    for (const QString& f : files) {
        const QString folder = metrics.elidedText(QDir::toNativeSeparators(QFileInfo(f).absolutePath()), Qt::ElideMiddle, 320);
        QAction* a = m_recentMenu->addAction(QStringLiteral("&%1 %2\t%3").arg(++n % 10).arg(QFileInfo(f).fileName(), folder), this,
                                             [this, f] { openFiles({f}); });
        a->setToolTip(QDir::toNativeSeparators(f));
        a->setIcon(AppState::instance()->recentThumbnail(f));
    }
    if (!files.isEmpty()) {
        m_recentMenu->addSeparator();
        m_recentMenu->addAction(m_act["clearRecent"]);
    }
    m_recentMenu->setEnabled(!files.isEmpty());
}

// ---------------------------------------------------------------------------
// Documentos
// ---------------------------------------------------------------------------

void MainWindow::newDefaultDocument()
{
    auto* doc = Document::createBlank(QSize(800, 600), Qt::white);
    addDocument(doc);
}

void MainWindow::restoreSessionOrNew()
{
    if (m_settings.reopenLast) {
        QStringList files;
        for (const QString& f : QSettings().value(QStringLiteral("session/files")).toStringList())
            if (QFileInfo::exists(f))
                files << f;
        if (!files.isEmpty()) {
            openFiles(files);
            if (!m_docs.isEmpty())
                return;
        }
    }
    newDefaultDocument();
}

void MainWindow::addDocument(Document* doc)
{
    doc->setParent(this);
    m_docs.append(doc);
    m_undoGroup->addStack(doc->undoStack());
    {
        const QSignalBlocker block(m_tabs);
        m_tabs->addTab(QString());
    }

    auto ifCurrent = [this, doc](auto fn) {
        return [this, doc, fn] {
            if (doc == m_doc)
                fn();
        };
    };
    connect(doc, &Document::modifiedChanged, this, [this, doc] {
        updateTab(doc);
        if (doc == m_doc)
            updateTitle();
    });
    connect(doc, &Document::filePathChanged, this, [this, doc] {
        updateTab(doc);
        if (doc == m_doc) {
            updateTitle();
            m_history->setDocument(doc);
            m_gifHistory->setDocument(doc);
        }
    });
    connect(doc, &Document::pixelsChanged, this, [this, doc] {
        m_thumbDirty.insert(doc);
        if (!m_thumbTimer.isActive())
            m_thumbTimer.start();
    });
    connect(doc, &Document::selectionChanged, this, ifCurrent([this] {
                updateActions();
                updateStatus();
            }));
    connect(doc, &Document::sizeChanged, this, ifCurrent([this] { updateStatus(); }));
    connect(doc, &Document::layersChanged, this, ifCurrent([this] { updateActions(); }));
    connect(doc, &Document::activeLayerChanged, this, ifCurrent([this] { updateActions(); }));

    setCurrentDocument(doc);
    updateTab(doc);
}

void MainWindow::setCurrentDocument(Document* doc, bool force)
{
    if (doc == m_doc && !force)
        return;
    m_doc = doc;
    m_canvas->setDocument(doc);
    m_undoGroup->setActiveStack(doc ? doc->undoStack() : nullptr);
    m_layers->setDocument(doc);
    m_history->setDocument(doc);
    m_gifHistory->setDocument(doc);
    {
        const QSignalBlocker block(m_tabs);
        // Dentro de un grupo contraído, el grupo se expande para verla.
        static_cast<ImageStrip*>(m_tabs)->revealTab(int(m_docs.indexOf(doc)));
        m_tabs->setCurrentIndex(int(m_docs.indexOf(doc)));
    }
    updateTitle();
    updateActions();
    updateStatus();
    updateCentralView();
}

bool MainWindow::closeDocument(Document* doc, bool ask)
{
    if (!doc)
        return true;
    finishTool();
    // La imagen de una sesión colaborativa: cerrarla es salir de ella (el invitado no la guarda).
    if (m_collab->isGuestDocument(doc)) {
        if (!m_collab->leave(ask))
            return false;
    } else {
        if (ask && !maybeSave(doc))
            return false;
        if (m_collab->isSessionDocument(doc) && !m_collab->leave(ask))
            return false;
    }
    const int index = int(m_docs.indexOf(doc));
    Document* next = doc == m_doc ? m_docs.value(index + 1, m_docs.value(index - 1)) : m_doc;
    m_canvas->forgetDocument(doc);
    m_undoGroup->removeStack(doc->undoStack());
    m_docs.removeAt(index);
    {
        const QSignalBlocker block(m_tabs);
        m_tabs->removeTab(index);
    }
    m_thumbDirty.remove(doc);
    m_gifDocs.remove(doc);
    if (doc == m_doc)
        m_doc = nullptr;
    setCurrentDocument(next, true);
    doc->deleteLater();
    return true;
}

bool MainWindow::maybeSave(Document* doc)
{
    if (!doc->isModified() || m_collab->isGuestDocument(doc))
        return true;
    setCurrentDocument(doc);
    // Con los iconos del tema del sistema, como siempre tuvo este diálogo.
    TaskDialog dlg(tr("Cambios no guardados"), Icons::system({"dialog-warning"}), doc->flattened(),
                   tr("«%1» tiene cambios sin guardar.\n¿Qué quiere hacer?").arg(doc->displayName()), this);
    const int saveChoice = dlg.addChoice(Icons::system({"document-save"}), tr("Guardar"),
                                         tr("Guardar la imagen y, a continuación, cerrarla."), true);
    const int discard = dlg.addChoice(Icons::system({"edit-delete", "user-trash"}), tr("No guardar"),
                                      tr("Descartar los cambios no guardados."));
    dlg.addChoice(Icons::system({"edit-undo"}), tr("Cancelar"), tr("Volver a Kaint."));
    const int answer = dlg.choose();
    if (answer == saveChoice)
        return save(doc);
    return answer == discard;
}

bool MainWindow::maybeSaveAll(const QList<Document*>& docs, bool quitting)
{
    QList<Document*> modified;
    for (Document* d : docs)
        if (d->isModified() && !m_collab->isGuestDocument(d))
            modified << d;
    if (modified.size() < 2 || !m_settings.askOnceOnClose) {
        for (Document* d : std::as_const(modified))
            if (!maybeSave(d))
                return false;
        return true;
    }

    // Como Paint.NET 5: las miniaturas de todas (un clic muestra esa imagen) y una sola respuesta.
    TaskDialog dlg(tr("Cambios no guardados"), Icons::system({"dialog-warning"}), QImage(),
                   tr("Las siguientes imágenes abiertas contienen cambios que no se han guardado. Haga clic en una "
                      "miniatura para mostrar la imagen en la ventana principal."),
                   this);
    auto* list = new QListWidget;
    list->setViewMode(QListView::IconMode);
    list->setFlow(QListView::LeftToRight);
    list->setWrapping(false);
    list->setMovement(QListView::Static);
    list->setIconSize(m_tabs->iconSize());
    list->setSpacing(4);
    list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    for (Document* d : std::as_const(modified)) {
        auto* item = new QListWidgetItem(m_tabs->tabIcon(int(m_docs.indexOf(d))), QString(), list);
        item->setToolTip(d->displayName());
        item->setSizeHint(m_tabs->iconSize() + QSize(8, 8));
        if (d == m_doc)
            list->setCurrentItem(item);
    }
    // Una fila de miniaturas; la barra de desplazamiento solo si no caben (el ancho de las opciones).
    const QSize cell = m_tabs->iconSize() + QSize(8 + 2 * list->spacing(), 8 + 2 * list->spacing());
    const bool scrolls = cell.width() * modified.size() > 440;
    list->setFixedHeight(cell.height() + 2 * list->frameWidth() + (scrolls ? list->horizontalScrollBar()->sizeHint().height() : 0));
    connect(list, &QListWidget::currentRowChanged, this, [this, modified](int row) {
        if (Document* d = modified.value(row))
            setCurrentDocument(d);
    });
    dlg.addContent(list);
    const int saveChoice = dlg.addChoice(Icons::system({"document-save"}), tr("Guardar"),
                                         quitting ? tr("Guardar las imágenes de la lista de arriba y, a continuación, salir.")
                                                  : tr("Guardar las imágenes de la lista de arriba y, a continuación, cerrarlas."),
                                         true);
    const int discard = dlg.addChoice(Icons::system({"edit-delete", "user-trash"}), tr("No guardar"),
                                      quitting ? tr("Descartar todos los cambios no guardados y, a continuación, salir.")
                                               : tr("Descartar todos los cambios no guardados y, a continuación, cerrarlas."));
    dlg.addChoice(Icons::system({"edit-undo"}), tr("Cancelar"), tr("Volver a Kaint."));
    const int answer = dlg.choose();
    if (answer == saveChoice) {
        for (Document* d : std::as_const(modified)) {
            setCurrentDocument(d);
            if (!save(d))
                return false;
        }
        return true;
    }
    return answer == discard;
}

void MainWindow::updateTab(Document* doc)
{
    const int i = int(m_docs.indexOf(doc));
    if (i < 0)
        return;
    const qreal dpr = devicePixelRatioF();
    const QImage source = doc == m_doc ? m_canvas->composite() : doc->flattened();
    QImage thumb = Raster::thumbnail(source, static_cast<ImageStrip*>(m_tabs)->thumbBox() * dpr);
    {
        // Borde fino: una imagen blanca no se pierde sobre el fondo claro.
        QPainter p(&thumb);
        p.setPen(QPen(QColor(0, 0, 0, 70), dpr));
        p.drawRect(QRectF(thumb.rect()).adjusted(dpr / 2, dpr / 2, -dpr / 2, -dpr / 2));
    }
    thumb.setDevicePixelRatio(dpr);
    m_tabs->setTabIcon(i, QIcon(QPixmap::fromImage(thumb)));
    auto* strip = static_cast<ImageStrip*>(m_tabs);
    strip->setUnsaved(i, doc->isModified());
    // Sesión colaborativa: la corona (la imagen del anfitrión guardada o no) y, en los invitados,
    // el equipo (su copia propia: ninguna, atrasada o al día).
    const CollabController::TabState collab = m_collab ? m_collab->tabState(doc) : CollabController::TabState();
    int computer = 0;
    if (collab.guest)
        computer = collab.copy == Collab::Session::LocalCopy::Current ? 3 : collab.copy == Collab::Session::LocalCopy::Behind ? 2 : 1;
    if (const auto preview = m_previewCollab.constFind(doc); preview != m_previewCollab.cend())
        strip->setCollab(i, true, preview->first, preview->second);
    else
        strip->setCollab(i, collab.collab, collab.collab ? (collab.hostSaved ? 2 : 1) : 0, computer);
    m_tabs->setTabToolTip(i, QStringLiteral("%1%2\n%3 × %4")
                                 .arg(doc->isModified() ? QStringLiteral("*") : QString(), doc->displayName())
                                 .arg(doc->size().width())
                                 .arg(doc->size().height()));
}

void MainWindow::showImageList()
{
    // Como Paint.NET: debajo de la flecha, cada imagen con su miniatura y su nombre, y la actual
    // resaltada.
    auto* popup = new QFrame(this, Qt::Popup);
    popup->setObjectName(QStringLiteral("ImageListPopup"));
    popup->setAttribute(Qt::WA_DeleteOnClose);
    popup->setFrameShape(QFrame::StyledPanel);
    auto* layout = new QVBoxLayout(popup);
    layout->setContentsMargins(1, 1, 1, 1);
    auto* list = new QListWidget(popup);
    list->setObjectName(QStringLiteral("ImageList"));
    list->setFrameShape(QFrame::NoFrame);
    list->setIconSize(m_tabs->iconSize());
    list->setUniformItemSizes(true);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(list);
    const int rowHeight = m_tabs->iconSize().height() + 10;
    int textWidth = 0;
    for (int i = 0; i < m_docs.size(); ++i) {
        Document* d = m_docs.at(i);
        const QString name = (d->isModified() ? QStringLiteral("*") : QString()) + d->displayName();
        // La miniatura de la actual sin teñir por el resaltado y, si tiene cambios sin guardar, con
        // el sol arriba a la izquierda, como en la tira.
        QIcon icon;
        QPixmap thumb = m_tabs->tabIcon(i).pixmap(m_tabs->iconSize());
        if (d->isModified()) {
            const QSize box = m_tabs->iconSize();
            const qreal dpr = devicePixelRatioF();
            QPixmap marked(box * dpr);
            marked.setDevicePixelRatio(dpr);
            marked.fill(Qt::transparent);
            QPainter p(&marked);
            const QSizeF size = thumb.deviceIndependentSize();
            p.drawPixmap(QPointF((box.width() - size.width()) / 2, (box.height() - size.height()) / 2), thumb);
            Icons::paintUnsavedStar(p, QRectF(0, 0, 12, 12));
            p.end();
            thumb = marked;
        }
        icon.addPixmap(thumb, QIcon::Normal);
        icon.addPixmap(thumb, QIcon::Selected);
        auto* item = new QListWidgetItem(icon, name, list);
        item->setSizeHint(QSize(0, rowHeight));
        textWidth = std::max(textWidth, list->fontMetrics().horizontalAdvance(name));
        if (d == m_doc)
            list->setCurrentItem(item);
    }
    auto choose = [this, popup, list](QListWidgetItem* item) {
        Document* d = m_docs.value(list->row(item));
        popup->close();
        if (d)
            setCurrentDocument(d);
    };
    connect(list, &QListWidget::itemClicked, popup, choose);
    connect(list, &QListWidget::itemActivated, popup, choose);
    connect(popup, &QObject::destroyed, m_listButton, [button = QPointer<QToolButton>(m_listButton)] {
        if (button)
            button->setDown(false);
    });
    const int rows = std::min<int>(int(m_docs.size()), 8);
    popup->resize(std::clamp(m_tabs->iconSize().width() + textWidth + 48, 220, 480), rows * rowHeight + 2 * 1 + 4);
    const QRect screen = m_listButton->screen()->availableGeometry();
    QPoint at = m_listButton->mapToGlobal(QPoint(0, m_listButton->height()));
    at.setX(std::min(at.x(), screen.right() - popup->width()));
    popup->move(at);
    m_listButton->setDown(true);
    popup->show();
    list->setFocus();
}

void MainWindow::updateTitle()
{
    // (Con el nombre del programa propio, si se eligió en Configuración.)
    const QString program = Icons::programTitle();
    if (!m_doc) {
        setWindowTitle(program);
        return;
    }
    setWindowTitle(QStringLiteral("%1%2 - %3").arg(m_doc->isModified() ? QStringLiteral("*") : QString(), m_doc->displayName(), program));
}

void MainWindow::updateActions()
{
    const bool has = m_doc != nullptr;
    const bool sel = has && !m_doc->selection().isEmpty();
    for (QAction* a : std::as_const(m_docActions))
        a->setEnabled(has);
    updateCollabActions();
    for (QAction* a : std::as_const(m_selActions))
        a->setEnabled(sel);
    const int count = has ? m_doc->layerCount() : 0;
    const int active = has ? m_doc->activeIndex() : -1;
    m_act["deleteLayer"]->setEnabled(count > 1);
    m_act["mergeDown"]->setEnabled(active > 0);
    m_act["layerUp"]->setEnabled(has && active < count - 1);
    m_act["layerDown"]->setEnabled(active > 0);
    m_act["layerAbove"]->setEnabled(has && active < count - 1);
    m_act["layerTop"]->setEnabled(has && active < count - 1);
    m_act["layerBelow"]->setEnabled(active > 0);
    m_act["layerBottom"]->setEnabled(active > 0);
    m_act["closeOthers"]->setEnabled(m_docs.size() > 1);
    m_act["nextImage"]->setEnabled(m_docs.size() > 1);
    m_act["prevImage"]->setEnabled(m_docs.size() > 1);
    // Con algo a medias en la herramienta (la selección de la varita mágica mientras se ajusta,
    // una forma...), Deshacer lo termina y lo deshace: en una imagen recién abierta, sin pasos en
    // el historial, Ctrl+Z no hacía nada y la selección se quedaba.
    m_act["undo"]->setEnabled(m_undoGroup->canUndo()
                              || (m_doc && m_tools && m_tools->current() && m_tools->current()->isBusy()));
    m_act["redo"]->setEnabled(m_undoGroup->canRedo());
    m_act["repeatEffect"]->setEnabled(has && m_lastEffect);
    m_act["repeatEffect"]->setVisible(m_lastEffect != nullptr);
    {
        const QSignalBlocker block(m_act["alphaLock"]);
        m_act["alphaLock"]->setChecked(has && m_doc->activeLayer() && m_doc->activeLayer()->props.alphaLock);
    }
    // En el editor de GIF, lo que trabaja con la selección o las herramientas no se ve y cambiaba
    // la imagen a escondidas (un pegado flotante que quedaba como una capa vieja al volver):
    // desactivado. Copiar, cortar y pegar trabajan con fotogramas (GifEditor).
    const bool gif = inGifEditor();
    for (QAction* a : std::as_const(m_imageOnlyActions))
        a->setEnabled(!gif);
    if (gif) {
        for (QAction* a : std::as_const(m_selActions))
            a->setEnabled(false);
        for (const char* name : {"selectAll", "deselect", "invertSel", "copySelection", "copyMerged", "pasteImage"})
            if (QAction* a = m_act.value(QLatin1String(name)))
                a->setEnabled(false);
        m_act["copy"]->setEnabled(true);
        m_act["cut"]->setEnabled(true);
        m_act["paste"]->setEnabled(true);
        m_act["pasteLayer"]->setEnabled(true);
    }
}

void MainWindow::showToolHint(const QString& hint)
{
    m_toolHint = hint;
    refreshHint();
}

void MainWindow::refreshHint()
{
    // En el editor de GIF, solo su icono y su nombre (lo demás de la barra es del lienzo).
    if (inGifEditor()) {
        m_hintIcon->setPixmap(Icons::theme({"video-x-generic", "image-gif", "media-playback-start"}).pixmap(QSize(16, 16), devicePixelRatioF()));
        m_hint->setText(tr("Editor de GIF"));
        return;
    }
    // Con una herramienta de selección en reposo y algo seleccionado, como Paint.NET: posición,
    // rectángulo delimitador y área de la selección.
    // Delante, como en paint.js, el icono de la herramienta o el de la selección.
    Tool* t = m_tools ? m_tools->current() : nullptr;
    auto setIcon = [this](const QIcon& icon) {
        m_hintIcon->setPixmap(icon.isNull() ? QPixmap() : icon.pixmap(QSize(16, 16), devicePixelRatioF()));
    };
    if (!m_doc || !t || !t->highlightSelection() || t->isBusy() || m_doc->selection().isEmpty()
        || m_toolHint != t->hint()) {
        setIcon(t ? t->icon() : QIcon());
        m_hint->setText(m_toolHint);
        return;
    }
    setIcon(Icons::named(QStringLiteral("status-selection")));
    const Unit u = m_canvas->unit();
    const double dpi = m_doc->dpi();
    auto len = [&](double px) { return QStringLiteral("%1 %2").arg(formatLength(px, u, dpi), unitSuffix(u)); };
    const Selection& sel = m_doc->selection();
    const QRect b = sel.bounds();
    double area = 0;
    const QImage& mask = sel.mask();
    for (int y = b.top(); y <= b.bottom(); ++y) {
        const uchar* row = mask.constScanLine(y);
        for (int x = b.left(); x <= b.right(); ++x)
            area += row[x];
    }
    const double ppu = pixelsPerUnit(u, dpi);
    area /= 255.0 * ppu * ppu;
    const QString squares = u == Unit::Inches        ? tr("pulgadas cuadradas")
                            : u == Unit::Centimeters ? tr("centímetros cuadrados")
                                                     : tr("píxeles cuadrados");
    m_hint->setText(tr("Esquina superior izquierda de la selección: %1, %2. Tamaño del rectángulo delimitador: %3 × "
                       "%4. Área: %5 %6")
                        .arg(len(b.left()), len(b.top()), len(b.width()), len(b.height()),
                             QLocale().toString(area, 'f', u == Unit::Pixels ? 0 : 2), squares));
}

void MainWindow::updateStatus()
{
    refreshHint();
    if (!m_doc) {
        m_selLabel->clear();
        m_posLabel->clear();
        return;
    }
    const Unit u = m_canvas->unit();
    const double dpi = m_doc->dpi();
    auto len = [&](double px) { return formatLength(px, u, dpi); };
    const QRect b = m_doc->selection().isEmpty() ? m_doc->rect() : m_doc->selection().bounds();
    m_selLabel->setText(QStringLiteral("%1 × %2").arg(len(b.width()), len(b.height())));
    m_selLabel->parentWidget()->setToolTip(
        m_doc->selection().isEmpty()
            ? tr("Tamaño de la imagen: %1 × %2 %3 (no hay selección)")
                  .arg(len(m_doc->size().width()), len(m_doc->size().height()), unitSuffix(u))
            : tr("Tamaño de la selección: %1 × %2 %3 · esquina superior izquierda: %4, %5")
                  .arg(len(b.width()), len(b.height()), unitSuffix(u), len(b.left()), len(b.top())));
}

// ---------------------------------------------------------------------------
// Archivo
// ---------------------------------------------------------------------------

void MainWindow::newImage()
{
    QSize initial(800, 600);
    if (m_doc)
        initial = m_doc->size();
    const QImage clip = QApplication::clipboard()->image();
    if (!clip.isNull())
        initial = clip.size();
    NewImageDialog dlg(initial, m_doc ? m_doc->dpi() : 96.0, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    Document* doc = Document::createBlank(dlg.imageSize(), dlg.background(AppState::instance()->secondary()));
    doc->setDpi(dlg.dpi());
    doc->undoStack()->setClean();
    addDocument(doc);
}

void MainWindow::open()
{
    // Con los vídeos (se abren con FFmpeg, un fotograma por capa).
    QString filter = ImageIO::openFilter();
    const QString videos = GifExport::videoPatterns();
    filter.replace(QRegularExpression(QStringLiteral("^([^;]*\\()([^)]*)\\)")), QStringLiteral("\\1\\2 %1)").arg(videos));
    filter.insert(filter.indexOf(QStringLiteral(";;")), QStringLiteral(";;") + tr("Vídeo (%1)").arg(videos));
    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Abrir"), m_lastDir, filter);
    openFiles(paths);
}

void MainWindow::acquireFromScanner()
{
    // En Linux se escanea con SANE (scanimage): el resultado se abre como imagen nueva.
    const QString scanimage = QStandardPaths::findExecutable(QStringLiteral("scanimage"));
    if (scanimage.isEmpty()) {
        QMessageBox::information(this, tr("Desde escáner"),
                                 tr("Para escanear hace falta SANE. Instálalo con:\n\n"
                                    "    sudo pacman -S sane sane-airscan\n\n"
                                    "(sane-airscan añade los escáneres e impresoras multifunción de red)."));
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QProcess list;
    list.start(scanimage, {QStringLiteral("-f"), QStringLiteral("%d\t%v %m%n")});
    list.waitForFinished(20000);
    QApplication::restoreOverrideCursor();
    QStringList ids, names;
    for (const QString& line : QString::fromLocal8Bit(list.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QStringList parts = line.split(QLatin1Char('\t'));
        ids << parts.value(0);
        names << parts.value(1, parts.value(0)).trimmed();
    }
    if (ids.isEmpty()) {
        QMessageBox::information(this, tr("Desde escáner"), tr("No se encontró ningún escáner."));
        return;
    }
    int choice = 0;
    if (ids.size() > 1) {
        bool ok = false;
        const QString picked = QInputDialog::getItem(this, tr("Desde escáner"), tr("Escáner:"), names, 0, false, &ok);
        if (!ok)
            return;
        choice = int(names.indexOf(picked));
    }

    QProgressDialog progress(tr("Escaneando con «%1»...").arg(names[choice]), tr("Cancelar"), 0, 0, this);
    progress.setWindowTitle(tr("Desde escáner"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    QProcess scan;
    QEventLoop loop;
    connect(&scan, &QProcess::finished, &loop, &QEventLoop::quit);
    connect(&progress, &QProgressDialog::canceled, &scan, &QProcess::kill);
    scan.start(scanimage, {QStringLiteral("-d"), ids[choice], QStringLiteral("--format=png"),
                           QStringLiteral("--resolution"), QStringLiteral("300")});
    loop.exec();
    progress.close();
    QImage img;
    img.loadFromData(scan.readAllStandardOutput(), "PNG");
    if (img.isNull()) {
        if (!progress.wasCanceled())
            QMessageBox::warning(this, tr("Desde escáner"),
                                 tr("No se pudo escanear:\n%1").arg(QString::fromLocal8Bit(scan.readAllStandardError())));
        return;
    }
    auto* doc = Document::createFromImage(img, tr("Fondo"));
    doc->setDpi(300);
    doc->setHistoryRootLabel(tr("Desde escáner"));
    addDocument(doc);
}

void MainWindow::openFiles(const QStringList& paths)
{
    // Los complementos de paint.js que se abren se instalan (como en la versión web).
    QStringList packages;
    for (const QString& path : paths)
        if (PaintJs::isPackage(path))
            packages << path;
    if (!packages.isEmpty())
        installDroppedPlugins(packages);
    for (const QString& path : paths) {
        if (PaintJs::isPackage(path))
            continue;
        const QString abs = QFileInfo(path).absoluteFilePath();
        bool alreadyOpen = false;
        for (Document* d : std::as_const(m_docs)) {
            if (!d->filePath().isEmpty() && QFileInfo(d->filePath()).absoluteFilePath() == abs) {
                setCurrentDocument(d);
                alreadyOpen = true;
            }
        }
        if (alreadyOpen)
            continue;
        QString error;
        Document* doc = nullptr;
        // Vídeo (con FFmpeg): un fotograma por capa, como un GIF animado, y sin archivo (guardar no
        // lo pisa).
        const bool video = GifExport::isVideoFile(abs);
        if (video) {
            QList<QImage> images;
            int delay = 100;
            if (!GifExport::decodeVideo(abs, &images, &delay, this, &error)) {
                if (!error.isEmpty())
                    QMessageBox::critical(this, tr("No se pudo abrir"), tr("No se pudo abrir «%1»:\n%2").arg(abs, error));
                continue;
            }
            doc = new Document(images.first().size());
            QList<LayerPtr> layers;
            for (int i = 0; i < images.size(); ++i) {
                auto layer = std::make_shared<Layer>(Gif::nameWithDelay(tr("Fotograma %1").arg(i + 1), delay), images.first().size());
                Raster::blit(layer->image, images[i].convertToFormat(kLayerFormat), QPoint());
                layers << layer;
            }
            doc->replaceState(images.first().size(), layers, int(layers.size()) - 1);
            doc->setUntitledName(QFileInfo(abs).completeBaseName());
            doc->setHistoryRootLabel(tr("Abrir imagen"));
            doc->undoStack()->clear();
        } else {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            doc = ImageIO::load(abs, &error);
            QApplication::restoreOverrideCursor();
        }
        if (!doc) {
            QMessageBox::critical(this, tr("No se pudo abrir"), tr("No se pudo abrir «%1»:\n%2").arg(abs, error));
            continue;
        }
        // GIF animado: como capas (un fotograma por capa, como hasta ahora), cada fotograma como
        // una imagen aparte, solo el primero como una imagen normal o los fotogramas que se elijan.
        bool asImages = false;
        // Fotogramas elegidos (vacío: todos).
        QList<int> frames;
        if ((ImageIO::isAnimation(abs) || video) && doc->layerCount() > 1) {
            int mode = m_settings.gifOpenMode;
            if (mode == 0) {
                TaskDialog ask(video ? tr("Abrir un vídeo") : tr("Abrir un GIF animado"), Icons::theme({"document-open"}), doc->layer(0)->image,
                               tr("«%1» tiene %n fotograma(s). ¿Cómo quieres abrirlo?", nullptr, doc->layerCount())
                                   .arg(QFileInfo(abs).fileName()),
                               this);
                auto* remember = new QCheckBox(tr("Recordar mi elección (se cambia en Configuración > Interfaz de usuario)"));
                ask.addContent(remember);
                const int layers = ask.addChoice(Icons::theme({"layer-visible-on", "window-layers"}), tr("Como capas"),
                                                 tr("Una sola imagen con un fotograma en cada capa, una encima de otra. Se "
                                                    "vuelve a guardar como GIF animado."),
                                                 true);
                const int images = ask.addChoice(Icons::theme({"image-x-generic", "folder-images"}), tr("Como imágenes"),
                                                 tr("Cada fotograma se abre como una imagen aparte. Al guardar como GIF se "
                                                    "pueden elegir las imágenes que forman la animación."));
                const int single = ask.addChoice(Icons::theme({"document-new", "image-x-generic"}), tr("Como una sola imagen"),
                                                 tr("Solo el primer fotograma, como una imagen normal (sin animación)."));
                const int pick = ask.addChoice(Icons::theme({"edit-select-all", "view-list-icons"}), tr("Elegir los fotogramas…"),
                                               tr("Elige qué fotogramas abrir y si se abren como capas o como imágenes."));
                const int choice = ask.choose();
                if (choice == layers) {
                    mode = 1;
                } else if (choice == images) {
                    mode = 2;
                } else if (choice == single) {
                    mode = 3;
                } else if (choice == pick) {
                    mode = 4;
                } else {
                    delete doc;
                    continue;
                }
                if (remember->isChecked()) {
                    m_settings.gifOpenMode = mode;
                    m_settings.save();
                }
            }
            asImages = mode == 2;
            if (mode == 3) {
                frames = {0};
            } else if (mode == 4) {
                GifFramesDialog pickFrames(doc, QFileInfo(abs).fileName(), this);
                if (pickFrames.exec() != QDialog::Accepted || pickFrames.frames().isEmpty()) {
                    delete doc;
                    continue;
                }
                frames = pickFrames.frames();
                asImages = pickFrames.openAs() == GifFramesDialog::OpenAs::Images;
                if (frames.size() == doc->layerCount())
                    frames.clear();
            }
            // Uno solo (el primero o el único elegido): una imagen normal.
            if (frames.size() == 1)
                asImages = true;
        }
        // La imagen en blanco del inicio se sustituye (no una sin archivo con nombre propio, como un
        // fotograma de un GIF).
        if (m_docs.size() == 1 && m_docs.first()->filePath().isEmpty() && m_docs.first()->untitledName().isEmpty()
            && !m_docs.first()->isModified() && m_docs.first()->undoStack()->count() == 0)
            closeDocument(m_docs.first());
        if (asImages) {
            // Cada fotograma, una imagen sin archivo ("animación (3)"); el nombre de su capa conserva
            // la duración, para volver a guardarlas como GIF eligiendo las imágenes.
            const QString base = QFileInfo(abs).completeBaseName();
            Document* first = nullptr;
            QList<int> indices = frames;
            if (indices.isEmpty())
                for (int i = 0; i < doc->layerCount(); ++i)
                    indices << i;
            for (int i : std::as_const(indices)) {
                const Layer* frame = doc->layer(i);
                Document* d = Document::createFromImage(frame->image, frame->props.name);
                d->setDpi(doc->dpi());
                d->setGifLoops(doc->gifLoops());
                // Sin archivo: guardar no pisa el GIF animado.
                d->setUntitledName(indices.size() == 1 ? base : tr("%1 (%2)").arg(base).arg(i + 1));
                d->setHistoryRootLabel(tr("Abrir imagen"));
                addDocument(d);
                if (!first)
                    first = d;
            }
            delete doc;
            if (first)
                setCurrentDocument(first);
        } else {
            if (!frames.isEmpty()) {
                // Solo los fotogramas elegidos, como capas, y sin archivo: guardar no pisa el GIF con
                // menos fotogramas.
                QList<LayerPtr> chosen;
                for (int i : std::as_const(frames))
                    chosen << doc->layerPtr(i);
                doc->replaceState(doc->size(), chosen, int(chosen.size()) - 1);
                doc->setFilePath(QString());
                doc->setUntitledName(QFileInfo(abs).completeBaseName());
            }
            const QString warning = doc->property(ImageIO::kImportWarningProperty).toString();
            addDocument(doc);
            // Lo que cambió al abrirlo (un PSD con grupos, de 16 bits...): se abre como imagen nueva.
            if (!warning.isEmpty())
                QMessageBox::information(this, tr("Abrir «%1»").arg(QFileInfo(abs).fileName()), warning);
        }
        AppState::instance()->addRecentFile(abs, m_canvas->composite());
        m_lastDir = QFileInfo(abs).absolutePath();
    }
}

QString MainWindow::screenPreview(const QStringList& files, const QString& output, bool gifEditor, const QString& popup)
{
    Document* first = nullptr;
    for (const QString& path : files) {
        QString error;
        Document* doc = ImageIO::load(QFileInfo(path).absoluteFilePath(), &error);
        if (!doc)
            return tr("No se pudo abrir «%1»: %2").arg(path, error);
        addDocument(doc);
        if (!first)
            first = doc;
    }
    if (first)
        setCurrentDocument(first);
    else
        newDefaultDocument();
    if (gifEditor && m_doc) {
        m_gifDocs.insert(m_doc);
        updateCentralView();
    }
    // La tira de imágenes (para el README): la primera, sin guardar (la estrella naranja); la
    // segunda, de una sesión colaborativa como anfitrión (la corona) y la última, como invitado (la
    // corona y el equipo, con su copia al día).
    const bool strip = popup == QLatin1String("strip");
    if (strip && m_docs.size() >= 3) {
        m_docs.first()->undoStack()->resetClean();
        m_previewCollab.insert(m_docs[1], {2, 0});
        m_previewCollab.insert(m_docs.last(), {2, 3});
        for (Document* doc : std::as_const(m_docs))
            updateTab(doc);
    }
    // Tiempo para que se pinte todo (miniaturas, ventanas, barra de opciones).
    QElapsedTimer wait;
    wait.start();
    while (wait.elapsed() < 800)
        QApplication::processEvents(QEventLoop::AllEvents, 20);
    QImage shot;
    if (popup.startsWith(QLatin1String("menu:"))) {
        // El menú (o un submenú suyo, por su título sin «&»), abierto y pintado como en pantalla.
        const QStringList path = popup.mid(5).split(QLatin1Char('/'));
        QMenu* menu = path.first() == QLatin1String("adjustments") ? m_adjustmentsMenu : m_effectsMenu;
        for (int i = 1; menu && i < path.size(); ++i) {
            QMenu* found = nullptr;
            for (QAction* a : menu->actions())
                if (a->menu() && a->menu()->title().remove(QLatin1Char('&')) == path[i])
                    found = a->menu();
            menu = found;
        }
        if (!menu)
            return tr("No existe el menú «%1».").arg(popup);
        menu->popup(mapToGlobal(QPoint(40, 40)));
        wait.restart();
        while (wait.elapsed() < 300)
            QApplication::processEvents(QEventLoop::AllEvents, 20);
        shot = menu->grab().toImage();
        menu->hide();
    } else if (popup.startsWith(QLatin1String("effect:"))) {
        // El diálogo del efecto con sus valores de fábrica («effect:id@1»: en su segunda pestaña).
        const QStringList parts = popup.mid(7).split(QLatin1Char('@'));
        const Effect* effect = Effects::find(parts.first());
        if (!effect || !m_doc || !m_doc->activeLayer())
            return tr("No existe el efecto «%1».").arg(popup);
        EffectDialog dlg(*effect, Effects::defaults(*effect, effectContext()), m_doc->activeLayer()->image, m_doc->rect(), this);
        if (parts.size() > 1)
            if (auto* tabs = dlg.findChild<QTabWidget*>())
                tabs->setCurrentIndex(parts[1].toInt());
        dlg.show();
        wait.restart();
        while (wait.elapsed() < 400)
            QApplication::processEvents(QEventLoop::AllEvents, 20);
        shot = dlg.grab().toImage();
    } else if (strip) {
        // De arriba abajo, toda la fila de arriba; a lo ancho, de la tira a la flecha de la lista.
        QWidget* top = m_tabs->parentWidget();
        QRect r = m_tabs->geometry().united(m_listButton->geometry()).adjusted(-6, 0, 6, 0);
        r.setTop(0);
        r.setBottom(top->height() - 1);
        shot = grab(r.translated(top->mapTo(this, QPoint(0, 0))).intersected(rect())).toImage();
    } else {
        shot = grab().toImage();
    }
    const QString suffix = QFileInfo(output).suffix().toLower();
    const bool jpeg = suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg");
    if (!shot.convertToFormat(jpeg ? QImage::Format_RGB32 : QImage::Format_ARGB32).save(output, nullptr, jpeg ? 95 : -1))
        return tr("No se pudo guardar «%1».").arg(output);
    return {};
}

bool MainWindow::save(Document* doc)
{
    if (!doc)
        return false;
    if (doc->filePath().isEmpty() || !ImageIO::isWritable(doc->filePath()))
        return saveAs(doc);
    return writeFile(doc, doc->filePath());
}

bool MainWindow::saveAs(Document* doc, const QString& suffix)
{
    if (!doc)
        return false;
    finishTool();
    QString selected;
    const QString filters = ImageIO::saveFilter(&selected);
    QString suggestion = doc->filePath();
    if (!suffix.isEmpty()) {
        // Exportar GIF (editor de GIF): ese formato, con el nombre de la imagen.
        const QString base = !suggestion.isEmpty() ? QFileInfo(suggestion).absolutePath() + QLatin1Char('/') + QFileInfo(suggestion).completeBaseName()
                             : m_lastDir + QLatin1Char('/') + (doc->untitledName().isEmpty() ? tr("Sin título") : doc->untitledName());
        suggestion = base + QLatin1Char('.') + suffix;
        for (const QString& f : filters.split(QStringLiteral(";;")))
            if (f.contains(QStringLiteral("*.") + suffix))
                selected = f;
        const QString path = QFileDialog::getSaveFileName(this, tr("Exportar GIF"), suggestion, filters, &selected);
        if (path.isEmpty())
            return false;
        return writeFile(doc, QFileInfo(path).suffix().isEmpty() ? path + QLatin1Char('.') + suffix : path);
    }
    if (!suggestion.isEmpty() && !ImageIO::isWritable(suggestion)) {
        suggestion = QFileInfo(suggestion).absolutePath() + QLatin1Char('/') + QFileInfo(suggestion).completeBaseName()
                   + QStringLiteral(".ora");
        for (const QString& f : filters.split(QStringLiteral(";;")))
            if (f.contains(QStringLiteral("*.ora")))
                selected = f;
    }
    if (suggestion.isEmpty()) {
        const QString base = doc->untitledName().isEmpty() ? tr("Sin título") : doc->untitledName();
        suggestion = m_lastDir + QLatin1Char('/') + base + (doc->layerCount() > 1 ? QStringLiteral(".ora") : QStringLiteral(".png"));
        if (doc->layerCount() > 1)
            for (const QString& f : filters.split(QStringLiteral(";;")))
                if (f.contains(QStringLiteral("*.ora")))
                    selected = f;
    }
    // Invitado de una sesión colaborativa: solo una copia (la imagen la guarda el anfitrión).
    const QString title = m_collab->isGuestDocument(doc) ? tr("Exportar una copia") : tr("Guardar como");
    QString path = QFileDialog::getSaveFileName(this, title, suggestion, filters, &selected);
    if (path.isEmpty())
        return false;
    if (QFileInfo(path).suffix().isEmpty()) {
        const int star = int(selected.indexOf(QStringLiteral("*.")));
        if (star >= 0) {
            const int end = int(selected.indexOf(QRegularExpression(QStringLiteral("[ )]")), star));
            path += selected.mid(star + 1, end - star - 1);
        }
    }
    return writeFile(doc, path);
}

bool MainWindow::inGifEditor() const
{
    return m_doc && m_gifDocs.contains(m_doc) && m_central && m_central->currentWidget() == m_gifEditor;
}

void MainWindow::toggleGifEditor()
{
    if (!m_doc)
        return;
    finishTool();
    if (m_gifDocs.contains(m_doc))
        m_gifDocs.remove(m_doc);
    else
        m_gifDocs.insert(m_doc);
    updateCentralView();
}

void MainWindow::updateCentralView()
{
    if (!m_central)
        return;
    const bool gif = m_doc && m_gifDocs.contains(m_doc);
    m_gifEditor->setOpenDocuments(m_docs);
    m_gifEditor->setDocument(gif ? m_doc : nullptr);
    m_central->setCurrentWidget(gif ? static_cast<QWidget*>(m_gifEditor) : m_workspace);
    if (QAction* a = m_act.value("gifEditor")) {
        const QSignalBlocker block(a);
        a->setChecked(gif);
    }
    // Las opciones de las herramientas no sirven en el editor de GIF: se esconden mientras tanto.
    if (m_options) {
        if (gif && m_options->isVisible()) {
            m_options->hide();
            m_optionsHiddenForGif = true;
        } else if (!gif && m_optionsHiddenForGif) {
            m_options->show();
            m_optionsHiddenForGif = false;
        }
    }
    if (gif)
        m_gifEditor->setFocus();
    else
        m_canvas->setFocus();
    // La barra de estado: en el editor de GIF, solo su icono y su nombre (el tamaño, la posición, las
    // unidades y el zum son del lienzo); al volver, lo que se vio antes.
    if (gif) {
        for (QWidget* w : statusBar()->findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
            if (w != m_hintIcon && w != m_hint && !w->inherits("QSizeGrip") && w->isVisible()) {
                w->hide();
                m_statusHiddenForGif << w;
            }
    } else {
        for (const QPointer<QWidget>& w : std::as_const(m_statusHiddenForGif))
            if (w)
                w->show();
        m_statusHiddenForGif.clear();
    }
    refreshHint();
    updateActions();
    updatePresence();
}

bool MainWindow::saveAll()
{
    for (Document* d : QList<Document*>(m_docs)) {
        if (!d->isModified())
            continue;
        setCurrentDocument(d);
        if (!save(d))
            return false;
    }
    return true;
}

void MainWindow::saveAllAs()
{
    if (m_docs.isEmpty())
        return;
    finishTool();
    // Los formatos de Guardar como, por su primera extensión.
    QStringList suffixes, labels;
    for (const QString& filter : ImageIO::saveFilter().split(QStringLiteral(";;"))) {
        const QRegularExpressionMatch m = QRegularExpression(QStringLiteral("\\*\\.([A-Za-z0-9]+)")).match(filter);
        if (!m.hasMatch() || suffixes.contains(m.captured(1).toLower()))
            continue;
        suffixes << m.captured(1).toLower();
        labels << filter;
    }
    QList<SaveAllDialog::Entry> entries;
    QString folder = m_lastDir;
    for (Document* d : std::as_const(m_docs)) {
        SaveAllDialog::Entry e;
        e.filePath = d->filePath();
        e.image = d == m_doc ? m_canvas->composite() : d->flattened();
        if (e.filePath.isEmpty()) {
            e.name = d->untitledName().isEmpty() ? tr("Sin título") : d->untitledName();
            e.suffix = d->layerCount() > 1 ? QStringLiteral("ora") : QStringLiteral("png");
            e.checked = true;
        } else {
            const QFileInfo info(e.filePath);
            e.name = info.completeBaseName();
            e.suffix = ImageIO::isWritable(e.filePath) ? info.suffix().toLower()
                                                        : (d->layerCount() > 1 ? QStringLiteral("ora") : QStringLiteral("png"));
            // Las que ya están guardadas, sin marcar.
            e.checked = d->isModified() || !ImageIO::isWritable(e.filePath);
            if (folder.isEmpty())
                folder = info.absolutePath();
        }
        entries << e;
    }
    if (folder.isEmpty())
        folder = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    // Si ya estaban todas guardadas, todas marcadas: si no, no quedaba ninguna y «Guardar» no se
    // podía pulsar (no dejaba volver a guardarlas, por ejemplo en otra carpeta).
    if (std::none_of(entries.cbegin(), entries.cend(), [](const SaveAllDialog::Entry& e) { return e.checked; }))
        for (SaveAllDialog::Entry& e : entries)
            e.checked = true;
    SaveAllDialog dlg(entries, suffixes, labels, folder, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const QList<Document*> docs = m_docs;
    QStringList errors;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    for (const SaveAllDialog::Result& r : dlg.results()) {
        Document* doc = docs.value(r.index);
        if (!doc || !m_docs.contains(doc))
            continue;
        const QString suffix = QFileInfo(r.path).suffix().toLower();
        ImageIO::SaveOptions options = m_saveOptions.value(suffix);
        if (ImageIO::isAnimation(r.path) && !m_saveOptions.contains(suffix))
            options.loops = doc->gifLoops();
        QString error;
        // Los formatos sin capas guardan la imagen acoplada (sin tocar las capas del documento).
        if (!ImageIO::save(doc, r.path, options, &error)) {
            errors << tr("«%1»: %2").arg(QFileInfo(r.path).fileName(), error);
            continue;
        }
        doc->setFilePath(r.path);
        doc->markSaved();
        AppState::instance()->addRecentFile(r.path, doc == m_doc ? m_canvas->composite() : doc->flattened());
        m_lastDir = QFileInfo(r.path).absolutePath();
        updateTab(doc);
    }
    QApplication::restoreOverrideCursor();
    updateTitle();
    if (!errors.isEmpty())
        QMessageBox::critical(this, tr("No se pudo guardar"), tr("No se pudieron guardar:\n\n%1").arg(errors.join(QLatin1Char('\n'))));
}

bool MainWindow::closeAll(Document* except)
{
    finishTool();
    QList<Document*> closing = m_docs;
    closing.removeAll(except);
    if (!maybeSaveAll(closing, false))
        return false;
    for (Document* d : std::as_const(closing))
        closeDocument(d, false);
    if (except)
        setCurrentDocument(except);
    return true;
}

void MainWindow::switchImage(int delta)
{
    if (m_docs.size() < 2)
        return;
    finishTool();
    const int n = int(m_docs.size());
    const int i = (int(m_docs.indexOf(m_doc)) + delta + n) % n;
    setCurrentDocument(m_docs[i]);
}

void MainWindow::moveImageTab(int delta)
{
    const int from = int(m_docs.indexOf(m_doc));
    const int to = from + delta;
    if (from < 0 || to < 0 || to >= m_docs.size())
        return;
    m_tabs->moveTab(from, to);
}

void MainWindow::showTabMenu(const QPoint& pos)
{
    const int index = m_tabs->tabAt(pos);
    Document* doc = m_docs.value(index);
    if (!doc)
        return;
    auto* strip = static_cast<ImageStrip*>(m_tabs);
    const bool range = strip->hasRange() && strip->inRange(index);
    if (!range)
        setCurrentDocument(doc);
    QMenu menu(this);
    // Los grupos de imágenes: agrupar las elegidas (Ctrl+clic) y, en una de un grupo, contraerlo o
    // expandirlo, su nombre, el color de su borde, sacarla o deshacerlo.
    if (range) {
        menu.addAction(Icons::theme({"list-add"}), tr("Agrupar las imágenes elegidas"), this, [strip] {
            if (strip->onGroupRequested)
                strip->onGroupRequested();
        });
        menu.exec(m_tabs->mapToGlobal(pos));
        return;
    }
    if (const int id = strip->groupOf(index); id > 0 && strip->hasGroup(id)) {
        const ImageStrip::Group g = strip->group(id);
        menu.addAction(g.collapsed ? tr("Expandir el grupo") : tr("Contraer el grupo"), this,
                       [strip, id, collapsed = g.collapsed] { strip->setCollapsed(id, !collapsed); });
        menu.addAction(Icons::theme({"edit-rename"}), tr("Cambiar el nombre del grupo…"), this, [this, strip, id] {
            bool ok = false;
            const QString name = QInputDialog::getText(this, tr("Grupo de imágenes"), tr("Nombre del grupo:"), QLineEdit::Normal,
                                                       strip->group(id).name, &ok);
            if (ok && !name.trimmed().isEmpty())
                strip->setGroupName(id, name.trimmed());
        });
        menu.addAction(Icons::theme({"color-management", "color-picker"}), tr("Color del borde del grupo…"), this, [this, strip, id] {
            const QColor c = QColorDialog::getColor(strip->group(id).color, this, tr("Color del borde del grupo…"));
            if (c.isValid())
                strip->setGroupColor(id, c);
        });
        menu.addAction(tr("Quitar del grupo"), this, [strip, index] { strip->removeFromGroup(index); });
        menu.addAction(tr("Desagrupar"), this, [strip, id] { strip->ungroup(id); });
        menu.addSeparator();
    }
    menu.addAction(m_act["save"]);
    menu.addAction(m_act["saveAs"]);
    menu.addSeparator();
    menu.addAction(m_act["close"]);
    menu.addAction(m_act["closeOthers"]);
    menu.addAction(m_act["closeAll"]);
    if (!doc->filePath().isEmpty()) {
        menu.addSeparator();
        menu.addAction(Icons::theme({"folder-open"}), tr("Abrir la carpeta contenedora"), this, [doc] {
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(doc->filePath()).absolutePath()));
        });
        menu.addAction(Icons::theme({"edit-copy-path", "edit-copy"}), tr("Copiar la ruta"), this,
                       [doc] { QApplication::clipboard()->setText(QDir::toNativeSeparators(doc->filePath())); });
    }
    menu.exec(m_tabs->mapToGlobal(pos));
}

bool MainWindow::eventFilter(QObject* obj, QEvent* e)
{
    // Clic central en una pestaña de imagen: cerrarla.
    if (obj == m_tabs && e->type() == QEvent::MouseButtonRelease) {
        auto* me = static_cast<QMouseEvent*>(e);
        if (me->button() == Qt::MiddleButton) {
            if (Document* d = m_docs.value(m_tabs->tabAt(me->position().toPoint())))
                closeDocument(d);
            return true;
        }
    }
    return QMainWindow::eventFilter(obj, e);
}

bool MainWindow::writeFile(Document* doc, const QString& requestedPath)
{
    finishTool();
    QString path = requestedPath;
    if (!ImageIO::isLayered(path) && !ImageIO::isAnimation(path) && doc->layerCount() > 1) {
        // Como Paint.NET: el formato no admite capas, así que primero se acopla la imagen.
        TaskDialog ask(tr("Guardar"), Icons::theme({"document-save"}), QImage(),
                       tr("Para guardar en este formato de archivo primero tiene que acoplar la imagen, lo que la "
                          "reducirá a una única capa."),
                       this);
        const int flattenChoice = ask.addChoice(Icons::theme({"image-flatten", "merge"}), tr("Acoplar"),
                                                tr("Se acoplará la imagen y, a continuación, se guardará. El "
                                                   "acoplamiento queda en el historial, pero el archivo tendrá una "
                                                   "sola capa."),
                                                true);
        // Sin acoplar: con todas las capas, en OpenRaster junto al nombre elegido.
        const QString oraPath = QFileInfo(path).absolutePath() + QLatin1Char('/') + QFileInfo(path).completeBaseName()
                                + QStringLiteral(".ora");
        const int oraChoice = ask.addChoice(Icons::theme({"document-save-as"}), tr("No acoplar (guardar como .ora)"),
                                            tr("Se guarda con todas sus capas en «%1».").arg(QFileInfo(oraPath).fileName()));
        ask.addChoice(Icons::theme({"dialog-cancel", "process-stop"}), tr("Cancelar"), tr("Cancelar la acción de guardado."));
        const int choice = ask.choose();
        if (choice == oraChoice) {
            if (QFileInfo::exists(oraPath) && oraPath != doc->filePath()
                && QMessageBox::question(this, tr("Guardar"), tr("«%1» ya existe. ¿Quieres reemplazarlo?").arg(QFileInfo(oraPath).fileName()))
                       != QMessageBox::Yes)
                return false;
            path = oraPath;
        } else if (choice == flattenChoice) {
            flattenDocument(doc);
        } else {
            return false;
        }
    }
    const QString suffix = QFileInfo(path).suffix().toLower();
    ImageIO::SaveOptions options = m_saveOptions.value(suffix);
    if (ImageIO::isAnimation(path) && !m_saveOptions.contains(suffix))
        options.loops = doc->gifLoops();
    // Fotogramas del GIF tal como quedaron en la configuración avanzada (omitidos, copias...).
    QList<Gif::Frame> edited;
    if (SaveConfigDialog::hasOptions(path)) {
        // GIF: los fotogramas son las capas o, con varias imágenes abiertas, las que se elijan.
        QList<SaveConfigDialog::ImageSource> sources;
        if (ImageIO::isAnimation(path) && m_docs.size() > 1)
            for (const Document* d : std::as_const(m_docs))
                sources << SaveConfigDialog::ImageSource{
                    d->displayName(), d->flattened(), d->layerCount() == 1 ? Gif::delayFromName(d->layer(0)->props.name, 0) : 0};
        QList<Gif::Frame> frames;
        if (ImageIO::isAnimation(path) && doc->layerCount() > 1)
            frames = ImageIO::animationFrames(doc, 0, true);
        else if (!sources.isEmpty())
            frames << Gif::Frame{doc->flattened(), 0};
        SaveConfigDialog dlg(doc->flattened(), path, options, this, frames, sources);
        if (dlg.exec() != QDialog::Accepted)
            return false;
        options = dlg.options();
        edited = dlg.animationFrames();
        m_saveOptions.insert(suffix, options);
    }
    QString error;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = edited.isEmpty() ? ImageIO::save(doc, path, options, &error)
                                     : ImageIO::saveAnimation(edited, path, options, &error);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::critical(this, tr("No se pudo guardar"), tr("No se pudo guardar «%1»:\n%2").arg(path, error));
        return false;
    }
    if (m_collab->isGuestDocument(doc)) {
        statusBar()->showMessage(tr("Copia exportada en %1. La imagen de la sesión la guarda el anfitrión.").arg(QDir::toNativeSeparators(path)),
                                 8000);
        m_collab->markExported(doc);
        return true;
    }
    doc->setFilePath(path);
    doc->markSaved();
    AppState::instance()->addRecentFile(path, doc == m_doc ? m_canvas->composite() : doc->flattened());
    m_lastDir = QFileInfo(path).absolutePath();
    updateTab(doc);
    updateTitle();
    return true;
}

QPrinter* MainWindow::printer()
{
    // Una sola impresora para toda la sesión: conserva la configuración de página.
    if (!m_printer) {
        m_printer = std::make_unique<QPrinter>(QPrinter::HighResolution);
        m_printer->setDocName(QStringLiteral("Kaint"));
    }
    return m_printer.get();
}

void MainWindow::renderPrint(QPrinter* printer, const QImage& image, double dpi, bool actualSize)
{
    QPainter p(printer);
    const QRect page = p.viewport();
    QSizeF size = image.size();
    if (actualSize && dpi > 0) {
        size *= printer->resolution() / dpi;
        if (size.width() > page.width() || size.height() > page.height())
            size = size.scaled(page.size(), Qt::KeepAspectRatio);
    } else {
        size = size.scaled(page.size(), Qt::KeepAspectRatio);
    }
    const QRectF target(page.x() + (page.width() - size.width()) / 2, page.y() + (page.height() - size.height()) / 2,
                        size.width(), size.height());
    p.setRenderHint(QPainter::SmoothPixmapTransform, size.width() < image.width());
    p.drawImage(target, image);
}

void MainWindow::print()
{
    // Vista previa de impresión con el ajuste a la página (como el asistente de impresión de
    // Paint.NET); desde ella se imprime o se configura la página.
    if (!m_doc)
        return;
    finishTool();
    const QImage image = m_doc->flattened();
    const double dpi = m_doc->dpi();
    QPrinter* pr = printer();
    if (!m_doc->filePath().isEmpty())
        pr->setDocName(QFileInfo(m_doc->filePath()).fileName());
    QPrintPreviewDialog preview(pr, this);
    preview.setWindowTitle(tr("Imprimir"));
#ifdef Q_OS_WIN
    // "Configurar página" (en Archivo ya no está): el diálogo de Windows de Qt se cierra de golpe si
    // no hay una impresora predeterminada que se pueda usar (usa su configuración sin comprobarla),
    // así que solo se ofrece con una.
    if (pr->outputFormat() != QPrinter::NativeFormat || QPrinterInfo::defaultPrinter().isNull()) {
        const QString pageSetupText = QCoreApplication::translate("QPrintPreviewDialog", "Page setup");
        for (QToolBar* bar : preview.findChildren<QToolBar*>())
            for (QAction* a : bar->actions())
                if (a->text() == pageSetupText || a->toolTip() == pageSetupText)
                    a->setVisible(false);
    }
#endif
    connect(&preview, &QPrintPreviewDialog::paintRequested, this,
            [this, image, dpi](QPrinter* target) { renderPrint(target, image, dpi, m_printActualSize); });
    if (auto* bar = preview.findChild<QToolBar*>()) {
        auto* fit = new QComboBox(bar);
        fit->addItem(tr("Ajustar a la página"));
        fit->addItem(tr("Tamaño real (%1 ppp)").arg(QLocale().toString(dpi, 'f', 0)));
        fit->setCurrentIndex(m_printActualSize ? 1 : 0);
        bar->addSeparator();
        bar->addWidget(fit);
        connect(fit, &QComboBox::currentIndexChanged, &preview, [this, &preview](int i) {
            m_printActualSize = i == 1;
            if (auto* widget = preview.findChild<QPrintPreviewWidget*>())
                widget->updatePreview();
        });
    }
    preview.resize(900, 700);
    preview.exec();
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    finishTool();
    if (!maybeSaveAll(m_docs, true) || !m_collab->leave(true)) {
        e->ignore();
        return;
    }
    QSettings s;
    QStringList session;
    for (Document* d : std::as_const(m_docs))
        if (!d->filePath().isEmpty())
            session << d->filePath();
    s.setValue(QStringLiteral("session/files"), session);
    if (m_tools->current())
        s.setValue(QStringLiteral("tools/current"), int(m_tools->current()->id()));
    s.setValue(QStringLiteral("window/geometry"), saveGeometry());
    s.setValue(QStringLiteral("view/pixelGrid"), m_act["pixelGrid"]->isChecked());
    m_workspace->saveState();
    AppState::instance()->save();
    Recovery::clear();
    e->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls() || e->mimeData()->hasImage())
        e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e)
{
    QStringList files;
    for (const QUrl& url : e->mimeData()->urls())
        if (url.isLocalFile())
            files << url.toLocalFile();
    // Los filtros de PhotoCraft, los looks y ajustes de LightCraft y los paquetes de paint.js que se
    // sueltan se instalan.
    QStringList plugins;
    for (const QString& f : std::as_const(files))
        if (CraftPlugins::isCraftFile(f) || PaintJs::isPackage(f))
            plugins << f;
    if (!plugins.isEmpty()) {
        for (const QString& f : std::as_const(plugins))
            files.removeAll(f);
        installDroppedPlugins(plugins);
    }
    QImage image;
    if (files.isEmpty() && e->mimeData()->hasImage())
        image = qvariant_cast<QImage>(e->mimeData()->imageData());
    e->acceptProposedAction();
    // La pregunta se hace después de terminar el arrastre: con un diálogo modal dentro de dropEvent,
    // la aplicación de origen se quedaría esperando.
    if (!files.isEmpty() || !image.isNull())
        QTimer::singleShot(0, this, [this, files, image] { handleDroppedImages(files, image); });
}

void MainWindow::handleDroppedImages(const QStringList& files, const QImage& image)
{
    if (!m_doc) {
        if (!files.isEmpty())
            openFiles(files);
        else
            addDocument(Document::createFromImage(image, tr("Fondo")));
        return;
    }
    const bool several = files.size() > 1;
    TaskDialog dlg(tr("Arrastrar y soltar"), Icons::theme({"insert-image", "document-import"}), QImage(),
                   several ? tr("¿Qué quiere hacer con los archivos?") : tr("¿Qué quiere hacer con el archivo?"), this);
    const int open = dlg.addChoice(Icons::theme({"document-open", "folder-open"}),
                                   several ? tr("Abrir los archivos") : tr("Abrir el archivo"),
                                   several ? tr("Abre las imágenes.") : tr("Abre la imagen."), true);
    const int layer = dlg.addChoice(Icons::theme({"layer-import", "document-import", "insert-image"}),
                                    several ? tr("Agregar capas") : tr("Agregar capa"),
                                    several ? tr("Carga las imágenes y las agrega como capas nuevas en la imagen actual.")
                                            : tr("Carga la imagen y la agrega como una nueva capa en la imagen actual."));
    dlg.addChoice(Icons::theme({"dialog-cancel", "edit-undo"}), tr("Cancelar"), QString());
    const int answer = dlg.choose();
    if (answer == open) {
        if (!files.isEmpty())
            openFiles(files);
        else
            addDocument(Document::createFromImage(image, tr("Fondo")));
    } else if (answer == layer) {
        finishTool();
        if (files.isEmpty()) {
            importImageAsLayer(image, m_doc->nextLayerName());
            return;
        }
        for (const QString& path : files) {
            QString error;
            QApplication::setOverrideCursor(Qt::WaitCursor);
            const QImage img = readLayerImage(path, &error);
            QApplication::restoreOverrideCursor();
            if (img.isNull()) {
                QMessageBox::critical(this, tr("No se pudo importar"), tr("No se pudo abrir «%1»:\n%2").arg(path, error));
                continue;
            }
            if (!importImageAsLayer(img, QFileInfo(path).completeBaseName()))
                break;
            m_lastDir = QFileInfo(path).absolutePath();
        }
    }
}

void MainWindow::installDroppedPlugins(const QStringList& files)
{
    PluginManager* pm = PluginManager::instance();
    bool replace = false;
    const QStringList existing = pm->installedAlready(files);
    if (!existing.isEmpty()) {
        QStringList names;
        for (const QString& f : existing)
            names << QFileInfo(f).fileName();
        const auto answer = QMessageBox::question(
            this, tr("Añadir complementos"),
            tr("Ya hay complementos con estos nombres:\n\n%1\n\n¿Quieres sustituirlos?").arg(names.join(QLatin1Char('\n'))),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::No);
        if (answer == QMessageBox::Cancel)
            return;
        replace = answer == QMessageBox::Yes;
    }
    QStringList errors;
    const QStringList copied = pm->install(files, replace, &errors);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    pm->scan();
    // Los que se sustituyeron estando cargados, con la versión nueva.
    for (const QString& f : existing)
        if (replace)
            pm->reload(QFileInfo(pm->userPath() + QLatin1Char('/') + QFileInfo(f).fileName()).absoluteFilePath());
    QApplication::restoreOverrideCursor();
    QStringList problems = errors;
    QStringList added, formats, webOnly;
    for (const PluginInfo& p : pm->plugins())
        if (copied.contains(p.id)) {
            if (p.effects.isEmpty() && p.fileTypes.isEmpty() && p.webOnlyEffects.isEmpty()) {
                problems << QStringLiteral("%1: %2").arg(p.file, p.errors.join(QLatin1Char(' ')));
                continue;
            }
            added << p.effects;
            formats << p.fileTypes;
            webOnly << p.webOnlyEffects;
        }
    if (!problems.isEmpty()) {
        QMessageBox::warning(this, tr("Añadir complementos"), tr("No se pudieron añadir:\n\n%1").arg(problems.join(QLatin1Char('\n'))));
        return;
    }
    QStringList lines;
    if (!added.isEmpty())
        lines << tr("Efectos añadidos (en Ajustes o Efectos): %1").arg(added.join(QStringLiteral(", ")));
    if (!formats.isEmpty())
        lines << tr("Formatos añadidos en Abrir y Guardar como: %1").arg(formats.join(QStringLiteral(", ")));
    if (!webOnly.isEmpty()) {
        // Lo que no se puede usar aquí se dice en una ventana: si no, parecería que no se instaló.
        lines << tr("Solo funcionan en la versión web de Kaint, porque abren una ventana propia hecha para el navegador: %1")
                     .arg(webOnly.join(QStringLiteral(", ")));
        QMessageBox::information(this, tr("Añadir complementos"), lines.join(QStringLiteral("\n\n")));
    } else if (!lines.isEmpty()) {
        showToolHint(lines.join(QStringLiteral(" · ")));
    }
}

// ---------------------------------------------------------------------------
// Edición
// ---------------------------------------------------------------------------

void MainWindow::finishTool()
{
    if (m_tools && m_tools->current())
        m_tools->current()->finish();
}

void MainWindow::push(QUndoCommand* cmd, const QString& icon)
{
    HistoryIcons::registerIcon(cmd->text(), icon);
    m_doc->undoStack()->push(cmd);
}

void MainWindow::undo()
{
    prepareHistoryJump();
    m_undoGroup->undo();
}

void MainWindow::redo()
{
    prepareHistoryJump();
    m_undoGroup->redo();
}

void MainWindow::updateCollabActions()
{
    if (!m_act.contains(QStringLiteral("collabHost")))
        return;
    const bool active = m_collab->isActive();
    for (const char* id : {"collabHost", "collabHostLan"})
        m_act[QLatin1String(id)]->setEnabled(m_doc && !active);
    for (const char* id : {"collabJoin", "collabJoinLan"})
        m_act[QLatin1String(id)]->setEnabled(!active);
    m_act["collabWindow"]->setEnabled(active);
    m_act["collabChat"]->setEnabled(m_collab->chatAvailable());
    m_act["collabChat"]->setVisible(m_collab->settings().chat);
    m_act["collabHistory"]->setEnabled(active);
    m_act["collabHistory"]->setVisible(active);
    m_act["collabLeave"]->setEnabled(active);
    m_act["collabLeave"]->setText(m_collab->isHosting() ? tr("&Terminar la sesión") : tr("&Salir de la sesión"));
}

void MainWindow::prepareHistoryJump()
{
    if (m_tools && m_tools->current())
        m_tools->current()->beforeHistoryJump();
}

void MainWindow::syncLayerWithHistory()
{
    // Al deshacer o rehacer se selecciona la capa que cambió ese paso (el más cercano al punto
    // al que se llega).
    if (!m_doc)
        return;
    QUndoStack* stack = m_doc->undoStack();
    const int now = stack->index();
    const int before = m_historyIndex.value(stack, now);
    m_historyIndex.insert(stack, now);
    // Lo que llega de la sesión colaborativa no cambia la capa en la que se está.
    if (now == before || m_collab->applyingRemote())
        return;
    const QUndoCommand* step = now < before ? stack->command(now) : stack->command(now - 1);
    const int index = m_doc->indexOf(History::layerOf(step));
    if (index >= 0 && index != m_doc->activeIndex())
        m_doc->setActiveIndex(index);
}

void MainWindow::syncToolWithHistory()
{
    // Al volver a un paso que su herramienta puede retomar (degradado), se activa esa
    // herramienta para seguir editándolo.
    if (m_syncingHistory || !m_doc || !m_tools || m_collab->applyingRemote())
        return;
    m_syncingHistory = true;
    const QUndoStack* stack = m_doc->undoStack();
    const QUndoCommand* top = stack->index() > 0 ? stack->command(stack->index() - 1) : nullptr;
    if (const auto* step = dynamic_cast<const EditableStep*>(top);
        step && m_tools->current() && m_tools->current()->id() != step->tool())
        m_tools->select(step->tool());
    if (m_tools->current())
        m_tools->current()->historyChanged();
    m_syncingHistory = false;
}

void MainWindow::copy(bool merged)
{
    if (!m_doc)
        return;
    if (inGifEditor()) {
        m_gifEditor->copyFrames();
        return;
    }
    finishTool();
    const QImage source = merged ? m_doc->flattened() : m_doc->activeLayer()->image;
    const Selection& selection = m_doc->selection();
    const QImage out = Raster::extractSelection(source, selection).convertToFormat(QImage::Format_ARGB32);
    // Además de la imagen, un PNG: Discord, los navegadores y otros programas leen ese formato y
    // conservan la transparencia (con el mapa de bits de Windows, CF_DIB, sale en blanco o negro).
    // Paint.NET hace lo mismo.
    auto* data = new QMimeData;
    data->setImageData(out);
    if (!selection.isEmpty() && !selection.isRectangular())
        data->setData(kSelectionShapeMime,
                      encodeSelectionShape(selection.placed(-selection.bounds().topLeft(), out.size()), out.size()));
    QByteArray png;
    QBuffer buffer(&png);
    if (buffer.open(QIODevice::WriteOnly) && out.save(&buffer, "PNG")) {
        data->setData(QStringLiteral("image/png"), png);
#ifdef Q_OS_WIN
        data->setData(QStringLiteral("application/x-qt-windows-mime;value=\"PNG\""), png);
#endif
    }
    QApplication::clipboard()->setMimeData(data);
    m_copyOrigin = m_doc->selection().isEmpty() ? QPoint(0, 0) : m_doc->selection().bounds().topLeft();
    m_copySignature = imageSignature(out);
}

QPoint MainWindow::pastePosition(const QImage& image) const
{
    // Donde se copió, si el portapapeles sigue siendo esa copia y ese sitio se ve entero; si no,
    // el sitio visible más cercano (o la esquina de lo visible para lo que viene de fuera).
    const QRect visible = m_canvas->visibleImageRect();
    QPoint pos = imageSignature(image) == m_copySignature ? m_copyOrigin : visible.topLeft();
    if (!visible.isEmpty() && !visible.contains(QRect(pos, image.size()))) {
        auto fit = [](int p, int size, int lo, int length) {
            return size >= length ? lo : std::clamp(p, lo, lo + length - size);
        };
        pos = QPoint(fit(pos.x(), image.width(), visible.left(), visible.width()),
                     fit(pos.y(), image.height(), visible.top(), visible.height()));
    }
    pos.setX(std::clamp(pos.x(), 0, std::max(0, m_doc->size().width() - image.width())));
    pos.setY(std::clamp(pos.y(), 0, std::max(0, m_doc->size().height() - image.height())));
    return pos;
}

void MainWindow::cut()
{
    if (!m_doc)
        return;
    if (inGifEditor()) {
        m_gifEditor->cutFrames();
        return;
    }
    copy(false);
    eraseSelection(tr("Cortar"));
}

void MainWindow::eraseSelection(const QString& text)
{
    if (!m_doc)
        return;
    finishTool();
    const LayerPtr layer = m_doc->activeLayerPtr();
    const QImage before = layer->image;
    const Selection selection = m_doc->selection();
    layer->image.detach();
    Raster::eraseSelection(layer->image, selection);
    const QRect r = selection.isEmpty() ? m_doc->rect() : selection.bounds();
    m_doc->notifyPixelsChanged(layer.get(), r);
    if (selection.isEmpty()) {
        pushPixelPatch(m_doc, layer, before, r, text, QStringLiteral("edit-delete"));
        return;
    }
    auto* cmd = new QUndoCommand(text);
    new PixelPatchCommand(m_doc, layer, before, r, text, cmd);
    new SelectionCommand(m_doc, selection, Selection(), text, -1, cmd);
    push(cmd, QStringLiteral("edit-delete"));
}

void MainWindow::fillSelection()
{
    if (!m_doc)
        return;
    finishTool();
    const LayerPtr layer = m_doc->activeLayerPtr();
    const QImage before = layer->image;
    layer->image.detach();
    const QRect r = m_doc->selection().isEmpty() ? m_doc->rect() : m_doc->selection().bounds();
    QImage overlay(r.size(), kLayerFormat);
    overlay.fill(AppState::instance()->primary());
    Raster::compositeRegion(layer->image, before, overlay, r, BlendMode::Normal, m_doc->selection());
    m_doc->notifyPixelsChanged(layer.get(), r);
    pushPixelPatch(m_doc, layer, before, r, tr("Rellenar la selección"), QStringLiteral("fill-color"));
}

QImage MainWindow::clipboardImage() const
{
    const QMimeData* md = QApplication::clipboard()->mimeData();
    if (!md)
        return {};
    if (md->hasImage()) {
        const QImage img = qvariant_cast<QImage>(md->imageData());
        if (!img.isNull())
            return img.convertToFormat(kLayerFormat);
    }
    for (const QUrl& url : md->urls()) {
        if (!url.isLocalFile())
            continue;
        QImageReader reader(url.toLocalFile());
        reader.setAutoTransform(true);
        const QImage img = reader.read();
        if (!img.isNull())
            return img.convertToFormat(kLayerFormat);
    }
    return {};
}

bool MainWindow::ensureCanvasFits(const QImage& image, const QString& title, bool importing)
{
    const QSize current = m_doc->size();
    const QSize size = image.size();
    if (size.width() <= current.width() && size.height() <= current.height())
        return true;
    TaskDialog dlg(title, Icons::theme({"edit-paste"}), image,
                   importing ? tr("La imagen que está importando es más grande que el tamaño del lienzo. ¿Qué desea hacer?")
                             : tr("La imagen que está pegando es más grande que el tamaño del lienzo. ¿Qué desea hacer?"),
                   this);
    const int expand = dlg.addChoice(Icons::theme({"transform-crop-and-resize", "transform-scale"}), tr("Ampliar lienzo"),
                                     tr("Expande automáticamente el lienzo para que la imagen quepa."), true);
    const int keep = dlg.addChoice(Icons::theme({"transform-crop"}), tr("Mantener el tamaño del lienzo"),
                                   tr("No expande el lienzo. Tendrá que mover la imagen para asegurarse de que la "
                                      "parte que desea se encuentra en los límites del lienzo."));
    dlg.addChoice(Icons::theme({"dialog-cancel", "edit-undo"}), tr("Cancelar"),
                  importing ? tr("Cancela la importación.") : tr("Cancela la acción de pegado."));
    const int answer = dlg.choose();
    if (answer == expand) {
        resizeCanvasTo(current.expandedTo(size), Qt::AlignTop | Qt::AlignLeft, tr("Ampliar lienzo"));
        return true;
    }
    return answer == keep;
}

void MainWindow::paste()
{
    if (inGifEditor()) {
        m_gifEditor->pasteFrames();
        return;
    }
    finishTool();
    const QImage img = clipboardImage();
    if (img.isNull()) {
        QMessageBox::information(this, tr("Pegar"), tr("El portapapeles no contiene ninguna imagen."));
        return;
    }
    if (!m_doc) {
        pasteIntoNewImage();
        return;
    }
    if (!ensureCanvasFits(img, tr("Pegar")))
        return;
    startPaste(img, tr("Pegar"));
    m_tools->select(ToolId::MoveSelected);
}

void MainWindow::startPaste(const QImage& img, const QString& text)
{
    // La forma de lo copiado en Kaint; si viene de fuera con transparencia alrededor, la de lo que
    // tiene píxeles, mezclado con lo de debajo (si no, lo transparente dejaba un hueco).
    const QMimeData* md = QApplication::clipboard()->mimeData();
    Selection shape;
    bool blend = false;
    if (md && md->hasFormat(kSelectionShapeMime))
        shape = decodeSelectionShape(md->data(kSelectionShapeMime), img.size());
    if (shape.isEmpty() && imageSignature(img) != m_copySignature) {
        shape = shapeFromAlpha(img);
        blend = !shape.isEmpty();
    }
    MoveSelectedTool::startPaste(m_doc, img, pastePosition(img), text, shape, blend);
}

void MainWindow::pasteIntoNewLayer()
{
    if (inGifEditor()) {
        m_gifEditor->pasteFrames();
        return;
    }
    finishTool();
    const QImage img = clipboardImage();
    if (!m_doc || img.isNull())
        return paste();
    if (!ensureCanvasFits(img, tr("Pegar en capa nueva")))
        return;
    const QString text = tr("Pegar en capa nueva");
    HistoryIcons::registerIcon(text, QStringLiteral("edit-paste"));
    m_doc->undoStack()->beginMacro(text);
    m_doc->undoStack()->push(new InsertLayerCommand(m_doc, m_doc->activeIndex() + 1,
                                                    std::make_shared<Layer>(m_doc->nextLayerName(), m_doc->size()), text));
    startPaste(img, text);
    m_doc->undoStack()->endMacro();
    m_tools->select(ToolId::MoveSelected);
}

void MainWindow::pasteIntoNewImage()
{
    finishTool();
    const QImage img = clipboardImage();
    if (img.isNull()) {
        QMessageBox::information(this, tr("Pegar"), tr("El portapapeles no contiene ninguna imagen."));
        return;
    }
    auto* doc = Document::createFromImage(img, tr("Fondo"));
    doc->setHistoryRootLabel(tr("Pegar en una imagen nueva"));
    addDocument(doc);
}

void MainWindow::selectAll()
{
    if (!m_doc)
        return;
    finishTool();
    push(new SelectionCommand(m_doc, m_doc->selection(), Selection::fromRect(m_doc->rect(), m_doc->size()),
                              tr("Seleccionar todo")),
         QStringLiteral("edit-select-all"));
}

void MainWindow::deselect()
{
    // Con algo editable pendiente (forma, texto...), Ctrl+D solo lo termina; con las
    // herramientas de selección, además anula la selección.
    if (Tool* t = m_tools->current(); t && t->isBusy()) {
        finishTool();
        if (!t->highlightSelection())
            return;
    }
    if (!m_doc || m_doc->selection().isEmpty())
        return;
    push(new SelectionCommand(m_doc, m_doc->selection(), Selection(), tr("Anular la selección")),
         QStringLiteral("edit-select-none"));
}

void MainWindow::copySelection()
{
    if (!m_doc || m_doc->selection().isEmpty())
        return;
    finishTool();
    QApplication::clipboard()->setText(m_doc->selection().toJson());
}

void MainWindow::pasteSelection(SelectionMode mode)
{
    if (!m_doc)
        return;
    finishTool();
    bool ok = false;
    const Selection pasted = Selection::fromJson(QApplication::clipboard()->text(), m_doc->size(), &ok);
    if (!ok) {
        QMessageBox::information(this, tr("Pegar selección"),
                                 tr("El portapapeles no contiene una selección (copiada con «Copiar la selección»)."));
        return;
    }
    const Selection result = mode == SelectionMode::Replace ? pasted : m_doc->selection().combined(pasted, mode);
    push(new SelectionCommand(m_doc, m_doc->selection(), result, tr("Pegar selección")),
         QStringLiteral("edit-paste"));
}

void MainWindow::saveSelectionAsShape()
{
    if (!m_doc || m_doc->selection().isEmpty())
        return;
    finishTool();
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Guardar como forma"), tr("Nombre de la forma:"), QLineEdit::Normal,
                                               tr("Mi forma"), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    QString file, error;
    if (!CustomShapes::save(name, m_doc->selection().path(), &file, &error)) {
        QMessageBox::warning(this, tr("Guardar como forma"), error);
        return;
    }
    emit AppState::instance()->customShapesChanged();
    statusBar()->showMessage(tr("Forma «%1» guardada: está en la herramienta Formas, entre las personalizadas.").arg(name), 7000);
}

void MainWindow::reshapeSelection(SelectionReshape kind)
{
    // Ampliar, reducir o difuminar el borde de la selección tantos píxeles.
    if (!m_doc || m_doc->selection().isEmpty())
        return;
    finishTool();
    const QString text = kind == SelectionReshape::Grow     ? tr("Ampliar la selección")
                         : kind == SelectionReshape::Shrink ? tr("Reducir la selección")
                                                            : tr("Difuminar la selección");
    bool ok = false;
    const int radius = QInputDialog::getInt(this, text, tr("Radio (píxeles):"), 4, 1, 1000, 1, &ok);
    if (!ok)
        return;
    const Selection& before = m_doc->selection();
    const Selection after = kind == SelectionReshape::Grow     ? before.grown(radius)
                            : kind == SelectionReshape::Shrink ? before.grown(-radius)
                                                               : before.feathered(radius);
    push(new SelectionCommand(m_doc, before, after, text), QStringLiteral("edit-select"));
}

void MainWindow::invertSelection()
{
    if (!m_doc || m_doc->selection().isEmpty())
        return;
    finishTool();
    push(new SelectionCommand(m_doc, m_doc->selection(), m_doc->selection().inverted(m_doc->size()),
                              tr("Invertir la selección")),
         QStringLiteral("edit-select-invert"));
}

// ---------------------------------------------------------------------------
// Imagen
// ---------------------------------------------------------------------------

void MainWindow::transformDocument(const QString& text, const QString& icon, QSize newSize,
                                   const std::function<QImage(const QImage&, int)>& fn, double dpi)
{
    finishTool();
    DocumentState before = DocumentState::capture(m_doc);
    DocumentState after = before;
    after.size = newSize;
    if (dpi > 0)
        after.dpi = dpi;
    for (int i = 0; i < after.images.size(); ++i)
        after.images[i] = fn(before.images[i], i).convertToFormat(kLayerFormat);
    after.selection = Selection();
    push(new DocumentStateCommand(m_doc, before, after, text), icon);
}

void MainWindow::cropToSelection()
{
    if (!m_doc || m_doc->selection().isEmpty())
        return;
    const Selection sel = m_doc->selection();
    const QRect b = sel.bounds();
    const bool rect = sel.isRectangular();
    transformDocument(tr("Recortar según la selección"), QStringLiteral("transform-crop"), b.size(),
                      [&](const QImage& img, int) { return rect ? img.copy(b) : Raster::extractSelection(img, sel); });
}

void MainWindow::resizeImage()
{
    if (!m_doc)
        return;
    finishTool();
    ResizeImageDialog dlg(m_doc->size(), m_doc->dpi(), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const QSize size = dlg.imageSize();
    if (size == m_doc->size()) {
        if (!qFuzzyCompare(dlg.dpi(), m_doc->dpi()))
            transformDocument(tr("Cambiar el tamaño"), QStringLiteral("transform-scale"), size,
                              [](const QImage& img, int) { return img; }, dlg.dpi());
        return;
    }
    const Resample::Method method = dlg.method();
    const bool gamma = dlg.gammaCorrect();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    transformDocument(tr("Cambiar el tamaño"), QStringLiteral("transform-scale"), size,
                      [&](const QImage& img, int) { return Resample::scale(img, size, method, gamma); }, dlg.dpi());
    QApplication::restoreOverrideCursor();
}

void MainWindow::resizeCanvas()
{
    if (!m_doc)
        return;
    finishTool();
    CanvasSizeDialog dlg(m_doc->size(), m_doc->dpi(), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    if (dlg.canvasSize() == m_doc->size() && qFuzzyCompare(dlg.dpi(), m_doc->dpi()))
        return;
    QColor fill = Qt::transparent;
    if (dlg.fill() == CanvasSizeDialog::Fill::Primary)
        fill = AppState::instance()->primary();
    else if (dlg.fill() == CanvasSizeDialog::Fill::Secondary)
        fill = AppState::instance()->secondary();
    resizeCanvasTo(dlg.canvasSize(), dlg.anchor(), tr("Tamaño del lienzo"), dlg.dpi(), fill);
}

void MainWindow::resizeCanvasTo(QSize size, Qt::Alignment anchor, const QString& text, double dpi, QColor background)
{
    const QSize old = m_doc->size();
    const int dx = (anchor & Qt::AlignLeft) ? 0 : (anchor & Qt::AlignRight) ? size.width() - old.width()
                                                                            : (size.width() - old.width()) / 2;
    const int dy = (anchor & Qt::AlignTop) ? 0 : (anchor & Qt::AlignBottom) ? size.height() - old.height()
                                                                            : (size.height() - old.height()) / 2;
    // Lo nuevo de la capa de abajo, con ese color (sin él, el secundario); las demás, transparentes.
    const QColor fill = background.isValid() ? background : AppState::instance()->secondary();
    transformDocument(text, QStringLiteral("transform-crop-and-resize"), size, [&](const QImage& img, int index) {
        QImage out(size, kLayerFormat);
        if (index == 0)
            out.fill(fill);
        else
            out.fill(kTransparent);
        Raster::blit(out, img.convertToFormat(kLayerFormat), QPoint(dx, dy));
        return out;
    }, dpi);
}

void MainWindow::flipImage(bool horizontal)
{
    if (!m_doc)
        return;
    transformDocument(horizontal ? tr("Voltear horizontalmente") : tr("Voltear verticalmente"),
                      horizontal ? QStringLiteral("object-flip-horizontal") : QStringLiteral("object-flip-vertical"),
                      m_doc->size(), [horizontal](const QImage& img, int) { return flipped(img, horizontal); });
}

void MainWindow::rotateImage(int degrees)
{
    if (!m_doc)
        return;
    const QSize size = degrees == 180 ? m_doc->size() : m_doc->size().transposed();
    const QString text = degrees == 90 ? tr("Girar 90° en sentido horario")
                       : degrees == 180 ? tr("Girar 180°") : tr("Girar 90° en sentido antihorario");
    transformDocument(text, degrees == 270 ? QStringLiteral("object-rotate-left") : QStringLiteral("object-rotate-right"),
                      size, [degrees](const QImage& img, int) { return img.transformed(QTransform().rotate(degrees)); });
}

void MainWindow::colorProfile()
{
    if (!m_doc)
        return;
    finishTool();
    ColorProfileDialog dlg(m_doc->colorSpace(), this);
    if (dlg.exec() != QDialog::Accepted || dlg.result() == ColorProfileDialog::Result::Cancel)
        return;
    const QColorSpace target = dlg.profile();
    DocumentState before = DocumentState::capture(m_doc);
    DocumentState after = before;
    after.colorSpace = target;
    if (dlg.result() == ColorProfileDialog::Result::Convert) {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const QColorTransform t = m_doc->colorSpace().transformationToColorSpace(target);
        for (QImage& img : after.images) {
            QImage converted = img.copy();
            converted.applyColorTransform(t);
            img = converted;
        }
        QApplication::restoreOverrideCursor();
        push(new DocumentStateCommand(m_doc, before, after, tr("Convertir perfil de colores")),
             QStringLiteral("color-management"));
    } else {
        push(new DocumentStateCommand(m_doc, before, after, tr("Asignar perfil de colores")),
             QStringLiteral("color-management"));
    }
}

void MainWindow::editAnimation()
{
    // Duración de los fotogramas de un GIF (en el nombre de cada capa).
    if (!m_doc)
        return;
    finishTool();
    AnimationDialog dlg(m_doc->layers(), m_saveOptions.value(QStringLiteral("gif")).frameDelay, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const QHash<int, QString> renamed = dlg.renamedLayers();
    if (renamed.isEmpty())
        return;
    const QString text = tr("Duración de los fotogramas");
    HistoryIcons::registerIcon(text, QStringLiteral("media-playback-start"));
    m_doc->undoStack()->beginMacro(text);
    for (auto it = renamed.cbegin(); it != renamed.cend(); ++it) {
        const LayerProps before = m_doc->layer(it.key())->props;
        LayerProps after = before;
        after.name = it.value();
        m_doc->undoStack()->push(new LayerPropsCommand(m_doc, it.key(), before, after, text));
    }
    m_doc->undoStack()->endMacro();
}

void MainWindow::flatten()
{
    flattenDocument(m_doc);
}

void MainWindow::flattenDocument(Document* doc)
{
    if (!doc || doc->layerCount() < 2)
        return;
    finishTool();
    DocumentState before = DocumentState::capture(doc);
    DocumentState after;
    auto layer = std::make_shared<Layer>(doc->layer(0)->props.name, doc->flattened());
    after.size = before.size;
    after.layers = {layer};
    after.images = {layer->image};
    after.props = {layer->props};
    after.active = 0;
    after.selection = before.selection;
    HistoryIcons::registerIcon(tr("Acoplar"), QStringLiteral("image-flatten"));
    doc->undoStack()->push(new DocumentStateCommand(doc, before, after, tr("Acoplar")));
}

// ---------------------------------------------------------------------------
// Capas
// ---------------------------------------------------------------------------

void MainWindow::addLayer()
{
    if (!m_doc)
        return;
    finishTool();
    push(new InsertLayerCommand(m_doc, m_doc->activeIndex() + 1,
                                std::make_shared<Layer>(m_doc->nextLayerName(), m_doc->size()), tr("Agregar nueva capa")),
         QStringLiteral("layer-new"));
}

void MainWindow::deleteLayer()
{
    if (!m_doc || m_doc->layerCount() < 2)
        return;
    finishTool();
    push(new RemoveLayerCommand(m_doc, m_doc->activeIndex(), tr("Eliminar la capa")), QStringLiteral("layer-delete"));
}

void MainWindow::duplicateLayer()
{
    if (!m_doc)
        return;
    finishTool();
    auto copy = std::make_shared<Layer>(*m_doc->activeLayer());
    copy->props.name = tr("%1 copia").arg(copy->props.name);
    push(new InsertLayerCommand(m_doc, m_doc->activeIndex() + 1, copy, tr("Duplicar la capa")),
         QStringLiteral("layer-duplicate"));
}

void MainWindow::mergeLayerDown()
{
    if (!m_doc || m_doc->activeIndex() <= 0)
        return;
    finishTool();
    const int i = m_doc->activeIndex();
    DocumentState before = DocumentState::capture(m_doc);
    DocumentState after = before;
    const LayerProps& top = before.props[i];
    QImage merged = before.images[i - 1].copy();
    if (top.visible)
        blendImage(merged, QPoint(0, 0), before.images[i], top.blend, top.opacity);
    after.images[i - 1] = merged;
    after.layers.removeAt(i);
    after.images.removeAt(i);
    after.props.removeAt(i);
    after.active = i - 1;
    push(new DocumentStateCommand(m_doc, before, after, tr("Combinar capa con la inferior")), QStringLiteral("layer-merge-down"));
}

void MainWindow::importLayer()
{
    if (!m_doc)
        return;
    finishTool();
    const QString path = QFileDialog::getOpenFileName(this, tr("Importar desde archivo"), m_lastDir, ImageIO::openFilter());
    if (path.isEmpty())
        return;
    QString error;
    const QImage img = readLayerImage(path, &error);
    if (img.isNull()) {
        QMessageBox::critical(this, tr("No se pudo importar"), error);
        return;
    }
    importImageAsLayer(img, QFileInfo(path).completeBaseName());
}

bool MainWindow::importImageAsLayer(const QImage& image, const QString& name)
{
    if (!m_doc || image.isNull())
        return false;
    if (!ensureCanvasFits(image, tr("Importar desde archivo"), true))
        return false;
    auto layer = std::make_shared<Layer>(name, m_doc->size());
    Raster::blit(layer->image, image.convertToFormat(kLayerFormat), QPoint(0, 0));
    push(new InsertLayerCommand(m_doc, m_doc->activeIndex() + 1, layer, tr("Importar desde archivo")),
         QStringLiteral("document-import"));
    return true;
}

void MainWindow::flipLayer(bool horizontal)
{
    if (!m_doc)
        return;
    finishTool();
    const LayerPtr layer = m_doc->activeLayerPtr();
    const QImage before = layer->image;
    layer->image = flipped(before, horizontal).convertToFormat(kLayerFormat);
    m_doc->notifyPixelsChanged(layer.get(), m_doc->rect());
    pushPixelPatch(m_doc, layer, before, m_doc->rect(),
                   horizontal ? tr("Voltear capa horizontalmente") : tr("Voltear capa verticalmente"),
                   horizontal ? QStringLiteral("object-flip-horizontal") : QStringLiteral("object-flip-vertical"));
}

void MainWindow::rotateLayer180()
{
    if (!m_doc)
        return;
    finishTool();
    const LayerPtr layer = m_doc->activeLayerPtr();
    const QImage before = layer->image;
    layer->image = before.transformed(QTransform().rotate(180)).convertToFormat(kLayerFormat);
    m_doc->notifyPixelsChanged(layer.get(), m_doc->rect());
    pushPixelPatch(m_doc, layer, before, m_doc->rect(), tr("Girar capa 180°"), QStringLiteral("object-rotate-right"));
}

void MainWindow::moveLayer(int delta)
{
    if (!m_doc)
        return;
    const int from = m_doc->activeIndex();
    const int to = from + delta;
    if (to < 0 || to >= m_doc->layerCount())
        return;
    finishTool();
    push(new MoveLayerCommand(m_doc, from, to, delta > 0 ? tr("Subir capa") : tr("Bajar capa")),
         delta > 0 ? QStringLiteral("layer-raise") : QStringLiteral("layer-lower"));
}

void MainWindow::moveLayerTo(int from, int to)
{
    if (!m_doc || from == to || from < 0 || to < 0 || from >= m_doc->layerCount() || to >= m_doc->layerCount())
        return;
    finishTool();
    push(new MoveLayerCommand(m_doc, from, to, to > from ? tr("Subir capa") : tr("Bajar capa")),
         to > from ? QStringLiteral("layer-raise") : QStringLiteral("layer-lower"));
}

void MainWindow::selectLayer(int index)
{
    if (!m_doc || index < 0 || index >= m_doc->layerCount())
        return;
    finishTool();
    m_doc->setActiveIndex(index);
}

void MainWindow::soloLayer(int index)
{
    if (!m_doc || !m_doc->layer(index))
        return;
    finishTool();
    bool othersHidden = m_doc->layer(index)->props.visible;
    for (int i = 0; i < m_doc->layerCount(); ++i)
        if (i != index && m_doc->layer(i)->props.visible)
            othersHidden = false;
    QList<int> changed;
    for (int i = 0; i < m_doc->layerCount(); ++i)
        if (m_doc->layer(i)->props.visible != (othersHidden || i == index))
            changed << i;
    if (changed.isEmpty())
        return;
    const QString text = tr("Visibilidad de las capas");
    HistoryIcons::registerIcon(text, QStringLiteral("layer-visible-on"));
    m_doc->undoStack()->beginMacro(text);
    for (int i : changed) {
        const LayerProps before = m_doc->layer(i)->props;
        LayerProps after = before;
        after.visible = !before.visible;
        m_doc->undoStack()->push(new LayerPropsCommand(m_doc, i, before, after, text));
    }
    m_doc->undoStack()->endMacro();
}

void MainWindow::rotateZoomLayer()
{
    if (const Effect* e = Effects::find(QStringLiteral("rotatezoom")))
        runEffect(e, false);
}

void MainWindow::layerProperties()
{
    if (!m_doc)
        return;
    finishTool();
    const int index = m_doc->activeIndex();
    const LayerProps before = m_doc->layer(index)->props;
    Document* doc = m_doc;
    LayerPropertiesDialog dlg(before, [doc, index](const LayerProps& p) { doc->setLayerProps(index, p); }, this);
    const bool accepted = execWithoutDimming(dlg) == QDialog::Accepted;
    const LayerProps after = dlg.props();
    doc->setLayerProps(index, before);
    if (accepted && after != before)
        push(new LayerPropsCommand(doc, index, before, after, tr("Propiedades de la capa")),
             QStringLiteral("layer-properties"));
}

void MainWindow::toggleAlphaLock()
{
    if (!m_doc || !m_doc->activeLayer())
        return;
    finishTool();
    const int index = m_doc->activeIndex();
    const LayerProps before = m_doc->layer(index)->props;
    LayerProps after = before;
    after.alphaLock = !before.alphaLock;
    push(new LayerPropsCommand(m_doc, index, before, after,
                               after.alphaLock ? tr("Bloquear la transparencia") : tr("Desbloquear la transparencia")),
         QStringLiteral("object-locked"));
    updateActions();
}

void MainWindow::toggleLayerVisibility(int index)
{
    if (!m_doc || !m_doc->layer(index))
        return;
    finishTool();
    const LayerProps before = m_doc->layer(index)->props;
    LayerProps after = before;
    after.visible = !before.visible;
    push(new LayerPropsCommand(m_doc, index, before, after, tr("Visibilidad de la capa")),
         after.visible ? QStringLiteral("layer-visible-on") : QStringLiteral("layer-visible-off"));
    if (!after.visible && index == m_doc->activeIndex() && m_settings.autoSelectVisibleLayer) {
        for (int d = 1; d < m_doc->layerCount(); ++d) {
            for (int i : {index - d, index + d}) {
                if (m_doc->layer(i) && m_doc->layer(i)->props.visible) {
                    m_doc->setActiveIndex(i);
                    return;
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Efectos
// ---------------------------------------------------------------------------

EffectContext MainWindow::effectContext() const
{
    EffectContext ctx;
    ctx.primary = AppState::instance()->primary();
    ctx.secondary = AppState::instance()->secondary();
    ctx.palette = AppState::instance()->palette();
    return ctx;
}

void MainWindow::runEffect(const Effect* effect, bool repeat)
{
    if (!effect || !m_doc || !m_doc->activeLayer())
        return;
    finishTool();
    const LayerPtr layer = m_doc->activeLayerPtr();
    const EffectContext ctx = effectContext();
    ParamValues values = m_effectValues.value(effect->id(), Effects::defaults(*effect));
    QImage result;

    const QRect roi = m_doc->selection().isEmpty() ? m_doc->rect() : m_doc->selection().bounds();
    if (effect->hasCustomDialog() && !repeat) {
        QString error;
        bool accepted = false;
        std::atomic_bool done{false};
        // Como en Paint.NET: lo que se cambia en la ventana del complemento se ve en la imagen. Llega
        // desde el hilo del diálogo; la última que no se mostró sustituye a las anteriores.
        EffectContext dialogCtx = ctx;
        auto alive = std::make_shared<std::atomic_bool>(true);
        auto latest = std::make_shared<std::pair<std::mutex, QImage>>();
        const Selection selection = m_doc->selection();
        const QImage before = layer->image;
        Document* doc = m_doc;
        const Layer* target = layer.get();
        dialogCtx.dialogPreview = [this, alive, latest, selection, before, doc, target](const QImage& image) {
            QImage shown = image;
            Effects::maskToSelection(before, shown, selection);
            bool first = false;
            {
                std::lock_guard lock(latest->first);
                first = latest->second.isNull();
                latest->second = std::move(shown);
            }
            if (!first)
                return;
            QMetaObject::invokeMethod(this, [this, alive, latest, doc, target] {
                QImage next;
                {
                    std::lock_guard lock(latest->first);
                    next = std::exchange(latest->second, QImage());
                }
                if (*alive && m_doc == doc && !next.isNull())
                    doc->setPreview(target, next);
            }, Qt::QueuedConnection);
        };
        std::thread worker([&] {
            accepted = effect->showCustomDialog(layer->image, roi, dialogCtx, &error);
            done = true;
        });
        setEnabled(false);
        while (!done)
            QApplication::processEvents(QEventLoop::AllEvents, 30);
        worker.join();
        *alive = false;
        setEnabled(true);
        if (!accepted) {
            m_doc->clearPreview();
            if (!error.isEmpty())
                QMessageBox::warning(this, effect->name(), error);
            return;
        }
    }
    if (!effect->params().isEmpty() && !repeat) {
        EffectDialog dlg(*effect, values, layer->image, roi, this);
        EffectPreview preview(m_doc, layer, *effect, ctx);
        connect(&dlg, &EffectDialog::valuesChanged, &preview, &EffectPreview::request);
        connect(&dlg, &EffectDialog::previewToggled, &preview, &EffectPreview::setShown);
        connect(&preview, &EffectPreview::progress, this, [this](int permille) {
            m_progress->setVisible(permille >= 0);
            if (permille >= 0)
                m_progress->setValue(permille);
        });
        preview.request(values);
        const bool accepted = execWithoutDimming(dlg) == QDialog::Accepted;
        if (accepted) {
            values = dlg.values();
            result = preview.finalResult(values, this);
        }
        m_progress->hide();
        if (!accepted)
            return;
    } else {
        result = applyEffectWithProgress(*effect, layer->image, m_doc->selection(), values, ctx, this);
    }
    const QString error = effect->takeError();
    if (!error.isEmpty()) {
        m_doc->clearPreview();
        QMessageBox::warning(this, effect->name(), tr("El efecto no se pudo aplicar:\n%1").arg(error));
        return;
    }
    if (result.isNull()) {
        // Cancelado: sin la vista previa de la ventana del complemento.
        m_doc->clearPreview();
        return;
    }

    const QImage before = layer->image;
    layer->image = result;
    m_doc->clearPreview();
    const QRect r = m_doc->selection().isEmpty() ? m_doc->rect() : m_doc->selection().bounds();
    m_doc->notifyPixelsChanged(layer.get(), r);
    pushPixelPatch(m_doc, layer, before, r, effect->name(), Icons::effectIcon(effect->id(), effect->iconName()));

    if (!effect->hidden()) {
        m_lastEffect = effect;
        m_act["repeatEffect"]->setText(tr("Repetir «%1»").arg(effect->name()));
        QIcon icon = Icons::theme(Icons::effectIcon(effect->id(), effect->iconName()));
        if (icon.isNull() && !effect->iconImage().isNull())
            icon = QIcon(QPixmap::fromImage(effect->iconImage()));
        m_act["repeatEffect"]->setIcon(icon);
    }
    m_effectValues.insert(effect->id(), values);
    updateActions();
}

// ---------------------------------------------------------------------------
// Configuración
// ---------------------------------------------------------------------------

void MainWindow::showSettings(SettingsDialog::Page page, const QString& section)
{
    finishTool();
    SettingsDialog dlg(m_settings, m_tools, this);
    dlg.showPage(page);
    if (!section.isEmpty())
        dlg.revealSection(section);
    dlg.setDiscordStatus(m_discord->statusText());
    connect(m_discord, &DiscordPresence::statusChanged, &dlg, [this, &dlg] { dlg.setDiscordStatus(m_discord->statusText()); });
    connect(&dlg, &SettingsDialog::checkUpdatesRequested, this, [this] { checkForUpdates(true); });
    connect(&dlg, &SettingsDialog::settingsChanged, this, [this](const AppSettings& s) {
        const QDateTime lastCheck = m_settings.lastUpdateCheck;
        const QString latest = m_settings.latestVersion;
        m_settings = s;
        m_settings.lastUpdateCheck = lastCheck;
        m_settings.latestVersion = latest;
        m_settings.save();
        applySettings();
    });
    dlg.exec();
    if (dlg.restartRequested())
        restartWith(dlg.settings());
}

void MainWindow::showWelcome()
{
    finishTool();
    WelcomeDialog dlg(m_settings, this);
    // El tema y el estilo se ven al momento (menos entrar o salir del tema KDE, que reinicia).
    connect(&dlg, &WelcomeDialog::previewRequested, this, [this](const AppSettings& s) {
        m_settings.uiStyle = s.uiStyle;
        m_settings.cursorStyle = s.cursorStyle;
        if (!Theme::needsRestart(Theme::Scheme(s.colorScheme)))
            m_settings.colorScheme = s.colorScheme;
        applySettings();
    });
    dlg.exec();
    const AppSettings chosen = dlg.settings();
    m_settings.uiStyle = chosen.uiStyle;
    m_settings.cursorStyle = chosen.cursorStyle;
    m_settings.discordEnabled = chosen.discordEnabled;
    m_settings.photocraftExtras = chosen.photocraftExtras;
    m_settings.pluginRuntime = chosen.pluginRuntime;
    m_settings.welcomed = true;
    const bool restart = chosen.language != m_settings.language || Theme::needsRestart(Theme::Scheme(chosen.colorScheme));
    if (!restart)
        m_settings.colorScheme = chosen.colorScheme;
    m_settings.save();
    applySettings();
    if (restart)
        restartWith(chosen);
}

void MainWindow::restartWith(const AppSettings& settings)
{
    // Se cierra como siempre (pregunta por los cambios sin guardar); main() abre Kaint de nuevo
    // con las imágenes que estaban abiertas. Si no se cierra, todo queda como estaba.
    const AppSettings previous = m_settings;
    m_settings.colorScheme = settings.colorScheme;
    m_settings.language = settings.language;
    // El estilo de la interfaz y la barra de título simulada (Linux) también reinician.
    m_settings.uiStyle = settings.uiStyle;
    m_settings.customFrame = settings.customFrame;
    m_settings.titleButtons = settings.titleButtons;
    m_settings.titleButtonsLeft = settings.titleButtonsLeft;
    m_settings.windowsVersion = settings.windowsVersion;
    m_settings.save();
    m_restartRequested = true;
    if (!close()) {
        m_restartRequested = false;
        m_settings = previous;
        m_settings.save();
    }
}

void MainWindow::keepOpaque()
{
    // Algunos estilos del escritorio (tema de KDE) vuelven translúcida la ventana; Kaint la
    // pinta opaca y con translucidez quedaban rastros. Con las esquinas redondeadas de Windows 11,
    // translúcida (el marco pinta todo menos las esquinas).
    const bool rounded = WindowFrame::isDecorated(this) && WindowFrame::roundedCorners();
    setAttribute(Qt::WA_TranslucentBackground, rounded);
    setAttribute(Qt::WA_NoSystemBackground, false);
}

bool MainWindow::nativeEvent(const QByteArray& type, void* message, qintptr* result)
{
    // Windows, como Paint.NET: mientras la ventana activa sea una de Kaint (Configuración, un
    // efecto, un aviso...), la barra de título de la principal se sigue viendo activa.
    if (Theme::keepTitleBarActive(this, type, message, result)) {
        QTimer::singleShot(0, this, &MainWindow::syncTitleBarActivation);
        return true;
    }
    return QMainWindow::nativeEvent(type, message, result);
}

void MainWindow::syncTitleBarActivation()
{
    Theme::syncTitleBarActivation(this);
    // La franja de los menús sigue a la barra de título (activa o inactiva).
    if (QWidget* top = menuWidget()) {
        top->update();
        m_listButton->update();
    }
}

void MainWindow::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::StyleChange)
        keepOpaque();
    if (e->type() == QEvent::WindowStateChange && m_captionButtons)
        QTimer::singleShot(0, this, &MainWindow::updateCaptionRow);
    if (e->type() == QEvent::WindowStateChange) {
        QTimer::singleShot(0, this, &MainWindow::repaintTopRow);
        QTimer::singleShot(250, this, &MainWindow::repaintTopRow);
    }
    QMainWindow::changeEvent(e);
}

void MainWindow::applyCanvasAppearance()
{
    // El color propio del fondo es el del brillo del tema (también al cambiar el del sistema).
    m_canvas->setAppearance(m_settings.dropShadow, Theme::isDark() ? m_settings.canvasBorderDark : m_settings.canvasBorderLight,
                            m_settings.checkerBrightness);
    if (m_gifEditor)
        m_gifEditor->setCheckerBrightness(m_settings.checkerBrightness);
}

void MainWindow::applyThemeLayout()
{
    applyCanvasAppearance();
    const Theme::Spec spec = Theme::active();
    Theme::styleTitleBar(this, TopRow::bandColor());
    // Paint.NET: los menús pegados arriba (sin franja encima aunque la tira de imágenes sea más
    // alta) y con un margen de la franja a su izquierda, antes de «Archivo».
    const bool paintNet = Theme::paintNetLook();
    // Con la barra de título simulada, sus botones llegan al borde derecho.
    m_topLayout->setContentsMargins(paintNet ? 4 : 0, 0, m_captionButtons ? 0 : 4, 0);
    m_topLayout->setAlignment(m_menuBar, Qt::AlignLeft | (paintNet ? Qt::AlignTop : Qt::AlignBottom));
    if (QWidget* top = menuWidget())
        top->update();
    const QSize icon(spec.iconSize, spec.iconSize);
    if (auto* main = findChild<QToolBar*>(QStringLiteral("MainToolBar")))
        main->setIconSize(icon);
    // Paint.NET: la regla girada, como una «Γ» (como la suya); con el de Kaint, la de siempre.
    const QIcon ruler = Icons::theme({"measure", "kruler"});
    m_act["rulers"]->setIcon(paintNet ? Icons::rotated(ruler) : ruler);
    // Los botones de las ventanas: con el aspecto de Paint.NET, sus iconos y algo más grandes, como
    // los suyos; con el de Kaint, los de siempre.
    const QSize cornerIcon = paintNet ? QSize(spec.iconSize + 4, spec.iconSize + 4) : icon;
    for (QToolButton* b : m_cornerButtons->findChildren<QToolButton*>()) {
        b->setIconSize(cornerIcon);
        QObject* holder = b->defaultAction() ? static_cast<QObject*>(b->defaultAction()) : static_cast<QObject*>(b);
        const QString pdnIcon = holder->property("pdnIcon").toString();
        if (pdnIcon.isEmpty())
            continue;
        const QIcon chosen = paintNet ? Icons::pdn(pdnIcon) : holder->property("kaintIcon").value<QIcon>();
        if (QAction* a = b->defaultAction())
            a->setIcon(chosen);
        else
            b->setIcon(chosen);
    }
    // Pegados debajo de los botones de la barra de título, como en Paint.NET (centrados en la fila de
    // los menús quedaban más abajo, con un hueco); con el aspecto de Kaint, centrados.
    m_topLayout->setAlignment(m_cornerButtons, Qt::AlignRight | (paintNet ? Qt::AlignTop : Qt::AlignVCenter));
    m_cornerButtons->update();
    if (auto* tools = findChild<ToolsPanel*>())
        tools->setIconSize(spec.toolIconSize);
    if (m_tabs)
        m_tabs->update();
    // Tras aplicar la hoja de estilo (con otra fuente o idioma, los menús miden otra cosa).
    QTimer::singleShot(0, this, &MainWindow::alignMainToolBar);
}

void MainWindow::alignMainToolBar()
{
    // Paint.NET: la barra principal mide lo mismo que la fila de menús, con su último botón (la
    // regla) debajo de «Efectos»; el sobrante se reparte entre los botones. Con el aspecto de
    // Kaint, cada botón con su tamaño.
    auto* main = findChild<QToolBar*>(QStringLiteral("MainToolBar"));
    if (!main || !m_menuBar)
        return;
    QList<QToolButton*> buttons;
    for (QAction* a : main->actions())
        if (auto* b = qobject_cast<QToolButton*>(main->widgetForAction(a)); b && a->isVisible())
            buttons << b;
    for (QToolButton* b : std::as_const(buttons))
        b->setMinimumWidth(0);
    if (Theme::paintNetLook() && !buttons.isEmpty()) {
        const int extra = m_menuBar->sizeHint().width() - main->sizeHint().width();
        if (extra > 0) {
            const int each = extra / int(buttons.size());
            int rest = extra % int(buttons.size());
            for (QToolButton* b : std::as_const(buttons))
                b->setMinimumWidth(b->sizeHint().width() + each + (rest-- > 0 ? 1 : 0));
        }
    }
    main->updateGeometry();
}

void MainWindow::applySettings()
{
    // El icono y el nombre del programa, al momento.
    Icons::setProgram(m_settings.programIcon, m_settings.programTitle);
    QApplication::setWindowIcon(Icons::app());
    setWindowIcon(Icons::app());
    updateTitle();
    {
        const QSignalBlocker b1(m_act["translucent"]), b2(m_act["rulers"]);
        m_act["translucent"]->setChecked(m_settings.translucentPanels);
        m_act["rulers"]->setChecked(m_settings.rulers);
    }
    m_workspace->setTranslucentOverImageOnly(m_settings.translucentOverImage);
    m_workspace->setTranslucentPanels(m_settings.translucentPanels);
    WindowFrame::setPanelClose7(WindowFrame::PanelClose7(std::clamp(m_settings.win7PanelClose, 0, 2)));
    if (WindowFrame::aeroExtended() != m_settings.win7ExtendedGlass) {
        WindowFrame::setAeroExtended(m_settings.win7ExtendedGlass);
        if (QWidget* top = menuWidget())
            top->update();
    }
    m_workspace->setPanelChrome(FloatingPanel::Style(m_settings.panelStyle), m_settings.panelBorder, m_settings.roundedWindows());
    m_canvas->setRulerStyle(Ruler::Style(m_settings.rulerStyle));
    m_canvas->setRulersVisible(m_settings.rulers);
    m_canvas->setOverscroll(m_settings.overscroll);
    m_canvas->setAutoScroll(m_settings.autoScroll);
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, !m_settings.systemFileDialogs);
    Icons::setSystemPointer(m_settings.systemPointer);
    Icons::setCursorStyle(Icons::CursorStyle(m_settings.cursorStyle));
    Theme::setUnderlineShortcuts(m_settings.menuMnemonics);
    m_layers->setDropLineMode(m_settings.layerDropLine);
    m_layers->setLockButtonVisible(m_settings.layerLockButton);
    m_history->setJumpButtonsVisible(m_settings.historyJumpButtons);
    m_gifHistory->setJumpButtonsVisible(m_settings.historyJumpButtons);
    EffectMenu::showPluginIcons() = m_settings.pluginMenuIcons;
    // La tira de imágenes a lo alto de toda la fila de arriba (o solo lo que ocupan las miniaturas).
    if (auto* strip = static_cast<ImageStrip*>(m_tabs)) {
        strip->setFullHeight(m_settings.stripFullHeight);
        m_topLayout->removeWidget(m_tabs);
        m_topLayout->addWidget(m_tabs, 0, 1, m_tabsColumnRows, 1, m_settings.stripFullHeight ? Qt::Alignment() : Qt::AlignBottom);
        // La flecha de la lista ocupa todo el alto de la tira (al pasar el puntero, todo el rectángulo).
        m_topLayout->removeWidget(m_listButton);
        if (m_settings.stripFullHeight)
            m_topLayout->addWidget(m_listButton, 0, 2, m_tabsColumnRows, 1);
        else
            m_topLayout->addWidget(m_listButton, m_menuRow, 2, 2, 1);
    }
    // El sistema de complementos de Paint.NET (.NET, OpenD2D o Wine): al cambiar, se revisan otra vez.
    // (Se compara con el aplicado antes: «Probar», en Configuración, ya cambia el de PdnBridge.)
    // (El respaldo de Wine también cambia lo que se sabe de cada complemento.)
    // .NET elegido pero no disponible (sin el runtime): OpenD2D.
    int chosenRuntime = std::clamp(m_settings.pluginRuntime, 0, 2);
    if (chosenRuntime == int(PdnBridge::Runtime::DotNet) && !PdnBridge::availableFor(PdnBridge::Runtime::DotNet))
        chosenRuntime = int(PdnBridge::Runtime::OpenD2D);
    const int pluginSystem = chosenRuntime * 2 + (m_settings.pluginWineFallback ? 1 : 0);
    if (PdnBridge::runtimeSelectable() && m_appliedPluginRuntime != pluginSystem) {
        const bool first = m_appliedPluginRuntime < 0;
        m_appliedPluginRuntime = pluginSystem;
        PdnBridge::setWineFallback(m_settings.pluginWineFallback);
        PdnBridge::setRuntime(PdnBridge::Runtime(chosenRuntime));
        if (!first)
            PluginManager::instance()->runtimeChanged();
    }
    BarSlider::setWheelBehavior(m_settings.wheelChangesValues, m_settings.wheelStep);
    // Las funciones de PhotoCraft y los efectos de BoltBait activados: los menús se rehacen si
    // cambian.
    if (Effects::extrasEnabled() != m_settings.photocraftExtras || m_appliedBoltBait != m_settings.boltBaitEffects) {
        Effects::setExtrasEnabled(m_settings.photocraftExtras);
        Effects::setBoltBaitEnabled(m_settings.boltBaitEffects);
        // Sin las funciones de PhotoCraft, el imán (Snap) se apaga y su botón desaparece.
        // (Siempre avisa: la barra de opciones muestra u oculta sus botones de PhotoCraft.)
        AppState::instance()->updateTools([extras = m_settings.photocraftExtras](ToolSettings& s) {
            if (!extras)
                s.moveSnap = false;
        });
        m_tools->setExtrasEnabled(m_settings.photocraftExtras);
        m_appliedBoltBait = m_settings.boltBaitEffects;
        rebuildEffectMenus();
    }
    m_colors->setShowTranslucent(m_settings.paletteTranslucent);
    ToolPreferences preferences;
    preferences.hideBoxWhileDragging = m_settings.hideMoveBox;
    preferences.replacePixels = m_settings.replacePixels;
    AppState::instance()->setPreferences(preferences);
    m_canvas->viewport()->update();
    FloatingPanel::setSnapping(m_settings.snapPanels);
    // Botones de la barra de título: el estilo y el lado cambian al momento.
    WindowFrame::setButtons(WindowFrame::Buttons(std::clamp(m_settings.titleButtons, 0, 8)));
    // Los automáticos con los temas que no traen botones propios (Claro, Oscuro, Azul...): los de
    // Windows 10, también con «Bordes: redondeados» (antes, tras pasar por un tema de Windows 7, que
    // los redondea, quedaban los de Windows 11).
    WindowFrame::setAutoFallback(WindowFrame::Buttons::Windows10);
#ifdef Q_OS_WIN
    // Windows 11 redondea (o no) las esquinas de las ventanas con el marco de Kaint al momento.
    WindowFrame::setRoundedCorners(m_settings.roundedWindows());
#endif
    WindowFrame::setButtonsOnLeft(m_settings.titleButtonsLeft);
    placeCaptionButtons();
    WindowFrame::refreshAll();
    // La fuente de la interfaz y (Linux) el color de la barra superior se ven al volver a aplicar
    // el tema.
    const bool fontChanged = Theme::uiFontFamily() != m_settings.uiFontFamily || Theme::uiFontSize() != m_settings.uiFontSize;
    // El color propio de la barra superior (en Windows, solo con la barra de título de Kaint).
#ifdef Q_OS_WIN
    const QColor ownTitle = WindowFrame::enabled() ? m_settings.titleBarColor : QColor();
#else
    const QColor ownTitle = m_settings.titleBarColor;
#endif
    const bool titleChanged = Theme::customTitleBarColor() != (ownTitle.isValid() ? QColor(ownTitle.rgb()) : QColor());
    Theme::setUiFont(m_settings.uiFontFamily, m_settings.uiFontSize);
    Theme::setCustomTitleBarColor(ownTitle);
    bool glassChanged = Theme::setAeroTransparency(m_settings.aeroTransparency);
    glassChanged = Theme::setWin7Glass(m_settings.win7Glass) || glassChanged;
    glassChanged = Theme::setWin8Color(m_settings.win8Color) || glassChanged;
    const bool selectionChanged = Theme::setSelectionColors(m_settings.selectionColorLight, m_settings.selectionColorDark);
    if (Theme::current() != Theme::Scheme(m_settings.colorScheme) || Theme::look() != Theme::Look(m_settings.uiStyle)
        || fontChanged || titleChanged || glassChanged || selectionChanged) {
        Theme::setLook(Theme::Look(m_settings.uiStyle));
        Theme::apply(Theme::Scheme(m_settings.colorScheme));
        // Con el tema nuevo: sus esquinas (redondeadas con Windows 7; en Linux solo se quitan al
        // momento, ponerlas necesita reiniciar) y sus botones de la barra de título.
        const bool rounded = m_settings.roundedWindows();
#ifdef Q_OS_WIN
        WindowFrame::setRoundedCorners(rounded);
#else
        if (!rounded)
            WindowFrame::setRoundedCorners(false);
#endif
        keepOpaque();
        placeCaptionButtons();
        WindowFrame::refreshAll();
    }
    applyCanvasAppearance();
    // Las animaciones son las de Kaint (Animations), también en Wayland: los efectos de menús y
    // listas de Qt son ventanas aparte que se ven primero en otro sitio y saltan a su lugar.
    for (auto effect : {Qt::UI_AnimateMenu, Qt::UI_FadeMenu, Qt::UI_AnimateTooltip, Qt::UI_FadeTooltip, Qt::UI_AnimateCombo})
        QApplication::setEffectEnabled(effect, false);
    QApplication::setEffectEnabled(Qt::UI_AnimateToolBox, m_settings.animations);
    Animations::setEnabled(m_settings.animations);
    Animations::setWindowAnimation(m_settings.windowAnimation, m_settings.windowAnimationMs);
    if (m_settings.autosaveMinutes > 0)
        m_autosaveTimer.start(m_settings.autosaveMinutes * 60 * 1000);
    else
        m_autosaveTimer.stop();
    setUnit(m_settings.unit);
    checkHistoryMemory();
    PresenceOptions presence;
    presence.enabled = m_settings.discordEnabled;
    presence.showFile = m_settings.discordShowFile;
    presence.showLayer = m_settings.discordShowLayer;
    presence.showFileInfo = m_settings.discordShowFileInfo;
    presence.showTool = m_settings.discordShowTool;
    presence.showElapsed = m_settings.discordShowElapsed;
    presence.showCollab = m_settings.discordShowCollab;
    presence.showCollabName = m_settings.discordShowCollabName;
    presence.invites = m_settings.discordInvites;
    presence.joinRequests = PresenceOptions::JoinRequests(std::clamp(m_settings.discordJoinRequests, 0, 2));
    presence.partyMax = m_settings.discordPartySize;
    presence.registerLaunch = m_settings.discordRegisterLaunch;
    m_discord->setOptions(presence);
    updatePresence();
    updateToolIconColors();
}

void MainWindow::updateToolIconColors()
{
    // Cubo, pincel, cambiar color y degradado con los colores activos, como en Paint.NET.
    const AppState* app = AppState::instance();
    Icons::setActiveColors(m_settings.coloredToolIcons ? app->primary() : QColor(),
                           m_settings.coloredToolIcons ? app->secondary() : QColor());
    if (auto* tools = findChild<ToolsPanel*>())
        tools->update();
    if (auto* bar = findChild<ToolOptionsBar*>())
        bar->update();
    if (m_hint)
        refreshHint();
    // Los cursores del pincel, el cubo... llevan los colores activos.
    m_canvas->updateCursor();
}

PresenceInfo MainWindow::presenceInfo() const
{
    PresenceInfo info;
    info.documents = int(m_docs.size());
    // El modo colaborativo (con el anfitrión entre las personas).
    if (m_collab && m_collab->isActive()) {
        const Collab::Session* session = m_collab->session();
        info.collab = true;
        info.collabHost = session->isHost();
        info.collabWaiting = session->state() != Collab::Session::State::Joined;
        info.collabP2P = session->connection() == Collab::Mode::P2P;
        info.collabPeople = info.collabWaiting ? 0 : int(session->peers().size());
        info.collabName = session->sessionName();
        if (info.collabP2P && !info.collabWaiting)
            info.collabCode = session->sessionCode();
    }
    if (!m_doc)
        return info;
    info.hasDocument = true;
    const QFileInfo file(m_doc->filePath());
    if (!m_doc->filePath().isEmpty()) {
        info.fileName = file.fileName();
        info.fileType = file.suffix().toUpper();
    }
    info.size = m_doc->size();
    info.gifEditor = m_gifDocs.contains(m_doc);
    info.frames = m_doc->layerCount();
    if (info.gifEditor)
        return info;
    info.layerCount = m_doc->layerCount();
    info.layerIndex = m_doc->activeIndex() + 1;
    if (const Layer* layer = m_doc->activeLayer())
        info.layerName = layer->props.name;
    if (m_tools && m_tools->current())
        info.tool = m_tools->current()->name();
    return info;
}

void MainWindow::updatePresence()
{
    if (m_discord)
        m_discord->update(presenceInfo());
}

void MainWindow::checkHistoryMemory()
{
    // Límite de memoria del historial (Configuración > Rendimiento): si se supera, se
    // descartan los pasos más antiguos y se avisa.
    if (!m_doc || m_settings.historyLimitMB <= 0)
        return;
    const qint64 limit = qint64(m_settings.historyLimitMB) * 1024 * 1024;
    const int dropped = History::trim(m_doc->undoStack(), limit);
    if (dropped > 0)
        statusBar()->showMessage(tr("El historial superaba %1 MB: se descartaron los %n pasos más antiguos.", nullptr, dropped)
                                     .arg(m_settings.historyLimitMB),
                                 8000);
}

void MainWindow::setUnit(int unit)
{
    // (También desde la esquina de las reglas: Canvas::unitRequested.)
    unit = std::clamp(unit, 0, 2);
    m_canvas->setUnit(Unit(unit));
    m_act[QStringLiteral("unit%1").arg(unit)]->setChecked(true);
    m_unitButton->setText(unitSuffix(Unit(unit)));
    if (m_settings.unit != unit) {
        m_settings.unit = unit;
        m_settings.save();
    }
    updateStatus();
}

void MainWindow::checkForUpdates(bool interactive)
{
    // La versión más reciente es la del último "release" (o etiqueta) del repositorio de
    // GitHub; las versiones preliminares solo si se piden.
    auto* nam = new QNetworkAccessManager(this);
    QNetworkRequest req(QUrl(QStringLiteral("https://api.github.com/repos/%1/releases").arg(QStringLiteral(KAINT_GITHUB))));
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Kaint/" KAINT_VERSION));
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setTransferTimeout(15000);
    QNetworkReply* reply = nam->get(req);
    const bool beta = m_settings.updatesBeta;
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam, interactive, beta] {
        reply->deleteLater();
        nam->deleteLater();
        const QByteArray data = reply->readAll();
        QString latest, url;
        if (reply->error() == QNetworkReply::NoError) {
            for (const QJsonValue& v : QJsonDocument::fromJson(data).array()) {
                const QJsonObject r = v.toObject();
                if (r.value(QLatin1String("draft")).toBool() || (!beta && r.value(QLatin1String("prerelease")).toBool()))
                    continue;
                latest = r.value(QLatin1String("tag_name")).toString();
                url = r.value(QLatin1String("html_url")).toString();
                break;
            }
        }
        m_settings.lastUpdateCheck = QDateTime::currentDateTime();
        m_settings.latestVersion = latest;
        m_settings.save();
        if (latest.isEmpty()) {
            if (interactive)
                QMessageBox::information(this, tr("Actualizaciones"),
                                         reply->error() == QNetworkReply::NoError
                                             ? tr("Tienes Kaint %1. Todavía no hay versiones publicadas en %2.")
                                                   .arg(QStringLiteral(KAINT_VERSION), QStringLiteral(KAINT_REPOSITORY))
                                             : tr("No se pudo comprobar si hay actualizaciones:\n%1").arg(reply->errorString()));
            return;
        }
        const bool newer = versionNewer(latest, QStringLiteral(KAINT_VERSION));
        // Con "No volver a recordar", la búsqueda automática ya no avisa (Configuración >
        // Actualizaciones lo vuelve a activar); "Buscar ahora" sí.
        if (newer && !interactive && !m_settings.updateReminder)
            return;
        if (!newer) {
            if (interactive)
                QMessageBox::information(this, tr("Actualizaciones"),
                                         tr("Kaint está actualizado (versión %1).").arg(QStringLiteral(KAINT_VERSION)));
            return;
        }
        QMessageBox box(QMessageBox::Information, tr("Actualizaciones"),
                        tr("Hay una versión nueva de Kaint: %1 (tienes la %2).").arg(latest, QStringLiteral(KAINT_VERSION)),
                        QMessageBox::NoButton, this);
        QPushButton* open = box.addButton(tr("Ver la versión nueva"), QMessageBox::AcceptRole);
        box.addButton(tr("Cerrar"), QMessageBox::RejectRole);
        auto* never = new QCheckBox(tr("No volver a recordar"), &box);
        never->setToolTip(tr("Se puede volver a activar en Configuración > Actualizaciones."));
        never->setChecked(!m_settings.updateReminder);
        box.setCheckBox(never);
        box.exec();
        if (never->isChecked() == m_settings.updateReminder) {
            m_settings.updateReminder = !never->isChecked();
            m_settings.save();
        }
        if (box.clickedButton() == open)
            QDesktopServices::openUrl(QUrl(url.isEmpty() ? QStringLiteral(KAINT_REPOSITORY) : url));
    });
}

void MainWindow::startupChecks()
{
#ifdef KAINT_UPDATE_CHECK
    if (m_settings.checkUpdates)
        QTimer::singleShot(3000, this, [this] { checkForUpdates(false); });
#endif
    offerRecovery();
}

void MainWindow::offerRecovery()
{
    // Kaint se cerró de golpe: se ofrecen las copias de recuperación de las imágenes sin guardar.
    const QList<Recovery::Entry> entries = Recovery::orphaned();
    if (entries.isEmpty())
        return;
    QStringList names;
    for (const Recovery::Entry& e : entries)
        names << QStringLiteral("• %1").arg(e.name);
    TaskDialog ask(tr("Recuperar imágenes"), Icons::theme({"document-revert", "edit-undo"}), QImage(),
                   tr("Kaint no se cerró correctamente. Se pueden recuperar estas imágenes con cambios sin guardar:\n%1")
                       .arg(names.join(QLatin1Char('\n'))),
                   this);
    const int recover = ask.addChoice(Icons::theme({"document-open"}), tr("Recuperar"),
                                      tr("Abre las imágenes tal como estaban en la última copia de recuperación."), true);
    ask.addChoice(Icons::theme({"edit-delete"}), tr("Descartar"), tr("Borra las copias de recuperación."));
    if (ask.choose() == recover) {
        for (const Recovery::Entry& e : entries) {
            QString error;
            Document* doc = Ora::load(e.file, &error);
            if (!doc)
                continue;
            doc->setFilePath(e.originalPath);
            doc->undoStack()->resetClean();
            addDocument(doc);
        }
    }
    Recovery::discard(entries);
    autosave();
}

void MainWindow::autosave()
{
    Recovery::save(m_docs);
}
