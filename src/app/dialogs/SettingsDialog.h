#pragma once

#include <QColor>
#include <QDateTime>
#include <QDialog>

class ColorButton;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QTreeWidget;
class ToolManager;
class ToolOptionsPanel;

// Preferencias de la aplicación (se guardan con QSettings).
struct AppSettings {
    // Interfaz de usuario
    bool animations = true;
    bool translucentPanels = true;
    bool overscroll = true;
    bool autoScroll = true;
    bool autoSelectVisibleLayer = false;
    bool coloredToolIcons = true;
    // Abrir y Guardar como con el diálogo del sistema (el de Firefox y los demás programas) o con
    // el de Qt.
    bool systemFileDialogs = true;
    // Cursores con una miniatura al lado (selección, degradado, línea, texto y formas): la flecha
    // del tema de cursores del sistema o la de Kaint.
    bool systemPointer = true;
    // Capas: al arrastrar una, se mueve en la lista con el ratón; con esto, se queda en su sitio
    // y una línea marca dónde quedará.
    bool layerDropLine = false;
    // Botones de ir al principio y al final del historial, junto a deshacer y rehacer.
    bool historyJumpButtons = false;
    // Colores ampliado: también los colores semitransparentes de la paleta (las filas de abajo de
    // la de fábrica, como en Paint.NET). El blanco de relleno de las paletas cortas nunca se ve.
    bool paletteTranslucent = true;
    // Ventanas flotantes: 0, como las de paint.js (Windows 10); 1, compactas. Grosor del borde
    // en píxeles.
    int panelStyle = 0;
    int panelBorder = 1;
    // Las ventanas flotantes se acoplan a los bordes del área de trabajo y a las demás ventanas al
    // arrastrarlas cerca.
    bool snapPanels = true;
    // Fuente de la interfaz (familia vacía o tamaño 0: los del sistema o del tema).
    QString uiFontFamily;
    int uiFontSize = 0;
    // Linux: color de la barra superior y de las cabeceras de las ventanas, como el de énfasis de
    // Windows (no válido: el del tema).
    QColor titleBarColor;
    // Linux, con el estilo de Paint.NET: la ventana principal y los diálogos con su propia barra de
    // título, como las de Windows 10, en vez de la del escritorio.
    bool windows10Frame = true;
    // Aspecto de la interfaz: 0, el de Kaint; 1, la recreación de Paint.NET (Theme::Look).
    int uiStyle = 1;
    // Cursores de las herramientas: 0, los de Kaint; 1, los de Paint.NET en pixel art
    // (Icons::CursorStyle).
    int cursorStyle = 1;
    // Se mostró la bienvenida del primer inicio.
    bool welcomed = false;
    // Subrayar siempre las letras de acceso de los menús (si no, solo con Alt).
    bool menuMnemonics = false;
    int colorScheme = 0;
    QString language = QStringLiteral("en");
    bool reopenLast = false;
    int autosaveMinutes = 5;
    int unit = 0;
    bool rulers = false;
    // Ruler::Style: 0, según el tema; 1, del color de la barra de herramientas; 2, oscura, como la
    // de paint.js (antes el 0 era la oscura).
    int rulerStyle = 0;
    bool dropShadow = true;
    // Color propio del fondo alrededor de la imagen, uno para los temas claros y otro para los
    // oscuros (no válido: el del tema).
    QColor canvasBorderLight;
    QColor canvasBorderDark;
    int checkerBrightness = 191;
    int startTool = -1;
    // Herramientas Mover: ocultar contorno y tiradores al arrastrar; pegar y mover reemplazando
    // los píxeles de debajo (también los transparentes). Ambas como Paint.NET.
    bool hideMoveBox = true;
    bool replacePixels = true;
    // Al cerrar con varias imágenes sin guardar: una sola pregunta para todas (como Paint.NET)
    // o una por imagen.
    bool askOnceOnClose = true;
    bool tabletEraser = true;
    int historyLimitMB = 4096;
    bool checkUpdates = true;
    bool updatesBeta = false;
    // Avisar con una ventana cuando la búsqueda automática encuentra una versión nueva (se quita
    // con "No volver a recordar" en el propio aviso).
    bool updateReminder = true;
    QDateTime lastUpdateCheck;
    QString latestVersion;
    bool discordEnabled = true;
    bool discordShowFile = true;
    bool discordShowLayer = true;
    bool discordShowFileInfo = true;
    bool discordShowTool = true;
    bool discordShowElapsed = true;

    static AppSettings load();
    void save() const;
};

// Configuración (Alt+X), como el diálogo de Paint.NET 5: secciones con icono a la izquierda,
// opciones a la derecha y los cambios se aplican al momento.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    enum class Page {
        Interface,
        Canvas,
        Tools,
        Tablet,
        Performance,
        Plugins,
        Discord,
        Updates,
        Diagnostics,
        IconPacks,
        Fonts,
        Shapes,
        Keyboard
    };

    SettingsDialog(const AppSettings& current, ToolManager* tools, QWidget* parent = nullptr);
    AppSettings settings() const { return m_settings; }
    void showPage(Page page);

    // Se aceptó reiniciar Kaint para aplicar el tema KDE (o salir de él) o el idioma; settings()
    // tiene los valores nuevos.
    bool restartRequested() const { return m_restartRequested; }

    // Texto de la página Diagnósticos (también se copia al portapapeles).
    static QString diagnostics();
    void setDiscordStatus(const QString& text);

signals:
    void settingsChanged(const AppSettings& settings);
    void checkUpdatesRequested();

private:
    QWidget* createInterfacePage();
    QWidget* createCanvasPage();
    QWidget* createToolsPage(ToolManager* tools);
    QWidget* createTabletPage();
    QWidget* createPerformancePage();
    QWidget* createPluginsPage();
    QWidget* createDiscordPage();
    QWidget* createUpdatesPage();
    QWidget* createDiagnosticsPage();
    QWidget* createIconPacksPage();
    QWidget* createFontsPage();
    QWidget* createShapesPage();
    QWidget* createKeyboardPage();
    void refreshIconPacks();
    void refreshFonts();
    void refreshShapes();
    void refreshShortcuts();
    void refreshPlugins();
    void showPluginDetails();
    void addPlugins();
    void changed();
    bool askRestart(const QString& question, const QString& restartText, const QString& cancelText);

    AppSettings m_settings;
    QListWidget* m_sections;
    QStackedWidget* m_pages;
    QTreeWidget* m_pluginList = nullptr;
    QPlainTextEdit* m_pluginDetails = nullptr;
    QCheckBox* m_showIncompatible = nullptr;
    QPushButton* m_openPluginFolder = nullptr;
    QLabel* m_updateStatus = nullptr;
    QLabel* m_discordStatus = nullptr;
    ToolOptionsPanel* m_toolDefaults = nullptr;
    QComboBox* m_iconPackChoice = nullptr;
    QListWidget* m_iconPacks = nullptr;
    QLabel* m_iconPackDetails = nullptr;
    QListWidget* m_fonts = nullptr;
    QListWidget* m_shapes = nullptr;
    class QLineEdit* m_shortcutSearch = nullptr;
    QTreeWidget* m_shortcuts = nullptr;
    QLabel* m_shortcutStatus = nullptr;
    bool m_restartRequested = false;
};
