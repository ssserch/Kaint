#include "app/Animations.h"
#include "app/AppState.h"
#include "app/Icons.h"
#include "app/MainWindow.h"
#include "app/Theme.h"
#include "app/WindowFrame.h"
#include "app/dialogs/SettingsDialog.h"
#include "core/plugins/PluginManager.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QImageReader>
#include <QLibraryInfo>
#include <QLocale>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>

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
                                QStringLiteral("windows98alt"), QStringLiteral("windows7dark")};
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
        // Estilo de la interfaz (kaint o paintnet) e idioma (en o es).
        const QString style = rawOption(argc, argv, QStringLiteral("--preview-style")).toLower();
        if (!style.isEmpty())
            preset.setValue(QStringLiteral("settings/uiStyle"), style.contains(QLatin1String("paint")) || style == QLatin1String("pdn") ? 1 : 0);
        // Botones de la barra de título (win11, win10, macos, win7, xp, win98) y su lado.
        const QString buttons = rawOption(argc, argv, QStringLiteral("--preview-title-buttons")).toLower();
        const QStringList buttonNames{QStringLiteral("win10"), QStringLiteral("win11"), QStringLiteral("macos"),
                                      QStringLiteral("win7"), QStringLiteral("xp"), QStringLiteral("win98")};
        if (buttonNames.contains(buttons))
            preset.setValue(QStringLiteral("settings/titleButtons"), int(buttonNames.indexOf(buttons)));
        // En las capturas la barra de Kaint solo va si se pide (--preview-frame).
        preset.setValue(QStringLiteral("settings/customWindowFrame"), true);
        yesNo("--preview-title-left", "settings/titleButtonsLeft");
        const QString windows = rawOption(argc, argv, QStringLiteral("--preview-windows")).toLower();
        if (windows == QLatin1String("11") || windows == QLatin1String("10"))
            preset.setValue(QStringLiteral("settings/windowsVersion"), windows.toInt());
        const QString language = rawOption(argc, argv, QStringLiteral("--preview-language")).toLower();
        if (language == QLatin1String("en") || language == QLatin1String("es"))
            preset.setValue(QStringLiteral("settings/language"), language);
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
    QApplication::setAttribute(Qt::AA_DontUseNativeMenuBar);
    QApplication app(argc, argv);
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    if (portalTheme)
        qunsetenv("QT_QPA_PLATFORMTHEME");
#endif
    QApplication::setApplicationName(QStringLiteral("Kaint"));
    QApplication::setOrganizationName(QStringLiteral("Kaint"));
    QApplication::setApplicationVersion(QStringLiteral(KAINT_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral(KAINT_APP_ID));

    Icons::setupTheme();
    Animations::install();
    QImageReader::setAllocationLimit(4096);

    // Idioma: los textos del código están en español; el inglés (el predeterminado) es una
    // traducción incluida en el programa.
    const QString language = AppSettings::load().language;
    qputenv("KAINT_LANG", language.toUtf8());
    QTranslator appTranslator;
    if (language != QLatin1String("es") && appTranslator.load(QStringLiteral(":/i18n/kaint_%1.qm").arg(language)))
        app.installTranslator(&appTranslator);
    QTranslator qtTranslator;
    if (qtTranslator.load(QLocale(language), QStringLiteral("qtbase"), QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        app.installTranslator(&qtTranslator);

    QCommandLineParser parser;
    parser.setApplicationDescription(QObject::tr("Editor de imágenes al estilo Paint.NET para KDE Plasma"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("archivos"), QObject::tr("Imágenes que abrir."),
                                 QStringLiteral("[archivos...]"));
    parser.addOption(QCommandLineOption(QStringLiteral("color-scheme"),
                                        QObject::tr("Tema: system, light, dark, blue, kde, custom, windows7, windows7dark, windowsxp, "
                                                    "windows98 o windows98alt (se guarda)."),
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
    parser.addOption(QCommandLineOption(QStringLiteral("preview-language"), QObject::tr("Idioma de la captura (en o es)."),
                                        QStringLiteral("en|es")));
    parser.addOption(QCommandLineOption(QStringLiteral("preview-rulers"), QObject::tr("Reglas en la captura (yes o no)."),
                                        QStringLiteral("yes|no")));
    parser.addOption(QCommandLineOption(QStringLiteral("preview-translucent"),
                                        QObject::tr("Ventanas translúcidas en la captura (yes o no)."), QStringLiteral("yes|no")));
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
    Theme::setSelectionColors(startup.selectionColorLight, startup.selectionColorDark);
    Theme::apply(scheme);
    WindowFrame::setButtons(WindowFrame::Buttons(std::clamp(startup.titleButtons, 0, 5)));
    WindowFrame::setButtonsOnLeft(startup.titleButtonsLeft);
    WindowFrame::setPanelClose7(WindowFrame::PanelClose7(std::clamp(startup.win7PanelClose, 0, 2)));
    // Recreación de Windows 11 (estilo Paint.NET): esquinas redondeadas en modo ventana.
    WindowFrame::setRoundedCorners(frame && Theme::paintNetLook() && startup.windowsVersion == 11);
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
        const QString error = window.screenPreview(parser.positionalArguments(), parser.value(QStringLiteral("screenpreview")));
        if (!error.isEmpty()) {
            std::fprintf(stderr, "%s\n", qPrintable(error));
            return 1;
        }
        return 0;
    }

    MainWindow window;
    window.show();
    // Como Paint.NET: primero se ve lo fundamental (la ventana con sus barras y las ventanas de
    // herramientas) y, ya pintada, se carga lo demás: los complementos (sus efectos y formatos
    // aparecen en los menús al terminar), las imágenes que abrir y los avisos.
    const QStringList files = parser.positionalArguments();
    afterFirstPaint(&window, [&window, files] {
        PluginManager::instance()->scan();
        if (files.isEmpty())
            window.restoreSessionOrNew();
        else
            window.openFiles(files);
        window.startupChecks();
        if (!AppSettings::load().welcomed)
            QTimer::singleShot(0, &window, &MainWindow::showWelcome);
    });
    const int code = app.exec();
    if (window.restartRequested())
        QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                QSettings().value(QStringLiteral("session/files")).toStringList());
    return code;
}
