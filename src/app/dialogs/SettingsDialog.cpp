#include "app/dialogs/SettingsDialog.h"

#include "app/AppState.h"
#include "app/DiscordPresence.h"
#include "app/IconPacks.h"
#include "app/Icons.h"
#include "app/Shortcuts.h"
#include "app/Theme.h"
#include "app/ToolOptionsBar.h"
#include "app/UserFonts.h"
#include "app/dialogs/DialogExec.h"
#include "app/dialogs/TaskDialog.h"
#include "app/dialogs/ThemeDialog.h"
#include "app/tools/ToolManager.h"
#include "app/widgets/EffectControls.h"
#include "app/widgets/UnitField.h"
#include "core/CustomShapes.h"
#include "core/plugins/PluginManager.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImageReader>
#include <QImageWriter>
#include <QInputDevice>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
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
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

AppSettings AppSettings::load()
{
    QSettings s;
    s.beginGroup(QStringLiteral("settings"));
    AppSettings a;
    auto get = [&](const char* key, auto def) { return s.value(QLatin1String(key), def).template value<decltype(def)>(); };
    a.animations = get("animations", a.animations);
    a.translucentPanels = get("translucentPanels", a.translucentPanels);
    a.overscroll = get("overscroll", a.overscroll);
    a.autoScroll = get("autoScroll", a.autoScroll);
    a.autoSelectVisibleLayer = get("autoSelectVisibleLayer", a.autoSelectVisibleLayer);
    a.coloredToolIcons = get("coloredToolIcons", a.coloredToolIcons);
    a.panelStyle = std::clamp(get("panelStyle", a.panelStyle), 0, 1);
    a.panelBorder = std::clamp(get("panelBorder", a.panelBorder), 1, 12);
    a.colorScheme = std::clamp(get("colorScheme", a.colorScheme), 0, 5);
#ifdef Q_OS_WIN
    if (a.colorScheme == int(Theme::Scheme::Kde))
        a.colorScheme = int(Theme::Scheme::System);
#endif
    a.autosaveMinutes = std::clamp(get("autosaveMinutes", a.autosaveMinutes), 0, 120);
    a.language = get("language", a.language) == QLatin1String("es") ? QStringLiteral("es") : QStringLiteral("en");
    a.reopenLast = get("reopenLast", a.reopenLast);
    a.unit = get("unit", a.unit);
    a.rulers = get("rulers", a.rulers);
    a.rulerStyle = std::clamp(get("rulerStyle", a.rulerStyle), 0, 1);
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
    a.tabletEraser = get("tabletEraser", a.tabletEraser);
    a.historyLimitMB = std::max(0, get("historyLimitMB", a.historyLimitMB));
    a.checkUpdates = get("checkUpdates", a.checkUpdates);
    a.updatesBeta = get("updatesBeta", a.updatesBeta);
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
    s.setValue(QStringLiteral("translucentPanels"), translucentPanels);
    s.setValue(QStringLiteral("overscroll"), overscroll);
    s.setValue(QStringLiteral("autoScroll"), autoScroll);
    s.setValue(QStringLiteral("autoSelectVisibleLayer"), autoSelectVisibleLayer);
    s.setValue(QStringLiteral("coloredToolIcons"), coloredToolIcons);
    s.setValue(QStringLiteral("panelStyle"), panelStyle);
    s.setValue(QStringLiteral("panelBorder"), panelBorder);
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
    s.setValue(QStringLiteral("tabletEraser"), tabletEraser);
    s.setValue(QStringLiteral("historyLimitMB"), historyLimitMB);
    s.setValue(QStringLiteral("checkUpdates"), checkUpdates);
    s.setValue(QStringLiteral("updatesBeta"), updatesBeta);
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
    check(tr("Animaciones"), &AppSettings::animations,
          tr("Menús y ventanas que aparecen con un fundido, capas que se deslizan al reordenarlas y otras transiciones."));
    check(tr("Ventanas translúcidas"), &AppSettings::translucentPanels,
          tr("Las ventanas flotantes se vuelven translúcidas cuando el puntero no está encima."));
    check(tr("Desplazamiento más allá del borde de la imagen (overscroll)"), &AppSettings::overscroll,
          tr("Permite ver un margen alrededor de la imagen; así el zum hacia el cursor funciona también en las esquinas."));
    check(tr("Desplazamiento automático al dibujar en el borde de la ventana"), &AppSettings::autoScroll);
    check(tr("Seleccionar automáticamente la capa visible más cercana tras ocultar una capa"),
          &AppSettings::autoSelectVisibleLayer);
    check(tr("Iconos de herramientas con los colores activos"), &AppSettings::coloredToolIcons,
          tr("El cubo de pintura, el pincel, cambiar color y el degradado muestran el color principal y el secundario."));

    // Temas: los tres primeros imitan Paint.NET; KDE usa el del escritorio. En Windows no hay
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
#ifndef Q_OS_WIN
    addScheme(tr("KDE"), Theme::Scheme::Kde, tr("El estilo y los colores reales de Plasma."));
#endif
    addScheme(tr("Windows 98"), Theme::Scheme::Windows98, tr("El clásico gris con relieve. También es una plantilla del personalizado."));
    addScheme(tr("Personalizado"), Theme::Scheme::Custom, tr("Colores, fondo del lienzo y diseño a tu gusto."));
    scheme->setCurrentIndex(std::max(0, scheme->findData(m_settings.colorScheme)));
    auto* customize = new QPushButton(tr("Personalizar…"), page.widget);
    customize->setToolTip(tr("Editar el tema personalizado."));
    schemeRow->addWidget(new QLabel(tr("Tema:"), page.widget));
    schemeRow->addWidget(scheme);
    schemeRow->addWidget(customize);
    schemeRow->addStretch();
    page.layout->addLayout(schemeRow);

    auto* languageRow = new QHBoxLayout;
    auto* language = new QComboBox(page.widget);
    language->addItem(QStringLiteral("English"), QStringLiteral("en"));
    language->addItem(QStringLiteral("Español"), QStringLiteral("es"));
    language->setCurrentIndex(language->findData(m_settings.language));
    languageRow->addWidget(new QLabel(tr("Idioma:"), page.widget));
    languageRow->addWidget(language);
    languageRow->addStretch();
    page.layout->addLayout(languageRow);

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

    // Ventanas flotantes: el estilo de las de paint.js (Windows 10) o el compacto, y su borde.
    page.layout->addWidget(sectionHeader(tr("Ventanas"), page.widget));
    auto* panelForm = new QFormLayout;
    panelForm->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    auto* panelStyle = new QComboBox(page.widget);
    panelStyle->addItem(tr("Windows 10 (como paint.js)"), 0);
    panelStyle->setItemData(0, tr("Cabecera más alta, botón de cerrar cuadrado y esquinas rectas."), Qt::ToolTipRole);
    panelStyle->addItem(tr("Compacto"), 1);
    panelStyle->setItemData(1, tr("La cabecera fina de siempre de Kaint."), Qt::ToolTipRole);
    panelStyle->setCurrentIndex(std::max(0, panelStyle->findData(m_settings.panelStyle)));
    auto* panelBorder = new QSpinBox(page.widget);
    panelBorder->setRange(1, 12);
    panelBorder->setValue(m_settings.panelBorder);
    panelBorder->setToolTip(tr("Grosor del borde de las ventanas Herramientas, Historial, Capas y Colores."));
    panelForm->addRow(tr("Estilo de las ventanas:"), panelStyle);
    panelForm->addRow(tr("Grosor del borde de las ventanas:"), withUnit(panelBorder, tr("px"), {}, page.widget));
    page.layout->addLayout(panelForm);
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

    // Color del fondo alrededor de la imagen, por separado para los temas claros y los oscuros
    // (al cambiar de tema se usa el suyo).
    auto borderRow = [&](const QString& text, QColor AppSettings::*field, bool dark) {
        auto* row = new QHBoxLayout;
        auto* custom = new QCheckBox(text, page.widget);
        custom->setChecked((m_settings.*field).isValid());
        auto* border = new ColorButton(page.widget);
        border->setColor((m_settings.*field).isValid() ? m_settings.*field : Theme::preset(dark).workspace);
        border->setEnabled(custom->isChecked());
        row->addWidget(custom);
        row->addWidget(border);
        row->addStretch();
        page.layout->addLayout(row);
        auto apply = [this, custom, border, field] {
            m_settings.*field = custom->isChecked() ? border->color() : QColor();
            border->setEnabled(custom->isChecked());
            changed();
        };
        connect(custom, &QCheckBox::toggled, this, apply);
        connect(border, &ColorButton::colorChanged, this, apply);
    };
    borderRow(tr("Usar un color personalizado para el fondo del lienzo con los temas claros"), &AppSettings::canvasBorderLight,
              false);
    borderRow(tr("Usar un color personalizado para el fondo del lienzo con los temas oscuros"), &AppSettings::canvasBorderDark,
              true);

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

    // Regla (Ver > Reglas): oscura como la de paint.js o del color de la barra de herramientas.
    auto* rulerRow = new QHBoxLayout;
    auto* ruler = new QComboBox(page.widget);
    ruler->addItem(tr("Oscura (como paint.js)"), 0);
    ruler->addItem(tr("Del color de la barra de herramientas"), 1);
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
    page.layout->addWidget(wrapped(tr("Complementos de efectos instalados (.so de Kaint o .dll de Paint.NET). Se guardan "
                                      "en %1; si un complemento trae varios archivos, añádelos todos o copia su carpeta "
                                      "ahí.")
                                       .arg(QDir::toNativeSeparators(PluginManager::instance()->userPath())),
                                   page.widget));
    m_pluginList = new QTreeWidget(page.widget);
    m_pluginList->setHeaderLabels({tr("Complemento"), tr("Tipo"), tr("Estado")});
    m_pluginList->setRootIsDecorated(false);
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

    auto* row = new QHBoxLayout;
    auto* add = new QPushButton(Icons::theme({"list-add", "document-import"}), tr("Añadir…"), page.widget);
    add->setToolTip(tr("Copiar complementos (.so o .dll) a la carpeta de complementos"));
    connect(add, &QPushButton::clicked, this, &SettingsDialog::addPlugins);
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

    connect(m_pluginList, &QTreeWidget::currentItemChanged, this, &SettingsDialog::showPluginDetails);
    connect(m_pluginList, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (column != 0)
            return;
        const QString id = item->data(0, Qt::UserRole).toString();
        const bool on = item->checkState(0) == Qt::Checked;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        PluginManager::instance()->setEnabled(id, on);
        QApplication::restoreOverrideCursor();
        QTimer::singleShot(0, this, &SettingsDialog::refreshPlugins);
    });
    connect(PluginManager::instance(), &PluginManager::changed, this, &SettingsDialog::refreshPlugins);
    refreshPlugins();
    return page.widget;
}

void SettingsDialog::refreshPlugins()
{
    const QString selected = m_pluginList->currentItem() ? m_pluginList->currentItem()->data(0, Qt::UserRole).toString()
                                                         : QString();
    // Se conserva el desplazamiento: marcar o desmarcar una casilla no salta a la seleccionada.
    const QSignalBlocker block(m_pluginList);
    const int scroll = m_pluginList->verticalScrollBar()->value();
    m_pluginList->clear();
    for (const PluginInfo& p : PluginManager::instance()->plugins()) {
        const bool incompatible = !p.pending && p.effects.isEmpty() && !p.errors.isEmpty();
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
        if (p.id == selected)
            m_pluginList->setCurrentItem(item);
    }
    if (!m_pluginList->currentItem() && m_pluginList->topLevelItemCount() > 0)
        m_pluginList->setCurrentItem(m_pluginList->topLevelItem(0));
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
        tr("Complementos (*.dll *.so);;Complementos de Paint.NET (*.dll);;Complementos de Kaint (*.so)"));
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
        field(tr("Sitio web"), p.website);
        field(tr("Descripción"), p.description);
        if (p.kind == QLatin1String("Paint.NET"))
            lines << QString()
                  << tr("Complemento de terceros para Paint.NET, sin modificar: Kaint lo ejecuta con su capa de "
                        "compatibilidad. Su autor no participa en Kaint ni da soporte a este uso.");
        if (!p.effects.isEmpty())
            lines << QString() << tr("Efectos:") << QStringLiteral("  ") + p.effects.join(QStringLiteral("\n  "));
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

QWidget* SettingsDialog::createIconPacksPage()
{
    PageBox page = makePage(this);
    page.layout->addWidget(wrapped(tr("Los paquetes de iconos de paint.js (archivos .zip con asset-pack.json) cambian los "
                                      "iconos de Kaint, también los de las herramientas. Los iconos que no traiga un "
                                      "paquete siguen siendo los de Kaint. Se guardan en este equipo."),
                                   page.widget));
    m_iconPacks = new QListWidget(page.widget);
    m_iconPacks->setIconSize(QSize(24, 24));
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
        // Vista previa: el icono de Abrir del paquete (o el de Kaint).
        const QString preview = QDir(p.folder).filePath(QStringLiteral("icons/menu_file_open_icon.png"));
        item->setIcon(!p.folder.isEmpty() && QFile::exists(preview) ? QIcon(preview) : Icons::named(QStringLiteral("document-open")));
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
