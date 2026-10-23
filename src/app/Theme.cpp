#include "app/Theme.h"

#include "app/Icons.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFontDatabase>
#include <QIcon>
#include <QJsonDocument>
#include <QPainter>
#include <QPixmap>
#include <QSettings>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QTimer>
#ifdef KAINT_HAVE_DBUS
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#endif

#include <algorithm>

namespace Theme {

const std::array<ColorRole, 18> kColorRoles{{
    {"accent", &Spec::accent, QT_TRANSLATE_NOOP("Theme", "Acento")},
    {"window", &Spec::window, QT_TRANSLATE_NOOP("Theme", "Ventana y diálogos")},
    {"text", &Spec::text, QT_TRANSLATE_NOOP("Theme", "Texto")},
    {"base", &Spec::base, QT_TRANSLATE_NOOP("Theme", "Campos y listas")},
    {"button", &Spec::button, QT_TRANSLATE_NOOP("Theme", "Botones")},
    {"border", &Spec::border, QT_TRANSLATE_NOOP("Theme", "Bordes")},
    {"menuBar", &Spec::menuBar, QT_TRANSLATE_NOOP("Theme", "Fila de menús")},
    {"menuBarText", &Spec::menuBarText, QT_TRANSLATE_NOOP("Theme", "Texto de los menús")},
    {"toolBar", &Spec::toolBar, QT_TRANSLATE_NOOP("Theme", "Barras de herramientas")},
    {"statusBar", &Spec::statusBar, QT_TRANSLATE_NOOP("Theme", "Barra de estado")},
    {"panel", &Spec::panel, QT_TRANSLATE_NOOP("Theme", "Ventanas flotantes")},
    {"panelTitle", &Spec::panelTitle, QT_TRANSLATE_NOOP("Theme", "Cabecera de las ventanas")},
    {"panelTitleText", &Spec::panelTitleText, QT_TRANSLATE_NOOP("Theme", "Título de las ventanas")},
    {"panelClose", &Spec::panelClose, QT_TRANSLATE_NOOP("Theme", "Botón de cerrar")},
    {"selection", &Spec::selection, QT_TRANSLATE_NOOP("Theme", "Selección")},
    {"selectionBorder", &Spec::selectionBorder, QT_TRANSLATE_NOOP("Theme", "Borde de la selección")},
    {"hover", &Spec::hover, QT_TRANSLATE_NOOP("Theme", "Bajo el puntero")},
    {"workspace", &Spec::workspace, QT_TRANSLATE_NOOP("Theme", "Fondo del lienzo")},
}};

namespace {

QString g_systemIconTheme;
QString g_configStamp;
Scheme g_current = Scheme::System;
Scheme g_start = Scheme::System;
Spec g_custom;
bool g_customLoaded = false;
Spec g_active = preset(false);
QFont g_baseFont;
bool g_baseFontSet = false;
bool g_fusion = false;
int g_systemDark = -1;
QFileSystemWatcher* g_watcher = nullptr;

// Imagen de fondo cargada y su copia escalada (se crea con la aplicación ya en marcha).
struct ImageCache {
    QString path;
    QPixmap image;
    QPixmap scaled;
    QSize scaledSize;
};

ImageCache& imageCache()
{
    static ImageCache cache;
    return cache;
}

QColor rgb(QRgb value) { return QColor::fromRgb(value); }

QColor mix(const QColor& a, const QColor& b, qreal t)
{
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * t), float(a.greenF() + (b.greenF() - a.greenF()) * t),
                            float(a.blueF() + (b.blueF() - a.blueF()) * t), float(a.alphaF() + (b.alphaF() - a.alphaF()) * t));
}

QColor contrast(const QColor& c)
{
    return 0.299 * c.red() + 0.587 * c.green() + 0.114 * c.blue() > 150 ? QColor(Qt::black) : QColor(Qt::white);
}

QString css(const QColor& c)
{
    if (c.alpha() == 255)
        return c.name();
    return QStringLiteral("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
}

QString kdeglobals()
{
    return QStandardPaths::locate(QStandardPaths::GenericConfigLocation, QStringLiteral("kdeglobals"));
}

bool onKde()
{
    return qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains(QLatin1String("KDE"), Qt::CaseInsensitive);
}

// Modo del escritorio: 1 oscuro, 0 claro, -1 desconocido.
int kdeScheme()
{
    const QString file = kdeglobals();
    if (file.isEmpty())
        return -1;
    const QSettings kde(file, QSettings::IniFormat);
    const QStringList rgbValues = kde.value(QStringLiteral("Colors:Window/BackgroundNormal")).toStringList();
    if (rgbValues.size() >= 3) {
        const QColor window(rgbValues[0].toInt(), rgbValues[1].toInt(), rgbValues[2].toInt());
        return window.lightness() < 128 ? 1 : 0;
    }
    const QString name = kde.value(QStringLiteral("ColorScheme")).toString();
    if (!name.isEmpty())
        return name.contains(QLatin1String("dark"), Qt::CaseInsensitive) ? 1 : 0;
    return -1;
}

int portalScheme()
{
#ifdef KAINT_HAVE_DBUS
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return -1;
    QDBusMessage call = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.portal.Desktop"), QStringLiteral("/org/freedesktop/portal/desktop"),
        QStringLiteral("org.freedesktop.portal.Settings"), QStringLiteral("ReadOne"));
    call << QStringLiteral("org.freedesktop.appearance") << QStringLiteral("color-scheme");
    const QDBusMessage reply = bus.call(call, QDBus::Block, 400);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return -1;
    QVariant value = reply.arguments().constFirst();
    while (value.metaType() == QMetaType::fromType<QDBusVariant>())
        value = value.value<QDBusVariant>().variant();
    const uint scheme = value.toUInt();
    return scheme == 1 ? 1 : scheme == 2 ? 0 : -1;
#else
    return -1;
#endif
}

int gtkScheme()
{
    const QString file = QStandardPaths::locate(QStandardPaths::GenericConfigLocation, QStringLiteral("gtk-3.0/settings.ini"));
    if (file.isEmpty())
        return -1;
    const QSettings gtk(file, QSettings::IniFormat);
    if (gtk.value(QStringLiteral("Settings/gtk-application-prefer-dark-theme")).toString() == QLatin1String("1"))
        return 1;
    const QString name = gtk.value(QStringLiteral("Settings/gtk-theme-name")).toString();
    if (!name.isEmpty())
        return name.contains(QLatin1String("dark"), Qt::CaseInsensitive) ? 1 : 0;
    return -1;
}

// Windows: "Elegir el modo de aplicación" (AppsUseLightTheme en el registro).
int windowsScheme()
{
#ifdef Q_OS_WIN
    const QSettings personalize(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\"
                                               "Themes\\Personalize"),
                                QSettings::NativeFormat);
    const QVariant light = personalize.value(QStringLiteral("AppsUseLightTheme"));
    if (light.isValid())
        return light.toInt() == 0 ? 1 : 0;
#endif
    return -1;
}

int readSystemScheme()
{
    // En Windows manda el registro; en Plasma, kdeglobals (cambia al aplicar el esquema); si no,
    // el portal de freedesktop y la configuración de GTK.
    int scheme = windowsScheme();
    if (scheme >= 0)
        return scheme;
    scheme = onKde() ? kdeScheme() : -1;
    if (scheme < 0)
        scheme = portalScheme();
    if (scheme < 0)
        scheme = kdeScheme();
    if (scheme < 0)
        scheme = gtkScheme();
    return scheme;
}

QString configStamp()
{
    QString stamp;
    for (const QString& file : {kdeglobals(), QStandardPaths::locate(QStandardPaths::GenericConfigLocation,
                                                                    QStringLiteral("gtk-3.0/settings.ini"))}) {
        const QFileInfo info(file);
        if (!file.isEmpty() && info.exists())
            stamp += QStringLiteral("%1:%2:%3;").arg(file).arg(info.lastModified().toMSecsSinceEpoch()).arg(info.size());
    }
    return stamp;
}

void watchSystem()
{
    if (g_watcher || !QCoreApplication::instance())
        return;
    g_watcher = new QFileSystemWatcher(notifier());
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    g_watcher->addPath(config);
    if (!kdeglobals().isEmpty())
        g_watcher->addPath(kdeglobals());
    // Plasma reescribe kdeglobals con otro archivo: se vuelve a vigilar tras cada cambio. La
    // carpeta cambia también por otros programas; solo cuenta si cambió kdeglobals o GTK.
    g_configStamp = configStamp();
    auto changed = [] {
        if (!kdeglobals().isEmpty() && !g_watcher->files().contains(kdeglobals()))
            g_watcher->addPath(kdeglobals());
        QTimer::singleShot(150, notifier(), [] {
            const QString stamp = configStamp();
            if (stamp == g_configStamp)
                return;
            g_configStamp = stamp;
            notifier()->systemSettingChanged();
        });
    };
    QObject::connect(g_watcher, &QFileSystemWatcher::fileChanged, notifier(), changed);
    QObject::connect(g_watcher, &QFileSystemWatcher::directoryChanged, notifier(), changed);
    // Windows (y cualquier plataforma que lo avise): el cambio de modo claro u oscuro.
    QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, notifier(),
                     &Notifier::systemSettingChanged);
#ifdef KAINT_HAVE_DBUS
    QDBusConnection::sessionBus().connect(QStringLiteral("org.freedesktop.portal.Desktop"),
                                          QStringLiteral("/org/freedesktop/portal/desktop"),
                                          QStringLiteral("org.freedesktop.portal.Settings"),
                                          QStringLiteral("SettingChanged"), notifier(), SLOT(systemSettingChanged()));
#endif
}

// Fuente del escritorio: sin su configuración, Qt usaría la suya.
QFont desktopFont()
{
    const QString file = kdeglobals();
    if (!file.isEmpty()) {
        const QSettings kde(file, QSettings::IniFormat);
        const QString description = kde.value(QStringLiteral("font")).toStringList().join(QLatin1Char(','));
        QFont font;
        if (!description.isEmpty() && font.fromString(description))
            return font;
    }
    return QFontDatabase::systemFont(QFontDatabase::GeneralFont);
}

// Tema de KDE descrito con la paleta del sistema (para lo que Kaint pinta por su cuenta).
Spec fromPalette(const QPalette& p)
{
    Spec s = preset(p.color(QPalette::Window).lightness() < 128);
    s.accent = p.color(QPalette::Highlight);
    s.window = s.menuBar = s.toolBar = s.statusBar = s.panel = p.color(QPalette::Window);
    s.text = s.menuBarText = p.color(QPalette::WindowText);
    s.base = p.color(QPalette::Base);
    s.button = p.color(QPalette::Button);
    s.border = p.color(QPalette::Mid);
    s.panelTitle = p.color(QPalette::Highlight);
    s.panelTitleText = p.color(QPalette::HighlightedText);
    s.selection = s.selectionBorder = p.color(QPalette::Highlight);
    s.hover = p.color(QPalette::Highlight).lighter(130);
    QColor window = p.color(QPalette::Window);
    window.setAlpha(255);
    s.workspace = s.dark ? window.darker(135) : window.darker(112);
    return s;
}

// Marcas de casillas y botones de opción y flechas de los desplegables para la hoja de estilo:
// SVG en la caché, uno por color.
QString indicatorImage(const char* kind, const QColor& color)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/theme-1");
    const QString file = QStringLiteral("%1/%2-%3.svg").arg(dir, QLatin1String(kind), color.name().mid(1));
    if (QFile::exists(file))
        return file;
    const QString line = QStringLiteral("<path d='%2' fill='none' stroke='%1' stroke-width='%3' stroke-linecap='round' "
                                        "stroke-linejoin='round'/>");
    QString shape;
    if (qstrcmp(kind, "check") == 0)
        shape = line.arg(QStringLiteral("%1"), QStringLiteral("M3.5 8.5l3 3 6-7"), QStringLiteral("2"));
    else if (qstrcmp(kind, "partial") == 0)
        shape = QStringLiteral("<rect x='4' y='7' width='8' height='2' fill='%1'/>");
    else if (qstrcmp(kind, "dot") == 0)
        shape = QStringLiteral("<circle cx='8' cy='8' r='3.5' fill='%1'/>");
    else if (qstrcmp(kind, "up") == 0)
        shape = line.arg(QStringLiteral("%1"), QStringLiteral("M3.5 10.5l4.5-4.5 4.5 4.5"), QStringLiteral("1.6"));
    else
        shape = line.arg(QStringLiteral("%1"), QStringLiteral("M3.5 5.5l4.5 4.5 4.5-4.5"), QStringLiteral("1.6"));
    QDir().mkpath(dir);
    QFile f(file);
    if (f.open(QIODevice::WriteOnly))
        f.write(QStringLiteral("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'>%1</svg>")
                    .arg(shape.arg(color.name()))
                    .toUtf8());
    return file;
}

QString iconThemeFor(const Spec& s)
{
    if (!s.iconTheme.isEmpty())
        return s.iconTheme;
    return Icons::breezeTheme(s.dark);
}

} // namespace

// ---------------------------------------------------------------------------
// Temas de Paint.NET 4/5 en Windows 10 (colores tomados de sus capturas)

Spec preset(bool dark)
{
    Spec s;
    s.dark = dark;
    if (dark) {
        s.accent = rgb(0x0078d4);
        s.window = rgb(0x2b2b2b);
        s.text = rgb(0xf0f0f0);
        s.base = rgb(0x1e1e1e);
        s.button = rgb(0x3a3a3a);
        s.border = rgb(0x5f5f5f);
        s.menuBar = rgb(0x000000);
        s.menuBarText = rgb(0xf0f0f0);
        s.toolBar = rgb(0x1f1f1f);
        s.statusBar = rgb(0x202020);
        s.panel = rgb(0x2b2b2b);
        s.panelTitle = rgb(0x2b2b2b);
        s.panelTitleText = rgb(0xd6d6d6);
        s.panelClose = rgb(0xa84646);
        s.selection = rgb(0x1c476d);
        s.selectionBorder = rgb(0x2a6fb0);
        s.hover = rgb(0x3a3a3a);
        s.workspace = rgb(0x303030);
        s.radius = 4;
    } else {
        s.accent = rgb(0x0078d7);
        s.window = rgb(0xf0f0f0);
        s.text = rgb(0x000000);
        s.base = rgb(0xffffff);
        s.button = rgb(0xe1e1e1);
        s.border = rgb(0xadadad);
        s.menuBar = rgb(0xffffff);
        s.menuBarText = rgb(0x000000);
        s.toolBar = rgb(0xffffff);
        s.statusBar = rgb(0xf3f3f3);
        s.panel = rgb(0xf3f3f3);
        s.panelTitle = rgb(0xf3f3f3);
        s.panelTitleText = rgb(0x3c3c3c);
        s.panelClose = rgb(0xc75050);
        s.selection = rgb(0xcce4f7);
        s.selectionBorder = rgb(0x3c7fb1);
        s.hover = rgb(0xe5f3ff);
        s.workspace = rgb(0xcbcbcb);
        s.radius = 0;
    }
    return s;
}

Spec resolved(const Spec& custom)
{
    Spec s = custom;
    const Spec base = preset(custom.dark);
    for (const ColorRole& role : kColorRoles)
        if (!(s.*role.color).isValid())
            s.*role.color = base.*role.color;
    return s;
}

// ---------------------------------------------------------------------------
// Tema personalizado: JSON (se guarda en la configuración y se puede exportar)

QJsonObject toJson(const Spec& spec)
{
    QJsonObject colors;
    for (const ColorRole& role : kColorRoles) {
        const QColor& c = spec.*role.color;
        if (c.isValid())
            colors.insert(QLatin1String(role.key), c.name(c.alpha() == 255 ? QColor::HexRgb : QColor::HexArgb));
    }
    return QJsonObject{
        {QStringLiteral("dark"), spec.dark},
        {QStringLiteral("colors"), colors},
        {QStringLiteral("workspaceImage"), spec.workspaceImage},
        {QStringLiteral("imageMode"), int(spec.imageMode)},
        {QStringLiteral("imageOpacity"), spec.imageOpacity},
        {QStringLiteral("iconSize"), spec.iconSize},
        {QStringLiteral("toolIconSize"), spec.toolIconSize},
        {QStringLiteral("buttonStyle"), int(spec.buttonStyle)},
        {QStringLiteral("density"), int(spec.density)},
        {QStringLiteral("radius"), spec.radius},
        {QStringLiteral("panelOpacity"), spec.panelOpacity},
        {QStringLiteral("fontFamily"), spec.fontFamily},
        {QStringLiteral("fontSize"), spec.fontSize},
        {QStringLiteral("iconTheme"), spec.iconTheme},
    };
}

Spec fromJson(const QJsonObject& json)
{
    Spec s;
    s.dark = json.value(QStringLiteral("dark")).toBool(true);
    const Spec base = preset(s.dark);
    const QJsonObject colors = json.value(QStringLiteral("colors")).toObject();
    for (const ColorRole& role : kColorRoles) {
        const QColor c = QColor::fromString(colors.value(QLatin1String(role.key)).toString());
        s.*role.color = c.isValid() ? c : QColor();
    }
    auto number = [&json](const char* key, int fallback, int lo, int hi) {
        return std::clamp(json.value(QLatin1String(key)).toInt(fallback), lo, hi);
    };
    s.workspaceImage = json.value(QStringLiteral("workspaceImage")).toString();
    s.imageMode = ImageMode(number("imageMode", int(base.imageMode), 0, 4));
    s.imageOpacity = number("imageOpacity", base.imageOpacity, 0, 100);
    s.iconSize = number("iconSize", base.iconSize, 12, 48);
    s.toolIconSize = number("toolIconSize", base.toolIconSize, 12, 48);
    s.buttonStyle = ButtonStyle(number("buttonStyle", int(base.buttonStyle), 0, 2));
    s.density = Density(number("density", int(base.density), 0, 2));
    s.radius = number("radius", base.radius, 0, 16);
    s.panelOpacity = number("panelOpacity", base.panelOpacity, 20, 100);
    s.fontFamily = json.value(QStringLiteral("fontFamily")).toString();
    s.fontSize = number("fontSize", 0, 0, 32);
    s.iconTheme = json.value(QStringLiteral("iconTheme")).toString();
    return s;
}

Spec loadCustom()
{
    QSettings settings;
    const QByteArray data = settings.value(QStringLiteral("settings/customTheme")).toByteArray();
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isObject())
        return fromJson(doc.object());
    Spec s;
    s.dark = systemPrefersDark();
    s.radius = preset(s.dark).radius;
    return s;
}

void saveCustom(const Spec& custom)
{
    QSettings settings;
    settings.setValue(QStringLiteral("settings/customTheme"), QJsonDocument(toJson(custom)).toJson(QJsonDocument::Compact));
}

// ---------------------------------------------------------------------------
// Paleta y hoja de estilo

QPalette palette(const Spec& s)
{
    const QColor disabled = mix(s.text, s.window, 0.55);
    const QColor link = s.dark ? s.accent.lighter(150) : s.accent;
    QPalette p(s.button, s.window);
    p.setColor(QPalette::Window, s.window);
    p.setColor(QPalette::WindowText, s.text);
    p.setColor(QPalette::Base, s.base);
    p.setColor(QPalette::AlternateBase, mix(s.base, s.text, 0.04));
    p.setColor(QPalette::Text, s.text);
    p.setColor(QPalette::Button, s.button);
    p.setColor(QPalette::ButtonText, s.text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Highlight, s.accent);
    p.setColor(QPalette::HighlightedText, contrast(s.accent));
    p.setColor(QPalette::ToolTipBase, s.base);
    p.setColor(QPalette::ToolTipText, s.text);
    p.setColor(QPalette::PlaceholderText, disabled);
    p.setColor(QPalette::Link, link);
    p.setColor(QPalette::LinkVisited, mix(link, QColor(155, 89, 182), 0.6));
    p.setColor(QPalette::Light, s.button.lighter(s.dark ? 140 : 115));
    p.setColor(QPalette::Midlight, s.button.lighter(s.dark ? 120 : 107));
    p.setColor(QPalette::Mid, s.border);
    p.setColor(QPalette::Dark, s.border.darker(s.dark ? 140 : 130));
    p.setColor(QPalette::Shadow, s.dark ? QColor(Qt::black) : rgb(0x767676));
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    p.setColor(QPalette::Accent, s.accent);
#endif
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, disabled);
    p.setColor(QPalette::Disabled, QPalette::Highlight, mix(s.accent, s.window, 0.5));
    p.setColor(QPalette::Inactive, QPalette::Highlight, s.accent);
    return p;
}

QString styleSheet(const Spec& s)
{
    const int d = int(s.density);
    const int r = s.radius;
    const int pad = std::array{1, 2, 4}[d];
    const int menuV = std::array{3, 4, 6}[d];
    const int menuH = std::array{6, 8, 12}[d];
    const int pushV = std::array{2, 3, 5}[d];
    const int pushH = std::array{8, 12, 16}[d];
    const QColor line = mix(s.toolBar, s.text, s.dark ? 0.18 : 0.14);
    const QColor field = s.dark ? s.border.lighter(125) : s.border.darker(125);
    const QColor buttonHover = s.dark ? s.button.lighter(122) : mix(s.button, s.accent, 0.10);
    const QColor buttonPressed = mix(s.button, s.accent, s.dark ? 0.35 : 0.22);
    const QColor hoverBorder = mix(s.hover, s.selectionBorder, 0.45);
    const QColor disabled = mix(s.text, s.window, 0.55);
    const QColor menu = s.dark ? s.window : mix(s.window, s.base, 0.5);
    const QColor track = mix(s.window, s.base, 0.4);
    const QColor thumb = mix(s.window, s.text, s.dark ? 0.32 : 0.25);
    const QColor sliderTrack = mix(s.window, s.text, s.dark ? 0.55 : 0.3);

    // Botones de las barras: planos (Paint.NET), con marco o redondeados.
    const int toolRadius = s.buttonStyle == ButtonStyle::Rounded ? std::max(r, s.iconSize / 2 + pad + 1) : r;
    const QString toolIdle = s.buttonStyle == ButtonStyle::Framed
                                 ? QStringLiteral("background: %1; border: 1px solid %2;").arg(css(s.button), css(s.border))
                                 : QStringLiteral("background: transparent; border: 1px solid transparent;");

    QString qss = QStringLiteral(R"(
QWidget#TopRow { background: %1; }
QMenuBar { background: %2; color: %3; border: 1px solid %4; border-bottom: none;
           border-top-left-radius: %5px; border-top-right-radius: %5px; padding: 1px 2px; }
QMenuBar::item { background: transparent; padding: %6px %7px; border: 1px solid transparent; border-radius: %5px; }
QMenuBar::item:selected { background: %8; border-color: %9; }
QMenuBar::item:pressed { background: %10; border-color: %11; }
)")
                      .arg(css(s.menuBar), css(s.toolBar), css(s.menuBarText), css(line))
                      .arg(r)
                      .arg(menuV - 1)
                      .arg(menuH)
                      .arg(css(s.hover), css(hoverBorder), css(s.selection), css(s.selectionBorder));

    qss += QStringLiteral(R"(
QMenu { background: %1; color: %2; border: 1px solid %3; padding: 3px; }
QMenu::item { padding: %4px 24px %4px %5px; border: 1px solid transparent; border-radius: %6px; }
QMenu::item:selected { background: %7; border-color: %8; color: %2; }
QMenu::item:disabled { color: %9; }
QMenu::separator { height: 1px; background: %10; margin: 3px 6px; }
QToolTip { background: %11; color: %2; border: 1px solid %3; padding: 2px; }
)")
               .arg(css(menu), css(s.text), css(s.border))
               .arg(menuV)
               .arg(menuH)
               .arg(r)
               .arg(css(s.selection), css(s.selectionBorder), css(disabled), css(line), css(s.base));

    qss += QStringLiteral(R"(
QToolBar { background: %1; border: none; spacing: 2px; padding: 1px 4px; }
QToolBar#ToolOptionsBar { border-bottom: 1px solid %2; }
QToolBar::separator { background: %2; width: 1px; height: 1px; margin: 4px 3px; }
QStatusBar { background: %3; color: %4; border-top: 1px solid %2; }
QStatusBar::item { border: none; }
QToolButton { %5 border-radius: %6px; padding: %7px; color: %4; }
QToolButton:hover { background: %8; border-color: %9; }
QToolButton:checked, QToolButton:pressed { background: %10; border-color: %11; }
QToolButton:disabled { color: %12; }
QToolButton[popupMode="1"] { padding-right: 14px; }
QToolButton[popupMode="2"] { padding-right: 10px; }
QToolButton::menu-indicator { subcontrol-origin: padding; subcontrol-position: center right; }
QToolButton#ImageListButton { padding-right: %7px; }
QToolButton#ImageListButton::menu-indicator { image: none; width: 0px; }
)")
               .arg(css(s.toolBar), css(line), css(s.statusBar), css(s.text), toolIdle)
               .arg(toolRadius)
               .arg(pad)
               .arg(css(s.hover), css(hoverBorder), css(s.selection), css(s.selectionBorder), css(disabled));

    qss += QStringLiteral(R"(
QPushButton { background: %1; color: %2; border: 1px solid %3; border-radius: %4px; padding: %5px %6px; }
QPushButton:hover { background: %7; border-color: %8; }
QPushButton:pressed, QPushButton:checked { background: %9; border-color: %8; }
QPushButton:default { border-color: %8; }
QPushButton:disabled { color: %10; background: %11; border-color: %12; }
QDialogButtonBox QPushButton { min-width: 72px; }
QComboBox { background: %1; color: %2; border: 1px solid %3; border-radius: %4px; padding: %13px 6px; }
QComboBox:editable { background: %14; border-color: %15; }
QComboBox:hover, QComboBox:on { border-color: %8; }
QComboBox:disabled { color: %10; }
QComboBox QAbstractItemView { background: %14; color: %2; border: 1px solid %3; outline: 0;
                              selection-background-color: %16; selection-color: %2; }
)")
               .arg(css(s.button), css(s.text), css(s.border))
               .arg(r)
               .arg(pushV)
               .arg(pushH)
               .arg(css(buttonHover), css(s.accent), css(buttonPressed), css(disabled), css(mix(s.button, s.window, 0.5)),
                    css(mix(s.border, s.window, 0.5)))
               .arg(std::max(1, pushV - 1))
               .arg(css(s.base), css(field), css(s.selection));

    qss += QStringLiteral(R"(
QLineEdit, QAbstractSpinBox, QPlainTextEdit, QTextEdit {
    background: %1; color: %2; border: 1px solid %3; border-radius: %4px;
    selection-background-color: %5; selection-color: %6; }
QLineEdit { padding: %7px 4px; }
QAbstractSpinBox { padding: %8px 2px; }
QLineEdit:focus, QAbstractSpinBox:focus, QPlainTextEdit:focus, QTextEdit:focus { border-color: %5; }
QLineEdit:disabled, QAbstractSpinBox:disabled { color: %9; }
QAbstractItemView { background: %1; color: %2; outline: 0; alternate-background-color: %10;
                    selection-background-color: %11; selection-color: %2; }
QListView::item:selected { background: %11; color: %2; border: 1px solid %12; }
QTreeView::item:selected { background: %11; color: %2; }
QListView::item:hover:!selected, QTreeView::item:hover:!selected { background: %13; }
QHeaderView::section { background: %14; color: %2; border: none; border-right: 1px solid %15;
                       border-bottom: 1px solid %15; padding: 3px 6px; }
)")
               .arg(css(s.base), css(s.text), css(field))
               .arg(r)
               .arg(css(s.accent), css(contrast(s.accent)))
               .arg(std::max(1, pad))
               .arg(std::max(0, pad - 1))
               .arg(css(disabled), css(mix(s.base, s.text, 0.04)), css(s.selection), css(s.selectionBorder),
                    css(s.hover), css(s.window), css(line));

    qss += QStringLiteral(R"(
QScrollBar:vertical { background: %1; width: 14px; margin: 0; border: none; }
QScrollBar:horizontal { background: %1; height: 14px; margin: 0; border: none; }
QScrollBar::handle { background: %2; border-radius: %3px; }
QScrollBar::handle:vertical { min-height: 24px; margin: 3px; }
QScrollBar::handle:horizontal { min-width: 24px; margin: 3px; }
QScrollBar::handle:hover { background: %4; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; border: none; background: none; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }
QSlider::groove:horizontal { height: 2px; background: %5; border: none; }
QSlider::groove:vertical { width: 2px; background: %5; border: none; }
QSlider::handle:horizontal { background: %6; width: 8px; margin: -8px 0; border-radius: 2px; }
QSlider::handle:vertical { background: %6; height: 8px; margin: 0 -8px; border-radius: 2px; }
QSlider::handle:hover { background: %7; }
QSlider::handle:disabled { background: %8; }
QProgressBar { background: %9; color: %10; border: 1px solid %11; border-radius: %12px; text-align: center; }
QProgressBar::chunk { background: %6; }
)")
               .arg(css(track), css(thumb))
               .arg(r > 0 ? 4 : 0)
               .arg(css(thumb.lighter(s.dark ? 130 : 85)), css(sliderTrack), css(s.accent), css(s.accent.lighter(120)),
                    css(disabled), css(s.base), css(s.text), css(s.border))
               .arg(r);

    qss += QStringLiteral(R"(
QTabWidget::pane { border: 1px solid %1; background: %2; top: -1px; }
QTabBar::tab { background: %3; color: %4; border: 1px solid %1; padding: 4px 10px; margin-right: -1px;
               border-top-left-radius: %5px; border-top-right-radius: %5px; }
QTabBar::tab:selected { background: %2; border-bottom-color: %2; }
QTabBar#ImageStrip::tab { background: transparent; border: 1px solid transparent; margin: 2px 1px; padding: 2px 4px;
                          border-radius: %5px; }
QTabBar#ImageStrip::tab:selected { background: %6; border-color: %7; }
QTabBar#ImageStrip::tab:hover:!selected { background: %8; border-color: %9; }
)")
               .arg(css(s.border), css(s.window), css(s.button), css(s.text))
               .arg(r)
               .arg(css(s.selection), css(s.accent), css(s.hover), css(hoverBorder));

    // Casillas: en claro, marca oscura sobre blanco (Windows 10); en oscuro, rellenas de acento.
    const QColor checkFill = s.dark ? s.accent : s.base;
    const QColor checkMark = s.dark ? contrast(s.accent) : s.text;
    const QColor indicatorBorder = s.dark ? mix(s.border, s.text, 0.25) : mix(s.border, s.text, 0.45);
    qss += QStringLiteral(R"(
QCheckBox::indicator, QAbstractItemView::indicator, QGroupBox::indicator {
    width: 13px; height: 13px; border: 1px solid %1; background: %2; border-radius: %3px; }
QRadioButton::indicator { width: 13px; height: 13px; border: 1px solid %1; background: %2; border-radius: 7px; }
QCheckBox::indicator:hover, QRadioButton::indicator:hover, QAbstractItemView::indicator:hover { border-color: %4; }
QCheckBox::indicator:checked, QAbstractItemView::indicator:checked, QGroupBox::indicator:checked {
    background: %5; border-color: %6; image: url("%7"); }
QCheckBox::indicator:indeterminate, QAbstractItemView::indicator:indeterminate {
    background: %5; border-color: %6; image: url("%8"); }
QRadioButton::indicator:checked { image: url("%9"); }
QCheckBox::indicator:disabled, QRadioButton::indicator:disabled { background: %10; border-color: %11; }
FloatingPanel QAbstractItemView { background: %12; border: none; }
)")
               .arg(css(indicatorBorder), css(s.base))
               .arg(std::min(r, 3))
               .arg(css(s.accent), css(checkFill), css(s.dark ? s.accent : indicatorBorder),
                    indicatorImage("check", checkMark), indicatorImage("partial", checkMark),
                    indicatorImage("dot", s.dark ? s.accent.lighter(130) : s.accent), css(mix(s.base, s.window, 0.5)),
                    css(mix(indicatorBorder, s.window, 0.5)), css(s.panel));

    // Flechas de desplegables y cajas numéricas (con borde propio, Qt ya no las dibuja).
    const QColor arrow = mix(s.text, s.window, 0.25);
    qss += QStringLiteral(R"(
QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: center right; width: 18px; border: none; }
QComboBox::down-arrow { image: url("%1"); width: 10px; height: 10px; }
QComboBox::down-arrow:disabled { image: url("%2"); }
QAbstractSpinBox { padding-right: 15px; }
QAbstractSpinBox::up-button, QAbstractSpinBox::down-button { subcontrol-origin: border; width: 15px; border: none;
                                                             border-left: 1px solid %3; background: transparent; }
QAbstractSpinBox::up-button { subcontrol-position: top right; }
QAbstractSpinBox::down-button { subcontrol-position: bottom right; }
QAbstractSpinBox::up-button:hover, QAbstractSpinBox::down-button:hover { background: %4; }
QAbstractSpinBox::up-button:pressed, QAbstractSpinBox::down-button:pressed { background: %5; }
QAbstractSpinBox::up-arrow { image: url("%6"); width: 9px; height: 9px; }
QAbstractSpinBox::down-arrow { image: url("%1"); width: 9px; height: 9px; }
QAbstractSpinBox::up-arrow:disabled, QAbstractSpinBox::up-arrow:off { image: url("%7"); }
QAbstractSpinBox::down-arrow:disabled, QAbstractSpinBox::down-arrow:off { image: url("%2"); }
)")
               .arg(indicatorImage("down", arrow), indicatorImage("down", disabled), css(mix(field, s.base, 0.5)),
                    css(s.hover), css(s.selection), indicatorImage("up", arrow), indicatorImage("up", disabled));
    return qss;
}

// ---------------------------------------------------------------------------
// Arranque y cambio de tema

void captureSystemDefaults()
{
    g_systemIconTheme = QIcon::themeName();
}

void prepare(Scheme scheme)
{
    g_start = scheme;
    if (scheme == Scheme::Kde)
        return;
    // Sin la configuración del escritorio (estilo, paleta, menú global, cambios de tema de
    // Plasma) y con Fusion, que pinta siempre con los colores de Kaint.
    QApplication::setDesktopSettingsAware(false);
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
}

void initStyle(Scheme scheme)
{
    if (g_systemIconTheme.isEmpty())
        captureSystemDefaults();
    watchSystem();
    if (scheme == Scheme::Kde) {
        g_baseFont = QApplication::font();
        g_baseFontSet = true;
        return;
    }
    if (QApplication::style()->name().compare(QLatin1String("fusion"), Qt::CaseInsensitive) != 0)
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    g_fusion = true;
    g_baseFont = desktopFont();
    g_baseFontSet = true;
    QApplication::setFont(g_baseFont);
}

void apply(Scheme scheme)
{
    g_current = scheme;
    if (g_systemIconTheme.isEmpty())
        captureSystemDefaults();
    if (scheme == Scheme::Kde) {
        // Solo al arrancar con él (cambiar desde o hacia KDE reinicia Kaint): estilo, paleta e
        // iconos de Plasma.
        qApp->setStyleSheet(QString());
        QApplication::setPalette(QPalette());
        if (!g_systemIconTheme.isEmpty())
            QIcon::setThemeName(g_systemIconTheme);
        g_active = fromPalette(QApplication::palette());
        emit notifier()->changed();
        return;
    }

    if (!g_fusion && QCoreApplication::instance()) {
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        g_fusion = true;
    }
    if (scheme == Scheme::Custom)
        g_active = resolved(custom());
    else
        g_active = preset(scheme == Scheme::Dark || (scheme == Scheme::System && systemPrefersDark()));

    if (g_baseFontSet) {
        QFont font = g_baseFont;
        if (scheme == Scheme::Custom && !g_active.fontFamily.isEmpty())
            font.setFamily(g_active.fontFamily);
        if (scheme == Scheme::Custom && g_active.fontSize > 0)
            font.setPointSize(g_active.fontSize);
        if (font != QApplication::font())
            QApplication::setFont(font);
    }
    QApplication::setPalette(palette(g_active));
    QIcon::setThemeName(iconThemeFor(g_active));
    const QString qss = styleSheet(g_active);
    if (qApp->styleSheet() != qss)
        qApp->setStyleSheet(qss);
    emit notifier()->changed();
}

void setCustom(const Spec& spec)
{
    g_custom = spec;
    g_customLoaded = true;
    if (g_current == Scheme::Custom)
        apply(Scheme::Custom);
}

Spec custom()
{
    if (!g_customLoaded) {
        g_custom = loadCustom();
        g_customLoaded = true;
    }
    return g_custom;
}

bool needsRestart(Scheme scheme)
{
    // El tema de KDE usa la integración de Plasma, que se decide al arrancar.
    return (scheme == Scheme::Kde) != (g_start == Scheme::Kde);
}

Scheme current()
{
    return g_current;
}

Spec active()
{
    return g_current == Scheme::Kde ? fromPalette(QApplication::palette()) : g_active;
}

bool isDark()
{
    const QPalette p = QApplication::palette();
    return p.color(QPalette::Window).lightness() < p.color(QPalette::WindowText).lightness();
}

bool systemPrefersDark()
{
    if (g_systemDark < 0)
        g_systemDark = readSystemScheme() == 1 ? 1 : 0;
    return g_systemDark == 1;
}

void Notifier::systemSettingChanged()
{
    const int before = g_systemDark;
    g_systemDark = readSystemScheme() == 1 ? 1 : 0;
    if (before != g_systemDark && g_current == Scheme::System)
        apply(Scheme::System);
}

Notifier* notifier()
{
    static Notifier* instance = new Notifier(QCoreApplication::instance());
    return instance;
}

// ---------------------------------------------------------------------------
// Fondo del área de trabajo

QColor workspaceColor()
{
    return active().workspace;
}

void paintWorkspace(QPainter& p, const QRect& area, const QRect& clip, const QColor& color)
{
    p.fillRect(clip, color.isValid() ? color : workspaceColor());
    if (g_current == Scheme::Kde || g_active.workspaceImage.isEmpty())
        return;
    ImageCache& cache = imageCache();
    if (cache.path != g_active.workspaceImage) {
        cache = ImageCache();
        cache.path = g_active.workspaceImage;
        cache.image.load(cache.path);
    }
    const QPixmap& image = cache.image;
    if (image.isNull() || area.isEmpty())
        return;

    p.save();
    p.setClipRect(clip);
    p.setOpacity(std::clamp(g_active.imageOpacity, 0, 100) / 100.0);
    const QSize size = image.size();
    switch (g_active.imageMode) {
    case ImageMode::Tile: {
        const QPoint offset((clip.left() - area.left()) % size.width(), (clip.top() - area.top()) % size.height());
        p.drawTiledPixmap(clip, image, offset);
        break;
    }
    case ImageMode::Center:
        p.drawPixmap(area.left() + (area.width() - size.width()) / 2, area.top() + (area.height() - size.height()) / 2,
                     image);
        break;
    case ImageMode::Stretch:
    case ImageMode::Fit:
    case ImageMode::Fill: {
        const QSize target = g_active.imageMode == ImageMode::Stretch ? area.size()
                             : g_active.imageMode == ImageMode::Fit
                                 ? size.scaled(area.size(), Qt::KeepAspectRatio)
                                 : size.scaled(area.size(), Qt::KeepAspectRatioByExpanding);
        const qreal dpr = p.device() ? p.device()->devicePixelRatioF() : 1.0;
        if (cache.scaledSize != target * dpr) {
            cache.scaledSize = target * dpr;
            cache.scaled = image.scaled(cache.scaledSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            cache.scaled.setDevicePixelRatio(dpr);
        }
        p.drawPixmap(area.left() + (area.width() - target.width()) / 2, area.top() + (area.height() - target.height()) / 2,
                     cache.scaled);
        break;
    }
    }
    p.restore();
}

} // namespace Theme
