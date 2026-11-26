#include "app/Animations.h"
#include "app/AppState.h"
#include "app/Icons.h"
#include "app/Intro.h"
#include "app/MainWindow.h"
#include "app/Theme.h"
#include "app/WindowFrame.h"
#include "app/dialogs/SettingsDialog.h"
#include "core/plugins/PluginManager.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QImageReader>
#include <QLibraryInfo>
#include <QLocale>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>

#include "app/CustomLanguages.h"

#include <memory>
#include <optional>

#include <algorithm>
#include <cstdio>
#include <functional>

namespace {

// Valor de "--nombre valor" o "--nombre=valor" antes de crear la aplicación.
QString rawOption(int argc, char** argv, const QString& name)
{
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QLatin1String("--"))
            break;
        if (arg == name && i + 1 < argc)
            return QString::fromLocal8Bit(argv[i + 1]);
        if (arg.startsWith(name + QLatin1Char('=')))
            return arg.section(QLatin1Char('='), 1);
    }
    return {};
}

// Tema guardado o el de --color-scheme (system, light, dark, kde, custom, windows98, blue, windows7,
// windowsxp, windows98alt o windows7dark), que también se guarda: así se puede recuperar el tema sin abrir la configuración.
#if defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD)
// Desde un AppImage (no instalado), el escritorio no encuentra el .desktop de Kaint y en Wayland la
// ventana salía con el icono genérico (el compositor saca el icono del .desktop según el app_id).
// Se copian su .desktop (apuntando al AppImage) y su icono a ~/.local/share, marcados como de Kaint:
// no se toca uno instalado por el sistema ni uno que haya hecho el usuario, y se pone al día si el
// AppImage cambia de sitio.
void integrateAppImage()
{
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    const QString appDir = qEnvironmentVariable("APPDIR");
    if (appImage.isEmpty() || appDir.isEmpty())
        return;
    const QString id = QStringLiteral(KAINT_APP_ID);
    const QString home = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (home.isEmpty())
        return;
    for (const QString& dir : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation))
        if (QFileInfo(dir).canonicalFilePath() != QFileInfo(home).canonicalFilePath()
            && QFileInfo::exists(dir + QStringLiteral("/applications/") + id + QStringLiteral(".desktop")))
            return;  // instalado (paquete, Flatpak...): ese manda
    const QString target = home + QStringLiteral("/applications/") + id + QStringLiteral(".desktop");
    QFile current(target);
    QByteArray before;
    if (current.open(QIODevice::ReadOnly)) {
        before = current.readAll();
        current.close();
        if (!before.contains("X-Kaint-AppImage=true"))
            return;  // uno propio del usuario
    }
    QFile source(appDir + QStringLiteral("/usr/share/applications/") + id + QStringLiteral(".desktop"));
    if (!source.open(QIODevice::ReadOnly))
        return;
    QStringList lines;
    const QString exec = QStringLiteral("\"%1\"").arg(appImage);
    for (const QString& line : QString::fromUtf8(source.readAll()).split(QLatin1Char('\n'))) {
        if (line.startsWith(QLatin1String("Exec=")))
            lines << QStringLiteral("Exec=") + exec + QStringLiteral(" %F");
        else if (line.startsWith(QLatin1String("TryExec=")))
            lines << QStringLiteral("TryExec=") + appImage;
        else
            lines << line;
    }
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
        lines.removeLast();
    lines << QStringLiteral("X-Kaint-AppImage=true") << QString();
    const QByteArray after = lines.join(QLatin1Char('\n')).toUtf8();
    if (after != before) {
        QDir().mkpath(QFileInfo(target).absolutePath());
        QSaveFile out(target);
        if (out.open(QIODevice::WriteOnly) && out.write(after) == after.size())
            out.commit();
    }
    // Los iconos (el PNG de 256 y el SVG), si faltan o cambiaron.
    for (const QString& rel : {QStringLiteral("/icons/hicolor/256x256/apps/") + id + QStringLiteral(".png"),
                               QStringLiteral("/icons/hicolor/scalable/apps/") + id + QStringLiteral(".svg")}) {
        QFile from(appDir + QStringLiteral("/usr/share") + rel);
        if (!from.open(QIODevice::ReadOnly))
            continue;
        const QByteArray data = from.readAll();
        QFile existing(home + rel);
        if (existing.open(QIODevice::ReadOnly) && existing.readAll() == data)
            continue;
        existing.close();
        QDir().mkpath(QFileInfo(home + rel).absolutePath());
        QSaveFile out(home + rel);
        if (out.open(QIODevice::WriteOnly) && out.write(data) == data.size())
            out.commit();
    }
}
#endif

Theme::Scheme startupScheme(int argc, char** argv)
{
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("Kaint"), QStringLiteral("Kaint"));
    settings.beginGroup(QStringLiteral("settings"));
    for (int i = 1; i < argc; ++i) {
        QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QLatin1String("--color-scheme") && i + 1 < argc)
            arg += QLatin1Char('=') + QString::fromLocal8Bit(argv[i + 1]);
        if (!arg.startsWith(QLatin1String("--color-scheme=")))
            continue;
        const QString value = arg.section(QLatin1Char('='), 1).toLower();
        const QStringList names{QStringLiteral("system"), QStringLiteral("light"), QStringLiteral("dark"),
                                QStringLiteral("kde"), QStringLiteral("custom"), QStringLiteral("windows98"),
                                QStringLiteral("blue"), QStringLiteral("windows7"), QStringLiteral("windowsxp"),
                                QStringLiteral("windows98alt"), QStringLiteral("windows7dark"), QStringLiteral("windows8"),
                                QStringLiteral("windows8dark"), QStringLiteral("windows7basic"), QStringLiteral("windows7basicdark"),
                                QStringLiteral("ubuntu"), QStringLiteral("ubuntudark"), QStringLiteral("gnome"),
                                QStringLiteral("gnomedark"), QStringLiteral("mint"), QStringLiteral("mintdark"),
                                QStringLiteral("elementary"), QStringLiteral("elementarydark")};
        settings.setValue(QStringLiteral("colorScheme"), std::max(0, int(names.indexOf(value))));
    }
    const auto scheme = Theme::Scheme(std::clamp(settings.value(QStringLiteral("colorScheme"), 0).toInt(), 0, Theme::kLastScheme));
#ifdef Q_OS_WIN
    // En Windows no hay tema nativo aparte (versiones anteriores lo tenían).
    if (scheme == Theme::Scheme::Kde)
        return Theme::Scheme::System;
#endif
    return scheme;
}

// Llama a 'next' cuando la ventana ya se ha pintado por primera vez (si no llegara a pintarse, al
// cabo de un momento).
void afterFirstPaint(QWidget* window, std::function<void()> next)
{
    class Watcher : public QObject {
    public:
        Watcher(QWidget* w, std::function<void()> next)
            : QObject(w)
            , m_next(std::move(next))
        {
            w->installEventFilter(this);
            QTimer::singleShot(1000, this, [this] { fire(); });
        }

        bool eventFilter(QObject*, QEvent* event) override
        {
            // Después de este paso del bucle de eventos, ya en la pantalla.
            if (event->type() == QEvent::Paint)
                QTimer::singleShot(0, this, [this] { fire(); });
            return false;
        }

    private:
        void fire()
        {
            if (!m_next)
                return;
            const std::function<void()> next = std::move(m_next);
            m_next = nullptr;
            deleteLater();
            next();
        }

        std::function<void()> m_next;
    };
    new Watcher(window, std::move(next));
}

} // namespace

int main(int argc, char** argv)
{
    // Captura de muestra (screenpreview.sh): sin ventana real y con la configuración de fábrica
    // en una carpeta temporal, sin tocar la del usuario.
    const bool preview = !rawOption(argc, argv, QStringLiteral("--screenpreview")).isEmpty();
    QTemporaryDir previewSettings;
    if (preview) {
        if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
            qputenv("QT_QPA_PLATFORM", "offscreen");
        const QString scale = rawOption(argc, argv, QStringLiteral("--preview-scale"));
        if (!scale.isEmpty())
            qputenv("QT_SCALE_FACTOR", scale.toLatin1());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, previewSettings.path());
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, previewSettings.path());
        QSettings preset(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("Kaint"), QStringLiteral("Kaint"));
        preset.setValue(QStringLiteral("settings/discordEnabled"), false);
        preset.setValue(QStringLiteral("settings/welcomed"), true);
        // Opciones de sí o no: los iconos de herramientas con los colores activos (de fábrica, sí),
        // las reglas y las ventanas translúcidas.
        auto yesNo = [&](const char* option, const char* key) {
            const QString value = rawOption(argc, argv, QLatin1String(option)).toLower();
            if (!value.isEmpty())
                preset.setValue(QLatin1String(key),
                                value != QLatin1String("no") && value != QLatin1String("false") && value != QLatin1String("0"));
        };
        yesNo("--preview-colored-icons", "settings/coloredToolIcons");
        yesNo("--preview-rulers", "settings/rulers");
        yesNo("--preview-translucent", "settings/translucentPanels");
        yesNo("--preview-extras", "settings/photocraftExtras");
        // Un tema personalizado (el JSON que exporta el editor de temas; con --color-scheme custom).
        const QString customTheme = rawOption(argc, argv, QStringLiteral("--preview-custom-theme"));
        if (!customTheme.isEmpty())
            preset.setValue(QStringLiteral("settings/customTheme"), customTheme.toUtf8());
        // El color de las ventanas de los temas Windows 7 (#rrggbb).
        const QColor win7Glass = QColor::fromString(rawOption(argc, argv, QStringLiteral("--preview-win7-color")));
        if (win7Glass.isValid())
            preset.setValue(QStringLiteral("settings/win7Glass"), win7Glass);
        const QColor win8Color = QColor::fromString(rawOption(argc, argv, QStringLiteral("--preview-win8-color")));
        if (win8Color.isValid())
            preset.setValue(QStringLiteral("settings/win8Color"), win8Color);
        // La herramienta elegida al empezar (su número).
        const QString tool = rawOption(argc, argv, QStringLiteral("--preview-tool"));
        if (!tool.isEmpty())
            preset.setValue(QStringLiteral("settings/startupTool"), tool.toInt());
        // Estilo de la interfaz (kaint o paintnet) e idioma (en o es).
        const QString style = rawOption(argc, argv, QStringLiteral("--preview-style")).toLower();
        if (!style.isEmpty())
            preset.setValue(QStringLiteral("settings/uiStyle"), style.contains(QLatin1String("paint")) || style == QLatin1String("pdn") ? 1 : 0);
        // Botones de la barra de título (auto, win11, win10, macos, win7, xp, win98) y su lado. Sin
        // elegir, los automáticos: los del tema (los de XP con el tema Windows XP...).
        const QString buttons = rawOption(argc, argv, QStringLiteral("--preview-title-buttons")).toLower();
        const QStringList buttonNames{QStringLiteral("win10"), QStringLiteral("win11"), QStringLiteral("macos"),
                                      QStringLiteral("win7"), QStringLiteral("xp"), QStringLiteral("win98"),
                                      QStringLiteral("auto")};
        preset.setValue(QStringLiteral("settings/titleButtons"), buttonNames.contains(buttons) ? int(buttonNames.indexOf(buttons)) : 6);
        preset.setValue(QStringLiteral("settings/titleButtonsAuto"), true);
        // En las capturas la barra de Kaint solo va si se pide (--preview-frame).
        preset.setValue(QStringLiteral("settings/customWindowFrame"), true);
        yesNo("--preview-title-left", "settings/titleButtonsLeft");
        const QString windows = rawOption(argc, argv, QStringLiteral("--preview-windows")).toLower();
        if (windows == QLatin1String("11") || windows == QLatin1String("10"))
            preset.setValue(QStringLiteral("settings/windowsVersion"), windows.toInt());
        const QString language = rawOption(argc, argv, QStringLiteral("--preview-language")).toLower().replace(QLatin1Char('-'), QLatin1Char('_'));
        if (language == QLatin1String("en") || language == QLatin1String("es"))
            preset.setValue(QStringLiteral("settings/language"), language);
        else if (language == QLatin1String("pt_br") || language == QLatin1String("pt"))
            preset.setValue(QStringLiteral("settings/language"), QStringLiteral("pt_BR"));
    }

    // Antes de crear la aplicación: salvo con el tema de KDE, el escritorio no debe imponer
    // su estilo ni su paleta.
    const Theme::Scheme scheme = startupScheme(argc, argv);
    // Sin ventana real, el tema de KDE solo llega si se pide su integración.
    if (preview && scheme == Theme::Scheme::Kde && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORMTHEME"))
        qputenv("QT_QPA_PLATFORMTHEME", "kde");
    Theme::prepare(scheme);
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    // Abrir y Guardar como con el diálogo del sistema, como Firefox: a través del portal de
    // freedesktop (sin la integración del escritorio, que Kaint deja fuera por su estilo propio,
    // Qt usaría el suyo). Sin portal, Qt vuelve al suyo; en Configuración se puede elegir siempre
    // el de Qt. Solo para Kaint: los programas que abra no heredan la variable.
    const bool portalTheme = !preview && scheme != Theme::Scheme::Kde && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORMTHEME");
    if (portalTheme)
        qputenv("QT_QPA_PLATFORMTHEME", "xdgdesktopportal");
#endif
    // Tamaño de la interfaz (Configuración, como el zum del navegador con Kaint.HTML): Qt lo toma al
    // crear la aplicación. Si ya viene de fuera (QT_SCALE_FACTOR, o las capturas), manda ese.
    bool ownScale = false;
    if (!preview && qEnvironmentVariableIsEmpty("QT_SCALE_FACTOR")) {
        const QSettings saved(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("Kaint"), QStringLiteral("Kaint"));
        const int scale = std::clamp(saved.value(QStringLiteral("settings/uiScale"), 100).toInt(), 50, 300);
        if (scale != 100) {
            qputenv("QT_SCALE_FACTOR", QByteArray::number(scale / 100.0));
            ownScale = true;
        }
    }
    QApplication::setAttribute(Qt::AA_DontUseNativeMenuBar);
    QApplication app(argc, argv);
    // (Los programas que abra Kaint no lo heredan.)
    if (ownScale)
        qunsetenv("QT_SCALE_FACTOR");
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    if (portalTheme)
        qunsetenv("QT_QPA_PLATFORMTHEME");
#endif
    QApplication::setApplicationName(QStringLiteral("Kaint"));
    QApplication::setOrganizationName(QStringLiteral("Kaint"));
    QApplication::setApplicationVersion(QStringLiteral(KAINT_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral(KAINT_APP_ID));
#if defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD)
    integrateAppImage();
#endif

    Icons::setupTheme();
    // El icono de todas las ventanas (también los diálogos), para los escritorios que lo toman de
    // la ventana (Wayland con xdg-toplevel-icon, X11).
    // El icono y el nombre del programa (los de Kaint o los propios, en Configuración).
    {
        const AppSettings program = AppSettings::load();
        Icons::setProgram(program.programIcon, program.programTitle);
    }
    QApplication::setWindowIcon(Icons::app());
    Animations::install();
    QImageReader::setAllocationLimit(4096);

    // Idioma: los textos del código están en español; el inglés (el predeterminado) es una
    // traducción incluida en el programa.
    // Un idioma personalizado ("custom:<id>", CustomLanguages) traduce lo suyo y deja el resto en
    // inglés.
    const QString setting = AppSettings::load().language;
    std::optional<CustomLanguages::Language> custom;
    if (setting.startsWith(QLatin1String("custom:")))
        custom = CustomLanguages::find(setting.mid(7));
    const QString language = custom ? QStringLiteral("en") : setting;
    qputenv("KAINT_LANG", language.toUtf8());
    QTranslator appTranslator;
    const bool english = language != QLatin1String("es") && appTranslator.load(QStringLiteral(":/i18n/kaint_%1.qm").arg(language));
    std::unique_ptr<CustomTranslator> customTranslator;
    if (custom) {
        CustomLanguages::setActive(custom);
        customTranslator = std::make_unique<CustomTranslator>(*custom, english ? &appTranslator : nullptr);
        app.installTranslator(customTranslator.get());
    } else if (english) {
        app.installTranslator(&appTranslator);
    }
    QTranslator qtTranslator;
    const QLocale qtLocale = custom && !custom->code.isEmpty() ? QLocale(custom->code) : QLocale(language);
    if (qtTranslator.load(qtLocale, QStringLiteral("qtbase"), QStringLiteral("_"), QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        app.installTranslator(&qtTranslator);

    QCommandLineParser parser;
    parser.setApplicationDescription(QObject::tr("Editor de imágenes al estilo Paint.NET para KDE Plasma"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("archivos"), QObject::tr("Imágenes que abrir."),
                                 QStringLiteral("[archivos...]"));
    parser.addOption(QCommandLineOption(QStringLiteral("color-scheme"),
                                        QObject::tr("Tema: system, light, dark, blue, kde, custom, windows8, windows8dark, windows7, "
                                                    "windows7dark, windows7basic, windows7basicdark, windowsxp, windows98, "
                                                    "windows98alt, ubuntu, ubuntudark, gnome, gnomedark, mint, mintdark, "
                                                    "elementary o elementarydark (se guarda)."),
                                        QStringLiteral("scheme")));
    parser.addOption(QCommandLineOption(QStringLiteral("screenpreview"),
                                        QObject::tr("Guarda una captura de la ventana con la configuración de fábrica "
                                                    "(la primera imagen es la actual) y sale."),
                                        QStringLiteral("archivo")));
    parser.addOption(QCommandLineOption(QStringLiteral("preview-size"),
                                        QObject::tr("Tamaño de la captura en píxeles (ancho x alto)."), QStringLiteral("WxH")));
    parser.addOption(QCommandLineOption(QStringLiteral("preview-scale"), QObject::tr("Escala de la interfaz en la captura."),
                                        QStringLiteral("factor")));
    parser.addOption(QCommandLineOption(QStringLiteral("preview-colored-icons"),
                                        QObject::tr("Iconos de herramientas con los colores activos en la captura (yes o no)."),
                                        QStringLiteral("yes|no")));
    parser.addOption(QCommandLineOption(QStringLiteral("preview-style"),
                                        QObject::tr("Estilo de la interfaz en la captura (kaint o paintnet)."),
                                        QStringLiteral("kaint|paintnet")));
    parser.addOption(QCommandLineOption(QStringLiteral("preview-language"), QObject::tr("Idioma de la captura (en, es o pt_BR)."),
                                        QStringLiteral("en|es")));
    parser.addOption(QCommandLineOption(QStringLiteral("preview-rulers"), QObject::tr("Reglas en la captura (yes o no)."),
                                        QStringLiteral("yes|no")));
    parser.addOption(QCommandLineOption(QStringLiteral("preview-translucent"),
                                        QObject::tr("Ventanas translúcidas en la captura (yes o no)."), QStringLiteral("yes|no")));
    parser.addOption(QCommandLineOption(QStringLiteral("preview-gif-editor"),
                                        QObject::tr("La primera imagen en el editor de GIF en la captura (yes o no)."),
                                        QStringLiteral("yes|no")));
    parser.addOption(QCommandLineOption(QStringLiteral("preview-extras"),
                                        QObject::tr("Las funciones de PhotoCraft en la captura (yes o no)."), QStringLiteral("yes|no")));
    parser.addOption(QCommandLineOption(QStringLiteral("preview-custom-theme"),
                                        QObject::tr("El tema personalizado de la captura (su JSON, con --color-scheme custom)."),
                                        QStringLiteral("json")));
    parser.addOption(QCommandLineOption(QStringLiteral("preview-win7-color"),
                                        QObject::tr("El color de las ventanas de los temas Windows 7 en la captura (#rrggbb)."),
                                        QStringLiteral("color")));
    parser.addOption(QCommandLineOption(QStringLiteral("preview-win8-color"),
                                        QObject::tr("El color de las ventanas de los temas Windows 8 en la captura (#rrggbb)."),
                                        QStringLiteral("color")));
    parser.addOption(QCommandLineOption(QStringLiteral("preview-tool"),
                                        QObject::tr("La herramienta elegida en la captura (su número)."), QStringLiteral("n")));
    parser.addOption(QCommandLineOption(QStringLiteral("preview-title-buttons"),
                                        QObject::tr("Botones de la barra de título en la captura (win11, win10, macos, win7, xp o win98)."),
                                        QStringLiteral("estilo")));
    parser.addOption(QCommandLineOption(QStringLiteral("preview-title-left"),
                                        QObject::tr("Botones de la barra de título a la izquierda en la captura (yes o no)."),
                                        QStringLiteral("sí/no")));
    parser.addOption(QCommandLineOption(QStringLiteral("preview-windows"),
                                        QObject::tr("Recreación de Windows en la captura (10 u 11)."), QStringLiteral("versión")));
    parser.addOption(QCommandLineOption(QStringLiteral("preview-frame"),
                                        QObject::tr("Barra de título de Windows 10 dibujada por Kaint en la captura (yes o no)."),
                                        QStringLiteral("yes|no")));
    parser.process(app);

    AppState::instance()->load();
    Theme::captureSystemDefaults();
    Theme::initStyle(scheme);
    const AppSettings startup = AppSettings::load();
    Theme::setLook(Theme::Look(startup.uiStyle));
    Theme::setUiFont(startup.uiFontFamily, startup.uiFontSize);
    // La barra de título dibujada por Kaint con los botones elegidos, si se quiere (por defecto en
    // Linux sí y en Windows no; en las capturas de muestra, solo si se pide).
    const QString previewFrame = parser.value(QStringLiteral("preview-frame")).toLower();
    const bool frame = startup.customFrame && scheme != Theme::Scheme::Kde
                       && (!preview || previewFrame == QLatin1String("yes") || previewFrame == QLatin1String("true"));
    // El color propio de la barra superior (en Windows, solo con la barra de título de Kaint).
#ifdef Q_OS_WIN
    Theme::setCustomTitleBarColor(frame ? startup.titleBarColor : QColor());
#else
    Theme::setCustomTitleBarColor(startup.titleBarColor);
#endif
    Theme::setAeroTransparency(startup.aeroTransparency);
    Theme::setWin7Glass(startup.win7Glass);
    Theme::setWin8Color(startup.win8Color);
    Theme::setSelectionColors(startup.selectionColorLight, startup.selectionColorDark);
    Theme::apply(scheme);
    WindowFrame::setButtons(WindowFrame::Buttons(std::clamp(startup.titleButtons, 0, 8)));
    // Automáticos con los temas sin botones propios (Claro, Oscuro, Azul...): los de Windows 10.
    WindowFrame::setAutoFallback(WindowFrame::Buttons::Windows10);
    WindowFrame::setButtonsOnLeft(startup.titleButtonsLeft);
    WindowFrame::setPanelClose7(WindowFrame::PanelClose7(std::clamp(startup.win7PanelClose, 0, 2)));
    WindowFrame::setAeroExtended(startup.win7ExtendedGlass);
    // Recreación de Windows 11 (estilo Paint.NET) y temas de Windows 7: esquinas redondeadas en
    // modo ventana.
    WindowFrame::setRoundedCorners(frame && startup.roundedWindows());
    WindowFrame::setEnabled(frame);

    if (preview) {
        const QStringList size = parser.value(QStringLiteral("preview-size")).toLower().split(QLatin1Char('x'));
        const double scale = std::max(1.0, parser.value(QStringLiteral("preview-scale")).toDouble());
        QSize pixels(1600, 900);
        if (size.size() == 2 && size[0].toInt() > 0 && size[1].toInt() > 0)
            pixels = QSize(size[0].toInt(), size[1].toInt());
        MainWindow window;
        window.resize(QSize(int(std::lround(pixels.width() / scale)), int(std::lround(pixels.height() / scale))));
        window.show();
        const QString gif = parser.value(QStringLiteral("preview-gif-editor")).toLower();
        const QString error = window.screenPreview(parser.positionalArguments(), parser.value(QStringLiteral("screenpreview")),
                                                   gif == QLatin1String("yes") || gif == QLatin1String("sí") || gif == QLatin1String("si"));
        if (!error.isEmpty()) {
            std::fprintf(stderr, "%s\n", qPrintable(error));
            return 1;
        }
        return 0;
    }

    MainWindow window;
    window.show();
    // La intro (la primera vez o, si se pide, siempre); la bienvenida de la primera vez, después.
    auto introDone = std::make_shared<bool>(false);
    auto welcomePending = std::make_shared<bool>(false);
    Intro::start(&window, startup.introAlways, startup.introPath, [&window, introDone, welcomePending] {
        *introDone = true;
        if (*welcomePending)
            QTimer::singleShot(0, &window, &MainWindow::showWelcome);
    });
    // Como Paint.NET: primero se ve lo fundamental (la ventana con sus barras y las ventanas de
    // herramientas) y, ya pintada, se carga lo demás: los complementos (sus efectos y formatos
    // aparecen en los menús al terminar), las imágenes que abrir y los avisos.
    QStringList files = parser.positionalArguments();
    // Discord abre Kaint con "discord-<ID>://…" al aceptar una invitación: no es un archivo (la
    // invitación llega después por el IPC de Discord).
    files.removeIf([](const QString& f) { return f.startsWith(QLatin1String("discord-"), Qt::CaseInsensitive) && f.contains(QLatin1String("://")); });
    afterFirstPaint(&window, [&window, files, introDone, welcomePending] {
        PluginManager::instance()->scan();
        if (files.isEmpty())
            window.restoreSessionOrNew();
        else
            window.openFiles(files);
        window.startupChecks();
        if (!AppSettings::load().welcomed) {
            if (*introDone)
                QTimer::singleShot(0, &window, &MainWindow::showWelcome);
            else
                *welcomePending = true;
        }
    });
    const int code = app.exec();
    if (window.restartRequested())
        QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                QSettings().value(QStringLiteral("session/files")).toStringList());
    return code;
}
