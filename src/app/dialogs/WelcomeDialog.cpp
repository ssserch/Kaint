#include "app/dialogs/WelcomeDialog.h"

#include "app/Icons.h"
#include "app/Theme.h"
#include "app/dialogs/AboutDialog.h"
#include "app/widgets/WaveLabel.h"
#include "core/plugins/PdnBridge.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

WelcomeDialog::WelcomeDialog(const AppSettings& current, QWidget* parent)
    : QDialog(parent)
    , m_settings(current)
{
    setWindowTitle(tr("Te damos la bienvenida a Kaint"));
    setWindowIcon(Icons::app());

    auto* version = new QLabel(QStringLiteral("Kaint v%1").arg(QStringLiteral(KAINT_VERSION)), this);
    // El autor y, a su lado, un botón discreto para donar.
    auto* author = new WaveLabel(tr("Por "), QStringLiteral("SSSerch"), QUrl(QStringLiteral("https://ssserch.com")), this);
    auto* donate = new QToolButton(this);
    donate->setText(tr("♥ Donar"));
    donate->setToolTip(tr("Apoyar el desarrollo de Kaint en Ko-fi"));
    donate->setAutoRaise(true);
    donate->setCursor(Qt::PointingHandCursor);
    QFont small = donate->font();
    small.setPointSizeF(small.pointSizeF() * 0.9);
    donate->setFont(small);
    connect(donate, &QToolButton::clicked, this, [] { QDesktopServices::openUrl(QUrl(QStringLiteral("https://ko-fi.com/opposition"))); });
    auto* authorRow = new QHBoxLayout;
    authorRow->setSpacing(6);
    authorRow->addWidget(author);
    authorRow->addWidget(donate);
    authorRow->addStretch();

    auto* intro = new QLabel(tr("Elige cómo quieres que se vea Kaint. Todo se puede cambiar más tarde en Configuración."), this);
    intro->setWordWrap(true);
    auto* rule = new QFrame(this);
    rule->setFrameShape(QFrame::HLine);
    rule->setFrameShadow(QFrame::Sunken);

    // Tema, como en Configuración (sin el editor del personalizado).
    auto* scheme = new QComboBox(this);
    scheme->addItem(tr("Seguir al sistema"), int(Theme::Scheme::System));
    scheme->addItem(tr("Claro"), int(Theme::Scheme::Light));
    scheme->addItem(tr("Oscuro"), int(Theme::Scheme::Dark));
    scheme->addItem(tr("Azul"), int(Theme::Scheme::Blue));
#ifndef Q_OS_WIN
    scheme->addItem(tr("KDE"), int(Theme::Scheme::Kde));
#endif
    scheme->addItem(tr("Windows 7"), int(Theme::Scheme::Windows7));
    scheme->addItem(tr("Windows 7 oscuro"), int(Theme::Scheme::Windows7Dark));
    scheme->addItem(tr("Windows XP"), int(Theme::Scheme::WindowsXP));
    scheme->addItem(tr("Windows 98"), int(Theme::Scheme::Windows98));
    scheme->addItem(tr("Windows 98 ALT"), int(Theme::Scheme::Windows98Alt));
    scheme->addItem(tr("Personalizado"), int(Theme::Scheme::Custom));
    scheme->setCurrentIndex(std::max(0, scheme->findData(m_settings.colorScheme)));
    auto* look = new QComboBox(this);
    look->addItem(QStringLiteral("Kaint"), 0);
    look->addItem(QStringLiteral("Paint.NET"), 1);
    look->setItemData(1, tr("Como Paint.NET 5: más compacto, con las ventanas sin borde y los botones y menús más juntos."),
                      Qt::ToolTipRole);
    look->setCurrentIndex(std::max(0, look->findData(m_settings.uiStyle)));
    auto* cursors = new QComboBox(this);
    cursors->addItem(QStringLiteral("Kaint"), 0);
    cursors->addItem(QStringLiteral("Paint.NET"), 1);
    cursors->setItemData(1, tr("Los de Paint.NET, en pixel art: la cruz con el dibujo de la herramienta."), Qt::ToolTipRole);
    cursors->setCurrentIndex(std::max(0, cursors->findData(m_settings.cursorStyle)));
    auto* language = new QComboBox(this);
    language->addItem(QStringLiteral("English"), QStringLiteral("en"));
    language->addItem(QStringLiteral("Español"), QStringLiteral("es"));
    language->setCurrentIndex(std::max(0, language->findData(m_settings.language)));
    auto* discord = new QCheckBox(tr("Mostrar en Discord que usas Kaint (Rich Presence)"), this);
    discord->setChecked(m_settings.discordEnabled);
    discord->setToolTip(tr("Tu perfil de Discord muestra Kaint con la imagen y la herramienta; en Configuración eliges qué se ve."));
    // Las herramientas y los efectos de PhotoCraft (desactivados por defecto): se eligen ya aquí.
    auto* extras = new QCheckBox(tr("Activar las herramientas y los efectos extras de PhotoCraft"), this);
    extras->setChecked(m_settings.photocraftExtras);
    extras->setToolTip(tr("Lazo magnético, Selección de objetos, Quitar, Parche, Pincel corrector y Pincel corrector puntual, "
                          "y más ajustes y efectos (Intensidad, Mapa de degradado, la galería de filtros...). Llevan la pieza de "
                          "puzle de los complementos."));
    // El aviso va en el idioma elegido, no en el actual: es el que entiende quien lo cambia.
    auto* restartNote = new QLabel(this);
    restartNote->setEnabled(false);
    restartNote->setVisible(false);

    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    form->addRow(tr("Tema:"), scheme);
    form->addRow(tr("Estilo de la interfaz:"), look);
    form->addRow(tr("Cursores:"), cursors);
    form->addRow(tr("Idioma:"), language);
    // Con qué se cargan los complementos de Paint.NET (.dll): .NET, OpenD2D o Wine.
    // En Windows no hay que elegir (los carga el .NET Framework del sistema).
    if (PdnBridge::runtimeSelectable()) {
        auto* plugins = new QComboBox(this);
        plugins->setIconSize(QSize(16, 16));
        plugins->addItem(Icons::theme({"applications-development", "system-run"}), tr(".NET (recomendado)"), 2);
        plugins->setItemData(0, tr("El .NET moderno: también los complementos de Paint.NET 5 y sus ventanas en el diálogo de "
                                   "Kaint (hace falta el runtime de .NET)."),
                             Qt::ToolTipRole);
        plugins->addItem(Icons::openD2DIcon(), QStringLiteral("OpenD2D (mono)"), 0);
        plugins->setItemData(1, tr("mono con OpenD2D, el Direct2D de código abierto: no necesita nada más que mono."),
                             Qt::ToolTipRole);
        plugins->addItem(Icons::wineIcon(), tr("Wine"), 1);
        plugins->setItemData(2, tr("Los complementos se ejecutan dentro de Wine, como un programa de Windows (hace falta Wine)."),
                             Qt::ToolTipRole);
        plugins->setCurrentIndex(std::max(0, plugins->findData(m_settings.pluginRuntime)));
        plugins->setToolTip(tr("Con qué se cargan los complementos de Paint.NET (.dll). Se puede cambiar en Configuración → "
                               "Complementos."));
        form->addRow(tr("Complementos de Paint.NET:"), plugins);
        connect(plugins, &QComboBox::activated, this,
                [this, plugins](int i) { m_settings.pluginRuntime = plugins->itemData(i).toInt(); });
    }

    auto* box = new QDialogButtonBox(this);
    box->addButton(tr("Empezar"), QDialogButtonBox::AcceptRole)->setDefault(true);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(AboutDialog::createLogo(this), 0, Qt::AlignLeft);
    layout->addSpacing(4);
    layout->addWidget(version);
    layout->addLayout(authorRow);
    layout->addSpacing(8);
    layout->addWidget(rule);
    layout->addWidget(intro);
    layout->addLayout(form);
    layout->addWidget(discord);
    layout->addWidget(extras);
    layout->addWidget(restartNote);
    layout->addStretch();
    layout->addWidget(box);
    setMinimumWidth(460);

    const QString startLanguage = m_settings.language;
    connect(scheme, &QComboBox::activated, this, [this, scheme](int i) {
        m_settings.colorScheme = scheme->itemData(i).toInt();
        emit previewRequested(m_settings);
    });
    connect(look, &QComboBox::activated, this, [this, look](int i) {
        m_settings.uiStyle = look->itemData(i).toInt();
        emit previewRequested(m_settings);
    });
    connect(cursors, &QComboBox::activated, this, [this, cursors](int i) {
        m_settings.cursorStyle = cursors->itemData(i).toInt();
        emit previewRequested(m_settings);
    });
    connect(language, &QComboBox::activated, this, [this, language, restartNote, startLanguage](int i) {
        m_settings.language = language->itemData(i).toString();
        restartNote->setText(m_settings.language == QLatin1String("es") ? QStringLiteral("El idioma se aplica al reiniciar Kaint.")
                                                                         : QStringLiteral("The language is applied when Kaint restarts."));
        restartNote->setVisible(m_settings.language != startLanguage);
    });
    connect(discord, &QCheckBox::toggled, this, [this](bool on) { m_settings.discordEnabled = on; });
    connect(extras, &QCheckBox::toggled, this, [this](bool on) { m_settings.photocraftExtras = on; });
}
