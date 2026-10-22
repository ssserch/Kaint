// Prueba de humo de la interfaz: arranca la ventana sin pantalla (plataforma offscreen), usa
// varias herramientas como lo haría el ratón, comprueba el resultado y guarda capturas para
// revisarlas a mano.

#include "app/AppState.h"
#include "app/Canvas.h"
#include "app/DiscordPresence.h"
#include "app/Icons.h"
#include "app/MainWindow.h"
#include "app/Theme.h"
#include "app/ToolOptionsBar.h"
#include "app/Workspace.h"
#include "app/dialogs/EffectDialog.h"
#include "app/dialogs/SaveConfigDialog.h"
#include "app/dialogs/TaskDialog.h"
#include "app/panels/LayersPanel.h"
#include "app/panels/ToolsPanel.h"
#include "app/tools/ToolManager.h"
#include "app/tools/TransformBox.h"
#include "core/CustomShapes.h"
#include "core/Document.h"
#include "core/plugins/PluginManager.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLineF>
#include <QListWidget>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMouseEvent>
#include <QPrinter>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
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
// la detección del modo oscuro de Plasma y cuándo hace falta reiniciar. El fondo del lienzo
// siempre es opaco (un color translúcido dejaba rastros).
static void testTheme()
{
    CHECK(!Theme::needsRestart(Theme::Scheme::System) && !Theme::needsRestart(Theme::Scheme::Dark));
    CHECK(!Theme::needsRestart(Theme::Scheme::Custom) && Theme::needsRestart(Theme::Scheme::Kde));

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
    for (const auto& [name, value] : {std::pair{"XDG_CONFIG_HOME", oldConfig}, std::pair{"XDG_CURRENT_DESKTOP", oldDesktop}}) {
        if (value.isEmpty())
            qunsetenv(name);
        else
            qputenv(name, value);
    }
    Theme::apply(Theme::Scheme::Light);

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
    drag(canvas, {QPointF(450, 250), QPointF(520, 330), QPointF(640, 470)});
    CHECK(tools->current()->isBusy());
    {
        const int steps = doc->undoStack()->count();
        tools->current()->finish();
        CHECK(!tools->current()->isBusy());
        CHECK(doc->undoStack()->count() == steps + 1);
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

    // Línea/Curva editable: arrastrar un nodo curva la línea y todo queda en un solo paso.
    {
        tools->select(ToolId::Line);
        state->updateTools([](ToolSettings& s) { s.brushSize = 5; s.curveType = CurveType::Spline; s.dash = Qt::SolidLine; });
        const int steps = doc->undoStack()->count();
        drag(canvas, {QPointF(100, 650), QPointF(300, 650), QPointF(500, 650)});
        CHECK(tools->current()->isBusy());
        drag(canvas, {QPointF(233, 650), QPointF(233, 560)});
        CHECK(qAlpha(doc->activeLayer()->image.pixel(233, 560)) > 0);
        tools->current()->finish();
        CHECK(doc->undoStack()->count() == steps + 1);
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
        CHECK(doc->undoStack()->text(doc->undoStack()->index() - 1) == QStringLiteral("Casa de prueba"));
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
        QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(canvas, &esc);
        CHECK(!tools->current()->isBusy() && doc->undoStack()->count() == steps + 1);
        state->updateTools([](ToolSettings& s) { s.textAlign = TextAlign::Left; });
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

    // Sin selección, ni Mover selección ni Mover píxeles seleccionan nada por su cuenta.
    {
        CHECK(doc->selection().isEmpty());
        tools->select(ToolId::MoveSelection);
        const int steps = doc->undoStack()->count();
        drag(canvas, {QPointF(300, 300)});
        drag(canvas, {QPointF(300, 300), QPointF(340, 320)});
        CHECK(doc->selection().isEmpty());
        CHECK(doc->undoStack()->count() == steps);
        tools->select(ToolId::MoveSelected);
        drag(canvas, {QPointF(300, 300)});
        CHECK(doc->selection().isEmpty());
        CHECK(doc->undoStack()->count() == steps);
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

    std::printf("Historial: %d pasos · capas: %d\n", int(doc->undoStack()->count()), doc->layerCount());
    if (g_failures == 0)
        std::printf("La prueba de humo de la interfaz pasó.\n");
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}
