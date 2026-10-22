#include "app/AppState.h"
#include "app/Icons.h"
#include "app/MainWindow.h"
#include "app/Theme.h"
#include "app/dialogs/SettingsDialog.h"
#include "core/plugins/PluginManager.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QImageReader>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Kaint"));
    QApplication::setOrganizationName(QStringLiteral("Kaint"));
    QApplication::setApplicationVersion(QStringLiteral(KAINT_VERSION));
    // En Wayland es el app_id: KDE lo usa para el icono, la barra de tareas y las reglas de ventana.
    QGuiApplication::setDesktopFileName(QStringLiteral("io.github.kaint.Kaint"));

    Icons::setupTheme();
    // Qt limita por defecto a 256 MB la lectura de imágenes; un editor tiene que poder abrir fotos enormes.
    QImageReader::setAllocationLimit(4096);

    // Traducciones de los textos propios de Qt (diálogos estándar), si están instaladas.
    QTranslator qtTranslator;
    if (qtTranslator.load(QLocale(), QStringLiteral("qtbase"), QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        app.installTranslator(&qtTranslator);

    QCommandLineParser parser;
    parser.setApplicationDescription(QObject::tr("Editor de imágenes al estilo Paint.NET para KDE Plasma"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("archivos"), QObject::tr("Imágenes que abrir."),
                                 QStringLiteral("[archivos...]"));
    parser.process(app);

    AppState::instance()->load();
    // Combinación de colores antes de crear la ventana (Configuración > Interfaz de usuario).
    Theme::captureSystemDefaults();
    Theme::apply(Theme::Scheme(AppSettings::load().colorScheme));
    // Complementos activados: sus efectos aparecen en los menús.
    PluginManager::instance()->scan();

    MainWindow window;
    window.show();
    const QStringList files = parser.positionalArguments();
    if (files.isEmpty())
        window.restoreSessionOrNew();
    else
        window.openFiles(files);
    window.startupChecks();
    return app.exec();
}
