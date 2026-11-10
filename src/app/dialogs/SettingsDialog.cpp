#include "app/dialogs/SettingsDialog.h"

#include "app/AppState.h"
#include "app/Customization.h"
#include "app/DiscordPresence.h"
#include "app/IconPacks.h"
#include "app/Icons.h"
#include "app/Ruler.h"
#include "app/Shortcuts.h"
#include "app/Theme.h"
#include "app/ToolOptionsBar.h"
#include "app/UserFonts.h"
#include "app/WindowFrame.h"
#include "app/dialogs/DialogExec.h"
#include "app/dialogs/TaskDialog.h"
#include "app/dialogs/ThemeDialog.h"
#include "app/tools/ToolManager.h"
#include "core/effects/Effect.h"
#include "app/widgets/ColorWidgets.h"
#include "app/widgets/EffectControls.h"
#include "app/widgets/UnitField.h"
#include "core/CustomShapes.h"
#include "app/dialogs/PluginCatalogDialog.h"
#include "core/plugins/PluginManager.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCursor>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QMenu>
#include <QImageReader>
#include <QImageWriter>
#include <QInputDevice>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMap>
#include <QLocale>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyle>
#include <QSysInfo>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <memory>

namespace {

// La barra de título de Kaint (y, en Linux, las esquinas redondeadas de Windows 11, que necesitan
// las ventanas translúcidas desde que se crean) se pone o se quita al abrir Kaint: true si lo
// elegido no es lo que hay ahora. Con el tema KDE, siempre la del escritorio. En Windows las
// esquinas las pone el propio Windows 11 al momento.
bool frameNeedsRestart(const AppSettings& s)
{
    const bool frame = s.customFrame && s.colorScheme != int(Theme::Scheme::Kde);
#ifdef Q_OS_WIN
    const bool rounded = false;
#else
    const bool rounded = frame && s.uiStyle == 1 && s.windowsVersion == 11;
#endif
    return frame != WindowFrame::enabled() || rounded != WindowFrame::roundedCorners();
}

} // namespace

AppSettings AppSettings::load()
{
    QSettings s;
    s.beginGroup(QStringLiteral("settings"));
    AppSettings a;
    auto get = [&](const char* key, auto def) { return s.value(QLatin1String(key), def).template value<decltype(def)>(); };
    a.animations = get("animations", a.animations);
    a.windowAnimation = get("windowAnimation", a.windowAnimation);
    a.windowAnimationMs = std::clamp(get("windowAnimationMs", a.windowAnimationMs), 50, 2000);
    a.aeroTransparency = get("aeroTransparency", a.aeroTransparency);
    a.translucentPanels = get("translucentPanels", a.translucentPanels);
    a.translucentOverImage = get("translucentOverImage", a.translucentOverImage);
    a.overscroll = get("overscroll", a.overscroll);
    a.autoScroll = get("autoScroll", a.autoScroll);
    a.autoSelectVisibleLayer = get("autoSelectVisibleLayer", a.autoSelectVisibleLayer);
    a.coloredToolIcons = get("coloredToolIcons", a.coloredToolIcons);
    a.systemFileDialogs = get("systemFileDialogs", a.systemFileDialogs);
    a.systemPointer = get("systemPointer", a.systemPointer);
    a.layerDropLine = get("layerDropLine", a.layerDropLine);
    a.historyJumpButtons = get("historyJumpButtons", a.historyJumpButtons);
    a.pluginMenuIcons = get("pluginMenuIcons", a.pluginMenuIcons);
    a.kaintHtmlPlugins = get("kaintHtmlPlugins", a.kaintHtmlPlugins);
    a.paletteTranslucent = get("paletteTranslucent", a.paletteTranslucent);
    a.panelStyle = std::clamp(get("panelStyle", a.panelStyle), 0, 1);
    a.panelBorder = std::clamp(get("panelBorder", a.panelBorder), 1, 12);
    a.snapPanels = get("snapPanels", a.snapPanels);
    a.uiFontFamily = get("uiFontFamily", a.uiFontFamily);
    a.uiFontSize = std::clamp(get("uiFontSize", a.uiFontSize), 0, 24);
    a.titleBarColor = s.value(QStringLiteral("titleBarColor"), QColor()).value<QColor>();
    a.uiStyle = std::clamp(get("uiStyle", a.uiStyle), 0, 1);
    a.windowsVersion = get("windowsVersion", a.windowsVersion) == 11 ? 11 : 10;
    // La barra de título de Kaint o la del sistema. Antes no había casilla: los botones -1 («Los del
    // escritorio») eran la del sistema; sin nada elegido, lo de cada sistema.
    const int defaultButtons = a.windowsVersion == 11 ? 1 : 0;
    const int buttons = s.contains(QStringLiteral("titleButtons")) ? std::clamp(get("titleButtons", defaultButtons), -1, 5)
                                                                   : defaultButtons;
    if (s.contains(QStringLiteral("customWindowFrame")))
        a.customFrame = get("customWindowFrame", a.customFrame);
    else if (buttons < 0)
        a.customFrame = false;
    a.titleButtons = buttons < 0 ? defaultButtons : buttons;
    a.titleButtonsLeft = get("titleButtonsLeft", a.titleButtonsLeft);
    a.uiStyle = std::clamp(get("uiStyle", a.uiStyle), 0, 1);
    a.cursorStyle = std::clamp(get("cursorStyle", a.cursorStyle), 0, 1);
    a.welcomed = get("welcomed", a.welcomed);
    a.menuMnemonics = get("menuMnemonics", a.menuMnemonics);
    a.colorScheme = std::clamp(get("colorScheme", a.colorScheme), 0, Theme::kLastScheme);
#ifdef Q_OS_WIN
    if (a.colorScheme == int(Theme::Scheme::Kde))
        a.colorScheme = int(Theme::Scheme::System);
#endif
    a.autosaveMinutes = std::clamp(get("autosaveMinutes", a.autosaveMinutes), 0, 120);
    a.language = get("language", a.language) == QLatin1String("es") ? QStringLiteral("es") : QStringLiteral("en");
    a.reopenLast = get("reopenLast", a.reopenLast);
    a.unit = get("unit", a.unit);
    a.rulers = get("rulers", a.rulers);
    a.rulerStyle = std::clamp(get("rulerStyle", a.rulerStyle), 0, 2);
    a.dropShadow = get("dropShadow", a.dropShadow);
    a.canvasBorderLight = s.value(QStringLiteral("canvasBorderLight"), QColor()).value<QColor>();
    a.canvasBorderDark = s.value(QStringLiteral("canvasBorderDark"), QColor()).value<QColor>();
    // Antes había un solo color para todos los temas: pasa al tema que le corresponde por su
    // brillo (así un fondo oscuro ya no se queda en el tema claro).
    const QColor old = s.value(QStringLiteral("canvasBorder"), QColor()).value<QColor>();
    if (old.isValid() && !s.contains(QStringLiteral("canvasBorderLight")) && !s.contains(QStringLiteral("canvasBorderDark")))
        (old.lightness() < 128 ? a.canvasBorderDark : a.canvasBorderLight) = old;
    a.checkerBrightness = std::clamp(get("checkerboardBrightness", a.checkerBrightness), 0, 255);
    a.startTool = get("startTool", a.startTool);
    a.hideMoveBox = get("hideMoveBox", a.hideMoveBox);
    a.replacePixels = get("replacePixels", a.replacePixels);
    a.askOnceOnClose = get("askOnceOnClose", a.askOnceOnClose);
    a.gifOpenMode = std::clamp(get("gifOpenMode", a.gifOpenMode), 0, 2);
    a.tabletEraser = get("tabletEraser", a.tabletEraser);
    a.historyLimitMB = std::max(0, get("historyLimitMB", a.historyLimitMB));
    a.checkUpdates = get("checkUpdates", a.checkUpdates);
    a.updatesBeta = get("updatesBeta", a.updatesBeta);
    a.updateReminder = get("updateReminder", a.updateReminder);
    a.lastUpdateCheck = s.value(QStringLiteral("lastUpdateCheck")).toDateTime();
    a.latestVersion = get("latestVersion", a.latestVersion);
    a.discordEnabled = get("discordEnabled", a.discordEnabled);
    a.discordShowFile = get("discordShowFile", a.discordShowFile);
    a.discordShowLayer = get("discordShowLayer", a.discordShowLayer);
    a.discordShowFileInfo = get("discordShowFileInfo", a.discordShowFileInfo);
    a.discordShowTool = get("discordShowTool", a.discordShowTool);
    a.discordShowElapsed = get("discordShowElapsed", a.discordShowElapsed);
    return a;
}

void AppSettings::save() const
{
    QSettings s;
    s.beginGroup(QStringLiteral("settings"));
    s.setValue(QStringLiteral("animations"), animations);
    s.setValue(QStringLiteral("windowAnimation"), windowAnimation);
    s.setValue(QStringLiteral("windowAnimationMs"), windowAnimationMs);
    s.setValue(QStringLiteral("aeroTransparency"), aeroTransparency);
    s.setValue(QStringLiteral("translucentPanels"), translucentPanels);
    s.setValue(QStringLiteral("translucentOverImage"), translucentOverImage);
    s.setValue(QStringLiteral("overscroll"), overscroll);
    s.setValue(QStringLiteral("autoScroll"), autoScroll);
    s.setValue(QStringLiteral("autoSelectVisibleLayer"), autoSelectVisibleLayer);
    s.setValue(QStringLiteral("coloredToolIcons"), coloredToolIcons);
    s.setValue(QStringLiteral("systemFileDialogs"), systemFileDialogs);
    s.setValue(QStringLiteral("systemPointer"), systemPointer);
    s.setValue(QStringLiteral("layerDropLine"), layerDropLine);
    s.setValue(QStringLiteral("historyJumpButtons"), historyJumpButtons);
    s.setValue(QStringLiteral("pluginMenuIcons"), pluginMenuIcons);
    s.setValue(QStringLiteral("kaintHtmlPlugins"), kaintHtmlPlugins);
    s.setValue(QStringLiteral("paletteTranslucent"), paletteTranslucent);
    s.remove(QStringLiteral("allPaletteColors"));
    s.setValue(QStringLiteral("panelStyle"), panelStyle);
    s.setValue(QStringLiteral("panelBorder"), panelBorder);
    s.setValue(QStringLiteral("snapPanels"), snapPanels);
    s.setValue(QStringLiteral("uiFontFamily"), uiFontFamily);
    s.setValue(QStringLiteral("uiFontSize"), uiFontSize);
    s.setValue(QStringLiteral("titleBarColor"), titleBarColor);
    s.setValue(QStringLiteral("customWindowFrame"), customFrame);
    s.setValue(QStringLiteral("windows10Frame"), customFrame);
    s.setValue(QStringLiteral("titleButtons"), titleButtons);
    s.setValue(QStringLiteral("titleButtonsLeft"), titleButtonsLeft);
    s.setValue(QStringLiteral("windowsVersion"), windowsVersion);
    s.setValue(QStringLiteral("uiStyle"), uiStyle);
    s.setValue(QStringLiteral("cursorStyle"), cursorStyle);
    s.setValue(QStringLiteral("welcomed"), welcomed);
    s.setValue(QStringLiteral("menuMnemonics"), menuMnemonics);
    s.setValue(QStringLiteral("colorScheme"), colorScheme);
    s.setValue(QStringLiteral("autosaveMinutes"), autosaveMinutes);
    s.setValue(QStringLiteral("language"), language);
    s.setValue(QStringLiteral("reopenLast"), reopenLast);
    s.setValue(QStringLiteral("unit"), unit);
    s.setValue(QStringLiteral("rulers"), rulers);
    s.setValue(QStringLiteral("rulerStyle"), rulerStyle);
    s.setValue(QStringLiteral("dropShadow"), dropShadow);
    s.setValue(QStringLiteral("canvasBorderLight"), canvasBorderLight);
    s.setValue(QStringLiteral("canvasBorderDark"), canvasBorderDark);
    s.remove(QStringLiteral("canvasBorder"));
    s.setValue(QStringLiteral("checkerboardBrightness"), checkerBrightness);
    s.remove(QStringLiteral("checkerBrightness"));
    s.setValue(QStringLiteral("startTool"), startTool);
    s.setValue(QStringLiteral("hideMoveBox"), hideMoveBox);
    s.setValue(QStringLiteral("replacePixels"), replacePixels);
    s.setValue(QStringLiteral("askOnceOnClose"), askOnceOnClose);
    s.setValue(QStringLiteral("gifOpenMode"), gifOpenMode);
    s.setValue(QStringLiteral("tabletEraser"), tabletEraser);
    s.setValue(QStringLiteral("historyLimitMB"), historyLimitMB);
    s.setValue(QStringLiteral("checkUpdates"), checkUpdates);
    s.setValue(QStringLiteral("updatesBeta"), updatesBeta);
    s.setValue(QStringLiteral("updateReminder"), updateReminder);
    s.setValue(QStringLiteral("lastUpdateCheck"), lastUpdateCheck);
    s.setValue(QStringLiteral("latestVersion"), latestVersion);
    s.setValue(QStringLiteral("discordEnabled"), discordEnabled);
    s.setValue(QStringLiteral("discordShowFile"), discordShowFile);
    s.setValue(QStringLiteral("discordShowLayer"), discordShowLayer);
    s.setValue(QStringLiteral("discordShowFileInfo"), discordShowFileInfo);
    s.setValue(QStringLiteral("discordShowTool"), discordShowTool);
    s.setValue(QStringLiteral("discordShowElapsed"), discordShowElapsed);
    s.remove(QStringLiteral("discordClientId"));
}

// ---------------------------------------------------------------------------

namespace {

// Página con desplazamiento y un título grande, como las de Paint.NET.
struct PageBox {
    QWidget* widget;
    QVBoxLayout* layout;
};

PageBox makePage(QWidget* parent)
{
    auto* scroll = new QScrollArea(parent);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget(scroll);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 4, 12, 4);
    scroll->setWidget(content);
    return {scroll, layout};
}

QLabel* wrapped(const QString& text, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    l->setWordWrap(true);
    l->setTextFormat(Qt::PlainText);
    return l;
}

QWidget* sectionHeader(const QString& title, QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* l = new QHBoxLayout(w);
    l->setContentsMargins(0, 8, 0, 0);
    l->addWidget(new QLabel(title, w));
    auto* line = new QFrame(w);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    l->addWidget(line, 1);
    return w;
}

} // namespace

SettingsDialog::SettingsDialog(const AppSettings& current, ToolManager* tools, QWidget* parent)
    : QDialog(parent)
    , m_settings(current)
{
    setWindowTitle(tr("Configuración"));
    setWindowIcon(Icons::theme({"configure", "settings-configure"}));

    m_sections = new QListWidget(this);
    m_sections->setIconSize(QSize(24, 24));
    m_sections->setFixedWidth(200);
    m_sections->setSpacing(1);
    m_pages = new QStackedWidget(this);
    auto addPage = [&](const QString& name, std::initializer_list<const char*> icon, QWidget* page) {
        m_sections->addItem(new QListWidgetItem(Icons::theme(icon), name));
        m_pages->addWidget(page);
    };
    addPage(tr("Interfaz de usuario"), {"settings-interface", "preferences-desktop-theme", "preferences-desktop"}, createInterfacePage());
    addPage(tr("Lienzo"), {"settings-canvas", "image-x-generic", "view-preview"}, createCanvasPage());
    addPage(tr("Herramientas"), {"settings-tools", "tools", "draw-brush"}, createToolsPage(tools));
    addPage(tr("Lápiz y tableta"), {"settings-tablet", "input-tablet", "draw-calligraphic"}, createTabletPage());
    addPage(tr("Rendimiento"), {"settings-performance", "speedometer", "preferences-system-performance", "cpu"}, createPerformancePage());
    addPage(tr("Complementos"), {"settings-plugins", "preferences-plugin", "application-x-addon"}, createPluginsPage());
    addPage(tr("Discord"), {"discord", "im-user-online", "user-online", "network-connect"}, createDiscordPage());
    addPage(tr("Actualizaciones"), {"settings-updates", "update-none", "system-software-update"}, createUpdatesPage());
    addPage(tr("Diagnósticos"), {"settings-diagnostics", "tools-report-bug", "dialog-information"}, createDiagnosticsPage());
    // Como el grupo propio de paint.js: paquetes de iconos, fuentes, formas y atajos de teclado.
    addPage(tr("Paquetes de iconos"), {"settings-icon-packs", "preferences-desktop-icons", "folder-open"}, createIconPacksPage());
    addPage(tr("Fuentes"), {"settings-fonts", "preferences-desktop-font", "draw-text"}, createFontsPage());
    addPage(tr("Formas"), {"settings-shapes", "draw-rectangle", "shapes"}, createShapesPage());
    addPage(tr("Personalización"), {"settings-customize", "configure-toolbars", "preferences-desktop-theme-global", "configure"},
            createCustomizePage(tools));
    addPage(tr("Atajos de teclado"), {"settings-keyboard", "preferences-desktop-keyboard-shortcut", "input-keyboard"},
            createKeyboardPage());

    connect(m_sections, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);
    m_sections->setCurrentRow(0);

    // Como Paint.NET, los cambios se aplican al momento: solo hay "Cerrar".
    auto* box = new QDialogButtonBox(this);
    QPushButton* close = box->addButton(tr("Cerrar"), QDialogButtonBox::AcceptRole);
    close->setDefault(true);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);

    auto* top = new QHBoxLayout;
    top->addWidget(m_sections);
    top->addWidget(m_pages, 1);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(top, 1);
    layout->addWidget(box);
    resize(860, 600);
}

void SettingsDialog::showPage(Page page)
{
    m_sections->setCurrentRow(int(page));
}

void SettingsDialog::changed()
{
    emit settingsChanged(m_settings);
}

bool SettingsDialog::askRestart(const QString& question, const QString& restartText, const QString& cancelText)
{
    TaskDialog ask(tr("Reiniciar Kaint"), Icons::theme({"view-refresh"}), QImage(), question, this);
    const int restart = ask.addChoice(Icons::theme({"view-refresh"}), tr("Reiniciar ahora"), restartText, true);
    ask.addChoice(Icons::theme({"dialog-cancel", "edit-undo"}), tr("Cancelar"), cancelText);
    return ask.choose() == restart;
}

QWidget* SettingsDialog::createInterfacePage()
{
    PageBox page = makePage(this);
    auto check = [&](const QString& text, bool AppSettings::*field, const QString& tip = {}) {
        auto* c = new QCheckBox(text, page.widget);
        c->setChecked(m_settings.*field);
        c->setToolTip(tip);
        connect(c, &QCheckBox::toggled, this, [this, field](bool on) {
            m_settings.*field = on;
            changed();
        });
        page.layout->addWidget(c);
        return c;
    };
    QCheckBox* animations = check(tr("Animaciones"), &AppSettings::animations,
          tr("Menús y ventanas que aparecen con un fundido, capas que se deslizan al reordenarlas y otras transiciones."));
    {
        // Debajo, con sangría: la de abrir ventanas, como en Windows, y cuánto dura.
        QCheckBox* windowAnimation = check(tr("Animar las ventanas al abrirse"), &AppSettings::windowAnimation,
                                           tr("Como en Windows: la ventana (Configuración, un ajuste...) aparece desde un "
                                              "poco más pequeña y transparente, rápido al principio y despacio al final."));
        page.layout->removeWidget(windowAnimation);
        auto* duration = new QSpinBox(page.widget);
        duration->setRange(50, 2000);
        duration->setSingleStep(50);
        duration->setValue(m_settings.windowAnimationMs);
        duration->setToolTip(tr("Duración de la animación."));
        auto* indent = new QHBoxLayout;
        indent->setContentsMargins(20, 0, 0, 0);
        indent->addWidget(windowAnimation);
        indent->addWidget(withUnit(duration, tr("ms"), {}, page.widget));
        indent->addStretch();
        page.layout->addLayout(indent);
        const bool wayland = QGuiApplication::platformName().startsWith(QLatin1String("wayland"));
        auto sync = [animations, windowAnimation, duration, wayland] {
            windowAnimation->setEnabled(animations->isChecked() && !wayland);
            duration->setEnabled(windowAnimation->isEnabled() && windowAnimation->isChecked());
        };
        if (wayland)
            windowAnimation->setToolTip(tr("En Wayland las ventanas las anima el escritorio."));
        sync();
        connect(animations, &QCheckBox::toggled, this, sync);
        connect(windowAnimation, &QCheckBox::toggled, this, sync);
        connect(duration, &QSpinBox::valueChanged, this, [this](int v) {
            m_settings.windowAnimationMs = v;
            changed();
        });
    }
    QCheckBox* translucent = check(tr("Ventanas translúcidas"), &AppSettings::translucentPanels,
                                   tr("Las ventanas flotantes se vuelven translúcidas cuando el puntero no está encima."));
    {
        // Debajo, con sangría: solo mientras tapan la imagen (como Paint.NET).
        QCheckBox* overImage = check(tr("Solo mientras tapan la imagen"), &AppSettings::translucentOverImage,
                                     tr("Como en Paint.NET: una ventana se vuelve translúcida mientras el borde de la imagen la "
                                        "cruza o la imagen queda debajo, y vuelve a ser opaca cuando se aparta de la imagen."));
        page.layout->removeWidget(overImage);
        auto* indent = new QHBoxLayout;
        indent->setContentsMargins(20, 0, 0, 0);
        indent->addWidget(overImage);
        page.layout->addLayout(indent);
        overImage->setEnabled(translucent->isChecked());
        connect(translucent, &QCheckBox::toggled, overImage, &QWidget::setEnabled);
    }
    check(tr("Desplazamiento más allá del borde de la imagen (overscroll)"), &AppSettings::overscroll,
          tr("Permite ver un margen alrededor de la imagen; así el zum hacia el cursor funciona también en las esquinas."));
    check(tr("Desplazamiento automático al dibujar en el borde de la ventana"), &AppSettings::autoScroll);
    check(tr("Seleccionar automáticamente la capa visible más cercana tras ocultar una capa"),
          &AppSettings::autoSelectVisibleLayer);
    check(tr("Iconos de herramientas con los colores activos"), &AppSettings::coloredToolIcons,
          tr("El cubo de pintura, el pincel, cambiar color y el degradado muestran el color principal y el secundario."));
    check(tr("Subrayar las letras de acceso de los menús"), &AppSettings::menuMnemonics,
          tr("Las letras que abren cada menú y opción con Alt. Desactivado, como en Windows, solo se subrayan al pulsar Alt."));
    check(tr("Usar el puntero del sistema en los cursores con la herramienta en miniatura"), &AppSettings::systemPointer,
          tr("La selección, el degradado, la línea, el texto y las formas muestran la flecha (o la I) del tema de cursores "
             "del escritorio con su icono al lado. Desactivado, la flecha de Kaint."));
    check(tr("Usar el diálogo de archivos del sistema para abrir y guardar"), &AppSettings::systemFileDialogs,
          tr("El mismo que usan Firefox y los demás programas del escritorio. Desactivado, se usa el de Qt que trae Kaint."));
    check(tr("Al arrastrar una capa, marcar con una línea dónde quedará"), &AppSettings::layerDropLine,
          tr("Desactivado, la capa se mueve en la lista con el ratón mientras se arrastra."));
    check(tr("Mostrar los botones de ir al principio y al final del historial"), &AppSettings::historyJumpButtons,
          tr("En la ventana Historial, junto a deshacer y rehacer: vuelven a la imagen original o al último cambio."));
    check(tr("Mostrar los colores semitransparentes de la paleta en la vista ampliada de Colores"), &AppSettings::paletteTranslucent,
          tr("Las filas de abajo de la paleta de fábrica, como en Paint.NET. El blanco con que se rellenan las paletas de "
             "menos colores no se muestra nunca."));
    // El botón está en Complementos (se muestra o se esconde al momento).
    QCheckBox* kaintHtml = check(tr("Mostrar el botón «Complementos de Kaint.HTML»"), &AppSettings::kaintHtmlPlugins,
                                 tr("En Complementos, un botón para instalar complementos de paint.js del catálogo de la versión "
                                    "web de Kaint (Kaint.HTML)."));
    connect(kaintHtml, &QCheckBox::toggled, this, [this](bool on) {
        if (m_kaintHtmlButton)
            m_kaintHtmlButton->setVisible(on);
    });

    // Temas: los cuatro primeros imitan Paint.NET; KDE usa el del escritorio. En Windows no hay
    // tema nativo aparte: claro y oscuro ya son los de Windows, como en Paint.NET.
    auto* schemeRow = new QHBoxLayout;
    auto* scheme = new QComboBox(page.widget);
    auto addScheme = [scheme](const QString& text, Theme::Scheme id, const QString& tip) {
        scheme->addItem(text, int(id));
        scheme->setItemData(scheme->count() - 1, tip, Qt::ToolTipRole);
    };
    addScheme(tr("Seguir al sistema"), Theme::Scheme::System, tr("Claro u oscuro de Paint.NET, según el modo del escritorio."));
    addScheme(tr("Claro"), Theme::Scheme::Light, {});
    addScheme(tr("Oscuro"), Theme::Scheme::Dark, {});
    addScheme(tr("Azul"), Theme::Scheme::Blue, tr("El claro con las barras en degradado azulado, como el tema Azul de Paint.NET."));
#ifndef Q_OS_WIN
    addScheme(tr("KDE"), Theme::Scheme::Kde, tr("El estilo y los colores reales de Plasma."));
#endif
    addScheme(tr("Windows 7"), Theme::Scheme::Windows7, tr("Gris claro con los botones de cristal y el azul de Aero, con los colores de 7.css."));
    addScheme(tr("Windows 7 oscuro"), Theme::Scheme::Windows7Dark,
              tr("El de Windows 7 en grises oscuros, con el cristal ahumado."));
    addScheme(tr("Windows XP"), Theme::Scheme::WindowsXP, tr("El beige de Luna con las ventanas azules y los botones de XP, con los colores de XP.css."));
    addScheme(tr("Windows 98"), Theme::Scheme::Windows98, tr("El clásico gris con relieve. También es una plantilla del personalizado."));
    addScheme(tr("Windows 98 ALT"), Theme::Scheme::Windows98Alt,
              tr("El clásico de Windows 2000 y XP, como Paint.NET en Windows XP: gris claro, cabeceras de azul oscuro a celeste y Tahoma."));
    addScheme(tr("Personalizado"), Theme::Scheme::Custom, tr("Colores, fondo del lienzo y diseño a tu gusto."));
    scheme->setCurrentIndex(std::max(0, scheme->findData(m_settings.colorScheme)));
    auto* customize = new QPushButton(tr("Personalizar…"), page.widget);
    customize->setToolTip(tr("Editar el tema personalizado."));
    schemeRow->addWidget(new QLabel(tr("Tema:"), page.widget));
    schemeRow->addWidget(scheme);
    schemeRow->addWidget(customize);
    schemeRow->addStretch();
    page.layout->addLayout(schemeRow);
    // Windows 7: «Habilitar transparencia», como en Windows 7 (solo con sus temas).
    auto* aeroGlass = new QCheckBox(tr("Habilitar transparencia"), page.widget);
    aeroGlass->setChecked(m_settings.aeroTransparency);
    aeroGlass->setToolTip(tr("Como en Windows 7: el cristal de los marcos de las ventanas deja ver lo que hay detrás. "
                             "Solo con los temas Windows 7."));
    {
        auto* indent = new QHBoxLayout;
        indent->setContentsMargins(20, 0, 0, 0);
        indent->addWidget(aeroGlass);
        page.layout->addLayout(indent);
    }
    connect(aeroGlass, &QCheckBox::toggled, this, [this](bool on) {
        m_settings.aeroTransparency = on;
        changed();
    });
    auto syncAeroGlass = [this, aeroGlass] {
        const bool win7 = m_settings.colorScheme == int(Theme::Scheme::Windows7)
                          || m_settings.colorScheme == int(Theme::Scheme::Windows7Dark);
        aeroGlass->setEnabled(win7);
        aeroGlass->setVisible(win7);
    };
    syncAeroGlass();
    connect(this, &SettingsDialog::settingsChanged, this, syncAeroGlass);

    // Aspecto: el de Kaint o la recreación de Paint.NET (solo lo visual).
    auto* lookRow = new QHBoxLayout;
    auto* look = new QComboBox(page.widget);
    look->addItem(QStringLiteral("Kaint"), 0);
    look->setItemData(0, tr("El aspecto propio de Kaint."), Qt::ToolTipRole);
    look->addItem(QStringLiteral("Paint.NET"), 1);
    look->setItemData(1, tr("Como Paint.NET 5: más compacto, con las ventanas sin borde y los botones y menús más juntos."),
                      Qt::ToolTipRole);
    look->setCurrentIndex(std::max(0, look->findData(m_settings.uiStyle)));
    look->setToolTip(tr("No se aplica con el tema KDE."));
    lookRow->addWidget(new QLabel(tr("Estilo de la interfaz:"), page.widget));
    lookRow->addWidget(look);
    lookRow->addStretch();
    page.layout->addLayout(lookRow);
    connect(look, &QComboBox::activated, this, [this, look](int i) {
        const int chosen = look->itemData(i).toInt();
        if (chosen == m_settings.uiStyle)
            return;
        m_settings.uiStyle = chosen;
        // La barra de título de Kaint y sus esquinas (las redondeadas de Windows 11 en Linux) se ponen
        // o se quitan al abrir Kaint: se ofrece reiniciar si cambian.
        if (frameNeedsRestart(m_settings)
            && askRestart(tr("La barra de título de la ventana cambia al abrir Kaint. ¿Quieres reiniciarlo ahora?"),
                          tr("Kaint se cerrará y se abrirá de nuevo con el estilo elegido. Si hay cambios sin guardar, te lo "
                             "preguntará antes."),
                          tr("Cambiar el estilo ahora y la barra de título la próxima vez que se abra Kaint."))) {
            m_restartRequested = true;
            accept();
            return;
        }
        changed();
    });

    // Cursores: los iconos de Kaint o los de Paint.NET en pixel art.
    auto* cursorRow = new QHBoxLayout;
    auto* cursors = new QComboBox(page.widget);
    cursors->addItem(QStringLiteral("Kaint"), 0);
    cursors->setItemData(0, tr("Los iconos de las herramientas, a la medida del puntero del sistema."), Qt::ToolTipRole);
    cursors->addItem(QStringLiteral("Paint.NET"), 1);
    cursors->setItemData(1, tr("Los de Paint.NET, en pixel art: la cruz con el dibujo de la herramienta."), Qt::ToolTipRole);
    cursors->setCurrentIndex(std::max(0, cursors->findData(m_settings.cursorStyle)));
    cursorRow->addWidget(new QLabel(tr("Cursores:"), page.widget));
    cursorRow->addWidget(cursors);
    cursorRow->addStretch();
    page.layout->addLayout(cursorRow);
    connect(cursors, &QComboBox::activated, this, [this, cursors](int i) {
        m_settings.cursorStyle = cursors->itemData(i).toInt();
        changed();
    });

    auto* languageRow = new QHBoxLayout;
    auto* language = new QComboBox(page.widget);
    language->addItem(QStringLiteral("English"), QStringLiteral("en"));
    language->addItem(QStringLiteral("Español"), QStringLiteral("es"));
    language->setCurrentIndex(language->findData(m_settings.language));
    languageRow->addWidget(new QLabel(tr("Idioma:"), page.widget));
    languageRow->addWidget(language);
    languageRow->addStretch();
    page.layout->addLayout(languageRow);

    // Fuente de la interfaz: la del sistema (o la del tema) o cualquiera instalada, y su tamaño.
    auto* fontRow = new QHBoxLayout;
    auto* uiFont = new QComboBox(page.widget);
    uiFont->addItem(tr("La del sistema"), QString());
    for (const QString& family : QFontDatabase::families())
        if (!QFontDatabase::isPrivateFamily(family))
            uiFont->addItem(family, family);
    uiFont->setCurrentIndex(std::max(0, uiFont->findData(m_settings.uiFontFamily)));
    uiFont->setMaxVisibleItems(16);
    uiFont->setToolTip(tr("Letra de los menús, las ventanas y los diálogos. «La del sistema» usa la del escritorio "
                          "(o la del tema personalizado)."));
    auto* uiFontSize = new QSpinBox(page.widget);
    uiFontSize->setRange(0, 24);
    uiFontSize->setSpecialValueText(tr("Automático"));
    uiFontSize->setValue(m_settings.uiFontSize);
    uiFontSize->setToolTip(tr("Tamaño de la letra en puntos (Automático: el del sistema o del tema)."));
    fontRow->addWidget(new QLabel(tr("Fuente de la interfaz:"), page.widget));
    fontRow->addWidget(uiFont);
    fontRow->addWidget(withUnit(uiFontSize, tr("pt"), {}, page.widget));
    fontRow->addStretch();
    page.layout->addLayout(fontRow);
    connect(uiFont, &QComboBox::activated, this, [this, uiFont](int i) {
        m_settings.uiFontFamily = uiFont->itemData(i).toString();
        changed();
    });
    connect(uiFontSize, &QSpinBox::valueChanged, this, [this](int v) {
        m_settings.uiFontSize = v;
        changed();
    });

    // El color de la barra superior (y de las cabeceras de las ventanas), como el color de énfasis
    // en las barras de título de Windows. En Windows, con la barra de título de Kaint (sin ella, la
    // de Windows lleva su color de énfasis).
    auto* titleRowWidget = new QWidget(page.widget);
    auto* titleRow = new QHBoxLayout(titleRowWidget);
    titleRow->setContentsMargins(0, 0, 0, 0);
    auto* ownTitle = new QCheckBox(tr("Color propio en la barra superior:"), page.widget);
    ownTitle->setToolTip(tr("Como «Mostrar el color de énfasis en las barras de título» de Windows: la fila de los "
                            "menús y las cabeceras de las ventanas llevan este color."));
    auto* titleColor = new ColorButton(page.widget);
    titleColor->setColor(m_settings.titleBarColor.isValid() ? m_settings.titleBarColor : QColor(0x74, 0x4d, 0xa9));
    ownTitle->setChecked(m_settings.titleBarColor.isValid());
    titleColor->setEnabled(ownTitle->isChecked());
    titleRow->addWidget(ownTitle);
    titleRow->addWidget(titleColor);
    titleRow->addStretch();
    page.layout->addWidget(titleRowWidget);
    auto applyTitle = [this, ownTitle, titleColor] {
        titleColor->setEnabled(ownTitle->isChecked());
        m_settings.titleBarColor = ownTitle->isChecked() ? titleColor->color() : QColor();
        changed();
    };
    connect(ownTitle, &QCheckBox::toggled, this, applyTitle);
    connect(titleColor, &ColorButton::colorChanged, this, applyTitle);

    // Paquete de iconos (como en paint.js; se administran en su página).
    auto* packRow = new QHBoxLayout;
    m_iconPackChoice = new QComboBox(page.widget);
    auto* managePacks = new QPushButton(tr("Administrar…"), page.widget);
    managePacks->setToolTip(tr("Añadir o quitar paquetes de iconos."));
    packRow->addWidget(new QLabel(tr("Paquete de iconos:"), page.widget));
    packRow->addWidget(m_iconPackChoice);
    packRow->addWidget(managePacks);
    packRow->addStretch();
    page.layout->addLayout(packRow);
    connect(m_iconPackChoice, &QComboBox::activated, this, [this](int i) {
        IconPacks::activate(m_iconPackChoice->itemData(i).toString());
        refreshIconPacks();
    });
    connect(managePacks, &QPushButton::clicked, this, [this] { showPage(Page::IconPacks); });

    // Ventanas flotantes: el estilo inspirado en Windows 10 o el compacto, y su borde.
    page.layout->addWidget(sectionHeader(tr("Ventanas"), page.widget));
    auto* panelForm = new QFormLayout;
    panelForm->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    auto* panelStyle = new QComboBox(page.widget);
    panelStyle->addItem(tr("Windows 10"), 0);
    panelStyle->setItemData(0, tr("Cabecera más alta, botón de cerrar cuadrado y esquinas algo redondeadas."), Qt::ToolTipRole);
    panelStyle->addItem(tr("Compacto"), 1);
    panelStyle->setItemData(1, tr("La cabecera fina de siempre de Kaint."), Qt::ToolTipRole);
    panelStyle->setCurrentIndex(std::max(0, panelStyle->findData(m_settings.panelStyle)));
    auto* panelBorder = new QSpinBox(page.widget);
    panelBorder->setRange(1, 12);
    panelBorder->setValue(m_settings.panelBorder);
    panelBorder->setToolTip(tr("Grosor del borde de las ventanas Herramientas, Historial, Capas y Colores."));
    panelForm->addRow(tr("Estilo de las ventanas:"), panelStyle);
    QWidget* borderField = withUnit(panelBorder, tr("px"), {}, page.widget);
    panelForm->addRow(tr("Grosor del borde de las ventanas:"), borderField);
    page.layout->addLayout(panelForm);
    check(tr("Acoplar las ventanas a los bordes y a las demás ventanas al moverlas"), &AppSettings::snapPanels,
          tr("Al soltar una ventana cerca de un borde del área de trabajo o de otra ventana, se coloca junto a él."));
    // Barra de título: la de Kaint (con los botones como los de Windows 11, 10, 7, XP o 98 o los de
    // macOS, a la derecha o a la izquierda) o la del sistema (la de Windows o, en Linux, la del
    // escritorio: en KDE, la de Plasma). Con el estilo Paint.NET, además, la recreación de Windows 10
    // u 11 (esquinas redondeadas en modo ventana). Por defecto, la de Kaint en Linux y la del
    // sistema en Windows.
    page.layout->addWidget(sectionHeader(tr("Barra de título"), page.widget));
    auto* customFrame = new QCheckBox(tr("Usar la barra de título y el marco de ventana de Kaint"), page.widget);
    customFrame->setChecked(m_settings.customFrame);
#ifdef Q_OS_WIN
    customFrame->setToolTip(tr("Las ventanas llevan la barra de título de Kaint, con los botones elegidos. Si se desactiva, "
                               "llevan la de Windows."));
#else
    customFrame->setToolTip(tr("Las ventanas llevan la barra de título de Kaint, con los botones elegidos. Si se desactiva, "
                               "llevan la del escritorio (en KDE, la de Plasma)."));
#endif
    page.layout->addWidget(customFrame);
    auto* titleForm = new QFormLayout;
    titleForm->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    auto* buttonsStyle = new QComboBox(page.widget);
    const struct { int id; QString name; QString tip; } styles[] = {
        {1, QStringLiteral("Windows 11"), tr("Finos, con el cuadrado de maximizar redondeado.")},
        {0, QStringLiteral("Windows 10"), tr("Los de siempre de Paint.NET: 46 píxeles de ancho y cerrar en rojo.")},
        {2, QStringLiteral("macOS"), tr("Los tres círculos de colores (cerrar, minimizar y zoom).")},
        {3, QStringLiteral("Windows 7"), tr("De cristal, colgando del borde de arriba, con cerrar en rojo.")},
        {4, QStringLiteral("Windows XP"), tr("Los azules de Luna con las esquinas redondeadas y cerrar en naranja.")},
        {5, QStringLiteral("Windows 98"), tr("Grises en relieve.")},
    };
    for (const auto& st : styles) {
        buttonsStyle->addItem(st.name, st.id);
        buttonsStyle->setItemData(buttonsStyle->count() - 1, st.tip, Qt::ToolTipRole);
    }
    buttonsStyle->setCurrentIndex(std::max(0, buttonsStyle->findData(m_settings.titleButtons)));
    buttonsStyle->setToolTip(tr("Los botones de cerrar, maximizar o restaurar y minimizar. Con el tema Windows 98, «Windows 10» "
                                "usa los de Windows 98."));
    auto* buttonsSide = new QComboBox(page.widget);
    buttonsSide->addItem(tr("A la derecha"), false);
    buttonsSide->addItem(tr("A la izquierda"), true);
    buttonsSide->setCurrentIndex(m_settings.titleButtonsLeft ? 1 : 0);
    auto* recreation = new QComboBox(page.widget);
    recreation->addItem(QStringLiteral("Windows 10"), 10);
    recreation->setItemData(0, tr("Ventanas con las esquinas rectas."), Qt::ToolTipRole);
    recreation->addItem(QStringLiteral("Windows 11"), 11);
    recreation->setItemData(1, tr("Ventanas con las esquinas algo redondeadas en modo ventana y, por defecto, los botones de "
                                  "Windows 11."),
                            Qt::ToolTipRole);
    recreation->setCurrentIndex(m_settings.windowsVersion == 11 ? 1 : 0);
    titleForm->addRow(tr("Botones de la barra de título:"), buttonsStyle);
    titleForm->addRow(tr("Posición de los botones:"), buttonsSide);
    titleForm->addRow(tr("Recreación de Windows:"), recreation);
    page.layout->addLayout(titleForm);
    // Poner o quitar la barra de Kaint (o, en Linux, las esquinas redondeadas) pide reiniciar. Si se
    // dice que no, todo vuelve a como estaba (la casilla, los botones de antes y su recreación).
    auto applyTitleChange = [this, customFrame, buttonsStyle, recreation](const AppSettings& before) {
        if (frameNeedsRestart(m_settings)) {
            if (!askRestart(tr("La barra de título de la ventana cambia al abrir Kaint. ¿Quieres reiniciarlo ahora?"),
                            tr("Kaint se cerrará y se abrirá de nuevo con la barra de título elegida. Si hay cambios sin "
                               "guardar, te lo preguntará antes."),
                            tr("Dejar la barra de título como estaba."))) {
                m_settings.customFrame = before.customFrame;
                m_settings.titleButtons = before.titleButtons;
                m_settings.windowsVersion = before.windowsVersion;
                const QSignalBlocker block(customFrame);
                customFrame->setChecked(m_settings.customFrame);
                buttonsStyle->setCurrentIndex(std::max(0, buttonsStyle->findData(m_settings.titleButtons)));
                recreation->setCurrentIndex(m_settings.windowsVersion == 11 ? 1 : 0);
                emit settingsChanged(m_settings);
                return;
            }
            m_restartRequested = true;
            changed();
            accept();
            return;
        }
        changed();
    };
    connect(customFrame, &QCheckBox::toggled, this, [this, applyTitleChange](bool on) {
        const AppSettings before = m_settings;
        m_settings.customFrame = on;
        applyTitleChange(before);
    });
    connect(buttonsStyle, &QComboBox::activated, this, [this, buttonsStyle, recreation, applyTitleChange](int i) {
        const AppSettings before = m_settings;
        m_settings.titleButtons = buttonsStyle->itemData(i).toInt();
        // Con el estilo Paint.NET, los botones de Windows 11 traen su recreación (ventanas con las
        // esquinas redondeadas) y los de Windows 10, la suya.
        if (m_settings.uiStyle == 1 && (m_settings.titleButtons == 0 || m_settings.titleButtons == 1)) {
            m_settings.windowsVersion = m_settings.titleButtons == 1 ? 11 : 10;
            recreation->setCurrentIndex(m_settings.windowsVersion == 11 ? 1 : 0);
        }
        applyTitleChange(before);
    });
    connect(buttonsSide, &QComboBox::activated, this, [this, buttonsSide](int i) {
        m_settings.titleButtonsLeft = buttonsSide->itemData(i).toBool();
        changed();
    });
    connect(recreation, &QComboBox::activated, this, [this, recreation, buttonsStyle, applyTitleChange](int i) {
        const int version = recreation->itemData(i).toInt();
        if (version == m_settings.windowsVersion)
            return;
        const AppSettings before = m_settings;
        m_settings.windowsVersion = version;
        // Cada recreación con sus botones (si no se eligieron otros).
        if (version == 11 && m_settings.titleButtons == 0)
            m_settings.titleButtons = 1;
        else if (version == 10 && m_settings.titleButtons == 1)
            m_settings.titleButtons = 0;
        buttonsStyle->setCurrentIndex(std::max(0, buttonsStyle->findData(m_settings.titleButtons)));
        applyTitleChange(before);
    });
    // Los botones, su posición y la recreación solo con la barra de Kaint (y la recreación, con el
    // estilo Paint.NET); con el tema KDE, siempre la barra del escritorio.
    auto syncTitle = [this, titleForm, customFrame, recreation, buttonsSide, buttonsStyle] {
        const bool kde = m_settings.colorScheme == int(Theme::Scheme::Kde);
        const bool own = m_settings.customFrame && !kde;
        const bool paintNet = m_settings.uiStyle == 1 && !kde;
        customFrame->setEnabled(!kde);
        if (kde)
            customFrame->setToolTip(tr("Con el tema KDE, las ventanas llevan la barra de título de Plasma."));
        recreation->setEnabled(own && paintNet);
        recreation->setToolTip(paintNet ? QString() : tr("Se desbloquea con el estilo de interfaz Paint.NET."));
        buttonsSide->setEnabled(own);
        buttonsStyle->setEnabled(own);
        buttonsStyle->setCurrentIndex(std::max(0, buttonsStyle->findData(m_settings.titleButtons)));
        for (QWidget* field : {static_cast<QWidget*>(recreation), static_cast<QWidget*>(buttonsSide), static_cast<QWidget*>(buttonsStyle)})
            if (QWidget* label = titleForm->labelForField(field))
                label->setEnabled(field->isEnabled());
    };
    syncTitle();
    connect(this, &SettingsDialog::settingsChanged, this, syncTitle);
#ifdef Q_OS_WIN
    // En Windows, el color propio va con la barra de título de Kaint.
    auto syncTitleColor = [this, titleRowWidget] {
        const bool own = m_settings.customFrame && m_settings.colorScheme != int(Theme::Scheme::Kde);
        titleRowWidget->setEnabled(own);
        titleRowWidget->setToolTip(own ? QString() : tr("Se desbloquea con la barra de título de Kaint."));
    };
    syncTitleColor();
    connect(this, &SettingsDialog::settingsChanged, this, syncTitleColor);
    connect(customFrame, &QCheckBox::toggled, this, syncTitleColor);
#else
    Q_UNUSED(titleRowWidget);
#endif
    // Con el estilo de Paint.NET las ventanas no tienen borde y llevan su propia cabecera: el
    // estilo y el borde no se usan y se ven desactivados (con su etiqueta). El estilo de la
    // interfaz, a su vez, no se aplica con el tema KDE.
    auto updateCompatibility = [this, panelForm, panelStyle, borderField, look] {
        const bool paintNet = m_settings.uiStyle == 1 && m_settings.colorScheme != int(Theme::Scheme::Kde);
        const QString why = tr("No se usa con el estilo de interfaz Paint.NET: sus ventanas no tienen borde.");
        for (QWidget* field : {static_cast<QWidget*>(panelStyle), borderField}) {
            field->setEnabled(!paintNet);
            field->setToolTip(paintNet ? why : QString());
            if (QWidget* label = panelForm->labelForField(field))
                label->setEnabled(!paintNet);
        }
        look->setEnabled(m_settings.colorScheme != int(Theme::Scheme::Kde));
    };
    updateCompatibility();
    connect(look, &QComboBox::activated, this, updateCompatibility);
    connect(this, &SettingsDialog::settingsChanged, this, updateCompatibility);
    connect(panelStyle, &QComboBox::activated, this, [this, panelStyle](int i) {
        m_settings.panelStyle = panelStyle->itemData(i).toInt();
        changed();
    });
    connect(panelBorder, &QSpinBox::valueChanged, this, [this](int v) {
        m_settings.panelBorder = v;
        changed();
    });

    // Entrar en el tema KDE o salir de él, y cambiar el idioma, reinicia Kaint: se pregunta antes
    // y, si no se acepta, se queda como estaba.
    auto chooseScheme = [this, scheme](int i) {
        if (i == m_settings.colorScheme)
            return true;
        if (Theme::needsRestart(Theme::Scheme(i))) {
            const QString question = i == int(Theme::Scheme::Kde)
                                         ? tr("El tema KDE usa la integración de Plasma, que solo se carga al abrir "
                                              "Kaint. ¿Quieres reiniciar Kaint ahora?")
                                         : tr("Para salir del tema KDE hay que reiniciar Kaint. ¿Quieres reiniciarlo "
                                              "ahora?");
            if (askRestart(question, tr("Kaint se cerrará y se abrirá de nuevo con el tema elegido. Si hay cambios sin "
                                        "guardar, te lo preguntará antes."),
                           tr("Seguir con el tema actual."))) {
                m_settings.colorScheme = i;
                m_restartRequested = true;
                accept();
            } else {
                scheme->setCurrentIndex(scheme->findData(m_settings.colorScheme));
            }
            return false;
        }
        m_settings.colorScheme = i;
        scheme->setCurrentIndex(scheme->findData(i));
        changed();
        return true;
    };
    connect(scheme, &QComboBox::activated, this, [scheme, chooseScheme](int index) { chooseScheme(scheme->itemData(index).toInt()); });
    connect(customize, &QPushButton::clicked, this, [this, chooseScheme] {
        if (!chooseScheme(int(Theme::Scheme::Custom)))
            return;
        ThemeDialog dlg(this);
        execWithoutDimming(dlg);
    });
    connect(language, &QComboBox::activated, this, [this, language](int i) {
        const QString chosen = language->itemData(i).toString();
        if (chosen == m_settings.language)
            return;
        if (askRestart(tr("El idioma se cambia al abrir Kaint. ¿Quieres reiniciarlo ahora?"),
                       tr("Kaint se cerrará y se abrirá de nuevo en el idioma elegido. Si hay cambios sin guardar, te "
                          "lo preguntará antes."),
                       tr("Seguir con el idioma actual."))) {
            m_settings.language = chosen;
            m_restartRequested = true;
            accept();
        } else {
            language->setCurrentIndex(language->findData(m_settings.language));
        }
    });

    page.layout->addWidget(sectionHeader(tr("Al iniciar"), page.widget));
    check(tr("Volver a abrir las imágenes de la última sesión"), &AppSettings::reopenLast);

    // GIF animados: como capas de una imagen (se vuelve a guardar como GIF) o un fotograma por imagen.
    auto* gifRow = new QHBoxLayout;
    auto* gifOpen = new QComboBox(page.widget);
    gifOpen->addItem(tr("Preguntar"), 0);
    gifOpen->addItem(tr("Como capas"), 1);
    gifOpen->setItemData(1, tr("Cada fotograma es una capa de una sola imagen (se vuelve a guardar como GIF tal cual)."),
                         Qt::ToolTipRole);
    gifOpen->addItem(tr("Como imágenes"), 2);
    gifOpen->setItemData(2, tr("Cada fotograma se abre como una imagen aparte."), Qt::ToolTipRole);
    gifOpen->setCurrentIndex(std::max(0, gifOpen->findData(m_settings.gifOpenMode)));
    gifRow->addWidget(new QLabel(tr("Al abrir un GIF animado:"), page.widget));
    gifRow->addWidget(gifOpen);
    gifRow->addStretch();
    page.layout->addLayout(gifRow);
    connect(gifOpen, &QComboBox::activated, this, [this, gifOpen](int i) {
        m_settings.gifOpenMode = gifOpen->itemData(i).toInt();
        changed();
    });

    page.layout->addWidget(sectionHeader(tr("Al cerrar"), page.widget));
    check(tr("Preguntar una sola vez por todas las imágenes sin guardar"), &AppSettings::askOnceOnClose,
          tr("Como en Paint.NET: «No guardar» descarta todas a la vez. Desactivado, se pregunta imagen por imagen."));

    // Copias de recuperación por si Kaint se cierra de golpe.
    page.layout->addWidget(sectionHeader(tr("Recuperación"), page.widget));
    auto* autosaveRow = new QHBoxLayout;
    auto* autosave = new QSpinBox(page.widget);
    autosave->setRange(0, 120);
    autosave->setValue(m_settings.autosaveMinutes);
    autosaveRow->addWidget(new QLabel(tr("Guardar una copia de recuperación de las imágenes sin guardar cada:"), page.widget));
    autosaveRow->addWidget(withUnit(autosave, tr("min"), tr("(0: nunca)"), page.widget));
    autosaveRow->addStretch();
    page.layout->addLayout(autosaveRow);
    connect(autosave, &QSpinBox::valueChanged, this, [this](int v) {
        m_settings.autosaveMinutes = v;
        changed();
    });
    page.layout->addStretch();
    return page.widget;
}

QWidget* SettingsDialog::createCanvasPage()
{
    PageBox page = makePage(this);
    auto* shadow = new QCheckBox(tr("Proyectar una sombra bajo el lienzo"), page.widget);
    shadow->setChecked(m_settings.dropShadow);
    connect(shadow, &QCheckBox::toggled, this, [this](bool on) {
        m_settings.dropShadow = on;
        changed();
    });
    page.layout->addWidget(shadow);

    // Color del borde del lienzo (el fondo alrededor de la imagen), como en Paint.NET: la casilla
    // activa el selector de color, que se ve en gris mientras está desactivado. Se guarda uno para
    // los temas claros y otro para los oscuros: aquí se edita el del tema actual.
    QColor AppSettings::*field = Theme::isDark() ? &AppSettings::canvasBorderDark : &AppSettings::canvasBorderLight;
    const QColor themeBorder = Theme::active().workspace;
    auto* custom = new QCheckBox(tr("Usar un color personalizado para el borde del lienzo"), page.widget);
    custom->setChecked((m_settings.*field).isValid());
    page.layout->addWidget(custom);

    auto* picker = new QWidget(page.widget);
    picker->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto* pickerRow = new QHBoxLayout(picker);
    // El selector, centrado en la página (con un margen a cada lado, no pegado a la izquierda).
    pickerRow->setContentsMargins(0, 0, 0, 4);
    pickerRow->addStretch();
    auto* swatch = new ColorButton(picker);
    swatch->setFixedSize(26, 26);
    swatch->setToolTip(tr("Elegir el color con más opciones"));
    auto* wheel = new ColorWheel(picker);
    wheel->setClockwise(true);
    wheel->setFixedSize(112, 112);
    auto* saturation = new ColorSlider(ColorSlider::Saturation, Qt::Vertical, picker);
    saturation->setFixedSize(16, 112);
    saturation->setToolTip(tr("Saturación"));
    auto* value = new ColorSlider(ColorSlider::Value, Qt::Vertical, picker);
    value->setFixedSize(16, 112);
    value->setToolTip(tr("Brillo"));
    auto* channels = new QGridLayout;
    channels->setVerticalSpacing(2);
    QList<QSpinBox*> spins;
    const QStringList names{tr("R:"), tr("V:"), tr("A:")};
    for (int i = 0; i < 3; ++i) {
        auto* spin = new QSpinBox(picker);
        spin->setRange(0, 255);
        spin->setAlignment(Qt::AlignRight);
        channels->addWidget(new QLabel(names.at(i), picker), i, 0);
        channels->addWidget(spin, i, 1);
        spins << spin;
    }
    auto* resetBorder = new QToolButton(picker);
    resetBorder->setIcon(Icons::theme({"edit-reset", "edit-undo"}));
    resetBorder->setToolTip(tr("Restablecer"));
    channels->addWidget(resetBorder, 3, 1, Qt::AlignLeft);
    pickerRow->addWidget(swatch, 0, Qt::AlignTop);
    pickerRow->addWidget(wheel, 0, Qt::AlignTop);
    pickerRow->addWidget(saturation, 0, Qt::AlignTop);
    pickerRow->addWidget(value, 0, Qt::AlignTop);
    pickerRow->addSpacing(12);
    pickerRow->addLayout(channels);
    pickerRow->addStretch();
    page.layout->addWidget(picker);

    auto border = std::make_shared<QColor>((m_settings.*field).isValid() ? m_settings.*field : themeBorder);
    auto show = [=] {
        const QColor c = *border;
        wheel->setColor(c);
        saturation->setColor(c);
        value->setColor(c);
        {
            const QSignalBlocker b(swatch);
            swatch->setColor(c);
        }
        const int rgb[3] = {c.red(), c.green(), c.blue()};
        for (int i = 0; i < 3; ++i) {
            const QSignalBlocker b(spins.at(i));
            spins.at(i)->setValue(rgb[i]);
        }
    };
    auto store = [this, custom, picker, field, border] {
        picker->setEnabled(custom->isChecked());
        m_settings.*field = custom->isChecked() ? *border : QColor();
        changed();
    };
    auto setBorder = [border, show, store](const QColor& c) {
        QColor opaque = c.toRgb();
        opaque.setAlpha(255);
        *border = opaque;
        show();
        store();
    };
    show();
    picker->setEnabled(custom->isChecked());
    connect(custom, &QCheckBox::toggled, this, store);
    connect(swatch, &ColorButton::colorChanged, this, setBorder);
    connect(wheel, &ColorWheel::colorPicked, this, [setBorder](const QColor& c, Qt::MouseButton) { setBorder(c); });
    connect(saturation, &ColorSlider::valueChanged, this, [border, setBorder](int v) {
        setBorder(QColor::fromHsv(std::max(0, border->hsvHue()), v * 255 / 100, border->value()));
    });
    connect(value, &ColorSlider::valueChanged, this, [border, setBorder](int v) {
        setBorder(QColor::fromHsv(std::max(0, border->hsvHue()), border->hsvSaturation(), v * 255 / 100));
    });
    for (int i = 0; i < 3; ++i)
        connect(spins.at(i), &QSpinBox::valueChanged, this, [border, setBorder, i](int v) {
            QColor c = *border;
            i == 0 ? c.setRed(v) : i == 1 ? c.setGreen(v) : c.setBlue(v);
            setBorder(c);
        });
    connect(resetBorder, &QToolButton::clicked, this, [setBorder, themeBorder] { setBorder(themeBorder); });

    // Brillo del tablero de transparencia (0..1 como en Paint.NET).
    page.layout->addWidget(new QLabel(tr("Brillo de tablero de ajedrez de transparencia"), page.widget));
    auto* row = new QHBoxLayout;
    auto* slider = new QSlider(Qt::Horizontal, page.widget);
    slider->setRange(25, 100);
    auto* spin = new QDoubleSpinBox(page.widget);
    spin->setRange(0.25, 1.0);
    spin->setSingleStep(0.05);
    spin->setDecimals(2);
    auto* reset = new QToolButton(page.widget);
    reset->setIcon(Icons::theme({"edit-reset", "edit-undo"}));
    reset->setToolTip(tr("Restablecer"));
    reset->setAutoRaise(true);
    row->addWidget(slider, 1);
    row->addWidget(spin);
    row->addWidget(reset);
    page.layout->addLayout(row);
    auto set = [this, slider, spin](double v) {
        const QSignalBlocker b1(slider), b2(spin);
        slider->setValue(int(std::lround(v * 100)));
        spin->setValue(v);
        m_settings.checkerBrightness = int(std::lround(v * 255));
        changed();
    };
    {
        const QSignalBlocker b1(slider), b2(spin);
        const double v = m_settings.checkerBrightness / 255.0;
        slider->setValue(int(std::lround(v * 100)));
        spin->setValue(v);
    }
    connect(slider, &QSlider::valueChanged, this, [set](int v) { set(v / 100.0); });
    connect(spin, &QDoubleSpinBox::valueChanged, this, set);
    connect(reset, &QToolButton::clicked, this, [set] { set(191 / 255.0); });

    // Regla (Ver > Reglas): según el tema, del color de la barra de herramientas u oscura.
    auto* rulerRow = new QHBoxLayout;
    auto* ruler = new QComboBox(page.widget);
    ruler->addItem(tr("Según el tema"), int(Ruler::Style::Auto));
    ruler->setItemData(0, tr("Oscura con los temas oscuros y del color de la barra de herramientas con los claros."),
                       Qt::ToolTipRole);
    ruler->addItem(tr("Del color de la barra de herramientas"), int(Ruler::Style::ToolBar));
    ruler->addItem(tr("Oscura"), int(Ruler::Style::PaintJs));
    ruler->setCurrentIndex(std::max(0, ruler->findData(m_settings.rulerStyle)));
    rulerRow->addWidget(new QLabel(tr("Reglas:"), page.widget));
    rulerRow->addWidget(ruler);
    rulerRow->addStretch();
    page.layout->addLayout(rulerRow);
    connect(ruler, &QComboBox::activated, this, [this, ruler](int i) {
        m_settings.rulerStyle = ruler->itemData(i).toInt();
        changed();
    });
    page.layout->addStretch();
    return page.widget;
}

QWidget* SettingsDialog::createToolsPage(ToolManager* tools)
{
    PageBox page = makePage(this);
    page.layout->addWidget(wrapped(tr("Puede elegir la herramienta que está activa cuando inicie Kaint, y qué configuración "
                                      "predeterminada se debe aplicar a los elementos de la barra de herramientas."),
                                   page.widget));
    auto* defaults = new LocalToolSettings(AppState::instance()->toolDefaults(), this);
    connect(defaults, &ToolSettingsModel::changed, this, [defaults] { AppState::instance()->setToolDefaults(defaults->tools()); });

    auto* buttons = new QHBoxLayout;
    auto* load = new QPushButton(tr("Cargar desde barra de htas."), page.widget);
    load->setToolTip(tr("Usa como predeterminadas las opciones que tiene ahora la barra de herramientas."));
    auto* reset = new QPushButton(tr("Restablecer"), page.widget);
    reset->setToolTip(tr("Vuelve a los valores de fábrica de todas las herramientas."));
    connect(load, &QPushButton::clicked, this, [defaults] { defaults->set(AppState::instance()->tools()); });
    connect(reset, &QPushButton::clicked, this, [defaults] { defaults->set(ToolSettings()); });
    buttons->addWidget(load);
    buttons->addWidget(reset);
    buttons->addStretch();
    page.layout->addLayout(buttons);

    auto* startRow = new QHBoxLayout;
    auto* startTool = new QComboBox(page.widget);
    startTool->addItem(tr("La última que se usó"), -1);
    for (Tool* t : tools->tools())
        startTool->addItem(t->icon(), t->name(), int(t->id()));
    startTool->setCurrentIndex(std::max(0, startTool->findData(m_settings.startTool)));
    connect(startTool, &QComboBox::activated, this, [this, startTool](int) {
        m_settings.startTool = startTool->currentData().toInt();
        changed();
    });
    startRow->addWidget(new QLabel(tr("Herramienta predeterminada:"), page.widget));
    startRow->addWidget(startTool);
    startRow->addStretch();
    page.layout->addLayout(startRow);

    page.layout->addWidget(sectionHeader(tr("Mover y pegar"), page.widget));
    auto check = [&](const QString& text, bool AppSettings::*field, const QString& tip) {
        auto* c = new QCheckBox(text, page.widget);
        c->setChecked(m_settings.*field);
        c->setToolTip(tip);
        connect(c, &QCheckBox::toggled, this, [this, field](bool on) {
            m_settings.*field = on;
            changed();
        });
        page.layout->addWidget(c);
    };
    check(tr("Ocultar el contorno y los tiradores mientras se arrastra"), &AppSettings::hideMoveBox,
          tr("Con Mover píxeles seleccionados solo queda el icono de mover; con Mover selección desaparecen los "
             "tiradores."));
    check(tr("Los píxeles pegados o movidos reemplazan a los de debajo, también los transparentes"),
          &AppSettings::replacePixels,
          tr("Como en Paint.NET: pegar una imagen con transparencia sobre otra en la misma capa deja ese rectángulo "
             "transparente. Desactivado, se mezclan con lo que hay debajo."));

    m_toolDefaults = new ToolOptionsPanel(defaults, page.widget);
    page.layout->addWidget(m_toolDefaults);
    page.layout->addStretch();
    return page.widget;
}

QWidget* SettingsDialog::createTabletPage()
{
    PageBox page = makePage(this);
    auto* eraser = new QCheckBox(tr("Usar la Goma con el extremo borrador del lápiz"), page.widget);
    eraser->setChecked(m_settings.tabletEraser);
    connect(eraser, &QCheckBox::toggled, this, [this](bool on) {
        m_settings.tabletEraser = on;
        changed();
    });
    page.layout->addWidget(eraser);
    page.layout->addWidget(wrapped(tr("Al dar la vuelta al lápiz se activa la herramienta Goma de borrar, y al volver a "
                                      "la punta, la herramienta anterior. La sensibilidad a la presión se activa en la "
                                      "barra de opciones del pincel, la goma y el lápiz."),
                                   page.widget));

    // Dispositivos detectados (Qt 6 los enumera también en Wayland).
    QStringList found;
    for (const QInputDevice* d : QInputDevice::devices())
        if (d->type() == QInputDevice::DeviceType::Stylus || d->type() == QInputDevice::DeviceType::Airbrush
            || d->type() == QInputDevice::DeviceType::Puck)
            found << d->name();
    found.removeDuplicates();
    auto* status = new QLabel(page.widget);
    status->setWordWrap(true);
    if (found.isEmpty())
        status->setText(QStringLiteral("⚠ ") + tr("No se detectó ningún lápiz ni tableta de dibujo."));
    else
        status->setText(tr("Tabletas y lápices detectados:\n%1").arg(found.join(QLatin1Char('\n'))));
    page.layout->addWidget(status);

    const QString settings = QStandardPaths::findExecutable(QStringLiteral("systemsettings"));
    if (!settings.isEmpty()) {
        auto* open = new QPushButton(Icons::theme({"configure", "input-tablet"}), tr("Abrir la configuración de tableta de KDE"),
                                     page.widget);
        connect(open, &QPushButton::clicked, this, [settings] {
            QProcess::startDetached(settings, {QStringLiteral("kcm_tablet")});
        });
        auto* row = new QHBoxLayout;
        row->addWidget(open);
        row->addStretch();
        page.layout->addLayout(row);
    }
    page.layout->addStretch();
    return page.widget;
}

QWidget* SettingsDialog::createPerformancePage()
{
    PageBox page = makePage(this);
    page.layout->addWidget(wrapped(tr("Kaint dibuja y aplica los efectos con el procesador, repartiendo el trabajo entre "
                                      "%1 hilos.").arg(QThread::idealThreadCount()),
                                   page.widget));
    page.layout->addWidget(sectionHeader(tr("Historial"), page.widget));
    auto* row = new QHBoxLayout;
    auto* limit = new QSpinBox(page.widget);
    limit->setRange(0, 1024 * 1024);
    limit->setSingleStep(256);
    limit->setValue(m_settings.historyLimitMB);
    connect(limit, &QSpinBox::valueChanged, this, [this](int v) {
        m_settings.historyLimitMB = v;
        changed();
    });
    row->addWidget(new QLabel(tr("Memoria máxima del historial de cada imagen:"), page.widget));
    row->addWidget(withUnit(limit, tr("MB"), tr("(0: sin límite)"), page.widget));
    row->addStretch();
    page.layout->addLayout(row);
    page.layout->addWidget(wrapped(tr("Si el historial de una imagen ocupa más, se descartan los pasos más antiguos "
                                      "(y se avisa en la barra de estado)."),
                                   page.widget));
    page.layout->addStretch();
    return page.widget;
}

QWidget* SettingsDialog::createPluginsPage()
{
    PageBox page = makePage(this);
    // Cada formato resaltado y, al dejar el puntero encima, de dónde viene (en vez de decirlo en el
    // texto, que ocupaba mucho).
    const QColor strong = palette().color(QPalette::Window).lightness() < 128 ? QColor(Qt::white) : QColor(Qt::black);
    const struct { const char* ext; QString tip; } formats[] = {
        {".so", tr("De Kaint")},
        {".dll", tr("De Paint.NET")},
        {".wasm", tr("Filtros de PhotoCraft")},
        {".cube", tr("Looks de LightCraft")},
        {".3dl", tr("Looks de LightCraft")},
        {".xmp", tr("Ajustes de LightCraft")},
        {".paintjs", tr("De paint.js (sin acceso a tus archivos ni a internet)")},
    };
    QStringList names;
    for (const auto& f : formats)
        names << QStringLiteral("<a href=\"%1\" style=\"color:%2; font-weight:600; text-decoration:none\">%1</a>")
                     .arg(QString::fromLatin1(f.ext), strong.name());
    auto* intro = new QLabel(tr("Complementos de efectos y formatos de archivo instalados (%1). Se guardan en %2; si un "
                                "complemento trae varios archivos, añádelos todos o copia su carpeta ahí. Aparecen en Efectos > "
                                "Complementos.")
                                 .arg(tr("%1, %2, %3, %4, %5, %6 y %7")
                                          .arg(names[0], names[1], names[2], names[3], names[4], names[5], names[6]),
                                      QDir::toNativeSeparators(PluginManager::instance()->userPath()).toHtmlEscaped()),
                             page.widget);
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    intro->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
    connect(intro, &QLabel::linkHovered, intro, [intro, formats](const QString& link) {
        // Sin la mano de los enlaces: no se pulsan, solo explican.
        intro->setCursor(Qt::ArrowCursor);
        for (const auto& f : formats)
            if (link == QLatin1String(f.ext)) {
                QToolTip::showText(QCursor::pos(), f.tip, intro);
                return;
            }
        QToolTip::hideText();
    });
    page.layout->addWidget(intro);
    m_pluginList = new QTreeWidget(page.widget);
    m_pluginList->setHeaderLabels({tr("Complemento"), tr("Tipo"), tr("Estado")});
    // Cada complemento con sus efectos debajo, cada uno con su casilla.
    m_pluginList->setRootIsDecorated(true);
    m_pluginList->setMinimumHeight(160);
    m_pluginList->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    page.layout->addWidget(m_pluginList, 2);
    m_pluginDetails = new QPlainTextEdit(page.widget);
    m_pluginDetails->setReadOnly(true);
    m_pluginDetails->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_pluginDetails->setMinimumHeight(100);
    page.layout->addWidget(m_pluginDetails, 1);
    m_showIncompatible = new QCheckBox(tr("Mostrar también los complementos que no son compatibles"), page.widget);
    connect(m_showIncompatible, &QCheckBox::toggled, this, &SettingsDialog::refreshPlugins);
    page.layout->addWidget(m_showIncompatible);
    auto* menuIcons = new QCheckBox(tr("Mostrar el icono de complemento junto a sus efectos en los menús"), page.widget);
    menuIcons->setToolTip(tr("En Ajustes y Efectos, los efectos que vienen de complementos llevan a la derecha la pieza de "
                             "puzle de los complementos."));
    menuIcons->setChecked(m_settings.pluginMenuIcons);
    connect(menuIcons, &QCheckBox::toggled, this, [this](bool on) {
        m_settings.pluginMenuIcons = on;
        changed();
    });
    page.layout->addWidget(menuIcons);

    auto* row = new QHBoxLayout;
    auto* add = new QPushButton(Icons::theme({"list-add", "document-import"}), tr("Añadir…"), page.widget);
    add->setToolTip(tr("Copiar complementos (.so, .dll, .wasm, .cube, .3dl, .xmp o .paintjs) a la carpeta de complementos"));
    connect(add, &QPushButton::clicked, this, &SettingsDialog::addPlugins);
    // El catálogo de complementos de paint.js (el de Explorar complementos de la versión web),
    // solo si se activó en Interfaz de usuario.
    auto* browse = new QPushButton(Icons::theme({"plugins", "application-x-addon"}), tr("Complementos de Kaint.HTML…"), page.widget);
    browse->setToolTip(tr("El catálogo de complementos de paint.js de la versión web de Kaint"));
    browse->setVisible(m_settings.kaintHtmlPlugins);
    m_kaintHtmlButton = browse;
    connect(browse, &QPushButton::clicked, this, [this] {
        PluginCatalogDialog dlg(this);
        dlg.exec();
        refreshPlugins();
    });
    auto* openFolder = new QPushButton(Icons::theme({"folder-open"}), tr("Abrir la carpeta"), page.widget);
    openFolder->setToolTip(tr("Abre la carpeta de complementos"));
    connect(openFolder, &QPushButton::clicked, this, [] {
        const QString dir = PluginManager::instance()->userPath();
        QDir().mkpath(dir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    m_openPluginFolder = new QPushButton(Icons::theme({"document-open-folder", "folder"}), tr("Abrir su carpeta"), page.widget);
    m_openPluginFolder->setToolTip(tr("Abre la carpeta donde está el complemento seleccionado"));
    connect(m_openPluginFolder, &QPushButton::clicked, this, [this] {
        if (QTreeWidgetItem* item = m_pluginList->currentItem())
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(item->data(0, Qt::UserRole).toString()).absolutePath()));
    });
    auto* rescan = new QToolButton(page.widget);
    rescan->setIcon(Icons::theme({"view-refresh"}));
    rescan->setToolTip(tr("Volver a buscar complementos"));
    connect(rescan, &QToolButton::clicked, this, [this] {
        PluginManager::instance()->scan();
        refreshPlugins();
    });
    row->addWidget(add);
    row->addWidget(openFolder);
    row->addWidget(m_openPluginFolder);
    row->addStretch();
    row->addWidget(rescan);
    page.layout->addLayout(row);
    // En su fila: con los demás, la de los botones no cabía a lo ancho.
    auto* catalogRow = new QHBoxLayout;
    catalogRow->addWidget(browse);
    catalogRow->addStretch();
    page.layout->addLayout(catalogRow);

    connect(m_pluginList, &QTreeWidget::currentItemChanged, this, &SettingsDialog::showPluginDetails);
    connect(m_pluginList, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (column != 0)
            return;
        const QString id = item->data(0, Qt::UserRole).toString();
        const bool on = item->checkState(0) == Qt::Checked;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        if (item->parent())
            PluginManager::instance()->setEffectEnabled(id, item->data(0, Qt::UserRole + 1).toString(), on);
        else
            PluginManager::instance()->setEnabled(id, on);
        QApplication::restoreOverrideCursor();
        QTimer::singleShot(0, this, &SettingsDialog::refreshPlugins);
    });
    // Lo plegado se recuerda mientras el diálogo está abierto (al marcar una casilla la lista se
    // vuelve a llenar).
    connect(m_pluginList, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem* item) {
        m_collapsedPlugins.insert(item->data(0, Qt::UserRole).toString());
    });
    connect(m_pluginList, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* item) {
        m_collapsedPlugins.remove(item->data(0, Qt::UserRole).toString());
    });
    connect(PluginManager::instance(), &PluginManager::changed, this, &SettingsDialog::refreshPlugins);
    refreshPlugins();
    return page.widget;
}

void SettingsDialog::refreshPlugins()
{
    const QTreeWidgetItem* current = m_pluginList->currentItem();
    const QString selected = current ? current->data(0, Qt::UserRole).toString() : QString();
    const QString selectedEffect = current && current->parent() ? current->data(0, Qt::UserRole + 1).toString() : QString();
    // Se conserva el desplazamiento: marcar o desmarcar una casilla no salta a la seleccionada.
    const QSignalBlocker block(m_pluginList);
    const int scroll = m_pluginList->verticalScrollBar()->value();
    m_pluginList->clear();
    for (const PluginInfo& p : PluginManager::instance()->plugins()) {
        const bool incompatible =
            !p.pending && p.effects.isEmpty() && p.fileTypes.isEmpty() && p.webOnlyEffects.isEmpty() && !p.errors.isEmpty();
        if (incompatible && !p.enabled && !m_showIncompatible->isChecked())
            continue;
        auto* item = new QTreeWidgetItem(m_pluginList);
        item->setText(0, p.name.isEmpty() ? p.file : p.name);
        item->setText(1, p.kind);
        QString state;
        if (p.pending)
            state = tr("Examinando…");
        else if (incompatible)
            state = tr("No compatible");
        else if (!p.enabled)
            state = tr("Desactivado");
        else if (!p.errors.isEmpty() && !p.loaded)
            state = tr("Error");
        else if (!p.errors.isEmpty())
            state = tr("Cargado con avisos");
        else if (!p.disabledEffects.isEmpty() && p.effectIds.size() == p.effects.size()) {
            int on = 0;
            for (const QString& e : p.effectIds)
                on += p.effectEnabled(e) ? 1 : 0;
            state = tr("%1 de %n efecto(s)", nullptr, int(p.effects.size())).arg(on);
        } else if (p.effects.isEmpty() && !p.fileTypes.isEmpty())
            state = tr("%n formato(s)", nullptr, int(p.fileTypes.size()));
        else if (p.effects.isEmpty() && !p.webOnlyEffects.isEmpty())
            state = tr("Solo en la versión web");
        else
            state = tr("%n efecto(s)", nullptr, int(p.effects.size()));
        item->setText(2, state);
        item->setIcon(0, Icons::theme(p.errors.isEmpty() ? std::initializer_list<const char*>{"plugins", "application-x-addon"}
                                                          : std::initializer_list<const char*>{"dialog-warning"}));
        if (incompatible && !p.enabled)
            item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
        else
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, p.enabled ? Qt::Checked : Qt::Unchecked);
        item->setData(0, Qt::UserRole, p.id);
        if (p.id == selected && selectedEffect.isEmpty())
            m_pluginList->setCurrentItem(item);
        // Sus efectos, cada uno con su casilla (como en Paint.NET). Con el complemento
        // desactivado se ven en gris, con lo que tenían marcado.
        for (int i = 0; i < p.effects.size() && i < p.effectIds.size(); ++i) {
            const QString& effectId = p.effectIds[i];
            auto* child = new QTreeWidgetItem(item);
            child->setText(0, p.effects[i]);
            child->setText(1, tr("Efecto"));
            const bool on = p.effectEnabled(effectId);
            child->setText(2, on ? tr("Activado") : tr("Desactivado"));
            const QImage icon = p.effectIcons.value(i);
            if (!icon.isNull())
                child->setIcon(0, QIcon(QPixmap::fromImage(icon)));
            child->setFlags((child->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsAutoTristate);
            if (!p.enabled || incompatible)
                child->setFlags(child->flags() & ~Qt::ItemIsEnabled);
            child->setCheckState(0, on ? Qt::Checked : Qt::Unchecked);
            child->setData(0, Qt::UserRole, p.id);
            child->setData(0, Qt::UserRole + 1, effectId);
            if (p.id == selected && effectId == selectedEffect)
                m_pluginList->setCurrentItem(child);
        }
        // paint.js: sus formatos de archivo y los efectos que solo funcionan en la versión web
        // (abren una ventana propia hecha para el navegador), sin casilla.
        auto infoChild = [&](const QString& name, const QString& type, const QString& childState, const QIcon& icon) {
            auto* child = new QTreeWidgetItem(item);
            child->setText(0, name);
            child->setText(1, type);
            child->setText(2, childState);
            child->setIcon(0, icon);
            child->setData(0, Qt::UserRole, p.id);
            child->setFlags(child->flags() & ~Qt::ItemIsUserCheckable);
            if (!p.enabled || incompatible)
                child->setFlags(child->flags() & ~Qt::ItemIsEnabled);
        };
        for (const QString& type : p.fileTypes)
            infoChild(type, tr("Formato"), p.loaded ? tr("En Abrir y Guardar como") : tr("Desactivado"),
                      Icons::theme({"image-x-generic", "document-open"}));
        for (int i = 0; i < p.webOnlyEffects.size(); ++i) {
            const QImage icon = p.webOnlyIcons.value(i);
            infoChild(p.webOnlyEffects[i], tr("Efecto"), tr("Solo en la versión web"),
                      icon.isNull() ? QIcon() : QIcon(QPixmap::fromImage(icon)));
            QTreeWidgetItem* child = item->child(item->childCount() - 1);
            child->setToolTip(0, tr("Abre una ventana propia hecha para el navegador, que la versión de escritorio de Kaint no puede "
                                    "mostrar. Úsalo en la versión web de Kaint."));
            child->setForeground(2, palette().color(QPalette::PlaceholderText));
        }
        item->setExpanded(!m_collapsedPlugins.contains(p.id));
    }
    if (!m_pluginList->currentItem() && m_pluginList->topLevelItemCount() > 0) {
        // Si se desmarcó el último efecto de la lista u otro que ya no está, el complemento.
        QTreeWidgetItem* fallback = m_pluginList->topLevelItem(0);
        for (int i = 0; i < m_pluginList->topLevelItemCount(); ++i)
            if (m_pluginList->topLevelItem(i)->data(0, Qt::UserRole).toString() == selected)
                fallback = m_pluginList->topLevelItem(i);
        m_pluginList->setCurrentItem(fallback);
    }
    showPluginDetails();
    m_openPluginFolder->setEnabled(m_pluginList->currentItem() != nullptr);
    m_pluginList->resizeColumnToContents(1);
    m_pluginList->resizeColumnToContents(2);
    m_pluginList->doItemsLayout();
    m_pluginList->verticalScrollBar()->setValue(scroll);
}

void SettingsDialog::addPlugins()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Añadir complementos"), QDir::homePath(),
        tr("Complementos (*.dll *.so *.wasm *.cube *.3dl *.xmp *.paintjs);;Complementos de Paint.NET (*.dll);;Complementos de Kaint (*.so);;"
           "Filtros de PhotoCraft (*.wasm);;Looks y ajustes de LightCraft (*.cube *.3dl *.xmp);;Complementos de paint.js (*.paintjs)"));
    if (files.isEmpty())
        return;
    PluginManager* pm = PluginManager::instance();
    bool replace = false;
    const QStringList existing = pm->installedAlready(files);
    if (!existing.isEmpty()) {
        QStringList names;
        for (const QString& f : existing)
            names << QFileInfo(f).fileName();
        const auto answer = QMessageBox::question(
            this, tr("Añadir complementos"),
            tr("Ya hay complementos con estos nombres:\n\n%1\n\n¿Quieres sustituirlos?").arg(names.join(QLatin1Char('\n'))),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::No);
        if (answer == QMessageBox::Cancel)
            return;
        replace = answer == QMessageBox::Yes;
    }
    QStringList errors;
    const QStringList copied = pm->install(files, replace, &errors);
    if (!errors.isEmpty())
        QMessageBox::warning(this, tr("Añadir complementos"),
                             tr("No se pudieron copiar:\n\n%1").arg(errors.join(QLatin1Char('\n'))));
    if (copied.isEmpty())
        return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    pm->scan();
    // Los que se sustituyeron estando cargados, con la versión nueva.
    if (replace)
        for (const QString& f : existing)
            pm->reload(QFileInfo(pm->userPath() + QLatin1Char('/') + QFileInfo(f).fileName()).absoluteFilePath());
    QApplication::restoreOverrideCursor();
    refreshPlugins();
    for (int i = 0; i < m_pluginList->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = m_pluginList->topLevelItem(i);
        if (copied.contains(item->data(0, Qt::UserRole).toString())) {
            m_pluginList->setCurrentItem(item);
            break;
        }
    }
}

void SettingsDialog::showPluginDetails()
{
    QTreeWidgetItem* item = m_pluginList->currentItem();
    if (!item) {
        m_pluginDetails->setPlainText(tr("No hay complementos instalados."));
        return;
    }
    const QString id = item->data(0, Qt::UserRole).toString();
    for (const PluginInfo& p : PluginManager::instance()->plugins()) {
        if (p.id != id)
            continue;
        QStringList lines;
        lines << QDir::toNativeSeparators(p.id) + (p.version.isEmpty() ? QString() : tr(", versión %1").arg(p.version));
        lines << QString();
        auto field = [&](const QString& name, const QString& value) {
            if (!value.isEmpty())
                lines << QStringLiteral("%1: %2").arg(name, value);
        };
        field(tr("Nombre"), p.name);
        field(tr("Tipo"), p.kind);
        field(tr("Versión"), p.version);
        field(tr("Autor"), p.author);
        field(tr("Derechos de autor"), p.copyright);
        field(tr("Licencia"), p.license);
        field(tr("Sitio web"), p.website);
        field(tr("Descripción"), p.description);
        if (p.kind == QLatin1String("Paint.NET"))
            lines << QString()
                  << tr("Complemento de terceros para Paint.NET, sin modificar: Kaint lo ejecuta con su capa de "
                        "compatibilidad. Su autor no participa en Kaint ni da soporte a este uso.");
        if (!p.effects.isEmpty()) {
            lines << QString() << tr("Efectos:");
            for (int i = 0; i < p.effects.size(); ++i) {
                const bool off = i < p.effectIds.size() && !p.effectEnabled(p.effectIds[i]);
                lines << QStringLiteral("  ") + p.effects[i] + (off ? tr(" (desactivado)") : QString());
            }
        }
        if (!p.fileTypes.isEmpty())
            lines << QString() << tr("Formatos de archivo (en Abrir y Guardar como):") << QStringLiteral("  ") + p.fileTypes.join(QStringLiteral("\n  "));
        if (!p.webOnlyEffects.isEmpty())
            lines << QString()
                  << tr("Solo en la versión web de Kaint (abren una ventana propia hecha para el navegador):")
                  << QStringLiteral("  ") + p.webOnlyEffects.join(QStringLiteral("\n  "));
        if (p.kind == QLatin1String("paint.js"))
            lines << QString()
                  << tr("Complemento de paint.js: Kaint lo ejecuta con su propio motor de JavaScript, sin acceso a tus "
                        "archivos ni a internet.");
        if (!p.errors.isEmpty())
            lines << QString() << tr("Problemas:") << QStringLiteral("  ") + p.errors.join(QStringLiteral("\n  "));
        if (p.pending)
            lines << QString() << tr("Kaint está examinando este complemento…");
        else if (!p.enabled)
            lines << QString() << tr("Este complemento está desactivado. Marca la casilla para cargarlo.");
        m_pluginDetails->setPlainText(lines.join(QLatin1Char('\n')));
        return;
    }
}

// Personalización: en qué menú sale cada efecto (también los de los complementos), qué efectos se
// ocultan y qué herramientas hay en la ventana Herramientas y en qué orden.
QWidget* SettingsDialog::createCustomizePage(ToolManager* tools)
{
    m_toolManager = tools;
    PageBox page = makePage(this);
    Customization* custom = Customization::instance();
    page.layout->addWidget(wrapped(tr("Ordena los menús a tu gusto: mueve cualquier efecto (también los de los complementos) "
                                      "a Ajustes, a Efectos o a otro submenú, por ejemplo a Efectos > Desenfoques, u ocúltalo "
                                      "quitando su casilla. Debajo, las herramientas de la ventana Herramientas."),
                                   page.widget));

    page.layout->addWidget(sectionHeader(tr("Menús de efectos"), page.widget));
    m_effectMenus = new QTreeWidget(page.widget);
    m_effectMenus->setHeaderLabels({tr("Efecto"), tr("Complemento")});
    m_effectMenus->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_effectMenus->setMinimumHeight(240);
    m_effectMenus->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    page.layout->addWidget(m_effectMenus, 2);

    auto* effectButtons = new QHBoxLayout;
    auto* moveTo = new QPushButton(Icons::theme({"go-jump", "transform-move", "edit-move"}), tr("Mover a"), page.widget);
    moveTo->setToolTip(tr("Pasa los efectos elegidos a otro menú o submenú"));
    auto* moveMenu = new QMenu(moveTo);
    moveTo->setMenu(moveMenu);
    auto* resetSelected = new QPushButton(tr("Restablecer"), page.widget);
    resetSelected->setToolTip(tr("Devuelve los efectos elegidos a su sitio de siempre y los muestra"));
    auto* resetAll = new QPushButton(tr("Restablecer todo"), page.widget);
    resetAll->setToolTip(tr("Todos los efectos en su sitio de siempre, sin ninguno oculto"));
    effectButtons->addWidget(moveTo);
    effectButtons->addWidget(resetSelected);
    effectButtons->addStretch();
    effectButtons->addWidget(resetAll);
    page.layout->addLayout(effectButtons);

    auto selectedEffects = [this] {
        QStringList ids;
        for (QTreeWidgetItem* item : m_effectMenus->selectedItems())
            if (item->parent())
                ids << item->data(0, Qt::UserRole).toString();
        return ids;
    };
    auto syncButtons = [this, moveTo, resetSelected, resetAll, selectedEffects] {
        const QStringList ids = selectedEffects();
        moveTo->setEnabled(!ids.isEmpty());
        bool changed = false;
        for (const QString& id : ids)
            changed = changed || Customization::instance()->isMoved(id) || Customization::instance()->isHidden(id);
        resetSelected->setEnabled(changed);
        resetAll->setEnabled(Customization::instance()->hasEffectChanges());
    };
    connect(m_effectMenus, &QTreeWidget::itemSelectionChanged, this, syncButtons);
    connect(m_effectMenus, &QTreeWidget::itemChanged, this, [](QTreeWidgetItem* item, int column) {
        if (column != 0 || !item->parent())
            return;
        Customization::instance()->setHidden({item->data(0, Qt::UserRole).toString()}, item->checkState(0) != Qt::Checked);
    });
    connect(moveMenu, &QMenu::aboutToShow, this, [this, moveMenu, selectedEffects] {
        // Ajustes, Efectos y cada submenú que hay (los de Kaint, los de los complementos y los
        // nuevos), y uno nuevo.
        moveMenu->clear();
        QStringList locations{QStringLiteral("adjustments"), QStringLiteral("effects")};
        QStringList submenus;
        for (const Effect* e : Effects::all()) {
            if (e->hidden())
                continue;
            for (const QString& loc : {Customization::defaultLocation(e), Customization::instance()->location(e)})
                if (loc.startsWith(QLatin1String("effects/")) && !submenus.contains(loc))
                    submenus << loc;
        }
        std::sort(submenus.begin(), submenus.end(), [](const QString& a, const QString& b) {
            return Customization::locationName(a).localeAwareCompare(Customization::locationName(b)) < 0;
        });
        auto addTarget = [this, moveMenu, selectedEffects](const QString& loc) {
            moveMenu->addAction(Customization::locationName(loc), this, [this, loc, selectedEffects] {
                Customization::instance()->setLocation(selectedEffects(), loc);
                refreshEffectMenus();
            });
        };
        for (const QString& loc : std::as_const(locations))
            addTarget(loc);
        moveMenu->addSeparator();
        for (const QString& loc : std::as_const(submenus))
            addTarget(loc);
        moveMenu->addSeparator();
        moveMenu->addAction(tr("Nuevo submenú de Efectos…"), this, [this, selectedEffects] {
            bool ok = false;
            const QString name = QInputDialog::getText(this, tr("Nuevo submenú"), tr("Nombre del submenú de Efectos:"),
                                                       QLineEdit::Normal, QString(), &ok)
                                     .simplified()
                                     .remove(QLatin1Char('/'));
            if (!ok || name.isEmpty())
                return;
            Customization::instance()->setLocation(selectedEffects(),
                                                   QStringLiteral("effects/") + Customization::submenuKey(name));
            refreshEffectMenus();
        });
    });
    connect(resetSelected, &QPushButton::clicked, this, [this, selectedEffects] {
        Customization::instance()->resetEffects(selectedEffects());
        refreshEffectMenus();
    });
    connect(resetAll, &QPushButton::clicked, this, [this] {
        Customization::instance()->resetEffects();
        refreshEffectMenus();
    });
    connect(PluginManager::instance(), &PluginManager::changed, this, &SettingsDialog::refreshEffectMenus);
    connect(custom, &Customization::effectMenusChanged, this, syncButtons);

    page.layout->addWidget(sectionHeader(tr("Ventana Herramientas"), page.widget));
    page.layout->addWidget(wrapped(tr("Quita la casilla de las herramientas que no uses y cambia su orden. Sus atajos de "
                                      "teclado siguen funcionando aunque estén ocultas."),
                                   page.widget));
    auto* toolsRow = new QHBoxLayout;
    m_toolOrder = new QListWidget(page.widget);
    m_toolOrder->setIconSize(QSize(20, 20));
    m_toolOrder->setMinimumHeight(220);
    m_toolOrder->setDragDropMode(QAbstractItemView::InternalMove);
    toolsRow->addWidget(m_toolOrder, 1);
    auto* toolButtons = new QVBoxLayout;
    auto* up = new QPushButton(Icons::theme({"go-up", "arrow-up"}), tr("Subir"), page.widget);
    auto* down = new QPushButton(Icons::theme({"go-down", "arrow-down"}), tr("Bajar"), page.widget);
    auto* resetTools = new QPushButton(tr("Restablecer"), page.widget);
    resetTools->setToolTip(tr("Todas las herramientas, en el orden de Paint.NET"));
    toolButtons->addWidget(up);
    toolButtons->addWidget(down);
    toolButtons->addStretch();
    toolButtons->addWidget(resetTools);
    toolsRow->addLayout(toolButtons);
    page.layout->addLayout(toolsRow);
    auto saveOrder = [this] {
        QList<int> order;
        for (int i = 0; i < m_toolOrder->count(); ++i)
            order << m_toolOrder->item(i)->data(Qt::UserRole).toInt();
        Customization::instance()->setToolOrder(order);
    };
    auto move = [this, saveOrder](int delta) {
        const int row = m_toolOrder->currentRow();
        if (row < 0 || row + delta < 0 || row + delta >= m_toolOrder->count())
            return;
        const QSignalBlocker block(m_toolOrder);
        QListWidgetItem* item = m_toolOrder->takeItem(row);
        m_toolOrder->insertItem(row + delta, item);
        m_toolOrder->setCurrentRow(row + delta);
        saveOrder();
    };
    connect(up, &QPushButton::clicked, this, [move] { move(-1); });
    connect(down, &QPushButton::clicked, this, [move] { move(+1); });
    connect(resetTools, &QPushButton::clicked, this, [this] {
        Customization::instance()->resetTools();
        refreshToolOrder();
    });
    connect(m_toolOrder->model(), &QAbstractItemModel::rowsMoved, this, [saveOrder] { saveOrder(); });
    connect(m_toolOrder, &QListWidget::itemChanged, this, [](QListWidgetItem* item) {
        Customization::instance()->setToolHidden(item->data(Qt::UserRole).toInt(), item->checkState() != Qt::Checked);
    });
    refreshEffectMenus();
    refreshToolOrder();
    syncButtons();
    return page.widget;
}

void SettingsDialog::refreshEffectMenus()
{
    if (!m_effectMenus)
        return;
    QSet<QString> selected;
    for (QTreeWidgetItem* item : m_effectMenus->selectedItems())
        selected.insert(item->data(0, Qt::UserRole).toString());
    const int scroll = m_effectMenus->verticalScrollBar()->value();
    const QSignalBlocker block(m_effectMenus);
    m_effectMenus->clear();
    const Customization* custom = Customization::instance();
    // Agrupados como en los menús: Ajustes, Efectos y sus submenús, en orden alfabético.
    QMap<QString, QList<const Effect*>> groups;
    for (const Effect* e : Effects::all())
        if (!e->hidden())
            groups[custom->location(e)].append(e);
    QStringList order = groups.keys();
    std::sort(order.begin(), order.end(), [](const QString& a, const QString& b) {
        auto rank = [](const QString& l) { return l == QLatin1String("adjustments") ? 0 : l == QLatin1String("effects") ? 1 : 2; };
        if (rank(a) != rank(b))
            return rank(a) < rank(b);
        return Customization::locationName(a).localeAwareCompare(Customization::locationName(b)) < 0;
    });
    for (const QString& loc : std::as_const(order)) {
        auto* group = new QTreeWidgetItem(m_effectMenus);
        group->setText(0, Customization::locationName(loc));
        group->setData(0, Qt::UserRole, QStringLiteral("group:") + loc);
        group->setFlags(Qt::ItemIsEnabled);
        QFont bold = group->font(0);
        bold.setBold(true);
        group->setFont(0, bold);
        QList<const Effect*> effects = groups.value(loc);
        std::sort(effects.begin(), effects.end(), [](const Effect* a, const Effect* b) {
            return a->name().localeAwareCompare(b->name()) < 0;
        });
        for (const Effect* e : std::as_const(effects)) {
            auto* item = new QTreeWidgetItem(group);
            QString name = e->name();
            if (custom->isMoved(e->id()))
                name += tr("  (antes en %1)").arg(Customization::locationName(Customization::defaultLocation(e)));
            item->setText(0, name);
            item->setText(1, e->pluginName());
            QIcon icon = Icons::theme(Icons::effectIcon(e->id(), e->iconName()));
            if (icon.isNull() && !e->iconImage().isNull())
                icon = QIcon(QPixmap::fromImage(e->iconImage()));
            item->setIcon(0, icon);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
            item->setCheckState(0, custom->isHidden(e->id()) ? Qt::Unchecked : Qt::Checked);
            item->setToolTip(0, tr("Quita la casilla para que no salga en los menús"));
            item->setData(0, Qt::UserRole, e->id());
            if (selected.contains(e->id()))
                item->setSelected(true);
        }
        group->setExpanded(true);
    }
    m_effectMenus->resizeColumnToContents(1);
    m_effectMenus->verticalScrollBar()->setValue(scroll);
}

void SettingsDialog::refreshToolOrder()
{
    if (!m_toolOrder || !m_toolManager)
        return;
    const QSignalBlocker block(m_toolOrder);
    const int current = m_toolOrder->currentItem() ? m_toolOrder->currentItem()->data(Qt::UserRole).toInt() : -1;
    m_toolOrder->clear();
    QList<int> defaults;
    for (Tool* t : m_toolManager->tools())
        defaults << int(t->id());
    for (int id : Customization::instance()->toolOrder(defaults)) {
        Tool* t = m_toolManager->tool(ToolId(id));
        if (!t)
            continue;
        auto* item = new QListWidgetItem(t->icon(), t->name(), m_toolOrder);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled);
        item->setCheckState(Customization::instance()->isToolHidden(id) ? Qt::Unchecked : Qt::Checked);
        item->setData(Qt::UserRole, id);
        if (id == current)
            m_toolOrder->setCurrentItem(item);
    }
}

QWidget* SettingsDialog::createDiscordPage()
{
    PageBox page = makePage(this);
    page.layout->addWidget(wrapped(tr("Muestra en tu perfil de Discord en qué estás trabajando con Kaint (Rich "
                                      "Presence). Funciona con la aplicación de Discord abierta en este equipo, también "
                                      "la versión Flatpak o Snap."),
                                   page.widget));
    auto* enabled = new QCheckBox(tr("Mostrar mi actividad en Discord"), page.widget);
    enabled->setChecked(m_settings.discordEnabled);
    page.layout->addWidget(enabled);

    page.layout->addWidget(sectionHeader(tr("Qué se muestra"), page.widget));
    auto* details = new QWidget(page.widget);
    auto* detailsLayout = new QVBoxLayout(details);
    detailsLayout->setContentsMargins(0, 0, 0, 0);
    auto option = [&](const QString& text, bool AppSettings::*member) {
        auto* check = new QCheckBox(text, details);
        check->setChecked(m_settings.*member);
        connect(check, &QCheckBox::toggled, this, [this, member](bool on) {
            m_settings.*member = on;
            changed();
        });
        detailsLayout->addWidget(check);
    };
    option(tr("El nombre del archivo"), &AppSettings::discordShowFile);
    option(tr("La capa activa (nombre y posición)"), &AppSettings::discordShowLayer);
    option(tr("El tipo de archivo y el tamaño de la imagen"), &AppSettings::discordShowFileInfo);
    option(tr("La herramienta que estoy usando"), &AppSettings::discordShowTool);
    option(tr("El tiempo que llevo en Kaint"), &AppSettings::discordShowElapsed);
    page.layout->addWidget(details);

    // La aplicación de Discord (su ID) viene de discord.json: no se cambia aquí.
    m_discordStatus = wrapped(QString(), page.widget);
    page.layout->addWidget(m_discordStatus);

    auto sync = [details](bool on) { details->setEnabled(on); };
    sync(m_settings.discordEnabled);
    connect(enabled, &QCheckBox::toggled, this, [this, sync](bool on) {
        m_settings.discordEnabled = on;
        sync(on);
        changed();
    });
    page.layout->addStretch();
    return page.widget;
}

void SettingsDialog::setDiscordStatus(const QString& text)
{
    if (m_discordStatus)
        m_discordStatus->setText(tr("Estado: %1").arg(text));
}

QWidget* SettingsDialog::createUpdatesPage()
{
    PageBox page = makePage(this);
    QStringList info;
    info << tr("Versión instalada: %1").arg(QStringLiteral(KAINT_VERSION));
#ifdef KAINT_UPDATE_CHECK
    auto* automatic = new QCheckBox(tr("Buscar actualizaciones automáticamente al iniciar Kaint"), page.widget);
    automatic->setChecked(m_settings.checkUpdates);
    connect(automatic, &QCheckBox::toggled, this, [this](bool on) {
        m_settings.checkUpdates = on;
        changed();
    });
    page.layout->addWidget(automatic);
    auto* reminder = new QCheckBox(tr("Avisar cuando haya una versión nueva"), page.widget);
    reminder->setToolTip(tr("Desactivado (o con «No volver a recordar» en el aviso), la versión nueva solo se ve "
                            "aquí; «Buscar ahora» siempre avisa."));
    reminder->setChecked(m_settings.updateReminder);
    reminder->setEnabled(m_settings.checkUpdates);
    connect(reminder, &QCheckBox::toggled, this, [this](bool on) {
        m_settings.updateReminder = on;
        changed();
    });
    connect(automatic, &QCheckBox::toggled, reminder, &QWidget::setEnabled);
    page.layout->addWidget(reminder);
    auto* row = new QHBoxLayout;
    auto* now = new QPushButton(tr("Buscar ahora"), page.widget);
    connect(now, &QPushButton::clicked, this, &SettingsDialog::checkUpdatesRequested);
    row->addWidget(now);
    row->addStretch();
    page.layout->addLayout(row);
    auto* beta = new QCheckBox(tr("Buscar también versiones preliminares (beta) de Kaint"), page.widget);
    beta->setChecked(m_settings.updatesBeta);
    connect(beta, &QCheckBox::toggled, this, [this](bool on) {
        m_settings.updatesBeta = on;
        changed();
    });
    page.layout->addWidget(beta);
    info << tr("Las versiones nuevas se publican en %1").arg(QStringLiteral(KAINT_REPOSITORY));
    if (m_settings.lastUpdateCheck.isValid())
        info << tr("Última comprobación: %1 · versión más reciente: %2")
                    .arg(QLocale().toString(m_settings.lastUpdateCheck, QLocale::ShortFormat),
                         m_settings.latestVersion.isEmpty() ? tr("ninguna publicada") : m_settings.latestVersion);
#else
    // Compilado sin la búsqueda en GitHub (Flatpak, paquetes de distribuciones): las versiones
    // nuevas llegan con el gestor de software.
    info << tr("Kaint se actualiza desde el gestor de software del sistema (Discover, GNOME Software "
               "o «flatpak update»).");
#endif
    m_updateStatus = wrapped(info.join(QLatin1Char('\n')), page.widget);
    page.layout->addWidget(m_updateStatus);
    page.layout->addStretch();
    return page.widget;
}

QString SettingsDialog::diagnostics()
{
    QList<std::pair<QString, QString>> rows;
    auto row = [&](const QString& k, const QString& v) { rows.append({k, v}); };
    auto blank = [&] { rows.append({QString(), QString()}); };
    row(QStringLiteral("Application"), QStringLiteral("Kaint v%1").arg(QStringLiteral(KAINT_VERSION)));
    row(QStringLiteral("Build Date"), QStringLiteral(__DATE__ " " __TIME__));
    row(QStringLiteral("Current Date and Time"), QDateTime::currentDateTime().toString(Qt::ISODate));
    row(QStringLiteral("Qt"), QStringLiteral("%1 (compilado con %2)").arg(QString::fromLatin1(qVersion()), QStringLiteral(QT_VERSION_STR)));
    row(QStringLiteral("Repository"), QStringLiteral(KAINT_REPOSITORY));
    blank();
    row(QStringLiteral("Install directory"), QDir::toNativeSeparators(QCoreApplication::applicationDirPath()));
    row(QStringLiteral("Settings file"), QDir::toNativeSeparators(QSettings().fileName()));
    row(QStringLiteral("Plugins directory"), QDir::toNativeSeparators(PluginManager::instance()->userPath()));
    blank();
    row(QStringLiteral("Language"), QLocale().name());
    row(QStringLiteral("Platform"), QGuiApplication::platformName());
    row(QStringLiteral("Desktop"), qEnvironmentVariable("XDG_CURRENT_DESKTOP"));
    row(QStringLiteral("Widget style"), QApplication::style()->name());
    row(QStringLiteral("Icon theme"), QIcon::themeName());
    const QStringList schemes{QStringLiteral("System"), QStringLiteral("Light"), QStringLiteral("Dark"),
                              QStringLiteral("KDE"), QStringLiteral("Custom")};
    row(QStringLiteral("Color scheme"), QStringLiteral("%1 (%2, system %3)")
                                            .arg(schemes.value(int(Theme::current())),
                                                 Theme::isDark() ? QStringLiteral("dark") : QStringLiteral("light"),
                                                 Theme::systemPrefersDark() ? QStringLiteral("dark") : QStringLiteral("light")));
    for (const QScreen* s : QGuiApplication::screens())
        row(QStringLiteral("Display"), QStringLiteral("%1: %2 x %3, %4x, %5 DPI")
                                            .arg(s->name())
                                            .arg(s->size().width())
                                            .arg(s->size().height())
                                            .arg(s->devicePixelRatio())
                                            .arg(qRound(s->logicalDotsPerInch())));
    int tablets = 0;
    for (const QInputDevice* d : QInputDevice::devices())
        if (d->type() == QInputDevice::DeviceType::Stylus || d->type() == QInputDevice::DeviceType::Airbrush)
            ++tablets;
    row(QStringLiteral("Tablets"), QString::number(tablets));
    blank();
    row(QStringLiteral("OS"), QSysInfo::prettyProductName());
    row(QStringLiteral("Kernel"), QSysInfo::kernelType() + QLatin1Char(' ') + QSysInfo::kernelVersion());
    row(QStringLiteral("CPU"), QStringLiteral("%1, %2 hilos").arg(QSysInfo::currentCpuArchitecture()).arg(QThread::idealThreadCount()));
    QFile meminfo(QStringLiteral("/proc/meminfo"));
    if (meminfo.open(QIODevice::ReadOnly)) {
        const QString text = QString::fromLatin1(meminfo.readAll());
        for (const QString& line : text.split(QLatin1Char('\n')))
            if (line.startsWith(QLatin1String("MemTotal")) || line.startsWith(QLatin1String("MemAvailable")))
                row(QStringLiteral("Memory ") + line.section(QLatin1Char(':'), 0, 0).mid(3), line.section(QLatin1Char(':'), 1).trimmed());
    }
    row(QStringLiteral("mono"), QStandardPaths::findExecutable(QStringLiteral("mono")));
    row(QStringLiteral("dotnet"), QStandardPaths::findExecutable(QStringLiteral("dotnet")));
    blank();
    QStringList readFormats, writeFormats;
    for (const QByteArray& f : QImageReader::supportedImageFormats())
        readFormats << QString::fromLatin1(f);
    for (const QByteArray& f : QImageWriter::supportedImageFormats())
        writeFormats << QString::fromLatin1(f);
    row(QStringLiteral("Image formats (read)"), readFormats.join(QLatin1Char(' ')));
    row(QStringLiteral("Image formats (write)"), writeFormats.join(QLatin1Char(' ')));
    blank();
    const QList<PluginInfo>& plugins = PluginManager::instance()->plugins();
    row(QStringLiteral("Plugins"), QString::number(plugins.size()));
    for (const PluginInfo& p : plugins)
        row(QStringLiteral("    ") + p.file, QStringLiteral("%1, %2%3")
                                             .arg(p.kind, p.enabled ? (p.loaded ? QStringLiteral("loaded") : QStringLiteral("error"))
                                                                    : QStringLiteral("disabled"),
                                                  p.errors.isEmpty() ? QString() : QStringLiteral(" (") + p.errors.join(QStringLiteral("; ")) + QLatin1Char(')')));
    QString out;
    for (const auto& [k, v] : rows)
        out += k.isEmpty() ? QStringLiteral("\n") : QStringLiteral("%1%2\n").arg(k.leftJustified(40), v);
    return out;
}

QWidget* SettingsDialog::createDiagnosticsPage()
{
    PageBox page = makePage(this);
    auto* tree = new QTreeWidget(page.widget);
    tree->setHeaderLabels({QStringLiteral("Item"), QStringLiteral("Value")});
    tree->setRootIsDecorated(false);
    tree->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    const QString text = diagnostics();
    for (const QString& line : text.split(QLatin1Char('\n'))) {
        auto* item = new QTreeWidgetItem(tree);
        item->setText(0, line.left(40).trimmed());
        item->setText(1, line.mid(40));
    }
    tree->resizeColumnToContents(0);
    page.layout->addWidget(tree, 1);
    auto* row = new QHBoxLayout;
    auto* copy = new QPushButton(Icons::theme({"edit-copy"}), tr("Copiar en el portapapeles"), page.widget);
    connect(copy, &QPushButton::clicked, this, [] { QApplication::clipboard()->setText(diagnostics()); });
    auto* folder = new QPushButton(Icons::theme({"folder-open"}), tr("Abrir la carpeta de configuración"), page.widget);
    connect(folder, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(QSettings().fileName()).absolutePath()));
    });
    row->addWidget(copy);
    row->addWidget(folder);
    row->addStretch();
    page.layout->addLayout(row);
    return page.widget;
}

// ---------------------------------------------------------------------------
// Paquetes de iconos, fuentes, formas y atajos de teclado (las páginas propias de paint.js)
// ---------------------------------------------------------------------------

namespace {

QPushButton* actionButton(const QIcon& icon, const QString& text, const QString& tip, QWidget* parent)
{
    auto* b = new QPushButton(icon, text, parent);
    b->setToolTip(tip);
    return b;
}

void openFolder(const QString& dir)
{
    QDir().mkpath(dir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

} // namespace

namespace {

// Vista previa de un paquete: una tira con iconos de los menús y de las herramientas, los del
// paquete o, si no los trae, los de Kaint.
QPixmap iconPackPreview(const QString& folder)
{
    static const char* const files[] = {
        "menu_file_new_icon", "menu_file_open_icon", "menu_file_save_icon", "menu_edit_cut_icon",
        "menu_edit_copy_icon", "menu_edit_paste_icon", "menu_edit_undo_icon", "menu_layers_add_new_layer_icon",
        "menu_image_crop_icon", "menu_utilities_settings_icon", "paint_brush_tool_icon", "zoom_tool_icon",
    };
    constexpr int side = 20, gap = 4;
    const int count = int(std::size(files));
    const qreal dpr = qApp->devicePixelRatio();
    QPixmap strip(QSize(count * (side + gap) - gap, side) * dpr);
    strip.setDevicePixelRatio(dpr);
    strip.fill(Qt::transparent);
    QPainter p(&strip);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    for (int i = 0; i < count; ++i) {
        const QString name = QString::fromLatin1(files[i]);
        QImage image;
        for (const QString& ext : {QStringLiteral("png"), QStringLiteral("webp")}) {
            if (!folder.isEmpty() && image.isNull())
                image.load(QStringLiteral("%1/icons/%2.%3").arg(folder, name, ext));
        }
        // Las herramientas de Kaint son dibujos; los menús, los PNG incluidos.
        if (image.isNull() && name.endsWith(QLatin1String("_tool_icon")))
            image = Icons::pdn(name == QLatin1String("zoom_tool_icon") ? QStringLiteral("zoom") : QStringLiteral("paintbrush"))
                        .pixmap(QSize(side, side), dpr)
                        .toImage();
        if (image.isNull())
            image.load(QStringLiteral(":/kaint/ui-icons/%1.png").arg(name));
        if (image.isNull())
            continue;
        const QImage scaled = image.scaled(QSize(side, side) * dpr, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        p.drawImage(QRectF(i * (side + gap) + (side - scaled.width() / dpr) / 2, (side - scaled.height() / dpr) / 2,
                           scaled.width() / dpr, scaled.height() / dpr),
                    scaled);
    }
    return strip;
}

} // namespace

QWidget* SettingsDialog::createIconPacksPage()
{
    PageBox page = makePage(this);
    page.layout->addWidget(wrapped(tr("Los paquetes de iconos (archivos .zip con asset-pack.json) cambian los "
                                      "iconos de Kaint, también los de las herramientas. Los iconos que no traiga un "
                                      "paquete siguen siendo los de Kaint. Se guardan en este equipo."),
                                   page.widget));
    m_iconPacks = new QListWidget(page.widget);
    // Cada paquete con su nombre y, debajo, una tira con algunos de sus iconos.
    m_iconPacks->setIconSize(QSize(12 * 24 - 4, 20));
    m_iconPacks->setViewMode(QListView::ListMode);
    m_iconPacks->setSpacing(2);
    m_iconPacks->setMinimumHeight(180);
    page.layout->addWidget(m_iconPacks, 1);
    m_iconPackDetails = wrapped(QString(), page.widget);
    page.layout->addWidget(m_iconPackDetails);

    auto* row = new QHBoxLayout;
    auto* use = actionButton(Icons::theme({"dialog-ok-apply", "dialog-ok"}), tr("Usar"), tr("Usar el paquete elegido"), page.widget);
    auto* add = actionButton(Icons::theme({"list-add", "document-import"}), tr("Añadir…"), tr("Instalar un paquete de iconos (.zip)"),
                             page.widget);
    auto* remove = actionButton(Icons::theme({"list-remove", "edit-delete"}), tr("Quitar"), tr("Desinstalar el paquete elegido"),
                                page.widget);
    auto* folder = actionButton(Icons::theme({"folder-open"}), tr("Abrir la carpeta"), tr("Abre la carpeta de los paquetes de iconos"),
                                page.widget);
    for (QPushButton* b : {use, add, remove, folder})
        row->addWidget(b);
    row->addStretch();
    page.layout->addLayout(row);

    auto selected = [this] {
        const QListWidgetItem* item = m_iconPacks->currentItem();
        return item ? item->data(Qt::UserRole).toString() : QString();
    };
    auto updateButtons = [this, use, remove, selected] {
        const QString id = selected();
        use->setEnabled(!id.isEmpty() && id != IconPacks::activeId());
        remove->setEnabled(!id.isEmpty() && id != IconPacks::defaultId());
        for (const IconPacks::Pack& p : IconPacks::installed()) {
            if (p.id != id)
                continue;
            QStringList lines;
            if (!p.description.isEmpty())
                lines << p.description;
            if (!p.folder.isEmpty())
                lines << tr("%n imagen(es)", nullptr, p.images);
            m_iconPackDetails->setText(lines.join(QLatin1Char('\n')));
        }
    };
    connect(m_iconPacks, &QListWidget::currentRowChanged, this, updateButtons);
    connect(m_iconPacks, &QListWidget::itemDoubleClicked, use, &QPushButton::click);
    connect(use, &QPushButton::clicked, this, [this, selected, updateButtons] {
        IconPacks::activate(selected());
        refreshIconPacks();
        updateButtons();
    });
    connect(add, &QPushButton::clicked, this, [this, updateButtons] {
        const QString file = QFileDialog::getOpenFileName(this, tr("Añadir un paquete de iconos"), QDir::homePath(),
                                                          tr("Paquetes de iconos (*.zip)"));
        if (file.isEmpty())
            return;
        QString error;
        const QString id = IconPacks::install(file, &error);
        if (id.isEmpty()) {
            QMessageBox::warning(this, tr("Añadir un paquete de iconos"), error);
            return;
        }
        if (!error.isEmpty())
            QMessageBox::information(this, tr("Añadir un paquete de iconos"), error);
        // Como en paint.js, el paquete instalado pasa a usarse.
        IconPacks::activate(id);
        refreshIconPacks();
        updateButtons();
    });
    connect(remove, &QPushButton::clicked, this, [this, selected, updateButtons] {
        const QString id = selected();
        if (id.isEmpty() || id == IconPacks::defaultId())
            return;
        if (QMessageBox::question(this, tr("Quitar el paquete de iconos"), tr("¿Quieres quitar este paquete de iconos?"))
            != QMessageBox::Yes)
            return;
        IconPacks::remove(id);
        refreshIconPacks();
        updateButtons();
    });
    connect(folder, &QPushButton::clicked, this, [] { openFolder(IconPacks::root()); });
    refreshIconPacks();
    updateButtons();
    return page.widget;
}

void SettingsDialog::refreshIconPacks()
{
    const QString active = IconPacks::activeId();
    const QList<IconPacks::Pack> packs = IconPacks::installed();
    if (m_iconPackChoice) {
        const QSignalBlocker block(m_iconPackChoice);
        m_iconPackChoice->clear();
        for (const IconPacks::Pack& p : packs)
            m_iconPackChoice->addItem(p.name, p.id);
        m_iconPackChoice->setCurrentIndex(std::max(0, m_iconPackChoice->findData(active)));
    }
    if (!m_iconPacks)
        return;
    const QString current = m_iconPacks->currentItem() ? m_iconPacks->currentItem()->data(Qt::UserRole).toString() : active;
    const QSignalBlocker block(m_iconPacks);
    m_iconPacks->clear();
    for (const IconPacks::Pack& p : packs) {
        QString text = p.name;
        QStringList extra;
        if (!p.author.isEmpty())
            extra << p.author;
        if (!p.version.isEmpty())
            extra << p.version;
        if (!extra.isEmpty())
            text += QStringLiteral(" (%1)").arg(extra.join(QStringLiteral(", ")));
        if (p.id == active)
            text += QStringLiteral(" · ") + tr("en uso");
        auto* item = new QListWidgetItem(text, m_iconPacks);
        item->setData(Qt::UserRole, p.id);
        item->setToolTip(p.description);
        item->setIcon(QIcon(iconPackPreview(p.folder)));
        QFont f = item->font();
        f.setBold(p.id == active);
        item->setFont(f);
        if (p.id == current)
            m_iconPacks->setCurrentItem(item);
    }
}

QWidget* SettingsDialog::createFontsPage()
{
    PageBox page = makePage(this);
    page.layout->addWidget(wrapped(tr("Añade fuentes para la herramienta Texto (.ttf, .otf, .ttc, .woff). Se copian a la "
                                      "carpeta de Kaint y solo las usa Kaint: no se instalan en el sistema."),
                                   page.widget));
    m_fonts = new QListWidget(page.widget);
    m_fonts->setMinimumHeight(180);
    m_fonts->setSelectionMode(QAbstractItemView::ExtendedSelection);
    page.layout->addWidget(m_fonts, 1);
    auto* row = new QHBoxLayout;
    auto* add = actionButton(Icons::theme({"list-add", "document-import"}), tr("Añadir fuentes…"), {}, page.widget);
    auto* remove = actionButton(Icons::theme({"list-remove", "edit-delete"}), tr("Quitar"), {}, page.widget);
    auto* clear = actionButton(Icons::theme({"edit-clear-all", "edit-clear"}), tr("Quitar todas"), {}, page.widget);
    auto* folder = actionButton(Icons::theme({"folder-open"}), tr("Abrir la carpeta"), {}, page.widget);
    for (QPushButton* b : {add, remove, clear, folder})
        row->addWidget(b);
    row->addStretch();
    page.layout->addLayout(row);
    connect(add, &QPushButton::clicked, this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(this, tr("Añadir fuentes"), QDir::homePath(),
                                                                tr("Fuentes (*.ttf *.otf *.ttc *.otc *.woff *.woff2)"));
        if (files.isEmpty())
            return;
        QStringList errors;
        UserFonts::add(files, &errors);
        if (!errors.isEmpty())
            QMessageBox::warning(this, tr("Añadir fuentes"),
                                 tr("No se pudieron añadir:\n\n%1").arg(errors.join(QLatin1Char('\n'))));
        refreshFonts();
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        for (const QListWidgetItem* item : m_fonts->selectedItems())
            UserFonts::remove(item->data(Qt::UserRole).toString());
        refreshFonts();
    });
    connect(clear, &QPushButton::clicked, this, [this] {
        if (m_fonts->count() == 0
            || QMessageBox::question(this, tr("Quitar todas"), tr("¿Quieres quitar todas las fuentes añadidas?")) != QMessageBox::Yes)
            return;
        UserFonts::removeAll();
        refreshFonts();
    });
    connect(folder, &QPushButton::clicked, this, [] { openFolder(UserFonts::folder()); });
    refreshFonts();
    return page.widget;
}

void SettingsDialog::refreshFonts()
{
    m_fonts->clear();
    for (const UserFonts::Font& f : UserFonts::installed()) {
        const QString name = QFileInfo(f.file).fileName();
        auto* item = new QListWidgetItem(f.families.isEmpty() ? name : QStringLiteral("%1 (%2)").arg(f.families.join(QStringLiteral(", ")), name),
                                         m_fonts);
        item->setData(Qt::UserRole, f.file);
        if (!f.families.isEmpty()) {
            QFont font = item->font();
            font.setFamilies({f.families.first()});
            item->setFont(font);
        }
    }
    if (m_fonts->count() == 0) {
        auto* empty = new QListWidgetItem(tr("Todavía no has añadido ninguna fuente."), m_fonts);
        empty->setFlags(Qt::NoItemFlags);
    }
}

QWidget* SettingsDialog::createShapesPage()
{
    PageBox page = makePage(this);
    page.layout->addWidget(wrapped(tr("Formas de Paint.NET (.xaml) para la herramienta Formas: aparecen en el grupo «Formas "
                                      "personalizadas». Se copian a la carpeta de formas de Kaint."),
                                   page.widget));
    m_shapes = new QListWidget(page.widget);
    m_shapes->setIconSize(QSize(24, 24));
    m_shapes->setMinimumHeight(180);
    m_shapes->setSelectionMode(QAbstractItemView::ExtendedSelection);
    page.layout->addWidget(m_shapes, 1);
    auto* row = new QHBoxLayout;
    auto* add = actionButton(Icons::theme({"list-add", "document-import"}), tr("Añadir formas…"), {}, page.widget);
    auto* remove = actionButton(Icons::theme({"list-remove", "edit-delete"}), tr("Quitar"),
                                tr("Solo se pueden quitar las formas de tu carpeta de formas."), page.widget);
    auto* folder = actionButton(Icons::theme({"folder-open"}), tr("Abrir la carpeta"), {}, page.widget);
    for (QPushButton* b : {add, remove, folder})
        row->addWidget(b);
    row->addStretch();
    page.layout->addLayout(row);
    const QString userDir = CustomShapes::searchPaths().value(0);
    auto changedShapes = [this] {
        CustomShapes::reload();
        emit AppState::instance()->customShapesChanged();
        refreshShapes();
    };
    connect(add, &QPushButton::clicked, this, [this, userDir, changedShapes] {
        const QStringList files = QFileDialog::getOpenFileNames(this, tr("Añadir formas"), QDir::homePath(),
                                                                tr("Formas de Paint.NET (*.xaml)"));
        if (files.isEmpty())
            return;
        QDir().mkpath(userDir);
        QStringList errors;
        for (const QString& file : files) {
            CustomShapes::Shape shape;
            QString error;
            const QString target = QDir(userDir).filePath(QFileInfo(file).fileName());
            if (!CustomShapes::load(file, &shape, &error)) {
                errors << QStringLiteral("%1: %2").arg(QFileInfo(file).fileName(), error);
                continue;
            }
            if (QFileInfo(target) != QFileInfo(file)) {
                QFile::remove(target);
                if (!QFile::copy(file, target))
                    errors << QFileInfo(file).fileName();
            }
        }
        if (!errors.isEmpty())
            QMessageBox::warning(this, tr("Añadir formas"), tr("No se pudieron añadir:\n\n%1").arg(errors.join(QLatin1Char('\n'))));
        changedShapes();
    });
    connect(remove, &QPushButton::clicked, this, [this, userDir, changedShapes] {
        bool any = false;
        for (const QListWidgetItem* item : m_shapes->selectedItems()) {
            const QString file = item->data(Qt::UserRole).toString();
            if (QFileInfo(file).absolutePath() == QFileInfo(userDir).absoluteFilePath())
                any = QFile::remove(file) || any;
        }
        if (any)
            changedShapes();
    });
    connect(folder, &QPushButton::clicked, this, [userDir] { openFolder(userDir); });
    refreshShapes();
    return page.widget;
}

void SettingsDialog::refreshShapes()
{
    m_shapes->clear();
    for (const CustomShapes::Shape& shape : CustomShapes::all()) {
        auto* item = new QListWidgetItem(Icons::customShape(shape.path), shape.name, m_shapes);
        item->setData(Qt::UserRole, shape.file);
        item->setToolTip(QDir::toNativeSeparators(shape.file));
    }
    if (m_shapes->count() == 0) {
        auto* empty = new QListWidgetItem(tr("Todavía no hay formas personalizadas."), m_shapes);
        empty->setFlags(Qt::NoItemFlags);
    }
}

QWidget* SettingsDialog::createKeyboardPage()
{
    PageBox page = makePage(this);
    page.layout->addWidget(wrapped(tr("Haz clic en un atajo y pulsa la combinación de teclas. Con la «x» se queda sin atajo; "
                                      "si otra acción lo usaba, se le quita."),
                                   page.widget));
    m_shortcutSearch = new QLineEdit(page.widget);
    m_shortcutSearch->setPlaceholderText(tr("Buscar acciones"));
    m_shortcutSearch->setClearButtonEnabled(true);
    page.layout->addWidget(m_shortcutSearch);
    m_shortcuts = new QTreeWidget(page.widget);
    m_shortcuts->setColumnCount(3);
    m_shortcuts->setHeaderLabels({tr("Acción"), tr("Atajo"), QString()});
    m_shortcuts->setRootIsDecorated(false);
    m_shortcuts->setUniformRowHeights(true);
    m_shortcuts->setMinimumHeight(260);
    m_shortcuts->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_shortcuts->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_shortcuts->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_shortcuts->header()->resizeSection(1, 200);
    m_shortcuts->header()->setStretchLastSection(false);
    page.layout->addWidget(m_shortcuts, 1);
    auto* row = new QHBoxLayout;
    m_shortcutStatus = new QLabel(page.widget);
    auto* resetAll = actionButton(Icons::theme({"edit-reset", "edit-undo"}), tr("Restablecer todos"),
                                  tr("Volver a los atajos de fábrica de todas las acciones"), page.widget);
    row->addWidget(m_shortcutStatus, 1);
    row->addWidget(resetAll);
    page.layout->addLayout(row);
    connect(m_shortcutSearch, &QLineEdit::textChanged, this, [this](const QString& text) {
        const QString needle = text.trimmed();
        for (int i = 0; i < m_shortcuts->topLevelItemCount(); ++i) {
            QTreeWidgetItem* item = m_shortcuts->topLevelItem(i);
            const auto* edit = qobject_cast<QKeySequenceEdit*>(m_shortcuts->itemWidget(item, 1));
            const QString keys = edit ? edit->keySequence().toString(QKeySequence::NativeText) : QString();
            item->setHidden(!needle.isEmpty() && !item->text(0).contains(needle, Qt::CaseInsensitive)
                            && !keys.contains(needle, Qt::CaseInsensitive));
        }
    });
    connect(resetAll, &QPushButton::clicked, this, [this] {
        Shortcuts::resetAll();
        refreshShortcuts();
        m_shortcutStatus->setText(tr("Se restablecieron todos los atajos."));
    });
    refreshShortcuts();
    return page.widget;
}

void SettingsDialog::refreshShortcuts()
{
    m_shortcuts->clear();
    for (const Shortcuts::Entry& e : Shortcuts::entries()) {
        auto* item = new QTreeWidgetItem(m_shortcuts, {Shortcuts::displayName(e.action)});
        item->setIcon(0, e.action->icon());
        item->setData(0, Qt::UserRole, e.id);
        auto* edit = new QKeySequenceEdit(e.action->shortcut(), m_shortcuts);
        edit->setMaximumSequenceLength(1);
        edit->setClearButtonEnabled(true);
        if (auto* line = edit->findChild<QLineEdit*>())
            line->setPlaceholderText(tr("Sin atajo"));
        auto* reset = new QToolButton(m_shortcuts);
        reset->setIcon(Icons::theme({"edit-reset", "edit-undo"}));
        reset->setToolTip(tr("Restablecer"));
        reset->setAutoRaise(true);
        reset->setEnabled(!Shortcuts::isDefault(e.id));
        m_shortcuts->setItemWidget(item, 1, edit);
        m_shortcuts->setItemWidget(item, 2, reset);
        const QString id = e.id;
        auto apply = [this, id, edit, reset] {
            QAction* taken = Shortcuts::assign(id, edit->keySequence());
            reset->setEnabled(!Shortcuts::isDefault(id));
            if (!taken) {
                m_shortcutStatus->clear();
                return;
            }
            // La otra acción se queda sin ese atajo: se actualiza su fila.
            m_shortcutStatus->setText(tr("Se quitó el atajo de «%1».").arg(Shortcuts::displayName(taken)));
            for (int i = 0; i < m_shortcuts->topLevelItemCount(); ++i) {
                QTreeWidgetItem* other = m_shortcuts->topLevelItem(i);
                if (other->text(0) != Shortcuts::displayName(taken))
                    continue;
                if (auto* otherEdit = qobject_cast<QKeySequenceEdit*>(m_shortcuts->itemWidget(other, 1))) {
                    const QSignalBlocker block(otherEdit);
                    otherEdit->setKeySequence(taken->shortcut());
                }
                if (auto* otherReset = qobject_cast<QToolButton*>(m_shortcuts->itemWidget(other, 2)))
                    otherReset->setEnabled(!Shortcuts::isDefault(other->data(0, Qt::UserRole).toString()));
            }
        };
        connect(edit, &QKeySequenceEdit::editingFinished, this, apply);
        connect(edit, &QKeySequenceEdit::keySequenceChanged, this, [apply](const QKeySequence& seq) {
            // Al borrarlo con la «x» no hay editingFinished.
            if (seq.isEmpty())
                apply();
        });
        connect(reset, &QToolButton::clicked, this, [this, id, edit, reset] {
            Shortcuts::reset(id);
            const QSignalBlocker block(edit);
            for (const Shortcuts::Entry& entry : Shortcuts::entries())
                if (entry.id == id)
                    edit->setKeySequence(entry.action->shortcut());
            reset->setEnabled(false);
            m_shortcutStatus->clear();
        });
    }
    if (m_shortcutSearch)
        emit m_shortcutSearch->textChanged(m_shortcutSearch->text());
}
