// Prueba de humo de la interfaz: arranca la ventana sin pantalla (plataforma offscreen), usa
// varias herramientas como lo haría el ratón, comprueba el resultado y guarda capturas para
// revisarlas a mano.

#include "app/AppState.h"
#include "app/Canvas.h"
#include "app/DiscordPresence.h"
#include "app/Icons.h"
#include "app/MainWindow.h"
#include "app/Recovery.h"
#include "app/Theme.h"
#include "app/ToolOptionsBar.h"
#include "app/Workspace.h"
#include "app/dialogs/AboutDialog.h"
#include "app/dialogs/EffectDialog.h"
#include "app/dialogs/SaveConfigDialog.h"
#include "app/dialogs/SettingsDialog.h"
#include "app/dialogs/TaskDialog.h"
#include "app/panels/ColorsPanel.h"
#include "app/panels/LayersPanel.h"
#include "app/panels/ToolsPanel.h"
#include "app/tools/Handles.h"
#include "app/tools/ToolManager.h"
#include "app/tools/TransformBox.h"
#include "app/widgets/FontChooser.h"
#include "app/widgets/WaveLabel.h"
#include "core/CustomShapes.h"
#include "core/Document.h"
#include "core/Ora.h"
#include "core/plugins/PluginManager.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsBlurEffect>
#include <QImage>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineF>
#include <QListWidget>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QMouseEvent>
#include <QPrinter>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QStatusBar>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QtEndian>
#include <QUndoStack>
#include <QWheelEvent>

#include <cmath>
#include <cstdio>

static int g_failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FALLO %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

static void settle()
{
    for (int i = 0; i < 5; ++i)
        QApplication::processEvents();
}

static void drag(Canvas* c, const QList<QPointF>& pts, Qt::MouseButton button = Qt::LeftButton,
                 Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QWidget* vp = c->viewport();
    auto send = [&](QEvent::Type type, const QPointF& img, Qt::MouseButton b, Qt::MouseButtons bs) {
        const QPointF v = c->imageToView(img);
        QMouseEvent e(type, v, vp->mapToGlobal(v), b, bs, mods);
        QApplication::sendEvent(vp, &e);
    };
    send(QEvent::MouseButtonPress, pts.first(), button, button);
    QElapsedTimer t;
    t.start();
    for (const QPointF& p : pts)
        send(QEvent::MouseMove, p, Qt::NoButton, button);
    if (pts.size() > 1)
        while (t.elapsed() < 70)
            QApplication::processEvents(QEventLoop::AllEvents, 10);
    send(QEvent::MouseButtonRelease, pts.last(), button, Qt::NoButton);
    settle();
}

static void typeText(Canvas* c, const QString& text)
{
    for (const QChar ch : text) {
        QKeyEvent press(QEvent::KeyPress, 0, Qt::NoModifier, QString(ch));
        QApplication::sendEvent(c, &press);
    }
    settle();
}

static QAction* findAction(QWidget* w, const QString& text)
{
    for (QAction* a : w->findChildren<QAction*>())
        if (a->text().remove(QLatin1Char('&')) == text)
            return a;
    return nullptr;
}

// Lee una trama del IPC de Discord: [código][longitud][JSON].
static bool readFrame(QLocalSocket* s, int* opcode, QJsonObject* payload, int timeoutMs = 2000)
{
    QElapsedTimer t;
    t.start();
    while (s->bytesAvailable() < 8 && t.elapsed() < timeoutMs)
        QApplication::processEvents(QEventLoop::AllEvents, 10), s->waitForReadyRead(10);
    if (s->bytesAvailable() < 8)
        return false;
    const QByteArray head = s->read(8);
    *opcode = int(qFromLittleEndian<quint32>(head.constData()));
    const qint64 length = qFromLittleEndian<quint32>(head.constData() + 4);
    while (s->bytesAvailable() < length && t.elapsed() < timeoutMs)
        QApplication::processEvents(QEventLoop::AllEvents, 10), s->waitForReadyRead(10);
    *payload = QJsonDocument::fromJson(s->read(length)).object();
    return true;
}

// Rich Presence: saludo, READY y SET_ACTIVITY contra un "Discord" falso.
static void testDiscordPresence()
{
    PresenceInfo info;
    info.hasDocument = true;
    info.fileName = QStringLiteral("foto.png");
    info.fileType = QStringLiteral("PNG");
    info.size = QSize(1920, 1080);
    info.layerIndex = 2;
    info.layerCount = 3;
    info.layerName = QStringLiteral("Sombras");
    info.tool = QStringLiteral("Pincel");
    PresenceOptions options;
    QJsonObject a = DiscordPresence::buildActivity(info, options, 1000);
    CHECK(a.value(QStringLiteral("details")).toString().contains(QStringLiteral("foto.png")));
    const QString state = a.value(QStringLiteral("state")).toString();
    CHECK(state.contains(QStringLiteral("Sombras")) && state.contains(QStringLiteral("2 de 3")));
    CHECK(state.contains(QStringLiteral("PNG")) && state.contains(QStringLiteral("1920 × 1080")));
    CHECK(a.value(QStringLiteral("timestamps")).toObject().value(QStringLiteral("start")).toInteger() == 1000);
    CHECK(a.value(QStringLiteral("assets")).toObject().value(QStringLiteral("small_text")).toString() == QLatin1String("Pincel"));
    options.showFile = options.showLayer = options.showFileInfo = options.showTool = options.showElapsed = false;
    a = DiscordPresence::buildActivity(info, options, 1000);
    CHECK(!QJsonDocument(a).toJson().contains("foto") && !a.contains(QStringLiteral("state")));
    CHECK(!a.contains(QStringLiteral("timestamps")));
    CHECK(!DiscordPresence::isValidClientId(QStringLiteral("PON___AQUI")));
    CHECK(DiscordPresence::isValidClientId(QStringLiteral("123456789012345678")));

    QTemporaryDir dir;
    const QByteArray oldRuntime = qgetenv("XDG_RUNTIME_DIR");
    qputenv("XDG_RUNTIME_DIR", QFile::encodeName(dir.path()));
    QLocalServer server;
    CHECK(server.listen(dir.filePath(QStringLiteral("discord-ipc-0"))));
    DiscordPresence presence;
    presence.setIntervals(50, 200);
    PresenceOptions on;
    on.clientId = QStringLiteral("123456789012345678");
    presence.update(info);
    presence.setOptions(on);
    CHECK(server.waitForNewConnection(2000));
    QLocalSocket* client = server.nextPendingConnection();
    CHECK(client);
    if (client) {
        int op = -1;
        QJsonObject msg;
        CHECK(readFrame(client, &op, &msg) && op == 0);
        CHECK(msg.value(QStringLiteral("client_id")).toString() == on.clientId);
        CHECK(presence.status() == DiscordPresence::Status::Connecting);
        client->write(DiscordPresence::frame(1, QJsonObject{{QStringLiteral("cmd"), QStringLiteral("DISPATCH")},
                                                           {QStringLiteral("evt"), QStringLiteral("READY")}}));
        client->flush();
        CHECK(readFrame(client, &op, &msg) && op == 1);
        CHECK(presence.status() == DiscordPresence::Status::Connected);
        CHECK(msg.value(QStringLiteral("cmd")).toString() == QLatin1String("SET_ACTIVITY"));
        const QJsonObject activity = msg.value(QStringLiteral("args")).toObject().value(QStringLiteral("activity")).toObject();
        CHECK(activity.value(QStringLiteral("details")).toString().contains(QStringLiteral("foto.png")));
        info.layerIndex = 1;
        info.layerName = QStringLiteral("Fondo");
        presence.update(info);
        CHECK(readFrame(client, &op, &msg) && op == 1);
        CHECK(msg.value(QStringLiteral("args")).toObject().value(QStringLiteral("activity")).toObject()
                  .value(QStringLiteral("state")).toString().contains(QStringLiteral("Fondo")));
        client->write(DiscordPresence::frame(3, QJsonObject{{QStringLiteral("n"), 7}}));
        client->flush();
        CHECK(readFrame(client, &op, &msg) && op == 4 && msg.value(QStringLiteral("n")).toInt() == 7);
        on.enabled = false;
        presence.setOptions(on);
        CHECK(presence.status() == DiscordPresence::Status::Disabled);
    }
    qputenv("XDG_RUNTIME_DIR", oldRuntime);
}

// Cuadro de transformación de las herramientas Mover: el punto central se puede mover, el giro
// (botón derecho) es alrededor de él y se restablece.
static void testTransformBox()
{
    auto near = [](QPointF a, QPointF b) { return QLineF(a, b).length() < 1e-6; };
    TransformBox box;
    box.set(QRectF(0, 0, 100, 50), QTransform());
    CHECK(near(box.pivot(), QPointF(50, 25)) && !box.pivotMoved());
    const QTransform view = QTransform::fromScale(2, 2);
    CHECK(box.hit(QPointF(100, 50), view, false, nullptr) == TransformBox::Mode::Pivot);
    CHECK(box.hit(QPointF(100, 50), view, true, nullptr) == TransformBox::Mode::Rotate);
    int handle = -1;
    CHECK(box.hit(QPointF(200, 100), view, false, &handle) == TransformBox::Mode::Scale && handle >= 0);
    bool inside = true;
    CHECK(box.hit(QPointF(600, 600), view, false, nullptr, &inside) == TransformBox::Mode::Translate && !inside);
    CHECK(box.hit(QPointF(220, 50), view, false, nullptr) == TransformBox::Mode::Rotate);
    CHECK(box.hit(QPointF(100, -25), view, false, nullptr) == TransformBox::Mode::Rotate);
    CHECK(box.hit(QPointF(150, 60), view, false, nullptr) == TransformBox::Mode::Translate);

    box.begin(TransformBox::Mode::Pivot, -1, QPointF(50, 25));
    box.update(QPointF(70, 30), Qt::NoModifier);
    box.end();
    CHECK(near(box.pivot(), QPointF(70, 30)) && box.pivotMoved());

    // 90° alrededor del punto movido: el punto no se desplaza.
    box.begin(TransformBox::Mode::Rotate, -1, QPointF(120, 30));
    box.update(QPointF(70, 80), Qt::NoModifier);
    box.end();
    CHECK(std::abs(box.rotationDegrees() - 90) < 1e-6);
    CHECK(near(box.pivot(), QPointF(70, 30)));
    CHECK(near(box.transform().map(QPointF(0, 0)), QPointF(100, -40)));
    box.begin(TransformBox::Mode::Rotate, -1, QPointF(70, 80));
    box.update(QPointF(70 - 50 * std::sin(0.3), 30 + 50 * std::cos(0.3)), Qt::ShiftModifier);
    box.end();
    CHECK(std::abs(box.rotationDegrees() - 105) < 1e-6);

    box.resetPivot();
    CHECK(!box.pivotMoved());
    CHECK(near(box.pivot(), box.transform().map(QPointF(50, 25))));
    box.begin(TransformBox::Mode::Pivot, -1, box.pivot());
    box.update(box.transform().map(QPointF(10, 10)), Qt::NoModifier);
    box.end();
    box.set(QRectF(0, 0, 100, 50), box.transform());
    CHECK(box.pivotMoved());
    box.set(QRectF(0, 0, 40, 40), QTransform());
    CHECK(!box.pivotMoved() && near(box.pivot(), QPointF(20, 20)));
}

// Temas: claro y oscuro de Paint.NET, el personalizado (colores propios, JSON, imagen de fondo),
// la detección del modo oscuro de Plasma y que entrar o salir del tema KDE pide reiniciar. El
// fondo del lienzo siempre es opaco (un color translúcido dejaba rastros).
static void testTheme()
{
    CHECK(!Theme::needsRestart(Theme::Scheme::Dark) && !Theme::needsRestart(Theme::Scheme::Custom));
    CHECK(Theme::needsRestart(Theme::Scheme::Kde));
    Theme::apply(Theme::Scheme::Light);
    CHECK(Theme::current() == Theme::Scheme::Light && !Theme::isDark());
    CHECK(QApplication::palette().color(QPalette::Window) == Theme::preset(false).window);
    CHECK(qApp->styleSheet().contains(QStringLiteral("QMenuBar")));
    Theme::apply(Theme::Scheme::Dark);
    CHECK(Theme::isDark() && Theme::workspaceColor() == Theme::preset(true).workspace);

    Theme::Spec custom;
    custom.dark = true;
    custom.accent = QColor(200, 30, 120);
    custom.iconSize = 24;
    custom.radius = 6;
    custom.buttonStyle = Theme::ButtonStyle::Rounded;
    CHECK(Theme::fromJson(Theme::toJson(custom)) == custom);
    Theme::setCustom(custom);
    Theme::apply(Theme::Scheme::Custom);
    CHECK(QApplication::palette().color(QPalette::Highlight) == custom.accent);
    CHECK(QApplication::palette().color(QPalette::Window) == Theme::preset(true).window);
    CHECK(Theme::active().iconSize == 24);

    QTemporaryDir dir;
    QImage tile(4, 4, QImage::Format_ARGB32);
    tile.fill(QColor(250, 0, 0));
    tile.save(dir.filePath(QStringLiteral("fondo.png")));
    custom.workspaceImage = dir.filePath(QStringLiteral("fondo.png"));
    custom.imageMode = Theme::ImageMode::Stretch;
    Theme::setCustom(custom);
    {
        Canvas canvas;
        canvas.resize(120, 90);
        CHECK(canvas.viewport()->grab().toImage().pixelColor(5, 5) == QColor(250, 0, 0));
    }

    // Fondo con tinte (blanco al 100 %) y exportación con la imagen dentro: al importar, la imagen
    // queda copiada en la carpeta de Kaint y el resto de opciones se conserva.
    custom.imageTint = Qt::white;
    custom.imageTintStrength = 100;
    custom.imageScale = 150;
    custom.imageAnchor = 2;
    custom.imageBlur = 3;
    custom.imageGrayscale = true;
    custom.classic = true;
    custom.panelTitleEnd = QColor(10, 130, 210);
    Theme::setCustom(custom);
    {
        Canvas canvas;
        canvas.resize(120, 90);
        CHECK(canvas.viewport()->grab().toImage().pixelColor(5, 5) == QColor(Qt::white));
    }
    {
        Theme::Spec back = Theme::fromJson(Theme::toJson(custom, true));
        CHECK(back.workspaceImage != custom.workspaceImage && QFile::exists(back.workspaceImage));
        CHECK(QImage(back.workspaceImage).pixelColor(0, 0) == QColor(250, 0, 0));
        back.workspaceImage = custom.workspaceImage;
        CHECK(back == custom);
        QFile::remove(Theme::fromJson(Theme::toJson(custom, true)).workspaceImage);
    }

    // Windows 98: gris con relieve, cabeceras en degradado y escritorio verde azulado.
    Theme::apply(Theme::Scheme::Windows98);
    CHECK(Theme::active().classic && QApplication::palette().color(QPalette::Window) == QColor(0xc0, 0xc0, 0xc0));
    CHECK(Theme::workspaceColor() == QColor(0x00, 0x80, 0x80) && Theme::active().panelTitleEnd != Theme::active().panelTitle);
    CHECK(qApp->styleSheet().contains(QStringLiteral("border-width: 2px")));
    CHECK(!Theme::needsRestart(Theme::Scheme::Windows98));
    Theme::apply(Theme::Scheme::Light);

    // "Seguir al sistema" con Plasma: kdeglobals dice si el esquema es oscuro.
    QTemporaryDir config;
    const QByteArray oldConfig = qgetenv("XDG_CONFIG_HOME");
    const QByteArray oldDesktop = qgetenv("XDG_CURRENT_DESKTOP");
    qputenv("XDG_CONFIG_HOME", config.path().toLocal8Bit());
    qputenv("XDG_CURRENT_DESKTOP", "KDE");
    auto writeKde = [&config](const char* window, const char* name) {
        QFile f(config.filePath(QStringLiteral("kdeglobals")));
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            f.write(QByteArray("[Colors:Window]\nBackgroundNormal=") + window + "\n\n[General]\nColorScheme=" + name + "\n");
    };
    writeKde("32,35,38", "BreezeDark");
    Theme::notifier()->systemSettingChanged();
    CHECK(Theme::systemPrefersDark());
    Theme::apply(Theme::Scheme::System);
    CHECK(Theme::isDark());
    writeKde("239,240,241", "BreezeLight");
    Theme::notifier()->systemSettingChanged();
    CHECK(!Theme::systemPrefersDark() && !Theme::isDark());
    {
        // Solo el nombre del esquema (sección [General], que QSettings lee sin grupo).
        QFile f(config.filePath(QStringLiteral("kdeglobals")));
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            f.write("[General]\nColorScheme=BreezeDark\n");
    }
    Theme::notifier()->systemSettingChanged();
    CHECK(Theme::systemPrefersDark());

    for (const auto& [name, value] : {std::pair{"XDG_CONFIG_HOME", oldConfig}, std::pair{"XDG_CURRENT_DESKTOP", oldDesktop}}) {
        if (value.isEmpty())
            qunsetenv(name);
        else
            qputenv(name, value);
    }
    Theme::apply(Theme::Scheme::Light);

    // Iconos de Breeze incluidos en Kaint (los que se usan en Windows o sin Breeze en el sistema).
    {
        const QString previous = QIcon::themeName();
        for (const QString& name : {QStringLiteral("kaint-breeze"), QStringLiteral("kaint-breeze-dark")}) {
            CHECK(QFile::exists(QStringLiteral(":/kaint/icon-themes/%1/index.theme").arg(name)));
            QIcon::setThemeName(name);
            CHECK(QIcon::hasThemeIcon(QStringLiteral("layer-new")) && QIcon::hasThemeIcon(QStringLiteral("media-skip-forward")));
            CHECK(!QIcon::fromTheme(QStringLiteral("edit-delete")).pixmap(16, 16).isNull());
        }
        QIcon::setThemeName(previous);
        CHECK(!Icons::breezeTheme(true).isEmpty());
    }

    Canvas canvas;
    canvas.resize(120, 90);
    canvas.setAppearance(false, QColor(10, 20, 30, 0), 255);
    const QColor corner = canvas.viewport()->grab().toImage().pixelColor(3, 3);
    CHECK(corner.alpha() == 255 && corner.red() == 10 && corner.green() == 20 && corner.blue() == 30);
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", qgetenv("QT_QPA_PLATFORM").isEmpty() ? "offscreen" : qgetenv("QT_QPA_PLATFORM"));
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("KaintSmokeTest"));
    QApplication::setApplicationName(QStringLiteral("KaintSmokeTest"));
    QSettings().clear();
    QSettings().setValue(QStringLiteral("settings/colorScheme"), int(Theme::Scheme::Light));
    Icons::setupTheme();
    QTemporaryDir shapesDir;
    {
        QFile f(shapesDir.filePath(QStringLiteral("casa.xaml")));
        if (f.open(QIODevice::WriteOnly))
            f.write("<ps:SimpleGeometryShape xmlns:ps=\"clr-namespace:PaintDotNet.Shapes;assembly=PaintDotNet.Framework\" "
                    "DisplayName=\"Casa de prueba\" Geometry=\"F1 M 0,40 L 50,0 L 100,40 L 100,100 L 0,100 Z\" />");
    }
    CustomShapes::setSearchPathsForTesting({shapesDir.path()});
    const QString outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();

    MainWindow w;
    w.resize(1500, 950);
    w.show();
    w.newDefaultDocument();
    settle();

    auto* canvas = w.findChild<Canvas*>();
    auto* tools = w.findChild<ToolManager*>();
    CHECK(canvas && tools && canvas->document());
    if (!canvas || !tools || !canvas->document())
        return 1;
    Document* doc = canvas->document();
    AppState* state = AppState::instance();

    // Pincel rojo
    state->setPrimary(QColor(220, 30, 40));
    state->updateTools([](ToolSettings& s) { s.brushSize = 24; s.hardness = 80; });
    tools->select(ToolId::Paintbrush);
    QList<QPointF> stroke;
    for (int i = 0; i <= 40; ++i)
        stroke << QPointF(80 + i * 8, 120 + 60 * std::sin(i / 6.0));
    drag(canvas, stroke);
    CHECK(QColor(doc->layer(0)->image.pixel(80, 120)).red() > 200);

    // Formas: elipse con contorno y relleno sobre una capa nueva
    QAction* addLayer = findAction(&w, QStringLiteral("Agregar nueva capa"));
    CHECK(addLayer);
    if (addLayer)
        addLayer->trigger();
    CHECK(doc->layerCount() == 2);
    state->setPrimary(QColor(30, 60, 200));
    state->setSecondary(QColor(250, 200, 40));
    state->updateTools([](ToolSettings& s) {
        s.brushSize = 6;
        s.shape = ShapeType::Star5;
        s.shapeStyle = ShapeStyle::OutlineFill;
    });
    tools->select(ToolId::Shapes);
    {
        // Como en Paint.NET: el paso aparece al dibujar la forma y "Finalizar forma" al terminar.
        const int steps = doc->undoStack()->count();
        drag(canvas, {QPointF(450, 250), QPointF(520, 330), QPointF(640, 470)});
        CHECK(tools->current()->isBusy());
        CHECK(doc->undoStack()->count() == steps + 1 && doc->undoStack()->text(steps) == QStringLiteral("Estrella de 5 puntas"));
        tools->current()->finish();
        CHECK(!tools->current()->isBusy());
        CHECK(doc->undoStack()->count() == steps + 2 && doc->undoStack()->text(steps + 1) == QStringLiteral("Finalizar forma"));
    }
    CHECK(doc->layer(1)->image.pixel(545, 380) != 0);

    // Degradado radial en otra capa con opacidad
    if (addLayer)
        addLayer->trigger();
    state->updateTools([](ToolSettings& s) { s.gradient = GradientType::Radial; });
    tools->select(ToolId::Gradient);
    state->setPrimary(QColor(255, 255, 255, 0));
    state->setSecondary(QColor(40, 170, 90, 160));
    drag(canvas, {QPointF(200, 420), QPointF(260, 480), QPointF(330, 560)});
    tools->current()->finish();

    // Texto con acentos
    state->setPrimary(Qt::black);
    state->updateTools([](ToolSettings& s) { s.fontSize = 40; s.bold = true; });
    tools->select(ToolId::Text);
    drag(canvas, {QPointF(90, 300)});
    CHECK(tools->current()->wantsKeyboard());
    typeText(canvas, QStringLiteral("¡Hola, Kaint! ñ á ü"));
    tools->current()->finish();
    CHECK(!tools->current()->wantsKeyboard());

    // Selección rectangular (hormigas) y mover píxeles
    tools->select(ToolId::RectSelect);
    drag(canvas, {QPointF(60, 60), QPointF(300, 200), QPointF(420, 230)});
    CHECK(!doc->selection().isEmpty());
    CHECK(doc->selection().bounds() == QRect(60, 60, 360, 170));

    const int before = doc->undoStack()->count();
    doc->setActiveIndex(0);
    tools->select(ToolId::MoveSelected);
    drag(canvas, {QPointF(200, 150), QPointF(210, 160), QPointF(230, 190)});
    drag(canvas, {QPointF(230, 190), QPointF(240, 200)});
    CHECK(doc->undoStack()->count() == before + 1);
    doc->undoStack()->undo();
    CHECK(doc->undoStack()->count() == before + 1 && doc->undoStack()->index() == before);
    doc->undoStack()->redo();

    // Efecto con vista previa en segundo plano
    const Effect* dents = Effects::find(QStringLiteral("dents"));
    CHECK(dents);
    if (dents) {
        EffectDialog dlg(*dents, Effects::defaults(*dents), doc->activeLayer()->image, doc->rect(), &w);
        dlg.show();
        settle();
        {
            EffectPreview preview(doc, doc->activeLayerPtr(), *dents, EffectContext());
            preview.request(dlg.values());
            QElapsedTimer t;
            t.start();
            while (!doc->hasPreview() && t.elapsed() < 5000)
                QApplication::processEvents(QEventLoop::AllEvents, 20);
            CHECK(doc->hasPreview());
            CHECK(!preview.finalResult(dlg.values(), &w).isNull());
            if (!outDir.isEmpty())
                dlg.grab().save(QDir(outDir).filePath(QStringLiteral("efecto.png")));
        }
        CHECK(!doc->hasPreview());
        dlg.close();
    }

    tools->select(ToolId::Paintbrush);
    settle();
    if (!outDir.isEmpty()) {
        QDir().mkpath(outDir);
        w.grab().save(QDir(outDir).filePath(QStringLiteral("ventana.png")));
    }

    // Varita mágica + bote de pintura dentro de la selección
    doc->setActiveIndex(1);
    tools->select(ToolId::MagicWand);
    drag(canvas, {QPointF(700, 120)});
    CHECK(!doc->selection().isEmpty() && !doc->selection().isPathBased());
    state->setPrimary(QColor(200, 0, 200));
    tools->select(ToolId::PaintBucket);
    drag(canvas, {QPointF(700, 120)});
    CHECK(QColor(doc->layer(1)->image.pixel(700, 120)) == QColor(200, 0, 200));
    tools->current()->finish();
    QAction* deselectAct = findAction(&w, QStringLiteral("Anular la selección"));
    if (deselectAct)
        deselectAct->trigger();
    CHECK(doc->selection().isEmpty());

    // Goma
    tools->select(ToolId::Eraser);
    drag(canvas, {QPointF(690, 110), QPointF(720, 140)});
    CHECK(qAlpha(doc->layer(1)->image.pixel(705, 125)) == 0);

    // Copiar y pegar (portapapeles dentro del proceso)
    tools->select(ToolId::RectSelect);
    drag(canvas, {QPointF(450, 250), QPointF(650, 480)});
    findAction(&w, QStringLiteral("Copiar"))->trigger();
    findAction(&w, QStringLiteral("Pegar"))->trigger();
    settle();
    CHECK(tools->current()->id() == ToolId::MoveSelected);
    drag(canvas, {QPointF(500, 300), QPointF(300, 300)});

    // Efecto sin diálogo y con diálogo (se acepta solo)
    auto acceptDialog = [] {
        QTimer::singleShot(400, [] {
            if (auto* d = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
                d->accept();
                return;
            }
            for (QWidget* w : QApplication::topLevelWidgets())
                if (auto* d = qobject_cast<QDialog*>(w); d && d->isVisible() && d->property("kaintPreviewDialog").toBool())
                    d->accept();
        });
    };
    findAction(&w, QStringLiteral("Invertir colores"))->trigger();
    acceptDialog();
    findAction(&w, QStringLiteral("Desenfoque gaussiano..."))->trigger();
    settle();
    CHECK(!doc->hasPreview());
    CHECK(doc->undoStack()->text(doc->undoStack()->index() - 1) == QStringLiteral("Desenfoque gaussiano"));

    // Operaciones de imagen
    findAction(&w, QStringLiteral("Girar 90° en sentido horario"))->trigger();
    CHECK(doc->size() == QSize(600, 800));
    findAction(&w, QStringLiteral("Voltear horizontalmente"))->trigger();
    findAction(&w, QStringLiteral("Combinar capa con la inferior"))->trigger();
    CHECK(doc->layerCount() == 2);
    if (auto* layers = w.findChild<LayersPanel*>()) {
        emit layers->soloRequested(0);
        CHECK(doc->layer(0)->props.visible && !doc->layer(1)->props.visible);
        emit layers->soloRequested(0);
        CHECK(doc->layer(0)->props.visible && doc->layer(1)->props.visible);
    }
    findAction(&w, QStringLiteral("Acoplar"))->trigger();
    CHECK(doc->layerCount() == 1);

    // Línea/Curva editable, como en Paint.NET: "Línea/Curva", un "Editar línea/curva" por cada
    // cambio y "Finalizar línea/curva"; al deshacer el final se sigue editando.
    {
        tools->select(ToolId::Line);
        state->updateTools([](ToolSettings& s) { s.brushSize = 5; s.curveType = CurveType::Spline; s.dash = Qt::SolidLine; });
        QUndoStack* stack = doc->undoStack();
        const int steps = stack->count();
        drag(canvas, {QPointF(100, 650), QPointF(300, 650), QPointF(500, 650)});
        CHECK(tools->current()->isBusy() && stack->count() == steps + 1 && stack->text(steps) == QStringLiteral("Línea/Curva"));
        drag(canvas, {QPointF(233, 650), QPointF(233, 560)});
        CHECK(qAlpha(doc->activeLayer()->image.pixel(233, 560)) > 0);
        CHECK(stack->count() == steps + 2 && stack->text(steps + 1) == QStringLiteral("Editar línea/curva"));
        state->updateTools([](ToolSettings& s) { s.brushSize = 9; });
        tools->current()->finish();
        CHECK(stack->count() == steps + 4 && stack->text(steps + 3) == QStringLiteral("Finalizar línea/curva"));
        tools->select(ToolId::Paintbrush);
        findAction(&w, QStringLiteral("Deshacer"))->trigger();
        settle();
        CHECK(tools->current()->id() == ToolId::Line && tools->current()->isBusy());
        CHECK(state->tools().brushSize == 9);
        findAction(&w, QStringLiteral("Deshacer"))->trigger();
        settle();
        CHECK(tools->current()->isBusy() && state->tools().brushSize == 5);
        drag(canvas, {QPointF(366, 650), QPointF(366, 740)});
        CHECK(stack->count() == steps + 3 && stack->text(steps + 2) == QStringLiteral("Editar línea/curva"));
        CHECK(qAlpha(doc->activeLayer()->image.pixel(233, 560)) > 0);
        tools->current()->finish();
        CHECK(!tools->current()->isBusy());
    }

    // Formas: A cambia de forma mientras se edita.
    {
        tools->select(ToolId::Shapes);
        state->updateTools([](ToolSettings& s) { s.shape = ShapeType::Rectangle; });
        drag(canvas, {QPointF(80, 80), QPointF(150, 150)});
        QKeyEvent a(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, QStringLiteral("a"));
        QApplication::sendEvent(canvas, &a);
        CHECK(state->tools().shape == ShapeType::RoundedRect);
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(canvas, &enter);
        CHECK(!tools->current()->isBusy());
    }

    // Formas personalizadas: aparecen tras las integradas (A llega a ellas) y se dibujan
    // estiradas al rectángulo, con su nombre en el historial.
    {
        tools->select(ToolId::Shapes);
        state->updateTools([](ToolSettings& s) { s.shape = ShapeType::Heart; });
        QKeyEvent a(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, QStringLiteral("a"));
        QApplication::sendEvent(canvas, &a);
        CHECK(state->tools().shape == ShapeType::Custom);
        CHECK(state->tools().customShape == QFileInfo(shapesDir.filePath(QStringLiteral("casa.xaml"))).absoluteFilePath());
        state->updateTools([](ToolSettings& s) { s.shapeStyle = ShapeStyle::Fill; });
        const QPointF c = QRectF(doc->rect()).center();
        drag(canvas, {c - QPointF(50, 50), c, c + QPointF(50, 50)});
        tools->current()->finish();
        CHECK(doc->undoStack()->text(doc->undoStack()->index() - 2) == QStringLiteral("Casa de prueba"));
        const QImage& img = doc->activeLayer()->image;
        const QPoint body = (c + QPointF(0, 35)).toPoint(), corner = (c + QPointF(-47, -47)).toPoint();
        CHECK(img.pixel(body) != img.pixel(corner));
        state->updateTools([](ToolSettings& s) {
            s.shape = ShapeType::Rectangle;
            s.shapeStyle = ShapeStyle::Outline;
        });
    }

    // Texto: el cursor se mueve con las flechas y se puede insertar en medio.
    {
        tools->select(ToolId::Text);
        state->updateTools([](ToolSettings& s) { s.textAlign = TextAlign::Center; });
        drag(canvas, {QPointF(300, 120)});
        typeText(canvas, QStringLiteral("ac"));
        QKeyEvent left(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);
        QApplication::sendEvent(canvas, &left);
        typeText(canvas, QStringLiteral("b"));
        QKeyEvent home(QEvent::KeyPress, Qt::Key_Home, Qt::NoModifier);
        QApplication::sendEvent(canvas, &home);
        QKeyEvent del(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
        QApplication::sendEvent(canvas, &del);
        CHECK(tools->current()->isBusy());
        const int steps = doc->undoStack()->count();
        CHECK(doc->undoStack()->text(steps - 1) == QStringLiteral("Texto"));
        QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(canvas, &esc);
        CHECK(!tools->current()->isBusy() && doc->undoStack()->count() == steps);
        state->updateTools([](ToolSettings& s) { s.textAlign = TextAlign::Left; });
    }

    // Pasos en vivo, como en Paint.NET: el cubo y el texto aparecen en el historial al empezar,
    // cambiar de herramienta los completa sin añadir otro y deshacer mientras se edita los quita.
    {
        QUndoStack* stack = doc->undoStack();
        tools->select(ToolId::PaintBucket);
        state->setPrimary(QColor(10, 200, 120));
        int steps = stack->count();
        drag(canvas, {QPointF(30, 560)});
        CHECK(tools->current()->isBusy() && stack->count() == steps + 1);
        CHECK(stack->text(steps) == QStringLiteral("Cubo de pintura"));
        tools->select(ToolId::Text);
        CHECK(stack->count() == steps + 1 && stack->index() == steps + 1);
        steps = stack->count();
        drag(canvas, {QPointF(40, 40)});
        CHECK(stack->count() == steps);
        typeText(canvas, QStringLiteral("xy"));
        CHECK(stack->count() == steps + 1 && tools->current()->isBusy());
        const QImage withText = doc->activeLayer()->image;
        findAction(&w, QStringLiteral("Deshacer"))->trigger();
        CHECK(!tools->current()->isBusy() && stack->index() == steps && stack->count() == steps + 1);
        CHECK(doc->activeLayer()->image != withText);
        findAction(&w, QStringLiteral("Rehacer"))->trigger();
        CHECK(doc->activeLayer()->image == withText);
        steps = stack->count();
        drag(canvas, {QPointF(40, 200)});
        typeText(canvas, QStringLiteral("z"));
        CHECK(stack->count() == steps + 1);
        QKeyEvent back(QEvent::KeyPress, Qt::Key_Backspace, Qt::NoModifier);
        QApplication::sendEvent(canvas, &back);
        QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(canvas, &esc);
        CHECK(!tools->current()->isBusy() && stack->count() == steps && stack->index() == steps);
    }

    // Copiar la selección (contorno JSON) y pegarla de nuevo.
    {
        tools->select(ToolId::RectSelect);
        drag(canvas, {QPointF(20, 20), QPointF(120, 90)});
        const QRect bounds = doc->selection().bounds();
        findAction(&w, QStringLiteral("Copiar selección"))->trigger();
        if (deselectAct)
            deselectAct->trigger();
        CHECK(doc->selection().isEmpty());
        findAction(&w, QStringLiteral("Reemplazar"))->trigger();
        CHECK(doc->selection().bounds() == bounds);
        if (deselectAct)
            deselectAct->trigger();
    }

    // Resto de herramientas
    tools->select(ToolId::LassoSelect);
    drag(canvas, {QPointF(100, 100), QPointF(300, 120), QPointF(250, 300), QPointF(120, 260)});
    CHECK(!doc->selection().isEmpty());
    tools->select(ToolId::EllipseSelect);
    drag(canvas, {QPointF(50, 400), QPointF(250, 600)}, Qt::LeftButton, Qt::ControlModifier);
    CHECK(doc->selection().bounds().bottom() >= 590);
    if (deselectAct)
        deselectAct->trigger();
    state->updateTools([](ToolSettings& s) { s.brushSize = 9; s.dash = Qt::DashLine; });
    tools->select(ToolId::Line);
    drag(canvas, {QPointF(40, 700), QPointF(560, 760)}, Qt::LeftButton, Qt::ShiftModifier);
    tools->select(ToolId::CloneStamp);
    drag(canvas, {QPointF(300, 400)}, Qt::LeftButton, Qt::ControlModifier);
    drag(canvas, {QPointF(100, 100), QPointF(160, 140)});
    tools->select(ToolId::Recolor);
    drag(canvas, {QPointF(300, 300), QPointF(400, 380)});
    tools->select(ToolId::ColorPicker);
    drag(canvas, {QPointF(10, 10)});
    CHECK(state->primary() == QColor(doc->flattened().pixel(10, 10)));
    tools->select(ToolId::Zoom);
    drag(canvas, {QPointF(300, 400)});
    CHECK(canvas->zoom() > 1.0);
    canvas->setZoom(1.0);

    // Suprimir borra los píxeles y anula la selección en un solo paso.
    {
        tools->select(ToolId::RectSelect);
        drag(canvas, {QPointF(40, 40), QPointF(100, 90), QPointF(140, 120)});
        CHECK(!doc->selection().isEmpty());
        const int steps = doc->undoStack()->count();
        const Selection sel = doc->selection();
        findAction(&w, QStringLiteral("Borrar la selección"))->trigger();
        CHECK(doc->selection().isEmpty());
        CHECK(qAlpha(doc->activeLayer()->image.pixel(70, 70)) == 0);
        CHECK(doc->undoStack()->count() == steps + 1);
        doc->undoStack()->undo();
        CHECK(doc->selection() == sel && qAlpha(doc->activeLayer()->image.pixel(70, 70)) == 255);
        doc->undoStack()->redo();
        CHECK(doc->selection().isEmpty());
    }

    // Zoom hacia el cursor: el punto bajo el puntero no se mueve, ni en las esquinas.
    {
        canvas->setZoom(1.0);
        settle();
        const QSizeF ds = doc->size();
        for (const QPointF img : {QPointF(3, 3), QPointF(ds.width() - 3, ds.height() - 3), QPointF(ds.width() / 2, 2)}) {
            canvas->setZoom(1.0);
            const QPointF v = canvas->imageToView(img);
            canvas->setZoom(8.0, v);
            CHECK(QLineF(canvas->viewToImage(v), img).length() < 0.5);
            const qreal before = canvas->zoom();
            QWheelEvent wheel(v, canvas->viewport()->mapToGlobal(v), QPoint(), QPoint(0, 120), Qt::NoButton,
                              Qt::ControlModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(canvas->viewport(), &wheel);
            CHECK(std::abs(canvas->zoom() / before - 1.2) < 0.01);
            CHECK(QLineF(canvas->viewToImage(v), img).length() < 0.5);
            QWheelEvent back(v, canvas->viewport()->mapToGlobal(v), QPoint(), QPoint(0, -360), Qt::NoButton,
                             Qt::ControlModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(canvas->viewport(), &back);
            CHECK(QLineF(canvas->viewToImage(v), img).length() < 0.5);
        }
        canvas->setZoom(1.0);
        settle();
    }

    // Sin selección, un clic con Mover selección o con Mover píxeles selecciona todo el lienzo
    // (como Paint.NET), ya al pulsar.
    {
        CHECK(doc->selection().isEmpty());
        tools->select(ToolId::MoveSelection);
        int steps = doc->undoStack()->count();
        drag(canvas, {QPointF(300, 300)});
        CHECK(doc->selection().bounds() == doc->rect());
        CHECK(doc->undoStack()->count() == steps + 1);
        doc->undoStack()->undo();
        CHECK(doc->selection().isEmpty());
        steps = doc->undoStack()->index();
        tools->select(ToolId::MoveSelected);
        const QPoint at = canvas->imageToView(QPointF(300, 300)).toPoint();
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(at), QPointF(canvas->viewport()->mapToGlobal(at)),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas->viewport(), &press);
        CHECK(doc->selection().bounds() == doc->rect());
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(at), QPointF(canvas->viewport()->mapToGlobal(at)),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(canvas->viewport(), &release);
        CHECK(doc->selection().bounds() == doc->rect());
        CHECK(doc->undoStack()->count() == steps + 1);
        doc->undoStack()->undo();
        CHECK(doc->selection().isEmpty());
    }

    // Al terminar una selección no se cambia de herramienta (como Paint.NET), salvo que se
    // elija en "Después de seleccionar" (y nunca con Ctrl/Alt o un clic).
    {
        tools->select(ToolId::RectSelect);
        drag(canvas, {QPointF(50, 50), QPointF(120, 110), QPointF(160, 150)});
        CHECK(tools->current()->id() == ToolId::RectSelect);
        CHECK(!doc->selection().isEmpty());
        drag(canvas, {QPointF(50, 50), QPointF(120, 80), QPointF(170, 100)}, Qt::LeftButton, Qt::ShiftModifier);
        const QRect square = doc->selection().bounds();
        CHECK(square.width() == square.height() && square.width() > 20);
        CHECK(tools->current()->id() == ToolId::RectSelect);
        drag(canvas, {QPointF(50, 50), QPointF(2000, 1500), QPointF(4000, 3000)});
        CHECK(!doc->selection().isEmpty() && doc->rect().contains(doc->selection().bounds()));
        AppState::instance()->updateTools([](ToolSettings& s) { s.afterSelect = AfterSelect::MoveSelection; });
        drag(canvas, {QPointF(60, 60), QPointF(130, 120), QPointF(170, 160)});
        CHECK(tools->current()->id() == ToolId::MoveSelection);
        tools->select(ToolId::EllipseSelect);
        drag(canvas, {QPointF(300, 50), QPointF(350, 110), QPointF(380, 150)}, Qt::LeftButton, Qt::ControlModifier);
        CHECK(tools->current()->id() == ToolId::EllipseSelect);
        drag(canvas, {QPointF(500, 500)});
        CHECK(doc->selection().isEmpty());
        CHECK(tools->current()->id() == ToolId::EllipseSelect);
        AppState::instance()->updateTools([](ToolSettings& s) { s.afterSelect = AfterSelect::MovePixels; });
        tools->select(ToolId::LassoSelect);
        drag(canvas, {QPointF(100, 100), QPointF(300, 120), QPointF(250, 300), QPointF(120, 260)});
        CHECK(tools->current()->id() == ToolId::MoveSelected);
        AppState::instance()->updateTools([](ToolSettings& s) { s.afterSelect = AfterSelect::DoNotSwitch; });
        if (deselectAct)
            deselectAct->trigger();
    }

    // Todos los ajustes y efectos desde el menú, aceptando su diálogo.
    for (const Effect* e : Effects::all()) {
        if (e->hidden())
            continue;
        const QString text = e->name() + (e->params().isEmpty() ? QString() : QStringLiteral("..."));
        QAction* a = findAction(&w, text);
        CHECK(a);
        if (!a)
            continue;
        const int index = doc->undoStack()->index();
        if (!e->params().isEmpty())
            acceptDialog();
        a->trigger();
        settle();
        CHECK(!doc->hasPreview());
        if (doc->undoStack()->index() != index + 1)
            std::fprintf(stderr, "  (sin cambios: %s)\n", qPrintable(e->id()));
        if (e->id() == QLatin1String("invertalpha")) {
            doc->undoStack()->undo();
            settle();
        }
    }

    // Deshacer y rehacer todo el historial debe reproducir exactamente cada estado.
    settle();
    const QImage finalImage = doc->flattened();
    const int steps = doc->undoStack()->count();
    doc->undoStack()->setIndex(0);
    settle();
    CHECK(doc->layerCount() == 1 && doc->size() == QSize(800, 600));
    QImage white(800, 600, QImage::Format_ARGB32);
    white.fill(Qt::white);
    CHECK(doc->flattened() == white);
    doc->undoStack()->setIndex(steps);
    settle();
    CHECK(doc->flattened() == finalImage);
    CHECK(canvas->composite() == finalImage.convertToFormat(QImage::Format_ARGB32_Premultiplied));
    if (!outDir.isEmpty())
        w.grab().save(QDir(outDir).filePath(QStringLiteral("final.png")));

    // Barra de opciones en el orden de Paint.NET 5.
    {
        CHECK(ToolOptionsBar::order(ToolId::RectSelect).first() == QLatin1String("selMode"));
        CHECK(ToolOptionsBar::order(ToolId::ColorPicker).last() == QLatin1String("selQuality"));
        CHECK(ToolOptionsBar::order(ToolId::Paintbrush).indexOf(QStringLiteral("fill"))
              < ToolOptionsBar::order(ToolId::Paintbrush).indexOf(QStringLiteral("smoothing")));
        auto* bar = w.findChild<ToolOptionsBar*>();
        CHECK(bar);
        if (bar) {
            tools->select(ToolId::ColorPicker);
            settle();
            QString lastTip;
            for (QAction* a : bar->actions())
                if (a->isVisible() && !a->isSeparator())
                    if (QWidget* wid = bar->widgetForAction(a))
                        lastTip = wid->toolTip();
            CHECK(lastTip.startsWith(QStringLiteral("Calidad de selección")));
            tools->select(ToolId::RectSelect);
            settle();
        }
    }

    // Paneles redimensionables por cualquier borde; la rejilla de herramientas se reorganiza
    // en columnas según el ancho.
    {
        FloatingPanel* toolsPanel = nullptr;
        FloatingPanel* historyPanel = nullptr;
        for (FloatingPanel* p : w.findChildren<FloatingPanel*>()) {
            if (p->id() == QLatin1String("tools"))
                toolsPanel = p;
            if (p->id() == QLatin1String("history"))
                historyPanel = p;
        }
        auto* grid = w.findChild<ToolsPanel*>();
        CHECK(toolsPanel && historyPanel && grid);
        if (toolsPanel && historyPanel && grid) {
            CHECK(grid->columns() == 2);
            auto edgeDrag = [](QWidget* panel, QPoint from, QPoint to) {
                auto send = [&](QEvent::Type type, QPoint p, Qt::MouseButton b, Qt::MouseButtons bs) {
                    QMouseEvent e(type, QPointF(p), QPointF(panel->mapToGlobal(p)), b, bs, Qt::NoModifier);
                    QApplication::sendEvent(panel, &e);
                };
                send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
                send(QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
                send(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
                settle();
            };
            const int h2 = toolsPanel->height();
            QPoint right(toolsPanel->width() - 2, toolsPanel->height() / 2);
            edgeDrag(toolsPanel, right, right - QPoint(40, 0));
            CHECK(grid->columns() == 1);
            CHECK(toolsPanel->height() > h2);
            right = QPoint(toolsPanel->width() - 2, toolsPanel->height() / 2);
            edgeDrag(toolsPanel, right, right + QPoint(120, 0));
            CHECK(grid->columns() >= 3);
            const QSize before = historyPanel->size();
            edgeDrag(historyPanel, QPoint(1, historyPanel->height() / 2), QPoint(-60, historyPanel->height() / 2));
            CHECK(historyPanel->width() == before.width() + 61);
            edgeDrag(historyPanel, QPoint(historyPanel->width() / 2, historyPanel->height() - 2),
                     QPoint(historyPanel->width() / 2, historyPanel->height() + 48));
            CHECK(historyPanel->height() == before.height() + 50);
            if (!outDir.isEmpty())
                w.grab().save(QDir(outDir).filePath(QStringLiteral("paneles.png")));
            if (QAction* reset = findAction(&w, QStringLiteral("Restablecer la posición de las ventanas")))
                reset->trigger();
            CHECK(grid->columns() == 2);
        }
    }

    // Configuración: la sección Discord va justo debajo de Complementos.
    {
        SettingsDialog dlg(AppSettings::load(), w.findChild<ToolManager*>(), &w);
        auto* list = dlg.findChild<QListWidget*>();
        CHECK(list);
        if (list) {
            int plugins = -1, discord = -1;
            for (int i = 0; i < list->count(); ++i) {
                if (list->item(i)->text() == QLatin1String("Complementos"))
                    plugins = i;
                if (list->item(i)->text() == QLatin1String("Discord"))
                    discord = i;
            }
            CHECK(plugins >= 0 && discord == plugins + 1);
        }
        CHECK(AppSettings().discordEnabled);
        if (!outDir.isEmpty()) {
            dlg.showPage(SettingsDialog::Page::Discord);
            dlg.resize(820, 560);
            dlg.show();
            settle();
            dlg.grab().save(QDir(outDir).filePath(QStringLiteral("discord.png")));
            dlg.showPage(SettingsDialog::Page::Plugins);
            settle();
            dlg.grab().save(QDir(outDir).filePath(QStringLiteral("complementos.png")));
        }
    }
    testDiscordPresence();
    testTransformBox();
    testTheme();

    // Acerca de: el logo se aclara (desenfoque animado), la firma enlaza a la web del autor y su
    // ola se mueve.
    {
        AboutDialog about(&w);
        about.show();
        settle();
        auto* blur = about.findChild<QGraphicsBlurEffect*>();
        const qreal startBlur = blur ? blur->blurRadius() : 0;
        CHECK(blur && startBlur > 0);
        CHECK(AboutDialog::credits().contains(QStringLiteral("https://www.qt.io/")));
        auto* wave = about.findChild<WaveLabel*>();
        CHECK(wave && wave->url() == QUrl(QStringLiteral("https://ssserch.com")) && wave->isVisible());
        if (wave) {
            const qreal before = wave->phase();
            QElapsedTimer clock;
            clock.start();
            while (clock.elapsed() < 200)
                QApplication::processEvents(QEventLoop::AllEvents, 20);
            CHECK(wave->phase() != before);
        }
        blur = about.findChild<QGraphicsBlurEffect*>();
        CHECK(!blur || blur->blurRadius() < startBlur);
        if (!outDir.isEmpty())
            about.grab().save(QDir(outDir).filePath(QStringLiteral("acerca.png")));
    }

    // Impresión: la imagen se dibuja centrada en la página (aquí, en un PDF).
    {
        QTemporaryDir dir;
        QPrinter printer(QPrinter::HighResolution);
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(dir.filePath(QStringLiteral("hoja.pdf")));
        MainWindow::renderPrint(&printer, doc->flattened(), doc->dpi(), false);
        CHECK(QFileInfo(dir.filePath(QStringLiteral("hoja.pdf"))).size() > 1000);
        printer.setOutputFileName(dir.filePath(QStringLiteral("real.pdf")));
        MainWindow::renderPrint(&printer, doc->flattened(), doc->dpi(), true);
        CHECK(QFileInfo(dir.filePath(QStringLiteral("real.pdf"))).size() > 1000);
        CHECK(findAction(&w, QStringLiteral("Configurar página...")));
        CHECK(findAction(&w, QStringLiteral("Animación...")));
    }
    {
        ImageIO::SaveOptions o;
        SaveConfigDialog ico(doc->flattened(), QStringLiteral("icono.ico"), o, &w);
        CHECK(ico.options().icoSizes == o.icoSizes);
        o.loops = 2;
        o.frameDelay = 250;
        const QList<Gif::Frame> frames{Gif::Frame{doc->flattened(), 0}, Gif::Frame{doc->flattened(), 40}};
        SaveConfigDialog gif(doc->flattened(), QStringLiteral("anim.gif"), o, &w, frames);
        const ImageIO::SaveOptions back = gif.options();
        CHECK(back.animation && back.loops == 2 && back.frameDelay == 250);
    }

    // Reconstruir el menú Efectos y luego deshabilitar la ventana no debe tocar acciones ya
    // borradas.
    {
        emit PluginManager::instance()->changed();
        settle();
        emit PluginManager::instance()->changed();
        QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        w.setEnabled(false);
        w.setEnabled(true);
        CHECK(findAction(&w, QStringLiteral("Desenfoque gaussiano...")));
    }

    // Diálogos de decisión de Paint.NET 5: pegar una imagen mayor que el lienzo ("Ampliar
    // lienzo") y cerrar con cambios ("Cancelar" no cierra).
    {
        auto choose = [&outDir](const QString& shot, int choice) {
            QTimer::singleShot(300, [=] {
                auto* d = qobject_cast<TaskDialog*>(QApplication::activeModalWidget());
                CHECK(d);
                if (!d)
                    return;
                if (!outDir.isEmpty())
                    d->grab().save(QDir(outDir).filePath(shot));
                if (auto* b = d->findChild<QAbstractButton*>(QStringLiteral("choice%1").arg(choice)))
                    b->click();
            });
        };
        const QSize before = doc->size();
        QImage big(before.width() + 200, before.height() + 100, QImage::Format_ARGB32);
        big.fill(QColor(30, 160, 90));
        QApplication::clipboard()->setImage(big);
        choose(QStringLiteral("pegar.png"), 0);
        findAction(&w, QStringLiteral("Pegar"))->trigger();
        settle();
        CHECK(doc->size() == big.size());
        CHECK(tools->current()->id() == ToolId::MoveSelected);
        tools->select(ToolId::RectSelect);
        settle();
        CHECK(doc->isModified());
        choose(QStringLiteral("cambios.png"), 2);
        CHECK(!w.close());
        CHECK(w.isVisible());
    }


    // Colores: solo cambia de tamaño manteniendo la proporción, y "Más"/"Menos" conservan la
    // escala (antes, al ampliarla, "Más" la volvía pequeña).
    {
        auto* colors = w.findChild<ColorsPanel*>();
        auto* panel = colors ? qobject_cast<FloatingPanel*>(colors->parentWidget()) : nullptr;
        CHECK(colors && panel);
        if (colors && panel) {
            const QSize base = colors->sizeHint();
            const QSize wide = colors->fitSize(QSize(base.width() * 3 / 2, base.height()), Qt::Horizontal);
            CHECK(std::abs(double(wide.height()) / wide.width() - double(base.height()) / base.width()) < 0.02);
            panel->resize(colors->sizeForScale(1.4) + (panel->size() - colors->size()));
            settle();
            const QSize compact = panel->size();
            CHECK(std::abs(colors->scale() - 1.4) < 0.05);
            auto* more = colors->findChild<QPushButton*>();
            CHECK(more && more->isCheckable());
            if (more) {
                more->click();
                settle();
                CHECK(colors->isExpanded() && std::abs(colors->scale() - 1.4) < 0.05);
                CHECK(panel->width() > compact.width());
                more->click();
                settle();
                CHECK(!colors->isExpanded() && std::abs(panel->width() - compact.width()) <= 2
                      && std::abs(panel->height() - compact.height()) <= 2);
            }
            panel->resize(colors->sizeForScale(1.0) + (panel->size() - colors->size()));
        }
        // Herramientas: dos columnas por defecto y Formas ocupa lo que queda de la última fila.
        auto* toolsGrid = w.findChild<ToolsPanel*>();
        CHECK(toolsGrid && toolsGrid->columns() == 2);
        if (toolsGrid) {
            QList<QToolButton*> buttons = toolsGrid->findChildren<QToolButton*>();
            CHECK(buttons.size() == 19 && buttons.last()->width() > buttons.first()->width() * 3 / 2);
        }
    }

    // Iconos teñidos con los colores activos (activado por defecto): el cubo cambia con el color
    // principal y el degradado también con el secundario.
    {
        Tool* bucket = tools->tool(ToolId::PaintBucket);
        Tool* gradientTool = tools->tool(ToolId::Gradient);
        state->setPrimary(QColor(220, 20, 20));
        const QImage red = bucket->icon().pixmap(40, 40).toImage();
        state->setPrimary(QColor(20, 180, 40));
        CHECK(bucket->icon().pixmap(40, 40).toImage() != red);
        const QImage before = gradientTool->icon().pixmap(40, 40).toImage();
        state->setSecondary(QColor(250, 220, 0));
        CHECK(gradientTool->icon().pixmap(40, 40).toImage() != before);
    }

    // Cubo de pintura: el icono de mover arrastra el punto de origen (el relleno lo sigue) sin
    // añadir pasos al historial.
    {
        w.newDefaultDocument();
        settle();
        Document* d = canvas->document();
        state->setPrimary(Qt::black);
        state->updateTools([](ToolSettings& s) { s.brushSize = 8; s.antialias = false; s.dash = Qt::SolidLine; });
        tools->select(ToolId::Line);
        drag(canvas, {QPointF(400, -10), QPointF(400, 300), QPointF(400, 700)});
        tools->current()->finish();
        state->setPrimary(QColor(220, 0, 0));
        tools->select(ToolId::PaintBucket);
        drag(canvas, {QPointF(100, 100)});
        const int steps = d->undoStack()->count();
        CHECK(QColor(d->activeLayer()->image.pixel(100, 300)) == QColor(220, 0, 0));
        const QPointF handle = canvas->viewToImage(Handles::moveIconPos(QRectF(canvas->imageToView(QPointF(100.5, 100.5)), QSizeF())));
        drag(canvas, {handle, handle + QPointF(300, 0), handle + QPointF(600, 0)});
        CHECK(tools->current()->isBusy() && d->undoStack()->count() == steps);
        CHECK(QColor(d->activeLayer()->image.pixel(100, 300)) == QColor(Qt::white));
        CHECK(QColor(d->activeLayer()->image.pixel(700, 300)) == QColor(220, 0, 0));
        tools->current()->finish();
        CHECK(d->undoStack()->count() == steps);

        // Con una selección, un clic fuera de ella no rellena nada (como en Paint.NET).
        tools->select(ToolId::RectSelect);
        drag(canvas, {QPointF(500, 100), QPointF(600, 150), QPointF(700, 200)});
        CHECK(!d->selection().isEmpty());
        tools->select(ToolId::PaintBucket);
        state->setPrimary(QColor(0, 0, 200));
        const int before = d->undoStack()->count();
        const QImage untouched = d->activeLayer()->image;
        drag(canvas, {QPointF(100, 400)});
        CHECK(!tools->current()->isBusy() && d->undoStack()->count() == before && d->activeLayer()->image == untouched);
        drag(canvas, {QPointF(600, 150)});
        CHECK(QColor(d->activeLayer()->image.pixel(600, 150)) == QColor(0, 0, 200));
        CHECK(QColor(d->activeLayer()->image.pixel(700, 400)) == QColor(220, 0, 0));
        tools->current()->finish();
        findAction(&w, QStringLiteral("Anular la selección"))->trigger();
        state->updateTools([](ToolSettings& s) { s.antialias = true; });
    }

    // Versión 1.0: degradado editable desde el historial, pegado donde se copió, Supr a mitad de
    // arrastre, lista de capas que no salta, fuentes, menús Efectos y Ayuda, información de la
    // selección, copias de recuperación y aviso de reinicio al pasar al tema KDE.
    {
        w.newDefaultDocument();
        settle();
        Document* d = canvas->document();
        QAction* undoAction = findAction(&w, QStringLiteral("Deshacer"));
        QAction* redoAction = findAction(&w, QStringLiteral("Rehacer"));
        CHECK(undoAction && redoAction);
        auto mouse = [canvas](QEvent::Type type, QPointF img, Qt::MouseButton b, Qt::MouseButtons bs) {
            const QPointF v = canvas->imageToView(img);
            QMouseEvent e(type, v, canvas->viewport()->mapToGlobal(v), b, bs, Qt::NoModifier);
            QApplication::sendEvent(canvas->viewport(), &e);
        };

        // Degradado: "Degradado" y "Finalizar degradado"; al deshacer el final se sigue editando,
        // y cada arrastre de un extremo es un paso más.
        tools->select(ToolId::Gradient);
        const int base = d->undoStack()->count();
        drag(canvas, {QPointF(100, 100), QPointF(200, 150), QPointF(300, 200)});
        CHECK(tools->current()->isBusy() && d->undoStack()->count() == base + 1);
        tools->current()->finish();
        CHECK(!tools->current()->isBusy() && d->undoStack()->count() == base + 2);
        tools->select(ToolId::Paintbrush);
        undoAction->trigger();
        settle();
        CHECK(tools->current()->id() == ToolId::Gradient && tools->current()->isBusy());
        const QRgb stepOne = d->activeLayer()->image.pixel(250, 180);
        drag(canvas, {QPointF(300, 200), QPointF(380, 260), QPointF(450, 300)});
        CHECK(d->undoStack()->count() == base + 2);
        CHECK(d->undoStack()->text(d->undoStack()->count() - 1) == QStringLiteral("Mover degradado"));
        CHECK(d->activeLayer()->image.pixel(250, 180) != stepOne);
        undoAction->trigger();
        settle();
        CHECK(tools->current()->isBusy() && d->activeLayer()->image.pixel(250, 180) == stepOne);
        undoAction->trigger();
        settle();
        CHECK(!tools->current()->isBusy());
        redoAction->trigger();
        settle();
        CHECK(tools->current()->isBusy());
        tools->current()->finish();

        // Pegar donde se copió si ese sitio se ve; si no, en lo visible más cercano.
        tools->select(ToolId::RectSelect);
        drag(canvas, {QPointF(120, 140), QPointF(150, 170), QPointF(170, 190)});
        CHECK(d->selection().bounds() == QRect(120, 140, 50, 50));
        findAction(&w, QStringLiteral("Copiar"))->trigger();
        findAction(&w, QStringLiteral("Anular la selección"))->trigger();
        findAction(&w, QStringLiteral("Pegar"))->trigger();
        settle();
        CHECK(d->selection().bounds().topLeft() == QPoint(120, 140));
        tools->select(ToolId::RectSelect);
        findAction(&w, QStringLiteral("Anular la selección"))->trigger();
        canvas->setZoom(8);
        settle();
        const QRect visible = canvas->visibleImageRect();
        CHECK(!visible.contains(QRect(120, 140, 50, 50)));
        findAction(&w, QStringLiteral("Pegar"))->trigger();
        settle();
        CHECK(visible.contains(d->selection().bounds()));
        tools->select(ToolId::RectSelect);
        findAction(&w, QStringLiteral("Anular la selección"))->trigger();
        canvas->setZoom(1);
        settle();

        // Supr a mitad de arrastre con Mover píxeles: lo borrado no vuelve a aparecer al seguir.
        drag(canvas, {QPointF(300, 300), QPointF(330, 320), QPointF(360, 340)});
        CHECK(d->selection().bounds() == QRect(300, 300, 60, 40));
        tools->select(ToolId::MoveSelected);
        mouse(QEvent::MouseButtonPress, QPointF(320, 320), Qt::LeftButton, Qt::LeftButton);
        mouse(QEvent::MouseMove, QPointF(400, 380), Qt::NoButton, Qt::LeftButton);
        findAction(&w, QStringLiteral("Borrar la selección"))->trigger();
        mouse(QEvent::MouseMove, QPointF(420, 400), Qt::NoButton, Qt::LeftButton);
        mouse(QEvent::MouseButtonRelease, QPointF(420, 400), Qt::LeftButton, Qt::NoButton);
        settle();
        CHECK(qAlpha(d->activeLayer()->image.pixel(410, 380)) == 0);
        CHECK(qAlpha(d->activeLayer()->image.pixel(310, 310)) == 0);
        CHECK(qAlpha(d->activeLayer()->image.pixel(450, 410)) == 255);

        // Mover selección: píxel a píxel por defecto, con su propia opción en la barra.
        CHECK(!state->tools().moveSelectionAntialias);
        CHECK(tools->tool(ToolId::MoveSelection)->options() & Tool::OptMoveSelectionQuality);

        // Marcar la casilla de otra capa no hace saltar la lista a la capa activa.
        for (int i = 0; i < 10; ++i)
            findAction(&w, QStringLiteral("Agregar nueva capa"))->trigger();
        settle();
        auto* layers = w.findChild<LayersPanel*>();
        auto* list = layers ? layers->findChild<QListWidget*>() : nullptr;
        CHECK(list);
        if (list) {
            list->verticalScrollBar()->setValue(list->verticalScrollBar()->maximum());
            const int scroll = list->verticalScrollBar()->value();
            CHECK(scroll > 0);
            emit layers->visibilityToggled(0);
            settle();
            CHECK(list->verticalScrollBar()->value() == scroll);
            emit layers->visibilityToggled(0);
            settle();
        }

        // Fuentes: el selector tiene ancho fijo y los tamaños son exactamente los de Paint.NET.
        auto* font = w.findChild<FontChooser*>();
        CHECK(font && font->sizePolicy().horizontalPolicy() == QSizePolicy::Fixed);
        if (font) {
            const int width = font->sizeHint().width();
            CHECK(width >= 150);
            font->setCurrentIndex(font->count() - 1);
            CHECK(font->sizeHint().width() == width);
        }
        bool sizesFound = false;
        for (QComboBox* c : w.findChildren<QComboBox*>()) {
            if (c->findText(QStringLiteral("216")) < 0 || c->findText(QStringLiteral("288")) < 0)
                continue;
            QStringList items;
            for (int i = 0; i < c->count(); ++i)
                items << c->itemText(i);
            sizesFound = items.join(QLatin1Char(' ')) == QStringLiteral("8 9 10 11 12 14 16 18 20 22 24 26 28 36 48 72 84 96 108 144 192 216 288");
        }
        CHECK(sizesFound);

        // Efectos: "Repetir" arriba del todo con el último efecto; Ayuda con los enlaces de Kaint.
        findAction(&w, QStringLiteral("Invertir colores"))->trigger();
        settle();
        QAction* repeat = findAction(&w, QStringLiteral("Repetir «Invertir colores»"));
        CHECK(repeat && repeat->isEnabled());
        QAction* effectsMenu = findAction(&w, QStringLiteral("Efectos"));
        CHECK(effectsMenu && effectsMenu->menu() && effectsMenu->menu()->actions().value(0) == repeat);
        QStringList help;
        for (QToolButton* b : w.findChildren<QToolButton*>())
            if (b->menu() && b->toolTip() == QStringLiteral("Ayuda"))
                for (QAction* a : b->menu()->actions())
                    help << a->text().remove(QLatin1Char('&'));
        CHECK(help.contains(QStringLiteral("Sitio web de Kaint")) && help.contains(QStringLiteral("Donar"))
              && help.contains(QStringLiteral("Enviar comentarios o informe de errores...")) && help.contains(QStringLiteral("Acerca de")));
        CHECK(!help.join(QString()).contains(QStringLiteral("Documentación")));

        // Con una selección y una herramienta de selección: posición, tamaño y área.
        tools->select(ToolId::RectSelect);
        drag(canvas, {QPointF(100, 100), QPointF(130, 120), QPointF(150, 140)});
        settle();
        bool infoShown = false;
        for (QLabel* l : w.statusBar()->findChildren<QLabel*>())
            if (l->text().contains(QStringLiteral("50 px × 40 px")) && l->text().contains(QStringLiteral("Área")))
                infoShown = true;
        CHECK(infoShown);
        findAction(&w, QStringLiteral("Anular la selección"))->trigger();

        // Copias de recuperación: se guardan las imágenes con cambios; si Kaint no se cerró bien
        // (carpeta de un proceso que ya no existe), se ofrecen y se pueden abrir.
        QTemporaryDir recoveryDir;
        Recovery::setRootForTesting(recoveryDir.path());
        CHECK(d->isModified());
        w.autosave();
        const QString mine = recoveryDir.filePath(QString::number(QCoreApplication::applicationPid()));
        CHECK(!QDir(mine).entryList({QStringLiteral("*.ora")}, QDir::Files).isEmpty());
        CHECK(Recovery::orphaned().isEmpty());
        // Una sesión cuyo proceso ya no existe: sus copias sin bloqueo vivo.
        const QString crashed = recoveryDir.filePath(QStringLiteral("999999999"));
        QDir().mkpath(crashed);
        for (const QFileInfo& f : QDir(mine).entryInfoList({QStringLiteral("*.ora"), QStringLiteral("*.json")}, QDir::Files))
            QFile::copy(f.absoluteFilePath(), crashed + QLatin1Char('/') + f.fileName());
        const QList<Recovery::Entry> entries = Recovery::orphaned();
        bool restoredLayers = false;
        for (const Recovery::Entry& e : entries) {
            QString error;
            std::unique_ptr<Document> restored(Ora::load(e.file, &error));
            if (restored && restored->layerCount() == d->layerCount())
                restoredLayers = true;
        }
        CHECK(restoredLayers);
        Recovery::discard(entries);
        CHECK(Recovery::orphaned().isEmpty() && !QDir(crashed).exists());
        Recovery::clear();
        CHECK(!QDir(mine).exists());
        Recovery::setRootForTesting(QString());

        // Pasar al tema KDE pide reiniciar: "Cancelar" deja el tema como estaba y "Reiniciar
        // ahora" cierra la configuración con el tema pedido.
        for (int choice : {1, 0}) {
            SettingsDialog settings(AppSettings::load(), tools, &w);
            QComboBox* scheme = nullptr;
            for (QComboBox* c : settings.findChildren<QComboBox*>())
                if (c->findText(QStringLiteral("Personalizado")) >= 0 && c->findText(QStringLiteral("Windows 98")) >= 0)
                    scheme = c;
            CHECK(scheme);
            if (!scheme)
                break;
            const int before = scheme->currentIndex();
            QTimer::singleShot(300, [choice] {
                if (auto* ask = qobject_cast<TaskDialog*>(QApplication::activeModalWidget()))
                    if (auto* b = ask->findChild<QAbstractButton*>(QStringLiteral("choice%1").arg(choice)))
                        b->click();
            });
            emit scheme->activated(scheme->findData(int(Theme::Scheme::Kde)));
            settle();
            if (choice == 1)
                CHECK(!settings.restartRequested() && scheme->currentIndex() == before);
            else
                CHECK(settings.restartRequested() && settings.settings().colorScheme == int(Theme::Scheme::Kde)
                      && settings.result() == QDialog::Accepted);
        }

        // Cambiar el idioma también pide reiniciar, con las mismas dos respuestas.
        for (int choice : {1, 0}) {
            SettingsDialog settings(AppSettings::load(), tools, &w);
            QComboBox* language = nullptr;
            for (QComboBox* c : settings.findChildren<QComboBox*>())
                if (c->findData(QStringLiteral("es")) >= 0 && c->findData(QStringLiteral("en")) >= 0)
                    language = c;
            CHECK(language);
            if (!language)
                break;
            const QString before = language->currentData().toString();
            const int other = language->findData(before == QLatin1String("es") ? QStringLiteral("en") : QStringLiteral("es"));
            QTimer::singleShot(300, [choice] {
                if (auto* ask = qobject_cast<TaskDialog*>(QApplication::activeModalWidget()))
                    if (auto* b = ask->findChild<QAbstractButton*>(QStringLiteral("choice%1").arg(choice)))
                        b->click();
            });
            language->setCurrentIndex(other);
            emit language->activated(other);
            settle();
            if (choice == 1)
                CHECK(!settings.restartRequested() && language->currentData().toString() == before);
            else
                CHECK(settings.restartRequested() && settings.settings().language != before);
        }

        // La flecha de la lista de imágenes va justo después de la última miniatura.
        auto* strip = w.findChild<QTabBar*>(QStringLiteral("ImageStrip"));
        auto* listButton = w.findChild<QToolButton*>(QStringLiteral("ImageListButton"));
        CHECK(strip && listButton && strip->count() > 0);
        if (strip && listButton && strip->count() > 0) {
            CHECK(strip->minimumSizeHint().width() <= strip->sizeHint().width());
            const int lastTab = strip->mapTo(w.window(), strip->tabRect(strip->count() - 1).topRight()).x();
            const int arrow = listButton->mapTo(w.window(), QPoint(0, 0)).x();
            CHECK(arrow - lastTab >= 0 && arrow - lastTab <= 12);
            CHECK(listButton->height() >= strip->height() - 8);

            // La flecha despliega la lista de imágenes con miniaturas; al elegir una se cierra.
            listButton->click();
            settle();
            auto* popup = qobject_cast<QFrame*>(QApplication::activePopupWidget());
            auto* imageList = popup ? popup->findChild<QListWidget*>(QStringLiteral("ImageList")) : nullptr;
            CHECK(imageList && imageList->count() == strip->count());
            if (imageList && imageList->count() > 1) {
                CHECK(imageList->currentRow() == strip->currentIndex());
                CHECK(imageList->iconSize() == strip->iconSize() && strip->iconSize().width() >= 64);
                if (!outDir.isEmpty())
                    w.grab().save(QDir(outDir).filePath(QStringLiteral("lista-imagenes.png")));
                const int target = strip->currentIndex() == 0 ? 1 : 0;
                emit imageList->itemClicked(imageList->item(target));
                settle();
                CHECK(!QApplication::activePopupWidget() && strip->currentIndex() == target);
            } else if (popup) {
                popup->close();
            }

            // La X va encima de la esquina de la miniatura (como en Paint.NET) y cierra la imagen;
            // arrastrar una miniatura sobre otra las reordena.
            w.newDefaultDocument();
            settle();
            auto mouse = [strip](QEvent::Type type, QPoint at, Qt::MouseButtons buttons) {
                QMouseEvent e(type, QPointF(at), QPointF(strip->mapToGlobal(at)),
                              type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton, buttons, Qt::NoModifier);
                QApplication::sendEvent(strip, &e);
            };
            const int tabs = strip->count();
            const int last = strip->currentIndex();
            const QRect tab = strip->tabRect(last);
            const QPoint xPos(tab.right() - 12, tab.top() + 12);
            mouse(QEvent::MouseMove, xPos, Qt::NoButton);
            mouse(QEvent::MouseButtonPress, xPos, Qt::LeftButton);
            mouse(QEvent::MouseButtonRelease, xPos, Qt::NoButton);
            settle();
            CHECK(strip->count() == tabs - 1);
            if (strip->count() >= 2) {
                strip->setCurrentIndex(0);
                settle();
                Document* first = canvas->document();
                const QPoint from = strip->tabRect(0).center(), to = strip->tabRect(1).center();
                mouse(QEvent::MouseButtonPress, from, Qt::LeftButton);
                mouse(QEvent::MouseMove, from + QPoint(10, 0), Qt::LeftButton);
                mouse(QEvent::MouseMove, to, Qt::LeftButton);
                mouse(QEvent::MouseButtonRelease, to, Qt::NoButton);
                settle();
                CHECK(strip->currentIndex() == 1 && canvas->document() == first);
            }
        }
    }

    std::printf("Historial: %d pasos · capas: %d\n", int(doc->undoStack()->count()), doc->layerCount());
    if (g_failures == 0)
        std::printf("La prueba de humo de la interfaz pasó.\n");
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}
