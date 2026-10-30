#include "app/Animations.h"
#include "app/AppState.h"
#include "app/Icons.h"
#include "app/MainWindow.h"
#include "app/Theme.h"
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

// Tema guardado o el de --color-scheme (system, light, dark, kde o custom), que también se
// guarda: así se puede recuperar el tema sin abrir la configuración.
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
                                QStringLiteral("blue")};
        settings.setValue(QStringLiteral("colorScheme"), std::max(0, int(names.indexOf(value))));
    }
    const auto scheme = Theme::Scheme(std::clamp(settings.value(QStringLiteral("colorScheme"), 0).toInt(), 0, 6));
#ifdef Q_OS_WIN
    // En Windows no hay tema nativo aparte (versiones anteriores lo tenían).
    if (scheme == Theme::Scheme::Kde)
        return Theme::Scheme::System;
#endif
    return scheme;
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
                                        QObject::tr("Tema: system, light, dark, kde o custom (se guarda)."),
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
    parser.process(app);

    AppState::instance()->load();
    Theme::captureSystemDefaults();
    Theme::initStyle(scheme);
    Theme::setLook(Theme::Look(AppSettings::load().uiStyle));
    Theme::apply(scheme);

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

    PluginManager::instance()->scan();

    MainWindow window;
    window.show();
    const QStringList files = parser.positionalArguments();
    if (files.isEmpty())
        window.restoreSessionOrNew();
    else
        window.openFiles(files);
    window.startupChecks();
    if (!AppSettings::load().welcomed)
        QTimer::singleShot(0, &window, &MainWindow::showWelcome);
    const int code = app.exec();
    if (window.restartRequested())
        QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                QSettings().value(QStringLiteral("session/files")).toStringList());
    return code;
}
