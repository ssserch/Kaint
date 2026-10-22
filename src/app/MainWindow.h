#pragma once

#include "app/DiscordPresence.h"
#include "app/dialogs/SettingsDialog.h"
#include "core/ImageIO.h"
#include "core/Selection.h"
#include "core/effects/Effect.h"

#include <QHash>
#include <QMainWindow>
#include <QSet>
#include <QTimer>

#include <memory>

class QPrinter;

#include <functional>
#include <initializer_list>

class Canvas;
class ColorsPanel;
class Document;
class FloatingPanel;
class HistoryPanel;
class LayersPanel;
class QComboBox;
class QLabel;
class QMenu;
class QMenuBar;
class QProgressBar;
class QSlider;
class QTabBar;
class QToolButton;
class QUndoCommand;
class QUndoGroup;
class ToolManager;
class ToolOptionsBar;
class Workspace;
struct DocumentState;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    ~MainWindow() override;

    // Dibuja la imagen en la página (ajustada a ella o a su tamaño real según los PPP).
    static void renderPrint(QPrinter* printer, const QImage& image, double dpi, bool actualSize);

    void openFiles(const QStringList& paths);
    void newDefaultDocument();
    void restoreSessionOrNew();
    void startupChecks();

protected:
    void changeEvent(QEvent* e) override;
    void closeEvent(QCloseEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    QAction* makeAction(const QString& text, std::initializer_list<const char*> icons, const QKeySequence& key,
                        std::function<void()> fn, QList<QAction*>* group = nullptr);
    void createTopRow();
    void createActions();
    void createMenus();
    void rebuildEffectMenus();
    void createToolBars();
    void createPanels();
    void createStatusBar();
    void rebuildRecentMenu();

    // Documentos
    void addDocument(Document* doc);
    void setCurrentDocument(Document* doc, bool force = false);
    bool closeDocument(Document* doc);
    bool maybeSave(Document* doc);
    void updateTab(Document* doc);
    void updateTitle();
    void updateActions();
    void updateStatus();

    // Archivo
    void newImage();
    void open();
    void acquireFromScanner();
    bool save(Document* doc);
    bool saveAs(Document* doc);
    bool saveAll();
    bool writeFile(Document* doc, const QString& path);
    void print();
    bool closeAll(Document* except = nullptr);
    void switchImage(int delta);
    void moveImageTab(int delta);
    void showTabMenu(const QPoint& pos);

    // Edición
    void undo();
    void redo();
    void cut();
    void copy(bool merged);
    void paste();
    void pasteIntoNewLayer();
    void pasteIntoNewImage();
    void selectAll();
    void deselect();
    void copySelection();
    void pasteSelection(SelectionMode mode);
    void invertSelection();
    void eraseSelection(const QString& text);
    void fillSelection();
    QImage clipboardImage() const;
    bool ensureCanvasFits(const QImage& image, const QString& title, bool importing = false);

    // Imagen 'dpi' > 0 cambia también la resolución del documento.
    void transformDocument(const QString& text, const QString& icon, QSize newSize,
                           const std::function<QImage(const QImage&, int)>& fn, double dpi = 0);
    void cropToSelection();
    void resizeImage();
    void resizeCanvas();
    void resizeCanvasTo(QSize size, Qt::Alignment anchor, const QString& text, double dpi = 0);
    void flipImage(bool horizontal);
    void rotateImage(int degrees);
    void colorProfile();
    void editAnimation();
    QPrinter* printer();
    void pageSetup();
    void flatten();

    // Capas
    void addLayer();
    void deleteLayer();
    void duplicateLayer();
    void mergeLayerDown();
    void importLayer();
    void flipLayer(bool horizontal);
    void rotateLayer180();
    void moveLayer(int delta);
    void moveLayerTo(int from, int to);
    void selectLayer(int index);
    void layerProperties();
    void toggleLayerVisibility(int index);
    void soloLayer(int index);
    void rotateZoomLayer();

    // Efectos
    void runEffect(const Effect* effect, bool repeat);
    EffectContext effectContext() const;

    void push(QUndoCommand* cmd, const QString& icon);
    void finishTool();

    // Configuración
    void showSettings(SettingsDialog::Page page = SettingsDialog::Page::Interface);
    void applySettings();
    void applyThemeLayout();
    void keepOpaque();
    void setUnit(int unit);
    void checkForUpdates(bool interactive);
    void checkHistoryMemory();
    PresenceInfo presenceInfo() const;
    void updatePresence();

    Canvas* m_canvas;
    Workspace* m_workspace;
    ToolManager* m_tools;
    ToolOptionsBar* m_options = nullptr;
    QTabBar* m_tabs = nullptr;
    QUndoGroup* m_undoGroup;

    QList<Document*> m_docs;
    Document* m_doc = nullptr;

    FloatingPanel* m_toolsPanel = nullptr;
    FloatingPanel* m_historyPanel = nullptr;
    FloatingPanel* m_layersPanel = nullptr;
    FloatingPanel* m_colorsPanel = nullptr;
    HistoryPanel* m_history = nullptr;
    LayersPanel* m_layers = nullptr;
    ColorsPanel* m_colors = nullptr;
    int m_colorsCompactWidth = 0;

    QMenuBar* m_menuBar = nullptr;
    QMenu* m_adjustmentsMenu = nullptr;
    QMenu* m_effectsMenu = nullptr;
    QList<QAction*> m_effectActions;
    QList<QMenu*> m_effectSubmenus;
    QWidget* m_cornerButtons = nullptr;
    class QHBoxLayout* m_cornerLayout = nullptr;

    QLabel* m_hint = nullptr;
    QLabel* m_posLabel = nullptr;
    QLabel* m_selLabel = nullptr;
    QComboBox* m_zoomBox = nullptr;
    QSlider* m_zoomSlider = nullptr;
    QToolButton* m_unitButton = nullptr;
    QProgressBar* m_progress = nullptr;
    AppSettings m_settings;

    QList<QAction*> m_docActions;
    QList<QAction*> m_selActions;
    QHash<QString, QAction*> m_act;
    QMenu* m_recentMenu = nullptr;

    const Effect* m_lastEffect = nullptr;
    QHash<QString, ParamValues> m_effectValues;
    QHash<QString, ImageIO::SaveOptions> m_saveOptions;
    QString m_lastDir;

    QTimer m_thumbTimer;
    QSet<Document*> m_thumbDirty;
    QTimer m_historyTimer;
    DiscordPresence* m_discord = nullptr;
    std::unique_ptr<QPrinter> m_printer;
    bool m_printActualSize = false;
    QTimer m_presenceTimer;
    int m_toolBeforeEraser = -1;
};
