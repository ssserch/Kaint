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
    bool translucentPanels = false;
    bool overscroll = true;        // desplazamiento más allá del borde de la imagen
    bool autoScroll = true;        // desplazar la vista al dibujar junto al borde de la ventana
    bool autoSelectVisibleLayer = false; // al ocultar una capa, activar la visible más cercana
    int colorScheme = 0;           // Theme::Scheme: seguir al sistema, claro u oscuro
    bool reopenLast = false;       // volver a abrir las imágenes de la sesión anterior
    int unit = 0;                  // Unit: píxeles, pulgadas o centímetros (menú Ver)
    bool rulers = false;
    // Lienzo
    bool dropShadow = true;
    QColor canvasBorder;           // inválido = color de la paleta del sistema
    int checkerBrightness = 255;
    // Herramientas
    int startTool = -1;            // -1 = la última usada
    // Lápiz y tableta
    bool tabletEraser = true;      // el extremo borrador del lápiz usa la Goma
    // Rendimiento
    int historyLimitMB = 4096;     // memoria máxima del historial (0 = sin límite)
    // Actualizaciones
    bool checkUpdates = true;
    bool updatesBeta = false;
    QDateTime lastUpdateCheck;
    QString latestVersion;
    // Discord (Rich Presence)
    bool discordEnabled = true;
    bool discordShowFile = true;
    bool discordShowLayer = true;
    bool discordShowFileInfo = true; // tipo de archivo y tamaño
    bool discordShowTool = true;
    bool discordShowElapsed = true;

    static AppSettings load();
    void save() const;
};

// Configuración (Alt+X), como el diálogo de Paint.NET 5: secciones con icono
// a la izquierda, opciones a la derecha y los cambios se aplican al momento.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    enum class Page { Interface, Canvas, Tools, Tablet, Performance, Plugins, Discord, Updates, Diagnostics };

    SettingsDialog(const AppSettings& current, ToolManager* tools, QWidget* parent = nullptr);
    AppSettings settings() const { return m_settings; }
    void showPage(Page page);

    // Texto de la página Diagnósticos (también se copia al portapapeles).
    static QString diagnostics();
    // Estado de la conexión con Discord (lo actualiza la ventana principal).
    void setDiscordStatus(const QString& text);

signals:
    // Se cambió alguna opción: la ventana la aplica y la guarda.
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
    void refreshPlugins();
    void showPluginDetails();
    void addPlugins();
    void changed();

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
};
