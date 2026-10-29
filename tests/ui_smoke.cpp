// Prueba de humo de la interfaz: arranca la ventana sin pantalla (plataforma offscreen), usa
// varias herramientas como lo haría el ratón, comprueba el resultado y guarda capturas para
// revisarlas a mano.

#include "app/Animations.h"
#include "app/AppState.h"
#include "app/Canvas.h"
#include "app/DiscordPresence.h"
#include "app/IconPacks.h"
#include "app/Icons.h"
#include "app/MainWindow.h"
#include "app/Recovery.h"
#include "app/Ruler.h"
#include "app/Shortcuts.h"
#include "app/Theme.h"
#include "app/ToolOptionsBar.h"
#include "app/Workspace.h"
#include "app/dialogs/AboutDialog.h"
#include "app/dialogs/EffectDialog.h"
#include "app/dialogs/ImageDialogs.h"
#include "app/dialogs/SaveConfigDialog.h"
#include "app/dialogs/SettingsDialog.h"
#include "app/dialogs/TaskDialog.h"
#include "app/panels/ColorsPanel.h"
#include "app/panels/LayersPanel.h"
#include "app/panels/ToolsPanel.h"
#include "app/tools/Handles.h"
#include "app/tools/ToolManager.h"
#include "app/tools/TransformBox.h"
#include "app/tools/ViewTools.h"
#include "app/widgets/ColorWidgets.h"
#include "app/widgets/EncodedPreview.h"
#include "app/widgets/FontChooser.h"
#include "app/widgets/WaveLabel.h"
#include "core/CustomShapes.h"
#include "core/Document.h"
#include "core/Ora.h"
#include "core/Palette.h"
#include "core/effects/Effect.h"
#include "core/Zip.h"
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
#include <QGraphicsOpacityEffect>
#include <QImage>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineF>
#include <QFontComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QMouseEvent>
#include <QPrinter>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSlider>
#include <QStatusBar>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QBuffer>
#include <QLayout>
#include <QPainter>
#include <QToolButton>
#include <QtEndian>
#include <QUndoStack>
#include <QVariantAnimation>
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
    CHECK(!DiscordPresence::isValidClientId(QStringLiteral("sin-configurar")));
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

    // Píxel a píxel: sin giro, las esquinas caen en píxeles enteros; girado 90°, también; con
    // otros giros no se toca (si se redondeara el desplazamiento, temblaría al girar).
    const QTransform loose = QTransform::fromScale(1.37, 0.81) * QTransform::fromTranslate(10.4, 7.3);
    const QTransform snapped = TransformBox::snappedToPixels(loose, QRectF(0, 0, 100, 50));
    for (const QPointF corner : {QPointF(0, 0), QPointF(100, 50)}) {
        const QPointF p = snapped.map(corner);
        CHECK(near(p, QPointF(std::round(p.x()), std::round(p.y()))));
    }
    QTransform turned;
    turned.translate(5.6, 3.2).rotate(30);
    const QTransform turnedSnap = TransformBox::snappedToPixels(turned, QRectF(0, 0, 100, 50));
    CHECK(turnedSnap == turned);
    QTransform quarter;
    quarter.translate(5.6, 3.2).rotate(90);
    const QRectF quarterRect = TransformBox::snappedToPixels(quarter, QRectF(0, 0, 100, 50)).mapRect(QRectF(0, 0, 100, 50));
    CHECK(near(quarterRect.topLeft(), QPointF(std::round(quarterRect.left()), std::round(quarterRect.top()))));

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
    // Sin color de énfasis en las barras de título (fuera de Windows), cabeceras del tema.
    CHECK(!Theme::titleBarColor().isValid() && Theme::active().panelTitle == Theme::preset(true).panelTitle);
    // Con él (morado de Windows), las cabeceras de las ventanas flotantes lo llevan, como en Paint.NET.
    const QColor purple(0x74, 0x4d, 0xa9);
    const Theme::Spec accented = Theme::withTitleBarColor(Theme::preset(true), purple);
    CHECK(accented.panelTitle == purple && accented.panelTitleEnd == purple);
    CHECK(accented.panelTitleText == QColor(Qt::white) && accented.panel == Theme::preset(true).panel);
    CHECK(Theme::withTitleBarColor(Theme::preset(false), QColor(0xff, 0xd8, 0x6a)).panelTitleText == QColor(Qt::black));
    CHECK(Theme::withTitleBarColor(Theme::preset(false), QColor()) == Theme::preset(false));

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
    // Sin haber usado ningún efecto, Efectos no muestra "Repetir".
    if (QAction* repeat = findAction(&w, QStringLiteral("Repetir el último efecto")))
        CHECK(!repeat->isVisible());
    else
        CHECK(false);

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
    // Como en Paint.NET, cada arrastre es un paso del historial.
    drag(canvas, {QPointF(200, 150), QPointF(210, 160), QPointF(230, 190)});
    drag(canvas, {QPointF(230, 190), QPointF(240, 200)});
    CHECK(doc->undoStack()->count() == before + 2);
    doc->undoStack()->undo();
    CHECK(doc->undoStack()->count() == before + 2 && doc->undoStack()->index() == before + 1);
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

    // Ctrl+Z / Ctrl+Y en el diálogo de un efecto: deshacen y rehacen sus cambios, también con el
    // foco en una caja numérica (un arrastre seguido es un solo paso).
    if (const Effect* blur = Effects::find(QStringLiteral("gaussianblur"))) {
        EffectDialog dlg(*blur, Effects::defaults(*blur), doc->activeLayer()->image, doc->rect(), &w);
        dlg.show();
        settle();
        QDoubleSpinBox* radius = nullptr;
        for (QDoubleSpinBox* box : dlg.findChildren<QDoubleSpinBox*>())
            if (std::abs(box->value() - 2.0) < 1e-9 && box->maximum() == 200)
                radius = box;
        CHECK(radius);
        if (radius) {
            radius->setValue(10);
            radius->setValue(12);
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < 800)
                QApplication::processEvents(QEventLoop::AllEvents, 20);
            radius->setValue(30);
            radius->setFocus();
            auto key = [](QWidget* target, int k, Qt::KeyboardModifiers mods) {
                QKeyEvent over(QEvent::ShortcutOverride, k, mods);
                QApplication::sendEvent(target, &over);
                QKeyEvent press(QEvent::KeyPress, k, mods);
                QApplication::sendEvent(target, &press);
                settle();
            };
            key(radius, Qt::Key_Z, Qt::ControlModifier);
            CHECK(std::abs(dlg.values().value(QStringLiteral("radius")).toDouble() - 12) < 1e-9 && std::abs(radius->value() - 12) < 1e-9);
            key(radius, Qt::Key_Z, Qt::ControlModifier);
            CHECK(std::abs(dlg.values().value(QStringLiteral("radius")).toDouble() - 2) < 1e-9);
            key(radius, Qt::Key_Y, Qt::ControlModifier);
            CHECK(std::abs(dlg.values().value(QStringLiteral("radius")).toDouble() - 12) < 1e-9);
        }
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

    // Un clic fuera de la imagen con el degradado y luego con la goma y Ctrl (el bloqueo de la
    // versión web): la goma no deja paso y se sigue pudiendo cambiar de herramienta.
    {
        tools->select(ToolId::Gradient);
        drag(canvas, {QPointF(-30, -30)});
        tools->select(ToolId::Eraser);
        const int steps = doc->undoStack()->count();
        drag(canvas, {QPointF(-40, -40)}, Qt::LeftButton, Qt::ControlModifier);
        CHECK(doc->undoStack()->count() == steps && !tools->current()->isBusy());
        tools->select(ToolId::Pencil);
        CHECK(tools->current()->id() == ToolId::Pencil);
        tools->select(ToolId::Eraser);
    }

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
        CHECK(!tools->current()->isBusy() && doc->undoStack()->count() == steps + 1);
        CHECK(doc->undoStack()->text(steps) == QStringLiteral("Finalizar texto"));

        // Al volver al paso del texto se sigue editando, con sus colores y opciones.
        const QColor textColor = state->primary();
        state->setPrimary(QColor(0, 150, 255));
        state->updateTools([](ToolSettings& s) { s.textAlign = TextAlign::Left; });
        const QImage written = doc->activeLayer()->image;
        findAction(&w, QStringLiteral("Deshacer"))->trigger();
        CHECK(tools->current()->isBusy() && state->primary() == textColor);
        CHECK(state->tools().textAlign == TextAlign::Center && doc->activeLayer()->image == written);
        typeText(canvas, QStringLiteral("d"));
        CHECK(doc->undoStack()->count() == steps + 1 && doc->undoStack()->text(steps) == QStringLiteral("Editar texto"));
        QApplication::sendEvent(canvas, &esc);
        CHECK(!tools->current()->isBusy() && doc->undoStack()->count() == steps + 2);
        CHECK(doc->undoStack()->text(steps + 1) == QStringLiteral("Finalizar texto"));
        CHECK(doc->activeLayer()->image != written);
        state->updateTools([](ToolSettings& s) { s.textAlign = TextAlign::Left; });
    }

    // Pasos en vivo, como en Paint.NET: el cubo y el texto aparecen en el historial al empezar;
    // cambiar de herramienta termina el cubo ("Finalizar…") y deshacer mientras se edita quita el texto.
    {
        QUndoStack* stack = doc->undoStack();
        tools->select(ToolId::PaintBucket);
        state->setPrimary(QColor(10, 200, 120));
        int steps = stack->count();
        drag(canvas, {QPointF(30, 560)});
        CHECK(tools->current()->isBusy() && stack->count() == steps + 1);
        CHECK(stack->text(steps) == QStringLiteral("Cubo de pintura"));
        tools->select(ToolId::Text);
        CHECK(stack->count() == steps + 2 && stack->index() == steps + 2);
        CHECK(stack->text(steps + 1) == QStringLiteral("Finalizar cubo de pintura"));
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
            CHECK(std::abs(canvas->zoom() / before - 1.12) < 0.01);
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

    // Iconos de la interfaz (los PNG de paint.js): existen todos los de la tabla, los
    // desactivados son grises, los ajustes y efectos tienen el suyo y la mano tiene su versión
    // agarrando.
    {
        CHECK(Icons::missingNamedIcons().isEmpty());
        const QIcon layerNew = Icons::theme({"layer-new"});
        const QImage normal = layerNew.pixmap(QSize(16, 16)).toImage().convertToFormat(QImage::Format_ARGB32);
        const QImage disabled =
            layerNew.pixmap(QSize(16, 16), QIcon::Disabled).toImage().convertToFormat(QImage::Format_ARGB32);
        bool colored = false, gray = !disabled.isNull();
        for (int y = 0; y < normal.height(); ++y)
            for (int x = 0; x < normal.width(); ++x) {
                const QRgb n = normal.pixel(x, y), d = disabled.pixel(x, y);
                colored = colored || (qAlpha(n) > 200 && (qRed(n) != qGreen(n) || qGreen(n) != qBlue(n)));
                gray = gray && (qAlpha(d) == 0 || (qRed(d) == qGreen(d) && qGreen(d) == qBlue(d)));
            }
        CHECK(colored && gray);
        int builtIn = 0, withIcon = 0;
        for (const Effect* e : Effects::all()) {
            if (!e->pluginName().isEmpty())
                continue;
            ++builtIn;
            if (Icons::effectIcon(e->id(), {}).startsWith(QLatin1String("effect-")))
                ++withIcon;
        }
        CHECK(builtIn > 40 && withIcon >= builtIn - 1);
        CHECK(!Icons::pdn(QStringLiteral("pan-grab")).isNull());
        CHECK(!Icons::handCursor(true).pixmap().isNull() && !Icons::handCursor(false).pixmap().isNull());
        const QIcon pressure = Icons::png(QStringLiteral("tool_config_strip_brush_enable_pressure_sensitivity_false"),
                                          QStringLiteral("tool_config_strip_brush_enable_pressure_sensitivity_true"));
        CHECK(pressure.pixmap(QSize(16, 16), QIcon::Normal, QIcon::Off).toImage()
              != pressure.pixmap(QSize(16, 16), QIcon::Normal, QIcon::On).toImage());
        // La ventana Herramientas lleva los iconos dibujados de Kaint; los cursores de la lupa y el
        // cuentagotas siguen siendo los PNG de paint.js.
        for (const QString& name : {QStringLiteral("rectangleselect"), QStringLiteral("zoom"), QStringLiteral("text"),
                                    QStringLiteral("shapes"), QStringLiteral("colorpicker")}) {
            const QImage drawn = Icons::tool(name, QIcon()).pixmap(QSize(24, 24)).toImage();
            CHECK(!drawn.isNull() && Icons::named(name).isNull());
        }
        if (auto* toolsPanel = w.findChild<ToolsPanel*>()) {
            const QList<QToolButton*> buttons = toolsPanel->findChildren<QToolButton*>();
            CHECK(!buttons.isEmpty()
                  && buttons.first()->icon().pixmap(QSize(22, 22)).toImage()
                         == Icons::tool(QStringLiteral("rectangleselect"), QIcon()).pixmap(QSize(22, 22)).toImage());
        }
        // La barra principal lleva los mismos PNG que los menús.
        if (auto* mainBar = w.findChild<QToolBar*>(QStringLiteral("MainToolBar"))) {
            QAction* save = findAction(&w, QStringLiteral("Guardar"));
            CHECK(save && mainBar->actions().contains(save));
        }
    }

    // Paquetes de iconos de paint.js: un ZIP con asset-pack.json (dentro de una carpeta) cambia
    // el icono de un menú y el de una herramienta dibujada; los que no trae siguen siendo los de
    // Kaint. Se descartan las rutas que salen de la carpeta del paquete.
    {
        auto solid = [](const QColor& c) {
            QImage img(32, 32, QImage::Format_ARGB32);
            img.fill(c);
            QByteArray bytes;
            QBuffer buffer(&bytes);
            buffer.open(QIODevice::WriteOnly);
            img.save(&buffer, "PNG");
            return bytes;
        };
        ZipWriter zip;
        zip.add(QStringLiteral("mi-paquete/asset-pack.json"),
                QByteArrayLiteral("{\"formatVersion\": 1, \"id\": \"test.pack\", \"name\": \"Paquete de prueba\", "
                                  "\"author\": \"Prueba\", \"version\": \"1.0.0\"}"));
        zip.add(QStringLiteral("mi-paquete/icons/menu_file_open_icon.png"), solid(QColor(255, 0, 0)));
        zip.add(QStringLiteral("mi-paquete/icons/text_tool_icon.png"), solid(QColor(0, 0, 255)));
        zip.add(QStringLiteral("mi-paquete/icons/../../fuera.png"), solid(Qt::green));
        QTemporaryDir packDir;
        const QString packFile = packDir.filePath(QStringLiteral("pack.zip"));
        {
            QFile f(packFile);
            CHECK(f.open(QIODevice::WriteOnly) && f.write(zip.finish()) > 0);
        }
        const QImage openBefore = Icons::named(QStringLiteral("document-open")).pixmap(QSize(16, 16)).toImage();
        const QImage saveBefore = Icons::named(QStringLiteral("document-save")).pixmap(QSize(16, 16)).toImage();
        const QIcon textIcon = Icons::tool(QStringLiteral("text"), QIcon());
        const QImage textBefore = textIcon.pixmap(QSize(16, 16)).toImage();
        QString error;
        const QString id = IconPacks::install(packFile, &error);
        CHECK(id == QStringLiteral("test.pack"));
        CHECK(!QFile::exists(IconPacks::root() + QStringLiteral("/fuera.png")));
        bool listed = false;
        for (const IconPacks::Pack& p : IconPacks::installed())
            listed = listed || (p.id == id && p.name == QStringLiteral("Paquete de prueba") && p.images == 2);
        CHECK(listed);
        IconPacks::activate(id);
        settle();
        CHECK(IconPacks::activeId() == id);
        const QImage openAfter = Icons::named(QStringLiteral("document-open")).pixmap(QSize(16, 16)).toImage();
        CHECK(openAfter != openBefore && openAfter.pixelColor(8, 8) == QColor(255, 0, 0));
        CHECK(Icons::named(QStringLiteral("document-save")).pixmap(QSize(16, 16)).toImage() == saveBefore);
        // El icono ya creado (el de la ventana Herramientas) cambia al volver a dibujarse.
        CHECK(textIcon.pixmap(QSize(16, 16)).toImage().pixelColor(8, 8) == QColor(0, 0, 255));
        IconPacks::activate(IconPacks::defaultId());
        CHECK(Icons::named(QStringLiteral("document-open")).pixmap(QSize(16, 16)).toImage() == openBefore);
        CHECK(textIcon.pixmap(QSize(16, 16)).toImage() == textBefore);
        CHECK(IconPacks::remove(id));
        CHECK(IconPacks::installed().size() == 1);
        // Un ZIP sin asset-pack.json no es un paquete.
        ZipWriter bad;
        bad.add(QStringLiteral("icons/menu_file_open_icon.png"), solid(Qt::red));
        {
            QFile f(packFile);
            CHECK(f.open(QIODevice::WriteOnly) && f.write(bad.finish()) > 0);
        }
        CHECK(IconPacks::install(packFile, &error).isEmpty() && !error.isEmpty());
    }

    // Cursores con el icono actual de cada herramienta (punto activo en la punta), la flecha de
    // girar que cambia con el ángulo, las paletas de muestra y la estrella de cambios sin guardar.
    {
        const QCursor pencil = Icons::toolCursor(QStringLiteral("pencil"));
        CHECK(pencil.shape() == Qt::BitmapCursor && !pencil.pixmap().isNull());
        CHECK(pencil.hotSpot().x() < pencil.pixmap().width() / 3 && pencil.hotSpot().y() > pencil.pixmap().height() * 2 / 3);
        const QCursor move = Icons::toolCursor(QStringLiteral("moveselectedpixels"));
        CHECK(move.hotSpot().x() < 4 && move.hotSpot().y() < 4);
        tools->select(ToolId::Paintbrush);
        settle();
        CHECK(canvas->viewport()->cursor().shape() == Qt::BitmapCursor);
        CHECK(Icons::rotateCursor(0).pixmap().toImage() != Icons::rotateCursor(M_PI / 2).pixmap().toImage());
        // Selección, degradado y línea: la flecha (punto activo en la punta) con el icono en
        // miniatura; el texto, la I; las formas, la forma elegida.
        for (ToolId id : {ToolId::RectSelect, ToolId::LassoSelect, ToolId::EllipseSelect, ToolId::Gradient, ToolId::Line}) {
            tools->select(id);
            const QCursor c = tools->current()->cursor();
            CHECK(c.shape() == Qt::BitmapCursor && c.hotSpot() == QPoint(0, 0) && !c.pixmap().isNull());
        }
        CHECK(Icons::miniToolCursor(QStringLiteral("gradient")).pixmap().toImage()
              != Icons::miniToolCursor(QStringLiteral("linecurve")).pixmap().toImage());
        tools->select(ToolId::Text);
        const QCursor ibeam = tools->current()->cursor();
        CHECK(ibeam.shape() == Qt::BitmapCursor && ibeam.hotSpot().x() > 0 && ibeam.hotSpot().y() > ibeam.hotSpot().x());
        tools->select(ToolId::Shapes);
        const ShapeType oldShape = state->tools().shape;
        state->updateTools([](ToolSettings& s) { s.shape = ShapeType::Rectangle; });
        const QImage rectCursor = tools->current()->cursor().pixmap().toImage();
        state->updateTools([](ToolSettings& s) { s.shape = ShapeType::Heart; });
        CHECK(tools->current()->cursor().pixmap().toImage() != rectCursor);
        CHECK(canvas->viewport()->cursor().pixmap().toImage() == tools->current()->cursor().pixmap().toImage());
        state->updateTools([oldShape](ToolSettings& s) { s.shape = oldShape; });
        tools->select(ToolId::Paintbrush);
        CHECK(Palette::rainbowColors().size() == Palette::kColorCount && Palette::smallColors().size() == Palette::kColorCount);
        CHECK(Palette::smallColors().value(40) == QColor(Qt::white) && Palette::rainbowColors().value(0) != Palette::rainbowColors().value(1));
        QImage star(32, 32, QImage::Format_ARGB32_Premultiplied);
        star.fill(Qt::transparent);
        {
            QPainter p(&star);
            Icons::paintUnsavedStar(p, QRectF(0, 0, 32, 32));
        }
        CHECK(star.pixelColor(16, 3).red() > 200 && star.pixelColor(16, 3).blue() < 80 && star.pixelColor(16, 16) == QColor(Qt::white));
    }

    // Atajos de teclado editables: cambiar uno lo quita de la acción que lo usaba; restablecer
    // vuelve a los de fábrica, también al abrir Kaint otra vez (QSettings).
    {
        QAction* save = findAction(&w, QStringLiteral("Guardar"));
        QAction* open = findAction(&w, QStringLiteral("Abrir..."));
        CHECK(save && open);
        if (save && open) {
            QString saveId, openId;
            for (const Shortcuts::Entry& e : Shortcuts::entries()) {
                if (e.action == save)
                    saveId = e.id;
                if (e.action == open)
                    openId = e.id;
            }
            CHECK(!saveId.isEmpty() && !openId.isEmpty());
            const QKeySequence openKey = open->shortcut();
            CHECK(Shortcuts::assign(saveId, openKey) == open);
            CHECK(save->shortcut() == openKey && open->shortcut().isEmpty());
            CHECK(QSettings().contains(QStringLiteral("shortcuts/") + saveId));
            CHECK(!Shortcuts::isDefault(saveId) && !Shortcuts::isDefault(openId));
            Shortcuts::resetAll();
            CHECK(open->shortcut() == openKey && save->shortcut() == QKeySequence(QKeySequence::Save));
            CHECK(!QSettings().contains(QStringLiteral("shortcuts/") + saveId));
        }
    }

    // Reglas como las de paint.js: oscuras, y las ventanas flotantes no las tapan.
    {
        QAction* rulers = findAction(&w, QStringLiteral("Reglas"));
        CHECK(rulers);
        auto* workspace = w.findChild<Workspace*>();
        if (rulers && workspace) {
            rulers->setChecked(true);
            settle();
            for (FloatingPanel* p : workspace->panels())
                CHECK(!p->isVisible() || (p->x() >= Ruler::kThickness && p->y() >= Ruler::kThickness));
            const auto rulerWidgets = canvas->findChildren<Ruler*>();
            CHECK(!rulerWidgets.isEmpty());
            if (!rulerWidgets.isEmpty())
                CHECK(rulerWidgets.first()->grab().toImage().pixelColor(2, 2).lightness() < 60);
            FloatingPanel* toolsWindow = nullptr;
            for (FloatingPanel* p : workspace->panels())
                if (p->id() == QStringLiteral("tools"))
                    toolsWindow = p;
            const QPoint withRulers = toolsWindow ? toolsWindow->pos() : QPoint();
            rulers->setChecked(false);
            settle();
            CHECK(toolsWindow && toolsWindow->pos() == withRulers - QPoint(Ruler::kThickness, Ruler::kThickness));
        }
    }

    // Ventanas al estilo de Windows 10 (como paint.js) y con el borde que se elija: el contenido
    // se aparta del borde.
    {
        auto* workspace = w.findChild<Workspace*>();
        FloatingPanel* history = nullptr;
        for (FloatingPanel* p : workspace ? workspace->panels() : QList<FloatingPanel*>())
            if (p->id() == QStringLiteral("history"))
                history = p;
        CHECK(history);
        if (workspace && history) {
            CHECK(FloatingPanel::chromeStyle() == FloatingPanel::Style::Windows10);
            const int compactTop = [&] {
                workspace->setPanelChrome(FloatingPanel::Style::Compact, 1);
                return history->layout()->contentsMargins().top();
            }();
            workspace->setPanelChrome(FloatingPanel::Style::Windows10, 1);
            CHECK(history->layout()->contentsMargins().top() > compactTop);
            workspace->setPanelChrome(FloatingPanel::Style::Windows10, 9);
            CHECK(history->layout()->contentsMargins().left() + history->frameWidth() >= 9);
            workspace->setPanelChrome(FloatingPanel::Style::Windows10, 1);
            CHECK(history->layout()->contentsMargins().left() + history->frameWidth() == FloatingPanel::kBorder);
        }
    }

    // Zum: el recuadro que se arrastra va de borde a borde de píxel, como la selección
    // rectangular, y se acerca a ese rectángulo.
    {
        tools->select(ToolId::Zoom);
        canvas->setZoom(4.0);
        settle();
        auto send = [&](QEvent::Type type, const QPointF& img, Qt::MouseButton b, Qt::MouseButtons bs) {
            const QPointF v = canvas->imageToView(img);
            QMouseEvent e(type, v, canvas->viewport()->mapToGlobal(v), b, bs, Qt::NoModifier);
            QApplication::sendEvent(canvas->viewport(), &e);
        };
        send(QEvent::MouseButtonPress, QPointF(40.3, 30.2), Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseMove, QPointF(50.2, 38.4), Qt::NoButton, Qt::LeftButton);
        send(QEvent::MouseMove, QPointF(60.4, 45.6), Qt::NoButton, Qt::LeftButton);
        settle();
        auto* zoomTool = qobject_cast<ZoomTool*>(tools->current());
        CHECK(zoomTool && zoomTool->dragRect() == QRect(40, 30, 20, 16));
        if (!outDir.isEmpty())
            canvas->viewport()->grab().save(QDir(outDir).filePath(QStringLiteral("zum.png")));
        send(QEvent::MouseButtonRelease, QPointF(60.4, 45.6), Qt::LeftButton, Qt::NoButton);
        settle();
        const QPointF center = canvas->viewToImage(QPointF(canvas->viewport()->width() / 2.0, canvas->viewport()->height() / 2.0));
        CHECK(canvas->zoom() > 4.0 && QLineF(center, QPointF(50, 38)).length() < 1.0);
        // Justo el rectángulo, sin margen: ocupa toda la vista a lo ancho o a lo alto.
        const QPointF topLeft = canvas->imageToView(QPointF(40, 30));
        const QPointF bottomRight = canvas->imageToView(QPointF(60, 46));
        const QSize viewSize = canvas->viewport()->size();
        CHECK(std::abs((bottomRight.x() - topLeft.x()) - viewSize.width()) < 1.5
              || std::abs((bottomRight.y() - topLeft.y()) - viewSize.height()) < 1.5);
        CHECK(topLeft.x() > -1.5 && topLeft.y() > -1.5 && bottomRight.x() < viewSize.width() + 1.5
              && bottomRight.y() < viewSize.height() + 1.5);
        CHECK(zoomTool && zoomTool->dragRect().isEmpty());
        canvas->zoomToWindow();
        tools->select(ToolId::RectSelect);
        settle();
    }

    // Animaciones (Configuración > Animaciones), como las de paint.js: las capas que cambian de
    // sitio se deslizan, las ventanas flotantes y los menús aparecen con un fundido y, sin
    // animaciones, todo cambia al momento.
    {
        auto wait = [](int ms) {
            QElapsedTimer clock;
            clock.start();
            while (clock.elapsed() < ms)
                QApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        auto animating = [](QObject* o) {
            for (QVariantAnimation* a : o->findChildren<QVariantAnimation*>())
                if (a->state() == QAbstractAnimation::Running)
                    return true;
            return false;
        };
        Animations::install();
        CHECK(Animations::enabled());
        auto* layersPanel = w.findChild<LayersPanel*>();
        FloatingPanel* history = nullptr;
        for (FloatingPanel* p : w.findChildren<FloatingPanel*>())
            if (p->id() == QLatin1String("history"))
                history = p;
        CHECK(layersPanel && history);
        if (layersPanel && history) {
            findAction(&w, QStringLiteral("Agregar nueva capa"))->trigger();
            settle();
            findAction(&w, QStringLiteral("Bajar capa"))->trigger();
            CHECK(animating(layersPanel));
            wait(Animations::kListMove + 120);
            CHECK(!animating(layersPanel));

            history->hide();
            history->showAnimated();
            auto* fade = qobject_cast<QGraphicsOpacityEffect*>(history->graphicsEffect());
            CHECK(fade && fade->opacity() < 0.5);
            wait(Animations::kPanelFade + 120);
            fade = qobject_cast<QGraphicsOpacityEffect*>(history->graphicsEffect());
            CHECK(!fade || fade->opacity() > 0.5);

            QMenu menu;
            menu.addAction(QStringLiteral("Uno"));
            menu.popup(w.mapToGlobal(QPoint(40, 40)));
            settle();
            CHECK(menu.graphicsEffect() && menu.graphicsEffect()->property("kaintFade").toBool());
            wait(Animations::kPopupFade + 120);
            CHECK(!menu.graphicsEffect());
            menu.close();

            Animations::setEnabled(false);
            findAction(&w, QStringLiteral("Subir capa"))->trigger();
            CHECK(!animating(layersPanel));
            history->hide();
            history->showAnimated();
            fade = qobject_cast<QGraphicsOpacityEffect*>(history->graphicsEffect());
            CHECK(!fade || fade->opacity() > 0.5);
            Animations::setEnabled(true);
            findAction(&w, QStringLiteral("Eliminar la capa"))->trigger();
            settle();
        }
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
        CHECK(!back.gifStack && !back.gifGlobalPalette && back.gifEffects == Gif::Effects());

        // Vista previa del GIF: el botón la reproduce con sus duraciones.
        QImage dot(60, 40, kLayerFormat);
        dot.fill(kTransparent);
        QList<Gif::Frame> moving;
        for (int i = 0; i < 3; ++i) {
            QImage f = dot.copy();
            f.setPixel(10 + 20 * i, 20, qRgba(255, 0, 0, 255));
            moving << Gif::Frame{f, 40};
        }
        SaveConfigDialog player(moving.first().image, QStringLiteral("anim.gif"), ImageIO::SaveOptions(), &w, moving);
        player.show();
        auto frameText = [&player] {
            for (QLabel* l : player.findChildren<QLabel*>())
                if (l->text().startsWith(QStringLiteral("Fotograma")))
                    return l->text();
            return QString();
        };
        QElapsedTimer t;
        t.start();
        while (frameText().isEmpty() && t.elapsed() < 5000)
            QApplication::processEvents(QEventLoop::AllEvents, 20);
        CHECK(frameText() == QStringLiteral("Fotograma 1 de 3"));
        auto* play = player.findChild<QToolButton*>();
        CHECK(play && play->isEnabled());
        if (play)
            play->click();
        t.restart();
        while (frameText() == QStringLiteral("Fotograma 1 de 3") && t.elapsed() < 2000)
            QApplication::processEvents(QEventLoop::AllEvents, 10);
        CHECK(frameText() != QStringLiteral("Fotograma 1 de 3"));
        if (!outDir.isEmpty())
            player.grab().save(QDir(outDir).filePath(QStringLiteral("gif-reproducir.png")));

        // Configuración avanzada: omitir el segundo fotograma y copiar el primero.
        QPushButton* advanced = nullptr;
        for (QPushButton* b : player.findChildren<QPushButton*>())
            if (b->text().startsWith(QStringLiteral("Configuración avanzada")))
                advanced = b;
        CHECK(advanced && advanced->isEnabled());
        QTimer::singleShot(300, [&outDir] {
            auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            CHECK(dlg);
            if (!dlg)
                return;
            QList<QPushButton*> skips, copies;
            for (QPushButton* b : dlg->findChildren<QPushButton*>()) {
                if (b->text() == QStringLiteral("Omitir"))
                    skips << b;
                else if (b->text() == QStringLiteral("Copiar"))
                    copies << b;
            }
            CHECK(skips.size() == 3 && copies.size() == 3);
            // También aquí, la vista previa con su botón de reproducir.
            auto* livePreview = dlg->findChild<EncodedPreview*>();
            CHECK(livePreview && livePreview->findChild<QToolButton*>());
            if (skips.size() == 3)
                skips[1]->click();
            settle();
            for (QPushButton* b : dlg->findChildren<QPushButton*>())
                if (b->text() == QStringLiteral("Copiar")) {
                    b->click();
                    break;
                }
            settle();
            if (!outDir.isEmpty())
                dlg->grab().save(QDir(outDir).filePath(QStringLiteral("gif-avanzada.png")));
            dlg->accept();
        });
        if (advanced)
            advanced->click();
        settle();
        CHECK(player.animationFrames().size() == 3);
        player.close();

        // "Mostrar capas ocultas": sus fotogramas entran en el GIF. Con el ojo cerrado en la
        // configuración avanzada, ese fotograma no va; tras editar con las ocultas a la vista ya
        // no se pueden quitar hasta volver a abrir el diálogo.
        QList<Gif::Frame> layered = moving;
        layered[1].hidden = true;
        SaveConfigDialog hidden(layered.first().image, QStringLiteral("anim.gif"), ImageIO::SaveOptions(), &w, layered);
        QPushButton* showHidden = nullptr;
        for (QPushButton* b : hidden.findChildren<QPushButton*>())
            if (b->text() == QStringLiteral("Mostrar capas ocultas"))
                showHidden = b;
        CHECK(showHidden && showHidden->isEnabled() && hidden.animationFrames().size() == 2);
        if (showHidden) {
            showHidden->click();
            CHECK(showHidden->text() == QStringLiteral("Ocultar las capas ocultas") && hidden.animationFrames().size() == 3
                  && hidden.options().gifHiddenLayers);
            showHidden->click();
            CHECK(hidden.animationFrames().size() == 2);
            showHidden->click();
        }
        QPushButton* hiddenAdvanced = nullptr;
        for (QPushButton* b : hidden.findChildren<QPushButton*>())
            if (b->text().startsWith(QStringLiteral("Configuración avanzada")))
                hiddenAdvanced = b;
        QTimer::singleShot(300, [] {
            auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            CHECK(dlg);
            if (!dlg)
                return;
            const QList<QToolButton*> eyes = dlg->findChildren<QToolButton*>(QStringLiteral("frameEye"));
            CHECK(eyes.size() == 3);
            if (!eyes.isEmpty())
                eyes.first()->click();
            settle();
            dlg->accept();
        });
        if (hiddenAdvanced)
            hiddenAdvanced->click();
        settle();
        CHECK(hidden.animationFrames().size() == 2);
        CHECK(showHidden && !showHidden->isEnabled());
        // "Predeterminado" lo reinicia todo, como cerrar y volver a abrir (sin cerrar la ventana).
        QPushButton* restart = nullptr;
        for (QPushButton* b : hidden.findChildren<QPushButton*>()) {
            CHECK(b->text() != QStringLiteral("Reiniciar"));
            if (b->text() == QStringLiteral("Predeterminado"))
                restart = b;
        }
        CHECK(restart);
        if (restart)
            restart->click();
        CHECK(showHidden && showHidden->isEnabled() && showHidden->text() == QStringLiteral("Mostrar capas ocultas"));
        CHECK(hidden.animationFrames().size() == 2 && !hidden.options().gifHiddenLayers);
        // En las cajas numéricas solo va el número (la unidad, al lado).
        for (QSpinBox* box : hidden.findChildren<QSpinBox*>())
            CHECK(box->suffix().isEmpty() && box->prefix().isEmpty() && box->specialValueText().isEmpty());
    }

    // El color propio del fondo del lienzo va por tema: el que había antes (uno solo) pasa al
    // claro o al oscuro según su brillo, y el tema claro ya no se queda con un fondo oscuro.
    {
        QSettings s;
        // Como en una configuración de antes de separarlos (otros bloques ya guardaron los nuevos).
        s.remove(QStringLiteral("settings/canvasBorderDark"));
        s.remove(QStringLiteral("settings/canvasBorderLight"));
        s.setValue(QStringLiteral("settings/canvasBorder"), QColor(0x2c, 0x2c, 0x2c));
        AppSettings migrated = AppSettings::load();
        CHECK(migrated.canvasBorderDark == QColor(0x2c, 0x2c, 0x2c) && !migrated.canvasBorderLight.isValid());
        migrated.save();
        CHECK(!s.contains(QStringLiteral("settings/canvasBorder")));
        const AppSettings back = AppSettings::load();
        CHECK(back.canvasBorderDark == QColor(0x2c, 0x2c, 0x2c) && !back.canvasBorderLight.isValid());
        s.remove(QStringLiteral("settings/canvasBorderDark"));
        s.remove(QStringLiteral("settings/canvasBorderLight"));
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

    // Pegar con transparencia, como en Paint.NET: dentro del rectángulo pegado, lo transparente
    // también reemplaza a lo de debajo (configurable); al arrastrar con Mover píxeles solo queda
    // el icono de mover; Cortar necesita una selección; el portapapeles lleva también un PNG.
    {
        auto* strip = w.findChild<QTabBar*>(QStringLiteral("ImageStrip"));
        const int first = strip ? strip->currentIndex() : -1;
        w.newDefaultDocument();
        settle();
        Document* d = canvas->document();
        QImage glass(120, 120, QImage::Format_ARGB32);
        glass.fill(Qt::transparent);
        for (int y = 0; y < 120; ++y)
            for (int x = 60; x < 120; ++x)
                glass.setPixelColor(x, y, QColor(200, 30, 30));
        const ToolPreferences prefs = state->preferences();
        CHECK(prefs.replacePixels && prefs.hideBoxWhileDragging && state->tools().moveInterpolation == Resample::Interpolation::Bicubic);
        for (bool replace : {true, false}) {
            ToolPreferences p = prefs;
            p.replacePixels = replace;
            state->setPreferences(p);
            d->activeLayer()->image.fill(Qt::black);
            d->notifyPixelsChanged(d->activeLayer(), d->rect());
            QApplication::clipboard()->setImage(glass);
            findAction(&w, QStringLiteral("Pegar"))->trigger();
            settle();
            const QPoint at = d->selection().bounds().topLeft();
            CHECK(d->selection().bounds().size() == glass.size());
            CHECK(QColor::fromRgba(d->activeLayer()->image.pixel(at + QPoint(90, 60))) == QColor(200, 30, 30));
            const QRgb hole = d->activeLayer()->image.pixel(at + QPoint(20, 60));
            CHECK(replace ? qAlpha(hole) == 0 : hole == qRgb(0, 0, 0));
            // Moverlo también reemplaza (o mezcla) en el sitio nuevo.
            drag(canvas, {QPointF(at + QPoint(30, 30)), QPointF(at + QPoint(230, 30))});
            const QRgb moved = d->activeLayer()->image.pixel(at + QPoint(220, 60));
            CHECK(replace ? qAlpha(moved) == 0 : moved == qRgb(0, 0, 0));
            tools->select(ToolId::RectSelect);
            findAction(&w, QStringLiteral("Anular la selección"))->trigger();
            settle();
        }
        state->setPreferences(prefs);

        // Arrastrar con Mover píxeles oculta el contorno y los tiradores hasta soltar.
        tools->select(ToolId::RectSelect);
        drag(canvas, {QPointF(100, 100), QPointF(160, 150), QPointF(200, 180)});
        tools->select(ToolId::MoveSelected);
        auto send = [canvas](QEvent::Type type, const QPointF& img, Qt::MouseButtons buttons) {
            const QPointF v = canvas->imageToView(img);
            QMouseEvent e(type, v, canvas->viewport()->mapToGlobal(v), type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                          buttons, Qt::NoModifier);
            QApplication::sendEvent(canvas->viewport(), &e);
        };
        CHECK(!tools->current()->hidesSelectionOutline());
        send(QEvent::MouseButtonPress, QPointF(125, 120), Qt::LeftButton);
        send(QEvent::MouseMove, QPointF(170, 150), Qt::LeftButton);
        CHECK(tools->current()->hidesSelectionOutline());
        if (!outDir.isEmpty())
            w.grab().save(QDir(outDir).filePath(QStringLiteral("mover-arrastrando.png")));
        send(QEvent::MouseButtonRelease, QPointF(170, 150), Qt::NoButton);
        settle();
        CHECK(!tools->current()->hidesSelectionOutline());
        ToolPreferences show = prefs;
        show.hideBoxWhileDragging = false;
        state->setPreferences(show);
        send(QEvent::MouseButtonPress, QPointF(185, 155), Qt::LeftButton);
        send(QEvent::MouseMove, QPointF(190, 155), Qt::LeftButton);
        CHECK(!tools->current()->hidesSelectionOutline());
        send(QEvent::MouseButtonRelease, QPointF(165, 150), Qt::NoButton);
        settle();
        state->setPreferences(prefs);

        // Cortar solo con algo seleccionado; al copiar, también va un PNG con la transparencia.
        tools->select(ToolId::RectSelect);
        QAction* cutAct = findAction(&w, QStringLiteral("Cortar"));
        CHECK(cutAct && cutAct->isEnabled());
        findAction(&w, QStringLiteral("Copiar"))->trigger();
        const QMimeData* copied = QApplication::clipboard()->mimeData();
        CHECK(copied && copied->hasImage() && copied->hasFormat(QStringLiteral("image/png")));
        if (copied)
            CHECK(QImage::fromData(copied->data(QStringLiteral("image/png")), "PNG").hasAlphaChannel());
        findAction(&w, QStringLiteral("Anular la selección"))->trigger();
        settle();
        CHECK(cutAct && !cutAct->isEnabled());

        // Cerrar varias imágenes sin guardar: una sola pregunta con sus miniaturas, y "No guardar"
        // las descarta todas a la vez.
        w.newDefaultDocument();
        settle();
        tools->select(ToolId::Pencil);
        drag(canvas, {QPointF(20, 20), QPointF(80, 60)});
        CHECK(d->isModified() && canvas->document()->isModified());
        const int before = strip ? strip->count() : 0;
        // El punto de "sin guardar" va en la esquina de la pestaña (no en la miniatura).
        CHECK(strip && strip->tabData(strip->currentIndex()).toBool());
        int thumbnails = -1;
        QTimer::singleShot(300, [&thumbnails, &outDir] {
            auto* ask = qobject_cast<TaskDialog*>(QApplication::activeModalWidget());
            CHECK(ask);
            if (!ask)
                return;
            if (auto* list = ask->findChild<QListWidget*>())
                thumbnails = list->count();
            if (!outDir.isEmpty())
                ask->grab().save(QDir(outDir).filePath(QStringLiteral("cambios-varias.png")));
            if (auto* b = ask->findChild<QAbstractButton*>(QStringLiteral("choice1")))
                b->click();
        });
        if (strip)
            strip->setCurrentIndex(first);
        settle();
        findAction(&w, QStringLiteral("Cerrar todas menos esta"))->trigger();
        settle();
        CHECK(thumbnails == 2);
        CHECK(strip && strip->count() == before - 2 && canvas->document() == doc);
    }

    // El campo hexadecimal de Colores no vuelve a poner su texto al perder el foco si no se
    // escribió en él (antes, tras copiar el código, un clic en el lienzo devolvía el color viejo).
    {
        auto* colors = w.findChild<ColorsPanel*>();
        QLineEdit* hex = nullptr;
        const QString code = state->primary().name().mid(1).toUpper();
        for (QLineEdit* e : colors ? colors->findChildren<QLineEdit*>() : QList<QLineEdit*>())
            if (e->text() == code)
                hex = e;
        CHECK(hex);
        if (hex) {
            const QColor keep = state->primary();
            hex->setFocus();
            settle();
            state->setPrimary(QColor(10, 200, 30));
            CHECK(hex->text() == QStringLiteral("0AC81E"));
            canvas->setFocus();
            settle();
            CHECK(state->primary() == QColor(10, 200, 30));
            state->setPrimary(keep);
        }
    }

    // Deslizadores como en Paint.NET: un clic en la barra lejos del tirador avanza un paso (una
    // décima parte) hacia el puntero y varios clics llegan hasta él; manteniendo pulsado sigue a
    // pasos hasta llegar; cerca del tirador va directo al puntero; arrastrando, lo sigue. Mientras
    // se pulsa cambia de color.
    {
        QSlider slider(Qt::Horizontal);
        slider.setRange(0, 100);
        slider.setValue(50);
        slider.resize(220, 24);
        slider.show();
        settle();
        auto send = [&slider](QEvent::Type type, int x, Qt::MouseButtons buttons) {
            const QPointF pos(x, slider.height() / 2);
            QMouseEvent e(type, pos, slider.mapToGlobal(pos), type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton, buttons,
                          Qt::NoModifier);
            QApplication::sendEvent(&slider, &e);
        };
        auto click = [&send](int x) {
            send(QEvent::MouseButtonPress, x, Qt::LeftButton);
            send(QEvent::MouseButtonRelease, x, Qt::NoButton);
        };
        auto wait = [](int ms) {
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < ms)
                QApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        send(QEvent::MouseButtonPress, 205, Qt::LeftButton);
        CHECK(slider.property("kaintPressed").toBool() && slider.value() == 60);
        send(QEvent::MouseButtonRelease, 205, Qt::NoButton);
        CHECK(!slider.property("kaintPressed").toBool());
        wait(700);
        CHECK(slider.value() == 60);
        for (int i = 0; i < 6; ++i)
            click(205);
        const int end = slider.value();
        CHECK(end > 90);
        click(205);
        CHECK(slider.value() == end);
        // Manteniendo pulsado, a pasos hasta el puntero.
        slider.setValue(0);
        send(QEvent::MouseButtonPress, 205, Qt::LeftButton);
        CHECK(slider.value() == 10);
        wait(1300);
        CHECK(slider.value() == end);
        send(QEvent::MouseButtonRelease, 205, Qt::NoButton);
        // Cerca del tirador, directo al puntero.
        slider.setValue(50);
        click(125);
        CHECK(slider.value() > 50 && slider.value() < 60);
        // Pulsado y moviendo, sigue al puntero.
        slider.setValue(50);
        send(QEvent::MouseButtonPress, 30, Qt::LeftButton);
        send(QEvent::MouseMove, 35, Qt::LeftButton);
        CHECK(slider.isSliderDown() && slider.value() < 20);
        send(QEvent::MouseMove, 110, Qt::LeftButton);
        CHECK(std::abs(slider.value() - 50) <= 3);
        send(QEvent::MouseButtonRelease, 110, Qt::NoButton);
        CHECK(!slider.isSliderDown() && !slider.property("kaintPressed").toBool() && std::abs(slider.value() - 50) <= 3);
    }

    // Las listas de tamaños (pincel, letra) se ven enteras.
    for (QComboBox* c : w.findChildren<QComboBox*>())
        if (c->isEditable() && c->findText(QStringLiteral("288")) >= 0)
            CHECK(c->maxVisibleItems() >= c->count());


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

    // Cubo de pintura: el icono de mover arrastra el punto de origen (el relleno lo sigue) y cada
    // arrastre es un paso "Editar cubo de pintura".
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
        CHECK(tools->current()->isBusy() && d->undoStack()->count() == steps + 1);
        CHECK(d->undoStack()->text(steps) == QStringLiteral("Editar cubo de pintura"));
        CHECK(QColor(d->activeLayer()->image.pixel(100, 300)) == QColor(Qt::white));
        CHECK(QColor(d->activeLayer()->image.pixel(700, 300)) == QColor(220, 0, 0));
        tools->current()->finish();
        CHECK(d->undoStack()->count() == steps + 2);

        // Al volver a un relleno en el historial se retoma: el punto de origen vuelve, sus colores
        // pasan a ser los actuales y cambiar el color lo vuelve a rellenar.
        state->setPrimary(QColor(0, 160, 0));
        drag(canvas, {QPointF(100, 100)});
        CHECK(QColor(d->activeLayer()->image.pixel(100, 300)) == QColor(0, 160, 0));
        QAction* undoAct = findAction(&w, QStringLiteral("Deshacer"));
        undoAct->trigger();
        CHECK(!tools->current()->isBusy());
        undoAct->trigger();
        CHECK(tools->current()->isBusy() && state->primary() == QColor(220, 0, 0));
        CHECK(QColor(d->activeLayer()->image.pixel(700, 300)) == QColor(220, 0, 0));
        state->setPrimary(QColor(250, 200, 0));
        CHECK(QColor(d->activeLayer()->image.pixel(700, 300)) == QColor(250, 200, 0));
        const int resumed = d->undoStack()->index();
        tools->current()->finish();
        CHECK(d->undoStack()->count() == resumed + 2);
        CHECK(d->undoStack()->text(resumed) == QStringLiteral("Editar cubo de pintura"));

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
        CHECK(QColor(d->activeLayer()->image.pixel(700, 400)) == QColor(250, 200, 0));
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

        // Efectos: "Repetir" arriba del todo, solo después de usar un efecto; "Complementos..."
        // solo con algún complemento activado. Ayuda con los enlaces de Kaint y los de paint.js.
        QAction* pluginsAction = findAction(&w, QStringLiteral("Complementos..."));
        bool anyPlugin = false;
        for (const Effect* e : Effects::all())
            anyPlugin = anyPlugin || !e->pluginName().isEmpty();
        CHECK(pluginsAction && pluginsAction->isVisible() == anyPlugin);
        findAction(&w, QStringLiteral("Invertir colores"))->trigger();
        settle();
        QAction* repeat = findAction(&w, QStringLiteral("Repetir «Invertir colores»"));
        CHECK(repeat && repeat->isEnabled() && repeat->isVisible());
        QAction* effectsMenu = findAction(&w, QStringLiteral("Efectos"));
        CHECK(effectsMenu && effectsMenu->menu() && effectsMenu->menu()->actions().value(0) == repeat);
        QStringList help;
        for (QToolButton* b : w.findChildren<QToolButton*>())
            if (b->menu() && b->toolTip() == QStringLiteral("Ayuda"))
                for (QAction* a : b->menu()->actions())
                    help << a->text().remove(QLatin1Char('&'));
        CHECK(help.contains(QStringLiteral("Sitio web de Kaint")) && help.contains(QStringLiteral("Donar"))
              && help.contains(QStringLiteral("Enviar comentarios o informe de errores...")) && help.contains(QStringLiteral("Acerca de")));
        CHECK(help.value(0) == QStringLiteral("Documentación") && help.contains(QStringLiteral("Foro"))
              && help.contains(QStringLiteral("Tutoriales")));

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

    // Historial y capas: deshacer selecciona la capa que cambió ese paso; con Mover píxeles, las
    // capas vinculadas (Ctrl+clic en Capas) se mueven a la vez en un solo paso y luego se vuelve a
    // la capa del principio.
    {
        w.newDefaultDocument();
        settle();
        Document* d = canvas->document();
        findAction(&w, QStringLiteral("Agregar nueva capa"))->trigger();
        settle();
        CHECK(d->layerCount() == 2 && d->activeIndex() == 1);
        state->setPrimary(QColor(200, 0, 0));
        tools->select(ToolId::Paintbrush);
        drag(canvas, {QPointF(100, 100), QPointF(160, 100)});
        d->setActiveIndex(0);
        findAction(&w, QStringLiteral("Deshacer"))->trigger();
        settle();
        CHECK(d->activeIndex() == 1);
        findAction(&w, QStringLiteral("Rehacer"))->trigger();
        settle();

        // Algo en las dos capas, mismo sitio.
        d->setActiveIndex(0);
        state->setPrimary(QColor(0, 0, 200));
        drag(canvas, {QPointF(100, 140), QPointF(160, 140)});
        d->setActiveIndex(1);
        tools->select(ToolId::RectSelect);
        drag(canvas, {QPointF(80, 80), QPointF(120, 120), QPointF(180, 160)});
        tools->select(ToolId::MoveSelected);
        auto* layersPanel = w.findChild<LayersPanel*>();
        auto* list = layersPanel ? layersPanel->findChild<QListWidget*>() : nullptr;
        CHECK(list);
        if (list) {
            // Ctrl+clic en la fila del fondo (la de abajo).
            const QPoint at = list->visualItemRect(list->item(1)).center();
            QMouseEvent press(QEvent::MouseButtonPress, QPointF(at), QPointF(list->viewport()->mapToGlobal(at)), Qt::LeftButton,
                              Qt::LeftButton, Qt::ControlModifier);
            QApplication::sendEvent(list->viewport(), &press);
            QMouseEvent release(QEvent::MouseButtonRelease, QPointF(at), QPointF(list->viewport()->mapToGlobal(at)),
                                Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
            QApplication::sendEvent(list->viewport(), &release);
        }
        CHECK(d->linkedLayers().size() == 1 && d->activeIndex() == 1);
        const int steps = d->undoStack()->count();
        drag(canvas, {QPointF(110, 100), QPointF(130, 130), QPointF(150, 160)});
        CHECK(d->undoStack()->count() == steps + 1 && d->activeIndex() == 1 && d->linkedLayers().isEmpty());
        CHECK(QColor(d->layer(1)->image.pixel(140, 160)) == QColor(200, 0, 0));
        CHECK(QColor(d->layer(0)->image.pixel(140, 200)) == QColor(0, 0, 200));
        findAction(&w, QStringLiteral("Deshacer"))->trigger();
        settle();
        CHECK(QColor(d->layer(0)->image.pixel(140, 140)) == QColor(0, 0, 200) && qAlpha(d->layer(1)->image.pixel(140, 160)) == 0);

        // Arrastrar una capa en la lista muestra la línea de dónde quedará.
        if (list) {
            const QPoint from = list->visualItemRect(list->item(0)).center();
            const QPoint to = list->visualItemRect(list->item(1)).center();
            auto send = [list](QEvent::Type type, QPoint at, Qt::MouseButtons buttons) {
                QMouseEvent e(type, QPointF(at), QPointF(list->viewport()->mapToGlobal(at)),
                              type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton, buttons, Qt::NoModifier);
                QApplication::sendEvent(list->viewport(), &e);
            };
            send(QEvent::MouseButtonPress, from, Qt::LeftButton);
            send(QEvent::MouseMove, from + QPoint(0, 12), Qt::LeftButton);
            send(QEvent::MouseMove, to, Qt::LeftButton);
            bool lineShown = false;
            for (QWidget* child : list->viewport()->findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
                lineShown = lineShown || (child->isVisible() && child->height() <= 4 && child->width() > 50);
            CHECK(lineShown);
            send(QEvent::MouseButtonRelease, to, Qt::NoButton);
        }
    }

    // Estirar con Mover píxeles una línea de 1 px subiendo y bajando sin soltar no deja restos
    // de los pasos intermedios (con bilineal y bicúbica se pinta un poco fuera del recuadro).
    for (auto interp : {Resample::Interpolation::Bilinear, Resample::Interpolation::Bicubic}) {
        w.newDefaultDocument();
        settle();
        Document* d = canvas->document();
        state->updateTools([interp](ToolSettings& t) { t.moveInterpolation = interp; });
        state->setPrimary(QColor(0, 40, 255));
        tools->select(ToolId::Pencil);
        drag(canvas, {QPointF(100.5, 100.5), QPointF(200.5, 100.5), QPointF(300.5, 100.5)});
        tools->select(ToolId::RectSelect);
        drag(canvas, {QPointF(100, 100), QPointF(200, 100.6), QPointF(301, 101)});
        tools->select(ToolId::MoveSelected);
        QList<QPointF> path{QPointF(301, 100.5)};
        for (int i = 1; i <= 40; ++i)
            path << QPointF(301 + i * 5, 100.5 + 60 * std::sin(i / 4.0));
        drag(canvas, path);
        QList<int> rows;
        const QImage& im = d->layer(0)->image;
        for (int y = 0; y < 300; ++y) {
            bool blue = false;
            for (int x = 90; x < 600 && !blue; ++x)
                blue = qBlue(im.pixel(x, y)) > qRed(im.pixel(x, y)) + 30;
            if (blue)
                rows << y;
        }
        CHECK(!rows.isEmpty() && rows.last() - rows.first() + 1 == rows.size());
        state->updateTools([](ToolSettings& t) { t.moveInterpolation = Resample::Interpolation::NearestNeighbor; });
    }

    // Tamaño de letra y demás listas de tamaños: se comportan como un botón; solo con doble clic
    // se puede escribir, y un clic fuera las vuelve a dejar como botón.
    {
        tools->select(ToolId::Text);
        settle();
        QComboBox* fontSize = nullptr;
        for (QComboBox* c : w.findChildren<QComboBox*>())
            if (c->isEditable() && c->findText(QStringLiteral("288")) >= 0)
                fontSize = c;
        CHECK(fontSize && fontSize->lineEdit()->isReadOnly());
        if (fontSize) {
            QLineEdit* edit = fontSize->lineEdit();
            const QPoint at = edit->rect().center();
            QMouseEvent dbl(QEvent::MouseButtonDblClick, QPointF(at), QPointF(edit->mapToGlobal(at)), Qt::LeftButton,
                            Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(edit, &dbl);
            CHECK(!edit->isReadOnly());
            QWidget* outside = w.statusBar();
            const QPoint out = outside->rect().center();
            QMouseEvent press(QEvent::MouseButtonPress, QPointF(out), QPointF(outside->mapToGlobal(out)), Qt::LeftButton,
                              Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(outside, &press);
            CHECK(edit->isReadOnly());
        }
        tools->select(ToolId::Paintbrush);
        settle();
        int sizeLists = 0;
        for (QComboBox* c : w.findChild<ToolOptionsBar*>()->findChildren<QComboBox*>())
            if (c->isEditable() && c->isVisible() && !qobject_cast<QFontComboBox*>(c)) {
                ++sizeLists;
                CHECK(c->lineEdit()->isReadOnly());
            }
        CHECK(sizeLists > 0);
    }

    // Mover píxeles: cambiar el muestreo se ve al momento en lo que se está moviendo.
    {
        w.newDefaultDocument();
        settle();
        Document* d = canvas->document();
        state->setPrimary(Qt::black);
        tools->select(ToolId::Pencil);
        for (int x = 100; x < 140; x += 3)
            drag(canvas, {QPointF(x + 0.5, 100.5), QPointF(x + 0.5, 139.5)});
        tools->select(ToolId::RectSelect);
        drag(canvas, {QPointF(100, 100), QPointF(120, 120), QPointF(140, 140)});
        tools->select(ToolId::MoveSelected);
        drag(canvas, {QPointF(140, 140), QPointF(170, 160), QPointF(197, 171)});
        const QImage nearest = d->layer(0)->image;
        state->updateTools([](ToolSettings& t) { t.moveInterpolation = Resample::Interpolation::Bilinear; });
        CHECK(d->layer(0)->image != nearest);
        state->updateTools([](ToolSettings& t) { t.moveInterpolation = Resample::Interpolation::NearestNeighbor; });
        CHECK(d->layer(0)->image == nearest);
        tools->current()->finish();
        findAction(&w, QStringLiteral("Anular la selección"))->trigger();
    }

    // Varita mágica: píxel a píxel por defecto; con calidad antidentado, contorno suave como el
    // lazo. El cubo tiene la opción de acumular (activada).
    {
        w.newDefaultDocument();
        settle();
        Document* d = canvas->document();
        state->setPrimary(Qt::black);
        state->updateTools([](ToolSettings& t) { t.brushSize = 30; t.shape = ShapeType::Ellipse; t.shapeStyle = ShapeStyle::Fill; });
        tools->select(ToolId::Shapes);
        drag(canvas, {QPointF(100, 100), QPointF(150, 140), QPointF(200, 180)});
        tools->current()->finish();
        tools->select(ToolId::MagicWand);
        drag(canvas, {QPointF(150, 140)});
        tools->current()->finish();
        CHECK(!d->selection().isEmpty() && !d->selection().isPathBased());
        state->updateTools([](ToolSettings& t) { t.selectionAntialias = true; });
        drag(canvas, {QPointF(150, 140)});
        tools->current()->finish();
        CHECK(!d->selection().isEmpty() && d->selection().isPathBased());
        state->updateTools([](ToolSettings& t) { t.selectionAntialias = false; });
        findAction(&w, QStringLiteral("Anular la selección"))->trigger();
        CHECK(state->tools().fillAccumulate);
        CHECK(tools->tool(ToolId::PaintBucket)->options() & Tool::OptFillAccumulate);
        CHECK(ToolOptionsBar::order(ToolId::PaintBucket).contains(QStringLiteral("accumulate")));
    }

    // Imagen nueva: las unidades van fuera de los campos (como en Paint.NET) y la unidad de la
    // impresión va a la par de la de la resolución.
    {
        NewImageDialog dlg(QSize(300, 200), 96, &w);
        for (QSpinBox* sb : dlg.findChildren<QSpinBox*>())
            CHECK(sb->suffix().isEmpty());
        for (QDoubleSpinBox* sb : dlg.findChildren<QDoubleSpinBox*>())
            CHECK(sb->suffix().isEmpty());
        QComboBox* resolution = nullptr;
        QComboBox* print = nullptr;
        for (QComboBox* c : dlg.findChildren<QComboBox*>()) {
            if (c->findText(QStringLiteral("píxeles/cm")) >= 0)
                resolution = c;
            if (c->findText(QStringLiteral("centímetros")) >= 0)
                print = c;
        }
        CHECK(resolution && print);
        if (resolution && print) {
            CHECK(resolution->currentIndex() == print->currentIndex());
            const int other = 1 - print->currentIndex();
            print->setCurrentIndex(other);
            emit print->activated(other);
            CHECK(resolution->currentIndex() == other);
            CHECK(dlg.imageSize() == QSize(300, 200) && std::abs(dlg.dpi() - 96) < 0.01);
            print->setCurrentIndex(1);
            emit print->activated(1);
        }
    }

    // Cada inicio empieza como Paint.NET: negro y blanco y las opciones predeterminadas
    // (Configuración > Herramientas). Las opciones guardadas por versiones anteriores (lo último
    // usado, sin versión) se descartan; las predeterminadas que se eligen se conservan.
    {
        state->setPrimary(QColor(10, 200, 30));
        state->setSecondary(QColor(200, 10, 30));
        state->save();
        QSettings settings;
        settings.setValue(QStringLiteral("colors/primary"), QColor(1, 2, 3));
        settings.remove(QStringLiteral("tools"));
        settings.setValue(QStringLiteral("tools/tolerance"), 64);
        settings.sync();
        state->load();
        CHECK(state->primary() == QColor(Qt::black) && state->secondary() == QColor(Qt::white));
        CHECK(state->tools().tolerance == 50 && !QSettings().contains(QStringLiteral("colors/primary")));
        ToolSettings defaults;
        defaults.tolerance = 70;
        defaults.hardness = 40;
        state->setToolDefaults(defaults);
        state->updateTools([](ToolSettings& t) { t.tolerance = 12; });
        state->save();
        state->load();
        CHECK(state->tools().tolerance == 70 && state->tools().hardness == 40);
        state->setToolDefaults(ToolSettings());
        state->load();
    }

    // Ranuras de color como en Paint.NET: el secundario inactivo solo lleva un marco negro fino;
    // el principal inactivo conserva un marco blanco más fino.
    if (auto* swatches = w.findChild<ColorSwatches*>()) {
        state->setPrimary(Qt::red);
        state->setSecondary(Qt::blue);
        settle();
        auto band = [swatches](bool secondary, int inset = 2) {
            const QImage img = swatches->grab().toImage();
            const QRectF r = secondary ? swatches->secondaryRect() : swatches->primaryRect();
            return QColor(img.pixel(int(secondary ? r.right() - inset : r.left() + inset), int(r.center().y())));
        };
        CHECK(band(false) == QColor(Qt::white) && band(true) == QColor(Qt::blue));
        // El cambio se anima en las dos a la vez (con las animaciones activadas).
        auto waitSwitch = [] {
            QElapsedTimer clock;
            clock.start();
            while (clock.elapsed() < Animations::kIndicator + 100)
                QApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        swatches->setEditingSecondary(true);
        bool switching = false;
        for (QVariantAnimation* a : swatches->findChildren<QVariantAnimation*>())
            switching = switching || a->state() == QAbstractAnimation::Running;
        CHECK(switching);
        waitSwitch();
        CHECK(band(false, 1) == QColor(Qt::white) && band(false, 3) == QColor(Qt::red) && band(true) == QColor(Qt::white));
        swatches->setEditingSecondary(false);
        waitSwitch();
        CHECK(band(false) == QColor(Qt::white) && band(true) == QColor(Qt::blue));
        state->resetColors();
    }

    // Breeze (su ToolsAreaManager) pide QMainWindow::menuBar() al pulir la ventana: los menús y
    // la barra principal no se deben perder.
    {
        struct BreezeLike : QObject {
            bool eventFilter(QObject* watched, QEvent* event) override
            {
                if (event->type() == QEvent::Polish)
                    if (auto* window = qobject_cast<QMainWindow*>(watched))
                        window->menuBar();
                return false;
            }
        } breeze;
        qApp->installEventFilter(&breeze);
        MainWindow second;
        second.show();
        settle();
        qApp->removeEventFilter(&breeze);
        CHECK(second.menuWidget() && second.menuWidget()->objectName() == QLatin1String("TopRow"));
        auto* mainBar = second.findChild<QToolBar*>(QStringLiteral("MainToolBar"));
        CHECK(mainBar && mainBar->isVisible() && mainBar->height() > 0);
    }

    std::printf("Historial: %d pasos · capas: %d\n", int(doc->undoStack()->count()), doc->layerCount());
    if (g_failures == 0)
        std::printf("La prueba de humo de la interfaz pasó.\n");
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}
