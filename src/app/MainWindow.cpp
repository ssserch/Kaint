#include "app/MainWindow.h"

#include "app/Theme.h"

#include "app/AppState.h"
#include "app/Canvas.h"
#include "app/Icons.h"
#include "app/ToolOptionsBar.h"
#include "app/Workspace.h"
#include "app/DiscordPresence.h"
#include "app/dialogs/DialogExec.h"
#include "app/dialogs/EffectDialog.h"
#include "app/dialogs/ImageDialogs.h"
#include "app/dialogs/SaveConfigDialog.h"
#include "app/dialogs/TaskDialog.h"
#include "app/panels/ColorsPanel.h"
#include "app/panels/HistoryPanel.h"
#include "app/panels/LayersPanel.h"
#include "app/panels/ToolsPanel.h"
#include "app/tools/MoveTools.h"
#include "app/tools/ToolManager.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/ImageIO.h"
#include "core/Raster.h"
#include "core/plugins/PluginManager.h"

#include <QActionGroup>
#include <QApplication>
#include <QCollator>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDateTime>
#include <QDragEnterEvent>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QColorTransform>
#include <QMessageBox>
#include <QMimeData>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPageSetupDialog>
#include <QPrintPreviewDialog>
#include <QPrintPreviewWidget>
#include <QPrinter>
#include <QProcess>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSlider>
#include <QStandardPaths>
#include <QStatusBar>
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

QString zoomText(qreal z)
{
    const qreal pct = z * 100;
    return (pct < 10 ? QString::number(pct, 'f', 1) : QString::number(std::round(pct))) + QStringLiteral(" %");
}

} // namespace

MainWindow::MainWindow()
    : m_canvas(new Canvas)
    , m_undoGroup(new QUndoGroup(this))
{
    setWindowIcon(Icons::app());
    setAcceptDrops(true);
    m_lastDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);

    m_tools = new ToolManager(m_canvas, this);
    m_workspace = new Workspace(m_canvas, this);
    setCentralWidget(m_workspace);

    createTopRow();
    createActions();
    createMenus();
    createToolBars();
    createPanels();
    createStatusBar();

    connect(m_tools, &ToolManager::toolChanged, this, [this](Tool* t) { m_hint->setText(t->hint()); });
    for (Tool* t : m_tools->tools())
        connect(t, &Tool::hintChanged, this, [this, t](const QString& h) {
            if (m_tools->current() == t)
                m_hint->setText(h);
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
    QSettings ts;
    const int lastTool = ts.value(QStringLiteral("tools/current"), int(ToolId::Paintbrush)).toInt();
    m_tools->select(ToolId(m_settings.startTool >= 0 ? m_settings.startTool : lastTool));
    if (!m_tools->current())
        m_tools->select(ToolId::Paintbrush);

    connect(m_undoGroup, &QUndoGroup::canUndoChanged, this, &MainWindow::updateActions);
    connect(m_undoGroup, &QUndoGroup::canRedoChanged, this, &MainWindow::updateActions);
    m_historyTimer.setSingleShot(true);
    m_historyTimer.setInterval(500);
    connect(&m_historyTimer, &QTimer::timeout, this, &MainWindow::checkHistoryMemory);
    connect(m_undoGroup, &QUndoGroup::indexChanged, this, [this] { m_historyTimer.start(); });
    connect(AppState::instance(), &AppState::recentFilesChanged, this, &MainWindow::rebuildRecentMenu);
    m_discord = new DiscordPresence(this);
    m_presenceTimer.setInterval(2000);
    connect(&m_presenceTimer, &QTimer::timeout, this, &MainWindow::updatePresence);
    m_presenceTimer.start();

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

    rebuildRecentMenu();
    updateActions();
    updateTitle();
}

MainWindow::~MainWindow()
{
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
    A["print"] = makeAction(tr("&Imprimir..."), {"document-print"}, QKeySequence::Print, [this] { print(); }, doc);
    A["pageSetup"] = makeAction(tr("Configurar pá&gina..."), {"document-page-setup", "document-properties"}, {},
                                [this] { pageSetup(); });
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
    A["cut"] = makeAction(tr("Cor&tar"), {"edit-cut"}, QKeySequence::Cut, [this] { cut(); }, doc);
    A["cut"]->setShortcuts({QKeySequence(QStringLiteral("Ctrl+X")), QKeySequence(QStringLiteral("Shift+Del"))});
    A["copy"] = makeAction(tr("&Copiar"), {"edit-copy"}, QKeySequence::Copy, [this] { copy(false); }, doc);
    A["copy"]->setShortcuts({QKeySequence(QStringLiteral("Ctrl+C")), QKeySequence(QStringLiteral("Ctrl+Ins"))});
    A["copyMerged"] = makeAction(tr("Copiar co&mbinado"), {"edit-copy"}, QKeySequence(QStringLiteral("Ctrl+Shift+C")),
                                 [this] { copy(true); }, doc);
    A["paste"] = makeAction(tr("&Pegar"), {"edit-paste"}, QKeySequence::Paste, [this] { paste(); });
    A["paste"]->setShortcuts({QKeySequence(QStringLiteral("Ctrl+V")), QKeySequence(QStringLiteral("Shift+Ins"))});
    A["pasteLayer"] = makeAction(tr("Pegar en capa &nueva"), {"edit-paste-in-place", "edit-paste"},
                                 QKeySequence(QStringLiteral("Ctrl+Shift+V")), [this] { pasteIntoNewLayer(); }, doc);
    A["pasteImage"] = makeAction(tr("Pegar en una &imagen nueva"), {"window-new", "edit-paste"},
                                 QKeySequence(QStringLiteral("Ctrl+Alt+V")), [this] { pasteIntoNewImage(); });
    A["copySelection"] = makeAction(tr("Copiar &selección"), {"edit-copy-path", "edit-copy"},
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
    A["eraseSel"] = makeAction(tr("&Borrar la selección"), {"edit-delete"}, QKeySequence(Qt::Key_Delete),
                               [this] { eraseSelection(tr("Borrar la selección")); }, sel);
    A["fillSel"] = makeAction(tr("&Rellenar la selección"), {"fill-color"}, QKeySequence(Qt::Key_Backspace),
                              [this] { fillSelection(); }, doc);
    A["invertSel"] = makeAction(tr("&Invertir la selección"), {"edit-select-invert"}, QKeySequence(QStringLiteral("Ctrl+I")),
                                [this] { invertSelection(); }, sel);
    A["selectAll"] = makeAction(tr("Seleccionar &todo"), {"edit-select-all"}, QKeySequence::SelectAll,
                                [this] { selectAll(); }, doc);
    A["deselect"] = makeAction(tr("&Anular la selección"), {"edit-select-none"}, QKeySequence(QStringLiteral("Ctrl+D")),
                               [this] { deselect(); }, sel);

    // Ver
    A["zoomIn"] = makeAction(tr("&Acercar"), {"zoom-in"}, QKeySequence::ZoomIn, [this] { m_canvas->zoomIn(); }, doc);
    A["zoomIn"]->setShortcuts({QKeySequence::ZoomIn, QKeySequence(QStringLiteral("Ctrl+="))});
    A["zoomOut"] = makeAction(tr("A&lejar"), {"zoom-out"}, QKeySequence::ZoomOut, [this] { m_canvas->zoomOut(); }, doc);
    A["zoomFit"] = makeAction(tr("&Zum a ventana"), {"zoom-fit-best"}, QKeySequence(QStringLiteral("Ctrl+B")),
                              [this] { m_canvas->toggleZoomToWindow(); }, doc);
    A["zoomSel"] = makeAction(tr("Zum a &selección"), {"zoom-select", "zoom-fit-selection"},
                              QKeySequence(QStringLiteral("Ctrl+Shift+B")),
                              [this] { m_canvas->zoomToRect(QRectF(m_doc->selection().bounds())); }, sel);
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
    A["rot180"] = makeAction(tr("Girar &180°"), {"object-rotate-right"}, {}, [this] { rotateImage(180); }, doc);
    A["colorProfile"] = makeAction(tr("&Perfil de colores..."), {"color-profile", "color-management"}, {},
                                   [this] { colorProfile(); }, doc);
    A["animation"] = makeAction(tr("A&nimación..."), {"media-playback-start", "video-x-generic"}, {},
                                [this] { editAnimation(); }, doc);
    A["flatten"] = makeAction(tr("&Acoplar"), {"layer-bottom", "merge"}, QKeySequence(QStringLiteral("Ctrl+Shift+F")),
                              [this] { flatten(); }, doc);

    // Capas
    A["addLayer"] = makeAction(tr("Agregar &nueva capa"), {"layer-new"}, QKeySequence(QStringLiteral("Ctrl+Shift+N")),
                               [this] { addLayer(); }, doc);
    A["deleteLayer"] = makeAction(tr("&Eliminar la capa"), {"layer-delete"}, QKeySequence(QStringLiteral("Ctrl+Shift+Del")),
                                  [this] { deleteLayer(); });
    A["duplicateLayer"] = makeAction(tr("&Duplicar la capa"), {"layer-duplicate"}, QKeySequence(QStringLiteral("Ctrl+Shift+D")),
                                     [this] { duplicateLayer(); }, doc);
    A["mergeDown"] = makeAction(tr("&Combinar capa con la inferior"), {"merge", "layer-bottom"},
                                QKeySequence(QStringLiteral("Ctrl+M")), [this] { mergeLayerDown(); });
    A["toggleLayer"] = makeAction(tr("Alternar &visibilidad de capa"), {"layer-visible-on"}, QKeySequence(QStringLiteral("Ctrl+,")),
                                  [this] { if (m_doc) toggleLayerVisibility(m_doc->activeIndex()); }, doc);
    A["importLayer"] = makeAction(tr("&Importar desde archivo..."), {"document-import", "insert-image"}, {},
                                  [this] { importLayer(); }, doc);
    A["flipLayerH"] = makeAction(tr("Voltear &horizontalmente"), {"object-flip-horizontal"}, {},
                                 [this] { flipLayer(true); }, doc);
    A["flipLayerV"] = makeAction(tr("Voltear &verticalmente"), {"object-flip-vertical"}, {},
                                 [this] { flipLayer(false); }, doc);
    A["rotateLayer180"] = makeAction(tr("Girar &180°"), {"object-rotate-right"}, {}, [this] { rotateLayer180(); }, doc);
    A["rotateZoom"] = makeAction(tr("&Giro y zum..."), {"transform-rotate", "object-rotate-right"},
                                 QKeySequence(QStringLiteral("Ctrl+Shift+Z")), [this] { rotateZoomLayer(); }, doc);
    A["layerTop"] = makeAction(tr("Ir a capa &superior"), {"go-top"}, QKeySequence(QStringLiteral("Ctrl+Alt+PgUp")),
                               [this] { if (m_doc) selectLayer(m_doc->layerCount() - 1); }, doc);
    A["layerAbove"] = makeAction(tr("Ir a capa de &encima"), {"go-up"}, QKeySequence(QStringLiteral("Alt+PgUp")),
                                 [this] { if (m_doc) selectLayer(m_doc->activeIndex() + 1); }, doc);
    A["layerBelow"] = makeAction(tr("Ir a capa de de&bajo"), {"go-down"}, QKeySequence(QStringLiteral("Alt+PgDown")),
                                 [this] { if (m_doc) selectLayer(m_doc->activeIndex() - 1); }, doc);
    A["layerBottom"] = makeAction(tr("Ir a capa in&ferior"), {"go-bottom"}, QKeySequence(QStringLiteral("Ctrl+Alt+PgDown")),
                                  [this] { selectLayer(0); }, doc);
    A["layerToTop"] = makeAction(tr("Enviar capa al &principio"), {"layer-top", "go-top"}, {},
                                 [this] { if (m_doc) moveLayerTo(m_doc->activeIndex(), m_doc->layerCount() - 1); });
    A["layerUp"] = makeAction(tr("&Subir capa"), {"layer-raise"}, {}, [this] { moveLayer(+1); });
    A["layerDown"] = makeAction(tr("&Bajar capa"), {"layer-lower"}, {}, [this] { moveLayer(-1); });
    A["layerToBottom"] = makeAction(tr("Enviar capa al &final"), {"layer-bottom", "go-bottom"}, {},
                                    [this] { if (m_doc) moveLayerTo(m_doc->activeIndex(), 0); });
    A["layerProps"] = makeAction(tr("&Propiedades de la capa..."), {"document-properties", "configure"},
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

    // Ayuda (botón "?" de la esquina)
    A["docs"] = makeAction(tr("&Documentación de Paint.NET (en línea)"), {"help-contents"}, QKeySequence::HelpContents, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://paint.net/doc/latest/index.html")));
    });
    addAction(A["docs"]);
    A["updates"] = makeAction(tr("&Buscar actualizaciones..."), {"update-none", "system-software-update"}, {},
                              [this] { checkForUpdates(true); });
    A["about"] = makeAction(tr("&Acerca de Kaint"), {"help-about"}, {}, [this] {
        QMessageBox::about(this, tr("Acerca de Kaint"),
                           tr("<h3>Kaint %1</h3>"
                              "<p>Editor de imágenes al estilo Paint.NET, hecho para KDE Plasma y Wayland "
                              "con C++20 y Qt %2.</p>"
                              "<p>Recrea el comportamiento de Paint.NET (la versión 3.36 es la última de código "
                              "abierto): capas con 14 modos de fusión, historial ilimitado, selecciones, "
                              "herramientas editables, más de 50 efectos y ajustes con vista previa, "
                              "complementos y proyectos en formato abierto OpenRaster (.ora).</p>"
                              "<p><a href=\"%3\">%3</a></p>")
                               .arg(QStringLiteral(KAINT_VERSION), QString::fromLatin1(qVersion()),
                                    QStringLiteral(KAINT_REPOSITORY)));
    });
    A["aboutQt"] = makeAction(tr("Acerca de &Qt"), {"help-about"}, {}, [] { QApplication::aboutQt(); });

    // Atajos de herramientas (una letra) y otros atajos sin menú.
    for (const QKeySequence& key : m_tools->shortcuts()) {
        auto* a = new QAction(this);
        a->setShortcut(key);
        connect(a, &QAction::triggered, this, [this, key] { m_tools->activateShortcut(key); });
        auto* back = new QAction(this);
        back->setShortcut(QKeySequence(Qt::SHIFT | key[0].key()));
        connect(back, &QAction::triggered, this, [this, key] { m_tools->activateShortcut(key, true); });
        addActions({a, back});
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
    file->addActions({A["save"], A["saveAs"], A["saveAll"]});
    file->addSeparator();
    file->addAction(A["pageSetup"]);
    file->addAction(A["print"]);
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
    QMenu* pasteSel = edit->addMenu(Icons::theme({"edit-paste"}), tr("Pegar s&elección"));
    for (int m : {int(SelectionMode::Replace), int(SelectionMode::Union), int(SelectionMode::Exclude),
                  int(SelectionMode::Intersect), int(SelectionMode::Xor)})
        pasteSel->addAction(A[QStringLiteral("pasteSelection%1").arg(m)]);
    m_docActions.append(pasteSel->menuAction());
    edit->addSeparator();
    edit->addActions({A["eraseSel"], A["fillSel"], A["invertSel"], A["selectAll"], A["deselect"]});

    QMenu* view = bar->addMenu(tr("&Ver"));
    view->addActions({A["zoomIn"], A["zoomOut"], A["zoomFit"], A["zoomSel"], A["actualSize"]});
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
    layers->addActions({A["addLayer"], A["deleteLayer"], A["duplicateLayer"], A["mergeDown"], A["toggleLayer"]});
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

    m_adjustmentsMenu = bar->addMenu(tr("A&justes"));
    m_effectsMenu = bar->addMenu(tr("Efe&ctos"));
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
    }
    m_adjustmentsMenu->clear();
    m_effectsMenu->clear();
    m_effectsMenu->addAction(m_act["plugins"]);
    m_effectsMenu->addSeparator();

    QList<const Effect*> effects;
    for (const Effect* e : Effects::all())
        if (!e->hidden())
            effects.append(e);
    const QCollator collator = [] {
        QCollator c;
        c.setCaseSensitivity(Qt::CaseInsensitive);
        return c;
    }();
    std::stable_sort(effects.begin(), effects.end(),
                     [&](const Effect* a, const Effect* b) { return collator.compare(a->name(), b->name()) < 0; });
    QStringList categories;
    for (const Effect* e : std::as_const(effects))
        if (!e->isAdjustment() && !e->category().isEmpty() && !categories.contains(e->category()))
            categories.append(e->category());
    std::sort(categories.begin(), categories.end(), [&](const QString& a, const QString& b) { return collator.compare(a, b) < 0; });
    QHash<QString, QMenu*> submenus;
    for (const QString& cat : std::as_const(categories)) {
        QMenu* sub = m_effectsMenu->addMenu(cat);
        submenus.insert(cat, sub);
        m_effectSubmenus.append(sub);
    }
    for (const Effect* e : std::as_const(effects)) {
        QMenu* menu = e->isAdjustment() ? m_adjustmentsMenu : submenus.value(e->category(), m_effectsMenu);
        QIcon icon = Icons::theme(e->iconName());
        if (icon.isNull() && !e->iconImage().isNull())
            icon = QIcon(QPixmap::fromImage(e->iconImage()));
        const bool hasDialog = !e->params().isEmpty() || e->hasCustomDialog();
        QAction* a = menu->addAction(icon, e->name() + (hasDialog ? QStringLiteral("...") : QString()));
        a->setShortcut(e->shortcut());
        if (!e->pluginName().isEmpty())
            a->setToolTip(tr("%1 (complemento %2)").arg(e->name(), e->pluginName()));
        connect(a, &QAction::triggered, this, [this, e] { runEffect(e, false); });
        m_docActions.append(a);
        m_effectActions.append(a);
    }
    updateActions();
}

void MainWindow::createToolBars()
{
    auto& A = m_act;
    QToolBar* main = addToolBar(tr("Principal"));
    main->setObjectName(QStringLiteral("MainToolBar"));
    main->setMovable(false);
    main->setIconSize(QSize(16, 16));
    main->addActions({A["new"], A["open"], A["save"]});
    main->addSeparator();
    main->addAction(A["print"]);
    main->addSeparator();
    main->addActions({A["cut"], A["copy"], A["paste"]});
    main->addSeparator();
    main->addActions({A["crop"], A["deselect"]});
    main->addSeparator();
    main->addActions({A["undo"], A["redo"]});
    main->addSeparator();
    main->addActions({A["pixelGrid"], A["rulers"]});

    addToolBarBreak();
    m_options = new ToolOptionsBar(m_tools, this);
    addToolBar(m_options);
}

void MainWindow::createTopRow()
{
    // Fila superior de Paint.NET 5: menús, tira de imágenes abiertas y, a la derecha, los
    // botones de las ventanas, Configuración y Ayuda.
    auto* top = new QWidget(this);
    top->setObjectName(QStringLiteral("TopRow"));
    auto* layout = new QHBoxLayout(top);
    layout->setContentsMargins(0, 0, 4, 0);
    layout->setSpacing(6);
    m_menuBar = new QMenuBar(top);
    m_menuBar->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    layout->addWidget(m_menuBar, 0, Qt::AlignVCenter);

    // Tira de imágenes abiertas con miniaturas.
    m_tabs = new QTabBar(top);
    m_tabs->setObjectName(QStringLiteral("ImageStrip"));
    m_tabs->setDocumentMode(true);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setExpanding(false);
    m_tabs->setDrawBase(false);
    m_tabs->setIconSize(QSize(48, 36));
    m_tabs->setUsesScrollButtons(true);
    m_tabs->setElideMode(Qt::ElideRight);
    m_tabs->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    layout->addWidget(m_tabs, 1);
    connect(m_tabs, &QTabBar::currentChanged, this, [this](int i) { setCurrentDocument(m_docs.value(i)); });
    connect(m_tabs, &QTabBar::tabCloseRequested, this, [this](int i) { closeDocument(m_docs.value(i)); });
    connect(m_tabs, &QTabBar::tabMoved, this, [this](int from, int to) { m_docs.move(from, to); });
    m_tabs->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tabs, &QTabBar::customContextMenuRequested, this, &MainWindow::showTabMenu);
    m_tabs->installEventFilter(this);

    // Lista de todas las imágenes (la flecha del final de la tira).
    auto* list = new QToolButton(top);
    list->setAutoRaise(true);
    list->setArrowType(Qt::DownArrow);
    list->setPopupMode(QToolButton::InstantPopup);
    list->setToolTip(tr("Imágenes abiertas"));
    auto* listMenu = new QMenu(list);
    connect(listMenu, &QMenu::aboutToShow, this, [this, listMenu] {
        listMenu->clear();
        for (Document* d : std::as_const(m_docs)) {
            const QString name = (d->isModified() ? QStringLiteral("*") : QString()) + d->displayName();
            QAction* a = listMenu->addAction(m_tabs->tabIcon(int(m_docs.indexOf(d))), name, this,
                                             [this, d] { setCurrentDocument(d); });
            a->setCheckable(true);
            a->setChecked(d == m_doc);
        }
        listMenu->addSeparator();
        listMenu->addActions({m_act["nextImage"], m_act["prevImage"], m_act["tabLeft"], m_act["tabRight"]});
    });
    list->setMenu(listMenu);
    layout->addWidget(list, 0, Qt::AlignVCenter);

    m_cornerButtons = new QWidget(top);
    m_cornerLayout = new QHBoxLayout(m_cornerButtons);
    m_cornerLayout->setContentsMargins(0, 0, 0, 0);
    m_cornerLayout->setSpacing(0);
    layout->addWidget(m_cornerButtons, 0, Qt::AlignVCenter);
    m_cornerButtons->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_cornerButtons, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(this);
        menu.addActions({m_act["resetPanels"], m_act["translucent"]});
        menu.exec(m_cornerButtons->mapToGlobal(pos));
    });
    setMenuWidget(top);
}

void MainWindow::createPanels()
{
    using Corner = FloatingPanel::Corner;
    auto* toolsWidget = new ToolsPanel(m_tools);
    m_toolsPanel = m_workspace->addPanel(QStringLiteral("tools"), tr("Herramientas"), toolsWidget, Corner::TopLeft,
                                         QPoint(8, 8), QSize());

    m_history = new HistoryPanel(m_undoGroup, m_act["undo"], m_act["redo"]);
    connect(m_history, &HistoryPanel::aboutToJump, this, &MainWindow::finishTool);
    m_historyPanel = m_workspace->addPanel(QStringLiteral("history"), tr("Historial"), m_history, Corner::TopRight,
                                           QPoint(8, 8), QSize(210, 240));

    m_layers = new LayersPanel({m_act["addLayer"], m_act["deleteLayer"], m_act["duplicateLayer"], m_act["mergeDown"],
                                m_act["layerUp"], m_act["layerDown"], m_act["layerProps"]});
    connect(m_layers, &LayersPanel::visibilityToggled, this, &MainWindow::toggleLayerVisibility);
    connect(m_layers, &LayersPanel::propertiesRequested, this, &MainWindow::layerProperties);
    m_layersPanel = m_workspace->addPanel(QStringLiteral("layers"), tr("Capas"), m_layers, Corner::BottomRight,
                                          QPoint(8, 8), QSize(230, 260));

    m_colors = new ColorsPanel;
    m_colorsPanel = m_workspace->addPanel(QStringLiteral("colors"), tr("Colores"), m_colors, Corner::BottomLeft,
                                          QPoint(8, 8), QSize(230, 330));
    connect(m_colors, &ColorsPanel::layoutChanged, this, [this] {
        const QSize hint = m_colorsPanel->sizeHint();
        if (m_colors->isExpanded()) {
            m_colorsCompactWidth = m_colorsPanel->width();
            m_colorsPanel->resize(std::max(hint.width(), m_colorsPanel->width()), hint.height());
        } else {
            m_colorsPanel->resize(m_colorsCompactWidth > 0 ? m_colorsCompactWidth : m_colorsPanel->width(), hint.height());
        }
        m_colorsPanel->reposition();
    });

    // Botones de las ventanas en la esquina superior derecha, como Paint.NET.
    const QList<std::tuple<FloatingPanel*, QString, QIcon, Qt::Key>> panels{
        {m_toolsPanel, tr("Herramientas"), Icons::theme({"tools", "draw-brush"}), Qt::Key_F5},
        {m_historyPanel, tr("Historial"), Icons::theme({"view-history"}), Qt::Key_F6},
        {m_layersPanel, tr("Capas"), Icons::theme({"layer-visible-on", "layer-new"}), Qt::Key_F7},
        {m_colorsPanel, tr("Colores"), Icons::theme({"color-management", "color-picker"}), Qt::Key_F8},
    };
    for (const auto& [panel, name, icon, key] : panels) {
        QAction* a = makeAction(name, {}, QKeySequence(key), [] {});
        a->setIcon(icon);
        a->setCheckable(true);
        a->setChecked(!panel->isHidden());
        a->setToolTip(QStringLiteral("%1 (%2)").arg(name, QKeySequence(key).toString(QKeySequence::NativeText)));
        connect(a, &QAction::toggled, panel, [panel](bool on) {
            panel->setVisible(on);
            if (on) {
                panel->raise();
                panel->reposition();
            }
        });
        connect(panel, &FloatingPanel::closed, a, [a] { a->setChecked(false); });
        addAction(a);
        auto* b = new QToolButton(m_cornerButtons);
        b->setDefaultAction(a);
        b->setAutoRaise(true);
        b->setIconSize(QSize(16, 16));
        m_cornerLayout->addWidget(b);
    }
    auto* gear = new QToolButton(m_cornerButtons);
    gear->setDefaultAction(m_act["settings"]);
    gear->setAutoRaise(true);
    gear->setIconSize(QSize(16, 16));
    m_cornerLayout->addSpacing(8);
    m_cornerLayout->addWidget(gear);
    auto* help = new QToolButton(m_cornerButtons);
    help->setIcon(Icons::theme({"help-contents", "help-about"}));
    help->setToolTip(tr("Ayuda"));
    help->setAutoRaise(true);
    help->setIconSize(QSize(16, 16));
    help->setPopupMode(QToolButton::InstantPopup);
    auto* helpMenu = new QMenu(help);
    helpMenu->addAction(m_act["docs"]);
    helpMenu->addSeparator();
    helpMenu->addActions({m_act["plugins"], m_act["updates"]});
    helpMenu->addSeparator();
    helpMenu->addActions({m_act["about"], m_act["aboutQt"]});
    help->setMenu(helpMenu);
    m_cornerLayout->addWidget(help);

    connect(m_layers, &LayersPanel::soloRequested, this, &MainWindow::soloLayer);
    connect(m_layers, &LayersPanel::moveRequested, this, &MainWindow::moveLayerTo);
    connect(PluginManager::instance(), &PluginManager::changed, this, &MainWindow::rebuildEffectMenus);
}

void MainWindow::createStatusBar()
{
    // Barra de estado de Paint.NET 5: ayuda de la herramienta, tamaño de la selección,
    // posición del cursor, unidades, zum y su deslizador.
    QStatusBar* bar = statusBar();
    m_hint = new QLabel(bar);
    m_hint->setTextFormat(Qt::PlainText);
    m_hint->setMinimumWidth(100);
    m_hint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    bar->addWidget(m_hint, 1);

    auto item = [bar](std::initializer_list<const char*> icons, const QString& tip) {
        auto* w = new QWidget(bar);
        auto* l = new QHBoxLayout(w);
        l->setContentsMargins(6, 0, 6, 0);
        l->setSpacing(4);
        auto* icon = new QLabel(w);
        icon->setPixmap(Icons::theme(icons).pixmap(16, 16));
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
    m_selLabel = item({"select-rectangular"}, tr("Tamaño de la selección"));
    m_posLabel = item({"tool-pointer", "edit-select"}, tr("Posición del cursor"));

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
    connect(m_canvas, &Canvas::zoomChanged, this, [this](qreal z) {
        const QSignalBlocker b1(m_zoomSlider), b2(m_zoomBox);
        m_zoomSlider->setValue(zoomToSlider(z));
        m_zoomBox->setEditText(zoomText(z));
    });
    connect(m_canvas, &Canvas::cursorMoved, this, [this](QPoint p, bool inside) {
        if (!inside || !m_doc) {
            m_posLabel->clear();
            return;
        }
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
    for (const QString& f : files) {
        QAction* a = m_recentMenu->addAction(QStringLiteral("&%1 %2").arg(++n % 10).arg(QFileInfo(f).fileName()), this,
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
    {
        const QSignalBlocker block(m_tabs);
        m_tabs->setCurrentIndex(int(m_docs.indexOf(doc)));
    }
    updateTitle();
    updateActions();
    updateStatus();
}

bool MainWindow::closeDocument(Document* doc)
{
    if (!doc)
        return true;
    finishTool();
    if (!maybeSave(doc))
        return false;
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
    if (doc == m_doc)
        m_doc = nullptr;
    setCurrentDocument(next, true);
    doc->deleteLater();
    return true;
}

bool MainWindow::maybeSave(Document* doc)
{
    if (!doc->isModified())
        return true;
    setCurrentDocument(doc);
    TaskDialog dlg(tr("Cambios no guardados"), Icons::theme({"dialog-warning"}), doc->flattened(),
                   tr("«%1» tiene cambios sin guardar.\n¿Qué quiere hacer?").arg(doc->displayName()), this);
    const int saveChoice = dlg.addChoice(Icons::theme({"document-save"}), tr("Guardar"),
                                         tr("Guardar la imagen y, a continuación, cerrarla."), true);
    const int discard = dlg.addChoice(Icons::theme({"edit-delete", "user-trash"}), tr("No guardar"),
                                      tr("Descartar los cambios no guardados."));
    dlg.addChoice(Icons::theme({"edit-undo"}), tr("Cancelar"), tr("Volver a Kaint."));
    const int answer = dlg.choose();
    if (answer == saveChoice)
        return save(doc);
    return answer == discard;
}

void MainWindow::updateTab(Document* doc)
{
    const int i = int(m_docs.indexOf(doc));
    if (i < 0)
        return;
    const qreal dpr = devicePixelRatioF();
    const QImage source = doc == m_doc ? m_canvas->composite() : doc->flattened();
    QImage thumb = Raster::thumbnail(source, m_tabs->iconSize() * dpr);
    if (doc->isModified()) {
        QPainter p(&thumb);
        p.setRenderHint(QPainter::Antialiasing);
        const qreal r = 5 * dpr;
        p.setPen(QPen(Qt::white, 1.5 * dpr));
        p.setBrush(QColor(255, 140, 0));
        p.drawEllipse(QPointF(r + 1, r + 1), r, r);
    }
    thumb.setDevicePixelRatio(dpr);
    m_tabs->setTabIcon(i, QIcon(QPixmap::fromImage(thumb)));
    m_tabs->setTabToolTip(i, QStringLiteral("%1%2\n%3 × %4")
                                 .arg(doc->isModified() ? QStringLiteral("*") : QString(), doc->displayName())
                                 .arg(doc->size().width())
                                 .arg(doc->size().height()));
}

void MainWindow::updateTitle()
{
    if (!m_doc) {
        setWindowTitle(QStringLiteral("Kaint"));
        return;
    }
    setWindowTitle(QStringLiteral("%1%2 - Kaint").arg(m_doc->isModified() ? QStringLiteral("*") : QString(), m_doc->displayName()));
}

void MainWindow::updateActions()
{
    const bool has = m_doc != nullptr;
    const bool sel = has && !m_doc->selection().isEmpty();
    for (QAction* a : std::as_const(m_docActions))
        a->setEnabled(has);
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
    m_act["undo"]->setEnabled(m_undoGroup->canUndo());
    m_act["redo"]->setEnabled(m_undoGroup->canRedo());
    m_act["repeatEffect"]->setEnabled(has && m_lastEffect);
}

void MainWindow::updateStatus()
{
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
    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Abrir"), m_lastDir, ImageIO::openFilter());
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
    for (const QString& path : paths) {
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
        QApplication::setOverrideCursor(Qt::WaitCursor);
        Document* doc = ImageIO::load(abs, &error);
        QApplication::restoreOverrideCursor();
        if (!doc) {
            QMessageBox::critical(this, tr("No se pudo abrir"), tr("No se pudo abrir «%1»:\n%2").arg(abs, error));
            continue;
        }
        if (m_docs.size() == 1 && m_docs.first()->filePath().isEmpty() && !m_docs.first()->isModified()
            && m_docs.first()->undoStack()->count() == 0)
            closeDocument(m_docs.first());
        addDocument(doc);
        AppState::instance()->addRecentFile(abs, m_canvas->composite());
        m_lastDir = QFileInfo(abs).absolutePath();
    }
}

bool MainWindow::save(Document* doc)
{
    if (!doc)
        return false;
    if (doc->filePath().isEmpty() || !ImageIO::isWritable(doc->filePath()))
        return saveAs(doc);
    return writeFile(doc, doc->filePath());
}

bool MainWindow::saveAs(Document* doc)
{
    if (!doc)
        return false;
    QString selected;
    const QString filters = ImageIO::saveFilter(&selected);
    QString suggestion = doc->filePath();
    if (!suggestion.isEmpty() && !ImageIO::isWritable(suggestion)) {
        suggestion = QFileInfo(suggestion).absolutePath() + QLatin1Char('/') + QFileInfo(suggestion).completeBaseName()
                   + QStringLiteral(".ora");
        for (const QString& f : filters.split(QStringLiteral(";;")))
            if (f.contains(QStringLiteral("*.ora")))
                selected = f;
    }
    if (suggestion.isEmpty()) {
        const QString base = tr("Sin título");
        suggestion = m_lastDir + QLatin1Char('/') + base + (doc->layerCount() > 1 ? QStringLiteral(".ora") : QStringLiteral(".png"));
        if (doc->layerCount() > 1)
            for (const QString& f : filters.split(QStringLiteral(";;")))
                if (f.contains(QStringLiteral("*.ora")))
                    selected = f;
    }
    QString path = QFileDialog::getSaveFileName(this, tr("Guardar como"), suggestion, filters, &selected);
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

bool MainWindow::closeAll(Document* except)
{
    for (Document* d : QList<Document*>(m_docs)) {
        if (d == except)
            continue;
        if (!closeDocument(d))
            return false;
    }
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
    Document* doc = m_docs.value(m_tabs->tabAt(pos));
    if (!doc)
        return;
    setCurrentDocument(doc);
    QMenu menu(this);
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

bool MainWindow::writeFile(Document* doc, const QString& path)
{
    finishTool();
    if (!ImageIO::isLayered(path) && !ImageIO::isAnimation(path) && doc->layerCount() > 1) {
        QMessageBox box(QMessageBox::Question, tr("Acoplar imagen"),
                        tr("El formato «%1» no admite capas, así que se guardará la imagen acoplada.\n"
                           "El documento abierto conserva sus capas. Para guardarlas, usa OpenRaster (.ora).")
                            .arg(QFileInfo(path).suffix().toUpper()),
                        QMessageBox::Save | QMessageBox::Cancel, this);
        box.button(QMessageBox::Save)->setText(tr("Guardar acoplada"));
        box.button(QMessageBox::Cancel)->setText(tr("Cancelar"));
        if (box.exec() != QMessageBox::Save)
            return false;
    }
    const QString suffix = QFileInfo(path).suffix().toLower();
    ImageIO::SaveOptions options = m_saveOptions.value(suffix);
    if (ImageIO::isAnimation(path) && !m_saveOptions.contains(suffix))
        options.loops = doc->gifLoops();
    if (SaveConfigDialog::hasOptions(path)) {
        const QList<Gif::Frame> frames = ImageIO::isAnimation(path) && doc->layerCount() > 1
                                             ? ImageIO::animationFrames(doc, 0)
                                             : QList<Gif::Frame>();
        SaveConfigDialog dlg(doc->flattened(), path, options, this, frames);
        if (dlg.exec() != QDialog::Accepted)
            return false;
        options = dlg.options();
        m_saveOptions.insert(suffix, options);
    }
    QString error;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = ImageIO::save(doc, path, options, &error);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::critical(this, tr("No se pudo guardar"), tr("No se pudo guardar «%1»:\n%2").arg(path, error));
        return false;
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

void MainWindow::pageSetup()
{
    QPageSetupDialog dlg(printer(), this);
    dlg.exec();
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    finishTool();
    for (Document* d : QList<Document*>(m_docs)) {
        if (!maybeSave(d)) {
            e->ignore();
            return;
        }
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
    if (!files.isEmpty()) {
        openFiles(files);
    } else if (e->mimeData()->hasImage()) {
        const QImage img = qvariant_cast<QImage>(e->mimeData()->imageData());
        if (!img.isNull())
            addDocument(Document::createFromImage(img, tr("Fondo")));
    }
    e->acceptProposedAction();
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
    finishTool();
    m_undoGroup->undo();
}

void MainWindow::redo()
{
    finishTool();
    m_undoGroup->redo();
}

void MainWindow::copy(bool merged)
{
    if (!m_doc)
        return;
    finishTool();
    const QImage source = merged ? m_doc->flattened() : m_doc->activeLayer()->image;
    const QImage out = Raster::extractSelection(source, m_doc->selection());
    QApplication::clipboard()->setImage(out.convertToFormat(QImage::Format_ARGB32));
}

void MainWindow::cut()
{
    if (!m_doc)
        return;
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
    dlg.addChoice(Icons::theme({"edit-undo"}), tr("Cancelar"),
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
    QPoint pos = m_canvas->visibleImageRect().topLeft();
    pos.setX(std::clamp(pos.x(), 0, std::max(0, m_doc->size().width() - img.width())));
    pos.setY(std::clamp(pos.y(), 0, std::max(0, m_doc->size().height() - img.height())));
    MoveSelectedTool::startPaste(m_doc, img, pos, tr("Pegar"));
    m_tools->select(ToolId::MoveSelected);
}

void MainWindow::pasteIntoNewLayer()
{
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
    QPoint pos = m_canvas->visibleImageRect().topLeft();
    pos.setX(std::clamp(pos.x(), 0, std::max(0, m_doc->size().width() - img.width())));
    pos.setY(std::clamp(pos.y(), 0, std::max(0, m_doc->size().height() - img.height())));
    MoveSelectedTool::startPaste(m_doc, img, pos, text);
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
    resizeCanvasTo(dlg.canvasSize(), dlg.anchor(), tr("Tamaño del lienzo"), dlg.dpi());
}

void MainWindow::resizeCanvasTo(QSize size, Qt::Alignment anchor, const QString& text, double dpi)
{
    const QSize old = m_doc->size();
    const int dx = (anchor & Qt::AlignLeft) ? 0 : (anchor & Qt::AlignRight) ? size.width() - old.width()
                                                                            : (size.width() - old.width()) / 2;
    const int dy = (anchor & Qt::AlignTop) ? 0 : (anchor & Qt::AlignBottom) ? size.height() - old.height()
                                                                            : (size.height() - old.height()) / 2;
    const QColor fill = AppState::instance()->secondary();
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
    if (!m_doc || m_doc->layerCount() < 2)
        return;
    finishTool();
    DocumentState before = DocumentState::capture(m_doc);
    DocumentState after;
    auto layer = std::make_shared<Layer>(m_doc->layer(0)->props.name, m_doc->flattened());
    after.size = before.size;
    after.layers = {layer};
    after.images = {layer->image};
    after.props = {layer->props};
    after.active = 0;
    after.selection = before.selection;
    push(new DocumentStateCommand(m_doc, before, after, tr("Acoplar")), QStringLiteral("layer-bottom"));
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
    push(new DocumentStateCommand(m_doc, before, after, tr("Combinar capa con la inferior")), QStringLiteral("merge"));
}

void MainWindow::importLayer()
{
    if (!m_doc)
        return;
    finishTool();
    const QString path = QFileDialog::getOpenFileName(this, tr("Importar desde archivo"), m_lastDir, ImageIO::openFilter());
    if (path.isEmpty())
        return;
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage img = reader.read();
    if (img.isNull()) {
        QMessageBox::critical(this, tr("No se pudo importar"), reader.errorString());
        return;
    }
    if (!ensureCanvasFits(img, tr("Importar desde archivo"), true))
        return;
    auto layer = std::make_shared<Layer>(QFileInfo(path).completeBaseName(), m_doc->size());
    Raster::blit(layer->image, img.convertToFormat(kLayerFormat), QPoint(0, 0));
    push(new InsertLayerCommand(m_doc, m_doc->activeIndex() + 1, layer, tr("Importar desde archivo")),
         QStringLiteral("document-import"));
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
             QStringLiteral("document-properties"));
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
        std::thread worker([&] {
            accepted = effect->showCustomDialog(layer->image, roi, ctx, &error);
            done = true;
        });
        setEnabled(false);
        while (!done)
            QApplication::processEvents(QEventLoop::AllEvents, 30);
        worker.join();
        setEnabled(true);
        if (!accepted) {
            if (!error.isEmpty())
                QMessageBox::warning(this, effect->name(), error);
            return;
        }
    }
    if (!effect->params().isEmpty() && !repeat) {
        EffectDialog dlg(*effect, values, layer->image, roi, this);
        EffectPreview preview(m_doc, layer, *effect, ctx);
        connect(&dlg, &EffectDialog::valuesChanged, &preview, &EffectPreview::request);
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
    if (result.isNull())
        return;

    const QImage before = layer->image;
    layer->image = result;
    m_doc->clearPreview();
    const QRect r = m_doc->selection().isEmpty() ? m_doc->rect() : m_doc->selection().bounds();
    m_doc->notifyPixelsChanged(layer.get(), r);
    pushPixelPatch(m_doc, layer, before, r, effect->name(), effect->iconName());

    if (!effect->hidden()) {
        m_lastEffect = effect;
        m_act["repeatEffect"]->setText(tr("Repetir «%1»").arg(effect->name()));
    }
    m_effectValues.insert(effect->id(), values);
    updateActions();
}

// ---------------------------------------------------------------------------
// Configuración
// ---------------------------------------------------------------------------

void MainWindow::showSettings(SettingsDialog::Page page)
{
    finishTool();
    SettingsDialog dlg(m_settings, m_tools, this);
    dlg.showPage(page);
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
}

void MainWindow::applySettings()
{
    {
        const QSignalBlocker b1(m_act["translucent"]), b2(m_act["rulers"]);
        m_act["translucent"]->setChecked(m_settings.translucentPanels);
        m_act["rulers"]->setChecked(m_settings.rulers);
    }
    m_workspace->setTranslucentPanels(m_settings.translucentPanels);
    m_canvas->setRulersVisible(m_settings.rulers);
    m_canvas->setAppearance(m_settings.dropShadow, m_settings.canvasBorder, m_settings.checkerBrightness);
    m_canvas->setOverscroll(m_settings.overscroll);
    m_canvas->setAutoScroll(m_settings.autoScroll);
    if (Theme::current() != Theme::Scheme(m_settings.colorScheme))
        Theme::apply(Theme::Scheme(m_settings.colorScheme));
    for (auto effect : {Qt::UI_AnimateMenu, Qt::UI_FadeMenu, Qt::UI_AnimateCombo, Qt::UI_AnimateTooltip,
                        Qt::UI_FadeTooltip, Qt::UI_AnimateToolBox})
        QApplication::setEffectEnabled(effect, m_settings.animations);
    QApplication::setEffectEnabled(Qt::UI_General, m_settings.animations);
    setUnit(m_settings.unit);
    checkHistoryMemory();
    PresenceOptions presence;
    presence.enabled = m_settings.discordEnabled;
    presence.showFile = m_settings.discordShowFile;
    presence.showLayer = m_settings.discordShowLayer;
    presence.showFileInfo = m_settings.discordShowFileInfo;
    presence.showTool = m_settings.discordShowTool;
    presence.showElapsed = m_settings.discordShowElapsed;
    m_discord->setOptions(presence);
    updatePresence();
}

PresenceInfo MainWindow::presenceInfo() const
{
    PresenceInfo info;
    info.documents = int(m_docs.size());
    if (!m_doc)
        return info;
    info.hasDocument = true;
    const QFileInfo file(m_doc->filePath());
    if (!m_doc->filePath().isEmpty()) {
        info.fileName = file.fileName();
        info.fileType = file.suffix().toUpper();
    }
    info.size = m_doc->size();
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
        box.exec();
        if (box.clickedButton() == open)
            QDesktopServices::openUrl(QUrl(url.isEmpty() ? QStringLiteral(KAINT_REPOSITORY) : url));
    });
}

void MainWindow::startupChecks()
{
    if (m_settings.checkUpdates)
        QTimer::singleShot(3000, this, [this] { checkForUpdates(false); });
}
