#include "app/Theme.h"

#include "app/Icons.h"

#include <QApplication>
#include <QAbstractButton>
#include <QStyleOption>
#include <QRegularExpression>
#include <QProxyStyle>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFontDatabase>
#include <QIcon>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPixmap>
#include <QSettings>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QTimer>
#include <QWidget>
#ifdef KAINT_HAVE_DBUS
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#endif

#include <algorithm>

#ifdef Q_OS_WIN
#include <QAbstractNativeEventFilter>
#include <windows.h>
#include <dwmapi.h>
#endif

namespace Theme {

const std::array<ColorRole, 19> kColorRoles{{
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
    {"panelTitleEnd", &Spec::panelTitleEnd, QT_TRANSLATE_NOOP("Theme", "Cabecera (final del degradado)")},
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
Look g_look = Look::Kaint;
// Letras de acceso: siempre subrayadas o solo con Alt (mientras se pulsa o se navega por los
// menús con el teclado).
bool g_underlineAlways = false;
bool g_altShortcuts = false;
int g_systemDark = -1;
QColor g_titleBar;
QFileSystemWatcher* g_watcher = nullptr;

// Imagen de fondo cargada y su copia escalada (se crea con la aplicación ya en marcha).
struct ImageCache {
    QString key;
    QPixmap image;
    QPixmap scaled;
    QSize scaledSize;
};

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

#ifdef Q_OS_WIN
class AccentWatcher : public QAbstractNativeEventFilter {
public:
    bool nativeEventFilter(const QByteArray& type, void* message, qintptr*) override
    {
        if (type != "windows_generic_MSG")
            return false;
        const UINT msg = static_cast<const MSG*>(message)->message;
        if (msg == WM_DWMCOLORIZATIONCOLORCHANGED || msg == WM_SETTINGCHANGE) {
            // Llega una vez por ventana: se comprueba una sola vez, un poco después.
            if (!m_timer) {
                m_timer = new QTimer(notifier());
                m_timer->setSingleShot(true);
                m_timer->setInterval(200);
                QObject::connect(m_timer, &QTimer::timeout, notifier(), &Notifier::systemSettingChanged);
            }
            m_timer->start();
        }
        return false;
    }

private:
    QTimer* m_timer = nullptr;
};

// Diálogos y demás ventanas: barra de título del sistema en el modo claro u oscuro del tema (la
// ventana principal la ajusta ella misma, con la franja de los menús).
bool ownTitleBar(const QWidget* w)
{
    const Qt::WindowType type = w->windowType();
    return w->isWindow() && (type == Qt::Window || type == Qt::Dialog) && !w->inherits("QMainWindow");
}

class DialogTitleBars : public QObject {
public:
    using QObject::QObject;

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Show && watched->isWidgetType() && ownTitleBar(static_cast<QWidget*>(watched)))
            styleTitleBar(static_cast<QWidget*>(watched), QColor());
        return false;
    }
};
#endif

// Pulsar Alt muestra el subrayado de las letras de acceso (como en Windows); un clic o cerrar los
// menús lo vuelve a quitar.
class AltShortcuts : public QObject {
public:
    using QObject::QObject;

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        switch (event->type()) {
        case QEvent::KeyPress:
            if (static_cast<QKeyEvent*>(event)->key() == Qt::Key_Alt)
                set(true);
            break;
        case QEvent::KeyRelease:
            // Al soltar Alt sin un menú abierto (con uno, sigue mientras se navega con el teclado).
            if (static_cast<QKeyEvent*>(event)->key() == Qt::Key_Alt && !QApplication::activePopupWidget())
                set(false);
            break;
        case QEvent::MouseButtonPress:
            set(false);
            break;
        case QEvent::Hide:
            if (qobject_cast<QMenu*>(watched))
                QTimer::singleShot(0, this, [this] {
                    if (!QApplication::activePopupWidget())
                        set(false);
                });
            break;
        default:
            break;
        }
        return false;
    }

private:
    void set(bool on)
    {
        if (on == g_altShortcuts)
            return;
        g_altShortcuts = on;
        if (g_underlineAlways)
            return;
        for (QWidget* w : QApplication::allWidgets())
            if (w->isVisible() && (qobject_cast<QMenuBar*>(w) || qobject_cast<QMenu*>(w) || qobject_cast<QAbstractButton*>(w)))
                w->update();
    }
};

void installAltShortcuts()
{
    static bool installed = false;
    if (installed || !qApp)
        return;
    installed = true;
    qApp->installEventFilter(new AltShortcuts(notifier()));
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
#ifdef Q_OS_WIN
    // El color de énfasis y si va en las barras de título: Windows lo avisa a cada ventana.
    static AccentWatcher accentWatcher;
    qApp->installNativeEventFilter(&accentWatcher);
    qApp->installEventFilter(new DialogTitleBars(notifier()));
#endif
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
    else if (qstrcmp(kind, "left") == 0)
        shape = line.arg(QStringLiteral("%1"), QStringLiteral("M10.5 3.5l-4.5 4.5 4.5 4.5"), QStringLiteral("1.6"));
    else if (qstrcmp(kind, "right") == 0)
        shape = line.arg(QStringLiteral("%1"), QStringLiteral("M5.5 3.5l4.5 4.5-4.5 4.5"), QStringLiteral("1.6"));
    else if (qstrcmp(kind, "tri-up") == 0)
        shape = QStringLiteral("<path d='M4 10.5h8l-4-5z' fill='%1'/>");
    else if (qstrcmp(kind, "tri-down") == 0)
        shape = QStringLiteral("<path d='M4 5.5h8l-4 5z' fill='%1'/>");
    else if (qstrcmp(kind, "tri-left") == 0)
        shape = QStringLiteral("<path d='M10.5 4v8l-5-4z' fill='%1'/>");
    else if (qstrcmp(kind, "tri-right") == 0)
        shape = QStringLiteral("<path d='M5.5 4v8l5-4z' fill='%1'/>");
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

// Bordes clásicos en relieve, como 98.css: claro arriba y a la izquierda, oscuro abajo y a la
// derecha (al revés en lo hundido: campos, botones pulsados).
QString classicStyleSheet(const Spec& s)
{
    const QColor face = s.button;
    const QColor hi = mix(face, Qt::white, 0.9);
    const QColor light = mix(face, Qt::white, 0.5);
    const QColor shadow = mix(face, Qt::black, 0.33);
    const QColor dark = mix(face, Qt::black, 0.95);
    const QString raised = QStringLiteral("border-style: solid; border-width: 2px; border-color: %1 %2 %2 %1; border-radius: 0;")
                               .arg(css(hi), css(dark));
    const QString sunken = QStringLiteral("border-style: solid; border-width: 2px; border-color: %1 %2 %2 %1; border-radius: 0;")
                               .arg(css(shadow), css(hi));
    const QString thinRaised = QStringLiteral("border-style: solid; border-width: 1px; border-color: %1 %2 %2 %1; border-radius: 0;")
                                   .arg(css(hi), css(shadow));
    const QString thinSunken = QStringLiteral("border-style: solid; border-width: 1px; border-color: %1 %2 %2 %1; border-radius: 0;")
                                   .arg(css(shadow), css(hi));
    const QColor selectedText = contrast(s.selection);
    QString qss = QStringLiteral(R"(
QToolButton, QToolButton:hover:!checked:!pressed { background: transparent; border: 1px solid transparent; border-radius: 0; }
QToolButton:disabled { color: %4; }
QToolButton:hover { %5 }
QToolButton:checked, QToolButton:pressed { background: %6; %7 }
QLineEdit, QAbstractSpinBox, QPlainTextEdit, QTextEdit, QComboBox, QComboBox:editable, QListView, QTreeView,
QTableView { background: %8; %3 }
QComboBox:hover, QComboBox:on, QLineEdit:focus, QAbstractSpinBox:focus { %3 }
QComboBox::drop-down { background: %1; %2 width: 13px; }
QComboBox::down-arrow { image: url("%9"); }
QAbstractSpinBox::up-button, QAbstractSpinBox::down-button { background: %1; %5 width: 14px; }
QAbstractSpinBox::up-button:pressed, QAbstractSpinBox::down-button:pressed { background: %1; %7 }
QAbstractSpinBox::up-arrow { image: url("%10"); }
QAbstractSpinBox::down-arrow { image: url("%9"); }
)")
                      .arg(css(face), raised, sunken, css(shadow), thinRaised, css(light), thinSunken, css(s.base),
                           indicatorImage("tri-down", s.text))
                      .arg(indicatorImage("tri-up", s.text));

    qss += QStringLiteral(R"(
QMenuBar { background: %1; border: none; border-radius: 0; padding: 1px; }
QMenuBar::item { border: none; border-radius: 0; }
QMenuBar::item:selected, QMenuBar::item:pressed { background: %2; color: %3; border: none; }
QMenu { background: %4; %5 padding: 1px; }
QMenu::item { border: none; border-radius: 0; padding: 3px 20px 3px 20px; }
QMenu::item:selected { background: %2; color: %3; border: none; }
QMenu::separator { height: 0px; border-top: 1px solid %6; border-bottom: 1px solid %7; margin: 3px 2px; }
QToolTip { background: #ffffe1; color: #000000; border: 1px solid #000000; }
QToolBar::separator { background: %6; width: 1px; height: 1px; border-right: 1px solid %7; margin: 3px 3px; }
QStatusBar::item { %8 }
QHeaderView::section { background: %4; %9 }
QGroupBox { border: 2px groove %4; margin-top: 8px; padding-top: 6px; }
QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 3px; }
FloatingPanel QAbstractItemView { background: %10; %11 }
QAbstractItemView, QComboBox QAbstractItemView { selection-background-color: %2; selection-color: %3; }
QListView::item:selected, QTreeView::item:selected { background: %2; color: %3; border: none; }
QLineEdit, QAbstractSpinBox, QPlainTextEdit, QTextEdit { selection-background-color: %2; selection-color: %3; }
)")
               .arg(css(s.menuBar), css(s.selection), css(selectedText), css(face), raised, css(shadow), css(hi), thinSunken,
                    thinRaised)
               .arg(css(s.base), sunken);

    qss += QStringLiteral(R"(
QScrollBar:vertical { background: %1; width: 16px; margin: 16px 0 16px 0; border: none; }
QScrollBar:horizontal { background: %1; height: 16px; margin: 0 16px 0 16px; border: none; }
QScrollBar::handle { background: %2; %3 }
QScrollBar::handle:vertical { min-height: 16px; margin: 0; }
QScrollBar::handle:horizontal { min-width: 16px; margin: 0; }
QScrollBar::handle:hover { background: %2; }
QScrollBar::add-line, QScrollBar::sub-line { background: %2; %3 subcontrol-origin: margin; }
QScrollBar::add-line:vertical { height: 12px; width: 12px; subcontrol-position: bottom; }
QScrollBar::sub-line:vertical { height: 12px; width: 12px; subcontrol-position: top; }
QScrollBar::add-line:horizontal { width: 12px; height: 12px; subcontrol-position: right; }
QScrollBar::sub-line:horizontal { width: 12px; height: 12px; subcontrol-position: left; }
QScrollBar::up-arrow { image: url("%4"); width: 10px; height: 10px; }
QScrollBar::down-arrow { image: url("%5"); width: 10px; height: 10px; }
QScrollBar::left-arrow { image: url("%6"); width: 10px; height: 10px; }
QScrollBar::right-arrow { image: url("%7"); width: 10px; height: 10px; }
QSlider::groove:horizontal { height: 2px; background: %8; %9 }
QSlider::groove:vertical { width: 2px; background: %8; %9 }
QSlider::handle:horizontal { background: %2; %3 width: 9px; margin: -9px 0; }
QSlider::handle:vertical { background: %2; %3 height: 9px; margin: 0 -9px; }
QSlider::handle:hover { background: %2; }
QProgressBar { background: %10; %11 color: %12; }
QProgressBar::chunk { background: %13; width: 10px; margin: 2px; }
)")
               .arg(css(light), css(face), raised, indicatorImage("tri-up", s.text), indicatorImage("tri-down", s.text),
                    indicatorImage("tri-left", s.text), indicatorImage("tri-right", s.text), css(dark), thinSunken)
               .arg(css(s.base), sunken, css(s.text), css(s.selection));

    qss += QStringLiteral(R"(
QTabWidget::pane { background: %1; %2 top: -2px; }
QTabBar::tab { background: %1; border-style: solid; border-width: 2px 2px 0 2px; border-color: %3 %4 %4 %3;
               border-radius: 0; padding: 3px 10px; margin-right: 0; }
QTabBar::tab:!selected { margin-top: 2px; }
QCheckBox::indicator, QAbstractItemView::indicator, QGroupBox::indicator { background: %5; %6 width: 9px; height: 9px; }
QCheckBox::indicator:checked, QAbstractItemView::indicator:checked, QGroupBox::indicator:checked {
    background: %5; border-color: %7 %3 %3 %7; image: url("%8"); }
QRadioButton::indicator { background: %5; %6 border-radius: 6px; width: 8px; height: 8px; }
QRadioButton::indicator:checked { background: %5; image: url("%9"); }
QCheckBox::indicator:hover, QRadioButton::indicator:hover { border-color: %7 %3 %3 %7; }
)")
               .arg(css(face), raised, css(hi), css(dark), css(s.base), sunken, css(shadow),
                    indicatorImage("check", s.text), indicatorImage("dot", s.text));
    return qss;
}

} // namespace

namespace {

bool classicButtons()
{
    return g_current != Scheme::Kde && g_active.classic;
}

// Estilo de la aplicación: Fusion, salvo los botones con bordes clásicos, que llevan el doble
// relieve de 98.css, el marco negro del botón por defecto y el foco punteado de Windows 98.
class KaintStyle : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;

    void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* p, const QWidget* widget) const override
    {
        // Flechas de desplazamiento de la tira de imágenes: las de Paint.NET.
        if ((element == PE_IndicatorArrowLeft || element == PE_IndicatorArrowRight) && widget && widget->parentWidget()
            && widget->parentWidget()->objectName() == QLatin1String("ImageStrip")) {
            const QIcon icon = Icons::named(element == PE_IndicatorArrowLeft ? QStringLiteral("strip-scroll-left")
                                                                              : QStringLiteral("strip-scroll-right"));
            if (!icon.isNull()) {
                const int side = std::min({option->rect.width(), option->rect.height(), 18});
                icon.paint(p, QRect(QPoint(0, 0), QSize(side, side)).translated(option->rect.center() - QPoint(side / 2, side / 2)),
                           Qt::AlignCenter, option->state & State_Enabled ? QIcon::Normal : QIcon::Disabled);
                return;
            }
        }
        if (classicButtons()) {
            if (element == PE_PanelButtonCommand) {
                drawClassicButton(option, p);
                return;
            }
            if (element == PE_FrameDefaultButton)
                return;
            if (element == PE_FrameFocusRect && qobject_cast<const QAbstractButton*>(widget)) {
                QPen dotted(option->palette.color(QPalette::ButtonText), 1, Qt::DotLine);
                p->save();
                p->setPen(dotted);
                p->setBrush(Qt::NoBrush);
                p->drawRect(option->rect.adjusted(1, 1, -2, -2));
                p->restore();
                return;
            }
        }
        QProxyStyle::drawPrimitive(element, option, p, widget);
    }

    int pixelMetric(PixelMetric metric, const QStyleOption* option, const QWidget* widget) const override
    {
        if (classicButtons() && (metric == PM_ButtonShiftHorizontal || metric == PM_ButtonShiftVertical))
            return 1;
        return QProxyStyle::pixelMetric(metric, option, widget);
    }

    int styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget, QStyleHintReturn* data) const override
    {
        // Las listas largas (las fuentes) se abren debajo, con barra de desplazamiento, en vez de
        // como un menú con flechas para desplazarse.
        if (hint == SH_ComboBox_Popup && widget && widget->property("kaintScrollableList").toBool())
            return 0;
        // Como en Windows: las letras de acceso, subrayadas solo con Alt (o siempre, en Configuración).
        if (hint == SH_UnderlineShortcut)
            return g_underlineAlways || g_altShortcuts ? 1 : 0;
        return QProxyStyle::styleHint(hint, option, widget, data);
    }

private:
    static void drawClassicButton(const QStyleOption* option, QPainter* p)
    {
        const auto* button = qstyleoption_cast<const QStyleOptionButton*>(option);
        const bool down = option->state & (State_Sunken | State_On);
        const bool isDefault = button && (button->features & QStyleOptionButton::DefaultButton);
        const QColor face = option->palette.color(QPalette::Button);
        const QColor hi = mix(face, Qt::white, 0.9), light = mix(face, Qt::white, 0.5);
        const QColor shadow = mix(face, Qt::black, 0.33), dark = mix(face, Qt::black, 0.95);
        QRect r = option->rect;
        p->save();
        p->setRenderHint(QPainter::Antialiasing, false);
        p->fillRect(r, face);
        if (isDefault) {
            p->setPen(dark);
            p->drawRect(r.adjusted(0, 0, -1, -1));
            r.adjust(1, 1, -1, -1);
        }
        auto bevel = [p](const QRect& box, const QColor& topLeft, const QColor& bottomRight) {
            p->setPen(topLeft);
            p->drawLine(box.topLeft(), box.topRight() - QPoint(1, 0));
            p->drawLine(box.topLeft(), box.bottomLeft() - QPoint(0, 1));
            p->setPen(bottomRight);
            p->drawLine(box.bottomLeft(), box.bottomRight());
            p->drawLine(box.topRight(), box.bottomRight());
        };
        const QRect outer = r.adjusted(0, 0, -1, -1), inner = r.adjusted(1, 1, -2, -2);
        if (down) {
            bevel(outer, dark, hi);
            bevel(inner, shadow, light);
        } else {
            bevel(outer, hi, dark);
            bevel(inner, light, shadow);
        }
        p->restore();
    }
};

QStyle* createStyle()
{
    return new KaintStyle(QStyleFactory::create(QStringLiteral("Fusion")));
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
        s.toolBar = rgb(0x202020);
        s.statusBar = rgb(0x202020);
        s.panel = rgb(0x262626);
        s.panelTitle = rgb(0x262626);
        s.panelTitleText = rgb(0xd6d6d6);
        s.panelClose = rgb(0xa84646);
        s.selection = rgb(0x1c476d);
        s.selectionBorder = rgb(0x2a6fb0);
        s.hover = rgb(0x3a3a3a);
        s.workspace = rgb(0x303030);
        s.radius = 4;
        s.panelTitleEnd = s.panelTitle;
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
        s.panel = rgb(0xf0f0f0);
        s.panelTitle = rgb(0xf0f0f0);
        s.panelTitleText = rgb(0x3c3c3c);
        s.panelClose = rgb(0xc75050);
        s.selection = rgb(0xcce4f7);
        s.selectionBorder = rgb(0x3c7fb1);
        s.hover = rgb(0xe5f3ff);
        s.workspace = rgb(0xcfcfcf);
        s.radius = 0;
        s.panelTitleEnd = s.panelTitle;
    }
    return s;
}

// Windows 98, con los colores de 98.css (https://jdan.github.io/98.css/): gris con relieve,
// cabeceras azul marino en degradado y el escritorio verde azulado detrás de la imagen.
Spec windows98()
{
    Spec s;
    s.dark = false;
    s.accent = rgb(0x000080);
    s.window = rgb(0xc0c0c0);
    s.text = rgb(0x000000);
    s.base = rgb(0xffffff);
    s.button = rgb(0xc0c0c0);
    s.border = rgb(0x808080);
    s.menuBar = rgb(0xc0c0c0);
    s.menuBarText = rgb(0x000000);
    s.toolBar = rgb(0xc0c0c0);
    s.statusBar = rgb(0xc0c0c0);
    s.panel = rgb(0xc0c0c0);
    s.panelTitle = rgb(0x000080);
    s.panelTitleEnd = rgb(0x1084d0);
    s.panelTitleText = rgb(0xffffff);
    s.panelClose = rgb(0xc0c0c0);
    s.selection = rgb(0x000080);
    s.selectionBorder = rgb(0x000080);
    s.hover = rgb(0xc0c0c0);
    s.workspace = rgb(0x008080);
    s.radius = 0;
    s.buttonStyle = ButtonStyle::Framed;
    s.density = Density::Compact;
    s.classic = true;
    s.pixelFont = true;
    s.panelOpacity = 100;
    s.fontFamily = QStringLiteral("Microsoft Sans Serif");
    s.fontSize = 8;
    return s;
}

Spec blue()
{
    // Como el tema Azul de Paint.NET 5: el claro, con los menús casi blancos, las barras de
    // herramientas y las reglas en degradado azulado (los degradados van en la hoja de estilo y en
    // la regla) y la barra de estado azul claro.
    Spec s = preset(false);
    s.blue = true;
    s.toolBar = rgb(0xfbfdff);
    s.statusBar = rgb(0xebf2fa);
    s.border = rgb(0x8fa6c3);
    s.hover = rgb(0xe5f1fb);
    s.selection = rgb(0xcce4f7);
    s.selectionBorder = rgb(0x3c7fb1);
    return s;
}

Spec resolved(const Spec& custom)
{
    Spec s = custom;
    const Spec base = preset(custom.dark);
    for (const ColorRole& role : kColorRoles)
        if (!(s.*role.color).isValid())
            s.*role.color = base.*role.color;
    // Sin final propio, la cabecera es de un solo color (el suyo, aunque se haya cambiado).
    if (!custom.panelTitleEnd.isValid())
        s.panelTitleEnd = s.panelTitle;
    return s;
}

// ---------------------------------------------------------------------------
// Tema personalizado: JSON (se guarda en la configuración y se puede exportar)

QString storeBackground(const QString& file)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/backgrounds");
    if (file.isEmpty() || QFileInfo(file).absolutePath() == QDir(dir).absolutePath())
        return file;
    QDir().mkpath(dir);
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly))
        return file;
    const QByteArray data = in.readAll();
    const QString name = QStringLiteral("%1-%2.%3")
                             .arg(QFileInfo(file).completeBaseName())
                             .arg(qHash(data), 8, 16, QLatin1Char('0'))
                             .arg(QFileInfo(file).suffix().toLower());
    const QString target = QDir(dir).filePath(name);
    if (!QFile::exists(target)) {
        QFile out(target);
        if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size())
            return file;
    }
    return target;
}

QJsonObject toJson(const Spec& spec, bool embedImage)
{
    QJsonObject colors;
    for (const ColorRole& role : kColorRoles) {
        const QColor& c = spec.*role.color;
        if (c.isValid())
            colors.insert(QLatin1String(role.key), c.name(c.alpha() == 255 ? QColor::HexRgb : QColor::HexArgb));
    }
    QJsonObject json{
        {QStringLiteral("dark"), spec.dark},
        {QStringLiteral("colors"), colors},
        {QStringLiteral("workspaceImage"), spec.workspaceImage},
        {QStringLiteral("imageMode"), int(spec.imageMode)},
        {QStringLiteral("imageOpacity"), spec.imageOpacity},
        {QStringLiteral("imageScale"), spec.imageScale},
        {QStringLiteral("imageAnchor"), spec.imageAnchor},
        {QStringLiteral("imageBlur"), spec.imageBlur},
        {QStringLiteral("imageGrayscale"), spec.imageGrayscale},
        {QStringLiteral("imageTint"), spec.imageTint.isValid() ? spec.imageTint.name() : QString()},
        {QStringLiteral("imageTintStrength"), spec.imageTintStrength},
        {QStringLiteral("classic"), spec.classic},
        {QStringLiteral("pixelFont"), spec.pixelFont},
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
    if (embedImage && !spec.workspaceImage.isEmpty()) {
        QFile f(spec.workspaceImage);
        if (f.open(QIODevice::ReadOnly)) {
            json.insert(QStringLiteral("workspaceImageName"), QFileInfo(spec.workspaceImage).fileName());
            json.insert(QStringLiteral("workspaceImageData"), QString::fromLatin1(f.readAll().toBase64()));
        }
    }
    return json;
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
    // Tema exportado con la imagen dentro: se guarda en la carpeta de Kaint.
    const QByteArray embedded = QByteArray::fromBase64(json.value(QStringLiteral("workspaceImageData")).toString().toLatin1());
    if (!embedded.isEmpty()) {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/backgrounds");
        QDir().mkpath(dir);
        const QString name = QFileInfo(json.value(QStringLiteral("workspaceImageName")).toString()).fileName();
        const QString target = QDir(dir).filePath(QStringLiteral("%1-%2").arg(qHash(embedded), 8, 16, QLatin1Char('0'))
                                                      .arg(name.isEmpty() ? QStringLiteral("fondo.png") : name));
        QFile out(target);
        if (QFile::exists(target) || (out.open(QIODevice::WriteOnly) && out.write(embedded) == embedded.size()))
            s.workspaceImage = target;
    }
    s.imageMode = ImageMode(number("imageMode", int(base.imageMode), 0, 4));
    s.imageOpacity = number("imageOpacity", base.imageOpacity, 0, 100);
    s.imageScale = number("imageScale", base.imageScale, 10, 400);
    s.imageAnchor = number("imageAnchor", base.imageAnchor, 0, 8);
    s.imageBlur = number("imageBlur", base.imageBlur, 0, 40);
    s.imageGrayscale = json.value(QStringLiteral("imageGrayscale")).toBool(false);
    s.imageTint = QColor::fromString(json.value(QStringLiteral("imageTint")).toString());
    s.imageTintStrength = number("imageTintStrength", base.imageTintStrength, 0, 100);
    s.classic = json.value(QStringLiteral("classic")).toBool(false);
    s.pixelFont = json.value(QStringLiteral("pixelFont")).toBool(false);
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

namespace {

// Recreación de Paint.NET 5 sobre la hoja de siempre: menús sin marco y más juntos, barras sin
// relleno de más, la pestaña de la imagen actual en azul, las listas con la selección de Paint.NET
// (azul con borde de acento), las casillas de las capas blancas y el botón «Más >>» claro.
QString paintNetStyleSheet(const Spec& s, const QColor& line)
{
    const QColor edge(0x00, 0x78, 0xd7);
    const QColor tab = s.dark ? QColor(0x3a, 0x63, 0xc0) : QColor(0xcc, 0xe8, 0xff);
    // Los menús van en un recuadro con borde arriba y a los lados, como en Paint.NET (gris en el
    // claro y azul oscuro en el Azul), con algo de espacio antes de «Archivo».
    const QColor menuEdge = s.blue ? QColor(0x36, 0x5d, 0x90) : s.dark ? line : QColor(0xb8, 0xb8, 0xb8);
    QString qss = QStringLiteral(R"(
QMenuBar { border: 1px solid %1; border-bottom: none; border-radius: 0px; padding: 0px 4px; }
)")
                      .arg(css(menuEdge));
    qss += QStringLiteral(R"(
QMenuBar::item { padding: 4px 4px; border-radius: 0px; }
QToolBar { spacing: 0px; padding: 0px 2px; }
QToolBar::separator { margin: 3px 2px; }
QTabBar#ImageStrip::tab { border-radius: 0px; margin: 0px 1px; }
QTabBar#ImageStrip::tab:selected { background: %1; border-color: %2; }
QComboBox#ZoomBox { background: transparent; border: 1px solid transparent; }
QComboBox#ZoomBox:hover { border-color: %3; }
QPushButton#ColorsMoreButton { background: #e1e1e1; color: #000000; border: 1px solid %2; border-radius: 0px; padding: 2px 8px; }
QPushButton#ColorsMoreButton:hover { background: #e5f1fb; }
QPushButton#ColorsMoreButton:pressed, QPushButton#ColorsMoreButton:checked { background: #cce4f7; }
QWidget#PanelButtons { background: %4; border-top: 1px solid %5; }
FloatingPanel QAbstractItemView { background: %6; }
)")
               .arg(css(tab), css(edge), css(line), css(s.toolBar), css(line), css(s.panel));
    if (s.dark)
        qss += QStringLiteral(R"(
FloatingPanel QAbstractItemView::indicator { background: #ffffff; border: 1px solid #333333; border-radius: 0px; }
FloatingPanel QAbstractItemView::indicator:checked { background: #ffffff; border-color: #333333; image: url("%1"); }
)")
                   .arg(indicatorImage("check", QColor(0x1e, 0x1e, 0x1e)));
    return qss;
}

} // namespace

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
    if (s.classic) {
        qss.remove(QRegularExpression(QStringLiteral("^QPushButton[^\n]*\n"), QRegularExpression::MultilineOption));
        qss += classicStyleSheet(s);
    }
    // Tirador de los deslizadores mientras se pulsa (SliderBehavior): del color del texto, como el
    // blanco de Paint.NET en el tema oscuro.
    qss += QStringLiteral("QSlider[kaintPressed=\"true\"]::handle { background: %1; }\n").arg(css(s.text));
    // Botones de las ventanas de la esquina superior derecha, como en Paint.NET: los activos con
    // el acento translúcido sobre la franja y un borde del acento (no el celeste de la selección).
    if (!s.classic)
        qss += QStringLiteral(R"(
QWidget#CornerButtons QToolButton:checked { background: %1; border: 1px solid %2; }
QWidget#CornerButtons QToolButton:checked:hover { background: %3; }
)")
                   .arg(QStringLiteral("rgba(%1, %2, %3, 64)").arg(s.accent.red()).arg(s.accent.green()).arg(s.accent.blue()),
                        css(s.accent),
                        QStringLiteral("rgba(%1, %2, %3, 96)").arg(s.accent.red()).arg(s.accent.green()).arg(s.accent.blue()));
    if (s.paintNet)
        qss += paintNetStyleSheet(s, line);
    // Azul: las dos barras de herramientas forman un degradado de casi blanco a azul claro, con una
    // línea azul oscura debajo; la franja de botones de las ventanas, azulada.
    if (s.blue)
        qss += QStringLiteral(R"(
QToolBar#MainToolBar { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #fafdff, stop:1 #ecf2fa); }
QToolBar#ToolOptionsBar { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #ebf1f9, stop:0.65 #dce7f5, stop:1 #dce7f5);
                          border-bottom: 1px solid #365d90; }
QStatusBar { border-top: 1px solid #a9bcd4; }
QWidget#PanelButtons { background: #d5dde7; border-top: 1px solid #58769c; }
)");
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
    QApplication::setStyle(createStyle());
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
    if (!dynamic_cast<KaintStyle*>(QApplication::style()))
        QApplication::setStyle(createStyle());
    g_fusion = true;
    g_baseFont = desktopFont();
    g_baseFontSet = true;
    QApplication::setFont(g_baseFont);
}

void apply(Scheme scheme)
{
    installAltShortcuts();
    g_current = scheme;
    g_titleBar = titleBarColor();
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
        QApplication::setStyle(createStyle());
        g_fusion = true;
    }
    if (scheme == Scheme::Custom)
        g_active = resolved(custom());
    else if (scheme == Scheme::Windows98)
        g_active = windows98();
    else if (scheme == Scheme::Blue)
        g_active = withTitleBarColor(blue(), g_titleBar);
    else
        g_active = withTitleBarColor(preset(scheme == Scheme::Dark || (scheme == Scheme::System && systemPrefersDark())),
                                     g_titleBar);
    g_active.paintNet = g_look == Look::PaintNet;
    if (g_active.paintNet) {
        // Paint.NET 5: todo más junto, esquinas rectas, iconos de herramientas algo más pequeños y,
        // con sus temas claro y oscuro, la selección azul de sus listas.
        g_active.radius = 0;
        g_active.density = Density::Compact;
        g_active.toolIconSize = std::min(g_active.toolIconSize, 20);
        if (scheme != Scheme::Custom && scheme != Scheme::Windows98) {
            g_active.selection = g_active.dark ? QColor(0x19, 0x5b, 0xa0) : QColor(0xcc, 0xe8, 0xff);
            g_active.selectionBorder = g_active.dark ? QColor(0x00, 0x78, 0xd7) : QColor(0x99, 0xd1, 0xff);
        }
    }

    if (g_baseFontSet) {
        // Fuente propia: la del personalizado o la de Windows 98.
        const bool ownFont = scheme == Scheme::Custom || scheme == Scheme::Windows98;
        QFont font = g_baseFont;
        // Si no está la fuente (Microsoft Sans Serif en Linux), las más parecidas.
        if (ownFont && !g_active.fontFamily.isEmpty())
            font.setFamilies({g_active.fontFamily, QStringLiteral("Tahoma"), QStringLiteral("Arial"),
                              QStringLiteral("Liberation Sans"), g_baseFont.family()});
        if (ownFont && g_active.fontSize > 0)
            font.setPointSize(g_active.fontSize);
        font.setStyleStrategy(ownFont && g_active.pixelFont ? QFont::NoAntialias : QFont::PreferDefault);
        font.setHintingPreference(ownFont && g_active.pixelFont ? QFont::PreferFullHinting : g_baseFont.hintingPreference());
        if (font != QApplication::font())
            QApplication::setFont(font);
    }
    QApplication::setPalette(palette(g_active));
    QIcon::setThemeName(iconThemeFor(g_active));
    const QString qss = styleSheet(g_active);
    if (qApp->styleSheet() != qss)
        qApp->setStyleSheet(qss);
#ifdef Q_OS_WIN
    for (QWidget* w : QApplication::topLevelWidgets())
        if (w->isVisible() && ownTitleBar(w))
            styleTitleBar(w, QColor());
#endif
    emit notifier()->changed();
}

void setUnderlineShortcuts(bool always)
{
    if (always == g_underlineAlways)
        return;
    g_underlineAlways = always;
    for (QWidget* w : QApplication::allWidgets())
        w->update();
}

void setLook(Look look)
{
    g_look = look;
}

Look look()
{
    return g_look;
}

bool paintNetLook()
{
    return g_current != Scheme::Kde && g_active.paintNet;
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

QColor titleBarColor()
{
#ifdef Q_OS_WIN
    // "Mostrar el color de énfasis en las barras de título" (AccentColor está en ABGR).
    const QSettings dwm(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\DWM"), QSettings::NativeFormat);
    if (dwm.value(QStringLiteral("ColorPrevalence")).toInt() == 1) {
        const auto abgr = quint32(dwm.value(QStringLiteral("AccentColor")).toLongLong());
        if (abgr != 0)
            return QColor(int(abgr & 0xff), int(abgr >> 8 & 0xff), int(abgr >> 16 & 0xff));
    }
#endif
    return {};
}

Spec withTitleBarColor(Spec spec, const QColor& titleBar)
{
    // Como Paint.NET: con el color de énfasis en las barras de título, las cabeceras de las
    // ventanas flotantes lo llevan también.
    if (titleBar.isValid()) {
        spec.panelTitle = spec.panelTitleEnd = titleBar;
        spec.panelTitleText = contrast(titleBar);
    }
    return spec;
}

void styleTitleBar(QWidget* window, const QColor& band)
{
#ifdef Q_OS_WIN
    // Modo oscuro de la barra nativa (Windows 10 1809+) y, si no lleva el color de énfasis, el de
    // la franja de los menús (Windows 11; en Windows 10 se ignora).
    const auto hwnd = reinterpret_cast<HWND>(window->winId());
    const BOOL dark = isDark();
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof dark);
    // Con el color de énfasis, o sin franja (diálogos), el color es el del sistema
    // (DWMWA_COLOR_DEFAULT), también si se activó con Kaint abierto.
    COLORREF caption = 0xFFFFFFFF, text = 0xFFFFFFFF;
    if (!titleBarColor().isValid() && band.isValid()) {
        caption = RGB(band.red(), band.green(), band.blue());
        text = band.lightness() < 128 ? RGB(255, 255, 255) : RGB(0, 0, 0);
    }
    DwmSetWindowAttribute(hwnd, 35, &caption, sizeof caption);
    DwmSetWindowAttribute(hwnd, 36, &text, sizeof text);
    // Windows 10 no repinta la barra de una ventana ya abierta hasta que se le pide.
    if (window->isVisible())
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
#else
    Q_UNUSED(window);
    Q_UNUSED(band);
#endif
}

#ifdef Q_OS_WIN
namespace {
// Mientras se pide a Windows que la pinte inactiva, ese WM_NCACTIVATE se deja pasar.
bool g_forceInactiveTitle = false;
} // namespace
#endif

bool keepTitleBarActive(QWidget* window, const QByteArray& type, void* message, qintptr* result)
{
#ifdef Q_OS_WIN
    Q_UNUSED(window);
    auto* msg = static_cast<MSG*>(message);
    if (type != "windows_generic_MSG" || msg->message != WM_NCACTIVATE || msg->wParam || g_forceInactiveTitle)
        return false;
    // Se pinta activa; si la ventana nueva resulta ser de otro programa, syncTitleBarActivation la
    // vuelve a pintar inactiva.
    *result = DefWindowProcW(msg->hwnd, WM_NCACTIVATE, TRUE, msg->lParam);
    return true;
#else
    Q_UNUSED(window);
    Q_UNUSED(type);
    Q_UNUSED(message);
    Q_UNUSED(result);
    return false;
#endif
}

void syncTitleBarActivation(QWidget* window)
{
#ifdef Q_OS_WIN
    const auto own = reinterpret_cast<HWND>(window->winId());
    const HWND active = GetForegroundWindow();
    DWORD pid = 0;
    if (active)
        GetWindowThreadProcessId(active, &pid);
    if (active && pid == GetCurrentProcessId()) {
        if (active != own)
            SendMessageW(own, WM_NCACTIVATE, TRUE, 0);
        return;
    }
    g_forceInactiveTitle = true;
    SendMessageW(own, WM_NCACTIVATE, FALSE, 0);
    g_forceInactiveTitle = false;
#else
    Q_UNUSED(window);
#endif
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
    // Claro, oscuro y sistema llevan el color de énfasis de las barras de título (Windows).
    const bool accented = g_current == Scheme::System || g_current == Scheme::Light || g_current == Scheme::Dark
                       || g_current == Scheme::Blue;
    if ((before != g_systemDark && g_current == Scheme::System) || (accented && titleBarColor() != g_titleBar))
        apply(g_current);
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
    if (g_current != Scheme::Kde)
        paintBackgroundImage(p, area, clip, g_active);
}

namespace {

// La imagen de fondo con sus retoques (escala de grises, desenfoque, tinte), en caché.
const QPixmap& processedBackground(const Spec& spec, ImageCache& cache)
{
    const QString key = QStringLiteral("%1|%2|%3|%4|%5")
                            .arg(spec.workspaceImage)
                            .arg(spec.imageGrayscale)
                            .arg(spec.imageBlur)
                            .arg(spec.imageTint.isValid() ? spec.imageTint.name() : QString())
                            .arg(spec.imageTintStrength);
    if (cache.key == key)
        return cache.image;
    cache = ImageCache();
    cache.key = key;
    QImage image(spec.workspaceImage);
    if (image.isNull())
        return cache.image;
    image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (spec.imageGrayscale) {
        for (int y = 0; y < image.height(); ++y) {
            QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                const int g = qGray(line[x]);
                line[x] = qRgba(g, g, g, qAlpha(line[x]));
            }
        }
    }
    if (spec.imageBlur > 0) {
        // Reducir y volver a ampliar con suavizado: un desenfoque rápido y sin bordes duros.
        const double f = 1.0 + spec.imageBlur / 2.0;
        const QSize small((image.size() / f).expandedTo(QSize(1, 1)));
        image = image.scaled(small, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                    .scaled(image.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    if (spec.imageTint.isValid() && spec.imageTintStrength > 0) {
        QPainter tp(&image);
        tp.setCompositionMode(QPainter::CompositionMode_SourceAtop);
        tp.setOpacity(spec.imageTintStrength / 100.0);
        tp.fillRect(image.rect(), spec.imageTint);
    }
    cache.image = QPixmap::fromImage(image);
    return cache.image;
}

// Posición de un rectángulo 'size' en 'area' según el ancla (0..8: de arriba a la izquierda a
// abajo a la derecha, 4 es el centro).
QPoint anchored(const QRect& area, QSize size, int anchor)
{
    const int col = std::clamp(anchor, 0, 8) % 3, row = std::clamp(anchor, 0, 8) / 3;
    return QPoint(area.left() + (area.width() - size.width()) * col / 2, area.top() + (area.height() - size.height()) * row / 2);
}

} // namespace

void paintBackgroundImage(QPainter& p, const QRect& area, const QRect& clip, const Spec& spec)
{
    if (spec.workspaceImage.isEmpty() || area.isEmpty())
        return;
    // Una caché para el lienzo y otra para las vistas previas (otro tamaño u otros retoques).
    static ImageCache main, preview;
    ImageCache& cache = &spec == &g_active ? main : preview;
    const QPixmap& image = processedBackground(spec, cache);
    if (image.isNull())
        return;

    p.save();
    p.setClipRect(clip);
    p.setOpacity(std::clamp(spec.imageOpacity, 0, 100) / 100.0);
    const double scale = std::clamp(spec.imageScale, 10, 400) / 100.0;
    const qreal dpr = p.device() ? p.device()->devicePixelRatioF() : 1.0;
    QSize target;
    switch (spec.imageMode) {
    case ImageMode::Tile:
    case ImageMode::Center: target = (QSizeF(image.size()) * scale).toSize().expandedTo(QSize(1, 1)); break;
    case ImageMode::Stretch: target = area.size(); break;
    case ImageMode::Fit: target = image.size().scaled(area.size(), Qt::KeepAspectRatio); break;
    case ImageMode::Fill: target = image.size().scaled(area.size(), Qt::KeepAspectRatioByExpanding); break;
    }
    if (cache.scaledSize != target * dpr) {
        cache.scaledSize = target * dpr;
        cache.scaled = target == image.size() ? image
                                              : image.scaled(cache.scaledSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        cache.scaled.setDevicePixelRatio(dpr);
    }
    if (spec.imageMode == ImageMode::Tile) {
        const QPoint origin = anchored(area, target, spec.imageAnchor);
        auto wrap = [](int v, int m) { return ((v % m) + m) % m; };
        const QPoint offset(wrap(clip.left() - origin.x(), target.width()), wrap(clip.top() - origin.y(), target.height()));
        p.drawTiledPixmap(clip, cache.scaled, offset);
    } else {
        p.drawPixmap(anchored(area, target, spec.imageAnchor), cache.scaled);
    }
    p.restore();
}

} // namespace Theme
