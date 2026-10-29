#include "app/dialogs/WelcomeDialog.h"

#include "app/Icons.h"
#include "app/Theme.h"
#include "app/dialogs/AboutDialog.h"
#include "app/widgets/WaveLabel.h"

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
#ifndef Q_OS_WIN
    scheme->addItem(tr("KDE"), int(Theme::Scheme::Kde));
#endif
    scheme->addItem(tr("Windows 98"), int(Theme::Scheme::Windows98));
    scheme->addItem(tr("Personalizado"), int(Theme::Scheme::Custom));
    scheme->setCurrentIndex(std::max(0, scheme->findData(m_settings.colorScheme)));
    auto* look = new QComboBox(this);
    look->addItem(QStringLiteral("Kaint"), 0);
    look->addItem(QStringLiteral("Paint.NET"), 1);
    look->setItemData(1, tr("Como Paint.NET 5: más compacto, con las ventanas sin borde y los botones y menús más juntos."),
                      Qt::ToolTipRole);
    look->setCurrentIndex(std::max(0, look->findData(m_settings.uiStyle)));
    auto* language = new QComboBox(this);
    language->addItem(QStringLiteral("English"), QStringLiteral("en"));
    language->addItem(QStringLiteral("Español"), QStringLiteral("es"));
    language->setCurrentIndex(std::max(0, language->findData(m_settings.language)));
    auto* discord = new QCheckBox(tr("Mostrar en Discord que usas Kaint (Rich Presence)"), this);
    discord->setChecked(m_settings.discordEnabled);
    discord->setToolTip(tr("Tu perfil de Discord muestra Kaint con la imagen y la herramienta; en Configuración eliges qué se ve."));
    auto* restartNote = new QLabel(tr("El idioma se aplica al reiniciar Kaint."), this);
    restartNote->setEnabled(false);
    restartNote->setVisible(false);

    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    form->addRow(tr("Tema:"), scheme);
    form->addRow(tr("Estilo de la interfaz:"), look);
    form->addRow(tr("Idioma:"), language);

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
    connect(language, &QComboBox::activated, this, [this, language, restartNote, startLanguage](int i) {
        m_settings.language = language->itemData(i).toString();
        restartNote->setVisible(m_settings.language != startLanguage);
    });
    connect(discord, &QCheckBox::toggled, this, [this](bool on) { m_settings.discordEnabled = on; });
}
