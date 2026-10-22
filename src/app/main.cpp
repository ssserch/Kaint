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
#include <QTranslator>

#include <algorithm>

namespace {

// Tema guardado o el de --color-scheme (system, light, dark, kde o custom), que también se
// guarda: así se puede recuperar el tema sin abrir la configuración.
Theme::Scheme startupScheme(int argc, char** argv)
{
    QSettings settings(QStringLiteral("Kaint"), QStringLiteral("Kaint"));
    settings.beginGroup(QStringLiteral("settings"));
    for (int i = 1; i < argc; ++i) {
        QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QLatin1String("--color-scheme") && i + 1 < argc)
            arg += QLatin1Char('=') + QString::fromLocal8Bit(argv[i + 1]);
        if (!arg.startsWith(QLatin1String("--color-scheme=")))
            continue;
        const QString value = arg.section(QLatin1Char('='), 1).toLower();
        const QStringList names{QStringLiteral("system"), QStringLiteral("light"), QStringLiteral("dark"),
                                QStringLiteral("kde"), QStringLiteral("custom")};
        settings.setValue(QStringLiteral("colorScheme"), std::max(0, int(names.indexOf(value))));
    }
    return Theme::Scheme(std::clamp(settings.value(QStringLiteral("colorScheme"), 0).toInt(), 0, 4));
}

} // namespace

int main(int argc, char** argv)
{
    // Antes de crear la aplicación: salvo con el tema de KDE, el escritorio no debe imponer
    // su estilo ni su paleta.
    const Theme::Scheme scheme = startupScheme(argc, argv);
    Theme::prepare(scheme);
    QApplication::setAttribute(Qt::AA_DontUseNativeMenuBar);
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Kaint"));
    QApplication::setOrganizationName(QStringLiteral("Kaint"));
    QApplication::setApplicationVersion(QStringLiteral(KAINT_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral("io.github.kaint.Kaint"));

    Icons::setupTheme();
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
    parser.process(app);

    AppState::instance()->load();
    Theme::captureSystemDefaults();
    Theme::initStyle(scheme);
    Theme::apply(scheme);
    PluginManager::instance()->scan();

    MainWindow window;
    window.show();
    const QStringList files = parser.positionalArguments();
    if (files.isEmpty())
        window.restoreSessionOrNew();
    else
        window.openFiles(files);
    window.startupChecks();
    const int code = app.exec();
    if (window.restartRequested())
        QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                QSettings().value(QStringLiteral("session/files")).toStringList());
    return code;
}
