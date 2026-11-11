#include "app/collab/CollabDialogs.h"

#include "app/Icons.h"
#include "app/collab/CollabController.h"
#include "app/collab/CollabServer.h"
#include "app/collab/Discovery.h"
#include "app/collab/P2P.h"
#include "app/collab/CollabSession.h"
#include "app/dialogs/TaskDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGraphicsBlurEffect>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace Collab {

namespace {

QIcon swatch(const QColor& color)
{
    QPixmap pm(14, 14);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(color.darker(160), 1));
    p.setBrush(color);
    p.drawRoundedRect(QRectF(0.5, 0.5, 13, 13), 3, 3);
    return QIcon(pm);
}

QString accessText(const QString& auth)
{
    switch (accessFromName(auth)) {
    case Access::Free: return QObject::tr("Libre");
    case Access::Approve: return QObject::tr("Con permiso del anfitrión");
    case Access::Password: return QObject::tr("Con contraseña");
    case Access::Ids: return QObject::tr("Equipos autorizados");
    }
    return auth;
}

QString platformText(const Peer& p)
{
    if (p.platform == QLatin1String("web"))
        return QObject::tr("Kaint para la web");
    return p.app.isEmpty() ? QStringLiteral("Kaint") : p.app;
}

// "1.2.3.4", "1.2.3.4:5000" o "[::1]:5000" → dirección y puerto.
void splitAddress(const QString& text, QString* host, int* port)
{
    QString t = text.trimmed();
    if (t.startsWith(QLatin1String("ws://")) || t.startsWith(QLatin1String("http://")))
        t = t.section(QStringLiteral("://"), 1);
    t = t.section(QLatin1Char('/'), 0, 0);
    if (t.startsWith(QLatin1Char('['))) {
        const int end = int(t.indexOf(QLatin1Char(']')));
        *host = t.mid(1, end - 1);
        if (t.mid(end + 1).startsWith(QLatin1Char(':')))
            *port = t.mid(end + 2).toInt();
        return;
    }
    if (t.count(QLatin1Char(':')) == 1) {
        *host = t.section(QLatin1Char(':'), 0, 0);
        *port = t.section(QLatin1Char(':'), 1).toInt();
        return;
    }
    *host = t;
}

QWidget* deviceIdRow(QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    auto* code = new QLabel(deviceCode(), w);
    QFont mono = code->font();
    mono.setFamily(QStringLiteral("monospace"));
    mono.setStyleHint(QFont::Monospace);
    mono.setBold(true);
    code->setFont(mono);
    code->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* copy = new QToolButton(w);
    copy->setIcon(Icons::theme({"edit-copy"}));
    copy->setToolTip(QObject::tr("Copiar"));
    copy->setAutoRaise(true);
    QObject::connect(copy, &QToolButton::clicked, w, [] { QApplication::clipboard()->setText(deviceCode()); });
    l->addWidget(code);
    l->addWidget(copy);
    l->addStretch();
    return w;
}

} // namespace

// ---------------------------------------------------------------------------

JoinDialog::JoinDialog(CollabController* collab, Mode mode, QWidget* parent)
    : QDialog(parent)
    , m_collab(collab)
    , m_mode(mode)
{
    const bool p2p = mode == Mode::P2P;
    setWindowTitle(p2p ? tr("Unirse a una sesión P2P") : tr("Unirse a una sesión de la red local"));
    setWindowIcon(Icons::theme(QStringLiteral("collab")));
    const Settings settings = collab->settings();

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    if (p2p) {
        auto* intro = new QLabel(tr("Escribe el código que te dio el anfitrión. Los dos equipos se encuentran por Internet y la "
                                    "imagen va directa de uno a otro."),
                                 this);
        intro->setWordWrap(true);
        layout->addWidget(intro);
        auto* codeRow = new QHBoxLayout;
        m_code = new QLineEdit(this);
        m_code->setPlaceholderText(tr("Ej.: ABCD-EFGH-JKLM-NPQR"));
        QFont mono = m_code->font();
        mono.setFamily(QStringLiteral("monospace"));
        mono.setStyleHint(QFont::Monospace);
        mono.setBold(true);
        m_code->setFont(mono);
        m_code->setMaxLength(32);
        auto* paste = new QToolButton(this);
        paste->setIcon(Icons::theme({"edit-paste"}));
        paste->setToolTip(tr("Pegar"));
        paste->setAutoRaise(true);
        connect(paste, &QToolButton::clicked, this, [this] { m_code->setText(QApplication::clipboard()->text().trimmed()); });
        codeRow->addWidget(m_code, 1);
        codeRow->addWidget(paste);
        form->addRow(tr("Código de la sesión:"), codeRow);
    } else {
        m_discovery = new Discovery(this);
        layout->addWidget(new QLabel(tr("Sesiones en la red local:"), this));
        m_found = new QTreeWidget(this);
        m_found->setRootIsDecorated(false);
        m_found->setHeaderLabels({tr("Sesión"), tr("Anfitrión"), tr("Dirección"), tr("Acceso"), tr("Personas")});
        m_found->header()->setStretchLastSection(false);
        m_found->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        m_found->setMinimumHeight(140);
        layout->addWidget(m_found, 1);
        connect(m_found, &QTreeWidget::itemSelectionChanged, this, &JoinDialog::pick);
        connect(m_found, &QTreeWidget::itemDoubleClicked, this, [this] {
            pick();
            start();
        });
        auto* addressRow = new QHBoxLayout;
        m_address = new QLineEdit(this);
        m_address->setPlaceholderText(tr("Ej.: 192.168.1.20"));
        m_port = new QSpinBox(this);
        m_port->setRange(1, 65535);
        m_port->setValue(kDefaultPort);
        addressRow->addWidget(m_address, 1);
        addressRow->addWidget(new QLabel(tr("Puerto:"), this));
        addressRow->addWidget(m_port);
        form->addRow(tr("Dirección IP:"), addressRow);
    }
    m_password = new QLineEdit(this);
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setPlaceholderText(tr("Solo si la sesión pide contraseña"));
    form->addRow(tr("Contraseña:"), m_password);
    m_name = new QLineEdit(settings.name, this);
    m_name->setPlaceholderText(settings.displayName());
    m_name->setMaxLength(40);
    form->addRow(tr("Tu nombre:"), m_name);
    form->addRow(tr("ID de este equipo:"), deviceIdRow(this));
    layout->addLayout(form);
    auto* idHint = new QLabel(tr("Si la sesión solo admite equipos autorizados, dale este ID al anfitrión."), this);
    idHint->setWordWrap(true);
    idHint->setEnabled(false);
    layout->addWidget(idHint);
    if (p2p)
        layout->addStretch();

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    auto* box = new QDialogButtonBox(this);
    m_join = box->addButton(tr("Unirse"), QDialogButtonBox::AcceptRole);
    m_join->setIcon(Icons::theme(QStringLiteral("collab")));
    m_join->setDefault(true);
    box->addButton(QDialogButtonBox::Cancel);
    connect(box, &QDialogButtonBox::accepted, this, &JoinDialog::start);
    connect(box, &QDialogButtonBox::rejected, this, &JoinDialog::reject);
    layout->addWidget(box);

    if (m_discovery) {
        connect(m_discovery, &Discovery::changed, this, &JoinDialog::refreshFound);
        if (!m_discovery->start())
            m_status->setText(tr("No se pueden buscar sesiones (el puerto %1 está ocupado). Escribe la dirección del anfitrión.")
                                  .arg(kBeaconPort));
        refreshFound();
    } else if (!p2pAvailable()) {
        setBusy(false, tr("Este Kaint se compiló sin la conexión P2P. Usa la red local (LAN)."), true);
        m_join->setEnabled(false);
    }
    connect(collab, &CollabController::documentReady, this, [this] {
        m_connecting = false;
        accept();
    });
    if (p2p) {
        resize(520, 300);
        m_code->setFocus();
    } else {
        resize(620, 480);
    }
}

void JoinDialog::refreshFound()
{
    const QString selected = m_found->currentItem() ? m_found->currentItem()->data(0, Qt::UserRole).toString() : QString();
    m_found->clear();
    const QList<FoundSession> sessions = m_discovery->sessions();
    for (const FoundSession& s : sessions) {
        auto* item = new QTreeWidgetItem(m_found);
        item->setText(0, s.name);
        item->setIcon(0, Icons::theme(QStringLiteral("collab")));
        item->setText(1, s.host);
        item->setText(2, QStringLiteral("%1:%2").arg(s.address).arg(s.port));
        item->setText(3, accessText(s.auth));
        item->setText(4, QString::number(s.peers));
        item->setData(0, Qt::UserRole, item->text(2));
        item->setData(1, Qt::UserRole, s.auth);
        if (item->text(2) == selected)
            m_found->setCurrentItem(item);
    }
    if (sessions.isEmpty()) {
        auto* item = new QTreeWidgetItem(m_found);
        item->setText(0, tr("Buscando sesiones…"));
        item->setFlags(Qt::NoItemFlags);
    }
}

void JoinDialog::pick()
{
    const QTreeWidgetItem* item = m_found->currentItem();
    if (!item || item->data(0, Qt::UserRole).toString().isEmpty())
        return;
    QString host;
    int port = 0;
    splitAddress(item->data(0, Qt::UserRole).toString(), &host, &port);
    m_address->setText(host);
    if (port > 0)
        m_port->setValue(port);
    if (item->data(1, Qt::UserRole).toString() == QLatin1String("password"))
        m_password->setFocus();
}

void JoinDialog::start()
{
    if (m_connecting)
        return;
    if (m_mode == Mode::P2P) {
        const QString code = normalizedSessionCode(m_code->text());
        if (code.isEmpty()) {
            setBusy(false, tr("El código tiene 16 letras y números, como ABCD-EFGH-JKLM-NPQR."), true);
            return;
        }
        Settings s = m_collab->settings();
        if (s.name != m_name->text().trimmed()) {
            s.name = m_name->text().trimmed();
            m_collab->applySettings(s);
        }
        m_collab->joinP2P(code, m_password->text());
        Session* session = m_collab->session();
        if (!session)
            return;
        setBusy(true, tr("Buscando la sesión…"));
        connect(session, &Session::stateChanged, this, [this, session] {
            if (session->state() == Session::State::Waiting)
                setBusy(true, tr("Esperando a que el anfitrión te deje entrar…"));
            else if (session->state() == Session::State::Connecting && !session->progressText().isEmpty())
                setBusy(true, session->progressText());
        });
        connect(session, &Session::ended, this, [this](const QString& reason) {
            if (m_connecting)
                setBusy(false, reason.isEmpty() ? tr("No se pudo entrar.") : reason, true);
        });
        return;
    }
    QString host;
    int port = 0;
    splitAddress(m_address->text(), &host, &port);
    if (host.isEmpty()) {
        setBusy(false, tr("Elige una sesión de la lista o escribe la dirección del anfitrión."), true);
        return;
    }
    if (port <= 0)
        port = m_port->value();
    Settings s = m_collab->settings();
    if (s.name != m_name->text().trimmed()) {
        s.name = m_name->text().trimmed();
        m_collab->applySettings(s);
    }
    m_collab->join(host, quint16(port), m_password->text());
    Session* session = m_collab->session();
    if (!session)
        return;
    setBusy(true, tr("Conectando con %1:%2…").arg(host).arg(port));
    connect(session, &Session::stateChanged, this, [this, session] {
        if (session->state() == Session::State::Waiting)
            setBusy(true, tr("Esperando a que el anfitrión te deje entrar…"));
    });
    connect(session, &Session::ended, this, [this](const QString& reason) {
        if (m_connecting)
            setBusy(false, reason.isEmpty() ? tr("No se pudo entrar.") : reason, true);
    });
}

void JoinDialog::setBusy(bool busy, const QString& status, bool error)
{
    m_connecting = busy;
    m_join->setEnabled(!busy);
    m_status->setText(status);
    m_status->setStyleSheet(error ? QStringLiteral("color: #c62828;") : QString());
}

void JoinDialog::reject()
{
    if (m_connecting && m_collab->session())
        m_collab->session()->leave();
    m_connecting = false;
    QDialog::reject();
}

// ---------------------------------------------------------------------------

AddressView::AddressView(Kind kind, QWidget* parent)
    : QWidget(parent)
    , m_kind(kind)
{
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(4);
    m_text = new QLabel(this);
    QFont bold = m_text->font();
    bold.setBold(true);
    m_text->setFont(bold);
    // Un margen para que el difuminado no se corte en los bordes.
    m_text->setContentsMargins(4, 2, 4, 2);
    m_text->setCursor(Qt::PointingHandCursor);
    m_text->installEventFilter(this);
    m_blur = new QGraphicsBlurEffect(m_text);
    m_blur->setBlurRadius(9);
    m_blur->setBlurHints(QGraphicsBlurEffect::QualityHint);
    m_text->setGraphicsEffect(m_blur);
    m_eye = new QToolButton(this);
    m_eye->setAutoRaise(true);
    m_eye->setCursor(Qt::PointingHandCursor);
    connect(m_eye, &QToolButton::clicked, this, [this] { setRevealed(!m_revealed); });
    m_copy = new QPushButton(Icons::theme({"edit-copy", "edit-paste"}), tr("Copiar"), this);
    m_copy->setToolTip(kind == Kind::Code ? tr("Copiar el código al portapapeles.") : tr("Copiar la dirección al portapapeles."));
    connect(m_copy, &QPushButton::clicked, this, [this] {
        if (m_address.isEmpty())
            return;
        QApplication::clipboard()->setText(m_address);
        m_copy->setText(m_kind == Kind::Code ? tr("Copiado") : tr("Copiada"));
        QTimer::singleShot(1500, m_copy, [this] { m_copy->setText(tr("Copiar")); });
    });
    row->addWidget(m_text);
    row->addWidget(m_eye);
    row->addWidget(m_copy);
    row->addStretch();
    refresh();
}

void AddressView::setAddress(const QString& text)
{
    if (text == m_address)
        return;
    m_address = text;
    refresh();
}

void AddressView::setRevealed(bool on)
{
    if (on == m_revealed)
        return;
    m_revealed = on;
    refresh();
}

QString AddressView::hint()
{
    return tr("Las direcciones y los códigos están difuminados para que no se vean en capturas ni transmisiones: haz clic "
              "en ellos (o en el ojo) para mostrarlos u ocultarlos.");
}

bool AddressView::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_text && event->type() == QEvent::MouseButtonRelease
        && static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton && !m_address.isEmpty()) {
        setRevealed(!m_revealed);
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void AddressView::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::StyleChange)
        refresh();
}

void AddressView::refresh()
{
    const bool empty = m_address.isEmpty();
    const bool code = m_kind == Kind::Code;
    m_text->setText(empty ? (code ? QString() : tr("Sin conexión a la red")) : m_address);
    m_blur->setEnabled(!m_revealed && !empty);
    if (code)
        m_text->setToolTip(empty ? QString() : m_revealed ? tr("Haz clic para ocultar el código.") : tr("Haz clic para mostrar el código."));
    else
        m_text->setToolTip(empty ? QString() : m_revealed ? tr("Haz clic para ocultar la dirección.") : tr("Haz clic para mostrar la dirección."));
    m_eye->setIcon(Icons::eye(!m_revealed));
    if (code)
        m_eye->setToolTip(m_revealed ? tr("Ocultar el código") : tr("Mostrar el código"));
    else
        m_eye->setToolTip(m_revealed ? tr("Ocultar la dirección") : tr("Mostrar la dirección"));
    m_eye->setVisible(!empty);
    m_copy->setEnabled(!empty);
}

QStringList lanAddresses()
{
    return Server::localAddresses();
}

// ---------------------------------------------------------------------------

SessionDialog::SessionDialog(CollabController* collab, QWidget* parent)
    : QDialog(parent)
    , m_collab(collab)
{
    setWindowTitle(tr("Sesión colaborativa"));
    setWindowIcon(Icons::theme(QStringLiteral("collab")));
    setAttribute(Qt::WA_DeleteOnClose);

    auto* layout = new QVBoxLayout(this);
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    QFont bold = m_status->font();
    bold.setBold(true);
    m_status->setFont(bold);
    layout->addWidget(m_status);
    m_addresses = new QLabel(this);
    m_addresses->setWordWrap(true);
    m_addresses->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_addresses);
    // Anfitrión: las direcciones para entrar, difuminadas y con su botón de copiar.
    m_hostInfo = new QWidget(this);
    auto* hostGrid = new QGridLayout(m_hostInfo);
    hostGrid->setContentsMargins(0, 0, 0, 0);
    m_joinHelp = new QLabel(m_hostInfo);
    m_joinHelp->setWordWrap(true);
    hostGrid->addWidget(m_joinHelp, 0, 0, 1, 2);
    m_codeLabel = new QLabel(tr("Código de la sesión:"), m_hostInfo);
    hostGrid->addWidget(m_codeLabel, 1, 0);
    m_codeView = new AddressView(AddressView::Kind::Code, m_hostInfo);
    hostGrid->addWidget(m_codeView, 1, 1);
    m_addressLabel = new QLabel(tr("Dirección:"), m_hostInfo);
    hostGrid->addWidget(m_addressLabel, 1, 0);
    m_hostAddress = new AddressView(AddressView::Kind::Address, m_hostInfo);
    hostGrid->addWidget(m_hostAddress, 1, 1);
    m_webLabel = new QLabel(tr("Desde un navegador:"), m_hostInfo);
    hostGrid->addWidget(m_webLabel, 2, 0);
    m_webAddress = new AddressView(AddressView::Kind::Address, m_hostInfo);
    hostGrid->addWidget(m_webAddress, 2, 1);
    m_otherLabel = new QLabel(tr("Otras direcciones:"), m_hostInfo);
    hostGrid->addWidget(m_otherLabel, 3, 0, Qt::AlignTop);
    m_otherAddresses = new QWidget(m_hostInfo);
    auto* others = new QVBoxLayout(m_otherAddresses);
    others->setContentsMargins(0, 0, 0, 0);
    hostGrid->addWidget(m_otherAddresses, 3, 1);
    auto* hint = new QLabel(AddressView::hint(), m_hostInfo);
    hint->setWordWrap(true);
    hint->setForegroundRole(QPalette::PlaceholderText);
    hostGrid->addWidget(hint, 4, 0, 1, 2);
    hostGrid->setColumnStretch(1, 1);
    layout->addWidget(m_hostInfo);

    // Participantes.
    auto* peersBox = new QGroupBox(tr("Participantes"), this);
    auto* peersLayout = new QVBoxLayout(peersBox);
    m_peers = new QTreeWidget(peersBox);
    m_peers->setRootIsDecorated(false);
    m_peers->setHeaderLabels({tr("Nombre"), tr("Aplicación"), tr("Edición")});
    m_peers->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_peers->setMinimumHeight(110);
    peersLayout->addWidget(m_peers);
    auto* peerButtons = new QHBoxLayout;
    m_admit = new QPushButton(tr("Dejar entrar"), peersBox);
    m_deny = new QPushButton(tr("Rechazar"), peersBox);
    m_kick = new QPushButton(Icons::theme({"list-remove", "edit-delete"}), tr("Expulsar"), peersBox);
    peerButtons->addWidget(m_admit);
    peerButtons->addWidget(m_deny);
    peerButtons->addStretch();
    peerButtons->addWidget(m_kick);
    peersLayout->addLayout(peerButtons);
    layout->addWidget(peersBox, 1);
    connect(m_peers, &QTreeWidget::itemSelectionChanged, this, &SessionDialog::refresh);
    connect(m_admit, &QPushButton::clicked, this, [this] {
        if (Session* s = m_collab->session(); s && selectedPeer(true) > 0)
            s->admit(selectedPeer(true), true);
    });
    connect(m_deny, &QPushButton::clicked, this, [this] {
        if (Session* s = m_collab->session(); s && selectedPeer(true) > 0)
            s->admit(selectedPeer(true), false);
    });
    connect(m_kick, &QPushButton::clicked, this, [this] {
        if (Session* s = m_collab->session(); s && selectedPeer(false) > 0)
            s->kick(selectedPeer(false));
    });

    // Sugerencias (anfitrión).
    m_proposalsBox = new QGroupBox(tr("Cambios sugeridos"), this);
    auto* propLayout = new QVBoxLayout(m_proposalsBox);
    m_proposals = new QListWidget(m_proposalsBox);
    m_proposals->setIconSize(QSize(96, 96));
    m_proposals->setMinimumHeight(120);
    propLayout->addWidget(m_proposals);
    auto* propButtons = new QHBoxLayout;
    m_accept = new QPushButton(Icons::theme({"dialog-ok-apply", "dialog-ok"}), tr("Aceptar"), m_proposalsBox);
    m_reject = new QPushButton(Icons::theme({"dialog-cancel"}), tr("Rechazar"), m_proposalsBox);
    m_acceptAll = new QPushButton(tr("Aceptar todos"), m_proposalsBox);
    m_autoAccept = new QCheckBox(tr("Aceptar automáticamente"), m_proposalsBox);
    m_autoAccept->setToolTip(tr("Los cambios de los invitados se aplican a la imagen sin preguntar."));
    propButtons->addWidget(m_accept);
    propButtons->addWidget(m_reject);
    propButtons->addWidget(m_acceptAll);
    propButtons->addStretch();
    propButtons->addWidget(m_autoAccept);
    propLayout->addLayout(propButtons);
    layout->addWidget(m_proposalsBox, 1);
    connect(m_proposals, &QListWidget::itemSelectionChanged, this, &SessionDialog::refresh);
    connect(m_accept, &QPushButton::clicked, this, [this] {
        if (Session* s = m_collab->session(); s && selectedProposal() > 0)
            s->acceptProposal(selectedProposal());
    });
    connect(m_reject, &QPushButton::clicked, this, [this] {
        if (Session* s = m_collab->session(); s && selectedProposal() > 0)
            s->rejectProposal(selectedProposal());
    });
    connect(m_acceptAll, &QPushButton::clicked, this, [this] {
        if (Session* s = m_collab->session())
            s->acceptAll();
    });
    connect(m_autoAccept, &QCheckBox::toggled, this, [this](bool on) {
        Settings st = m_collab->settings();
        if (st.autoAccept == on)
            return;
        st.autoAccept = on;
        m_collab->applySettings(st);
    });

    // Borrador (invitado con confirmaciones).
    m_draftBox = new QGroupBox(tr("Tus cambios"), this);
    auto* draftLayout = new QHBoxLayout(m_draftBox);
    m_draftStatus = new QLabel(m_draftBox);
    m_draftStatus->setWordWrap(true);
    m_propose = new QPushButton(Icons::theme({"mail-send", "document-send", "go-up"}), tr("Sugerir cambios"), m_draftBox);
    m_discard = new QPushButton(Icons::theme({"edit-clear-history", "edit-undo"}), tr("Descartar"), m_draftBox);
    draftLayout->addWidget(m_draftStatus, 1);
    draftLayout->addWidget(m_propose);
    draftLayout->addWidget(m_discard);
    layout->addWidget(m_draftBox);
    connect(m_propose, &QPushButton::clicked, this, [this] {
        if (Session* s = m_collab->session())
            s->propose();
    });
    connect(m_discard, &QPushButton::clicked, this, [this] {
        if (Session* s = m_collab->session())
            s->discardDraft();
    });

    // Nombre propio.
    auto* nameRow = new QHBoxLayout;
    nameRow->addWidget(new QLabel(tr("Tu nombre en pantalla:"), this));
    m_name = new QLineEdit(m_collab->settings().name, this);
    m_name->setPlaceholderText(m_collab->settings().displayName());
    m_name->setMaxLength(40);
    nameRow->addWidget(m_name, 1);
    auto* rename = new QPushButton(tr("Cambiar"), this);
    nameRow->addWidget(rename);
    layout->addLayout(nameRow);
    auto applyName = [this] {
        Settings st = m_collab->settings();
        st.name = m_name->text().trimmed();
        m_collab->applySettings(st);
    };
    connect(rename, &QPushButton::clicked, this, applyName);
    connect(m_name, &QLineEdit::returnPressed, this, applyName);

    auto* box = new QDialogButtonBox(this);
    m_leave = box->addButton(tr("Salir de la sesión"), QDialogButtonBox::DestructiveRole);
    QPushButton* close = box->addButton(QDialogButtonBox::Close);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(m_leave, &QPushButton::clicked, this, [this] { m_collab->leave(true); });
    layout->addWidget(box);

    connect(collab, &CollabController::changed, this, &SessionDialog::refresh);
    if (Session* s = collab->session())
        connect(s, &Session::proposalsChanged, this, &SessionDialog::refreshProposals);
    refreshProposals();
    refresh();
    resize(560, 620);
}

int SessionDialog::selectedPeer(bool waiting) const
{
    const QTreeWidgetItem* item = m_peers->currentItem();
    if (!item || item->data(0, Qt::UserRole + 1).toBool() != waiting)
        return -1;
    return item->data(0, Qt::UserRole).toInt();
}

int SessionDialog::selectedProposal() const
{
    const QListWidgetItem* item = m_proposals->currentItem();
    return item ? item->data(Qt::UserRole).toInt() : -1;
}

void SessionDialog::refreshProposals()
{
    const int selected = selectedProposal();
    m_proposals->clear();
    Session* s = m_collab->session();
    if (!s || !s->isHost())
        return;
    for (const Proposal& p : s->proposals()) {
        auto* item = new QListWidgetItem(m_proposals);
        QRect bounds;
        QImage preview = s->preview(p, QSize(96, 96), &bounds);
        if (!preview.isNull()) {
            // Sobre el tablero de transparencia, como el lienzo.
            QImage shown(preview.size(), QImage::Format_ARGB32_Premultiplied);
            QPainter painter(&shown);
            for (int y = 0; y < shown.height(); y += 6)
                for (int x = 0; x < shown.width(); x += 6)
                    painter.fillRect(x, y, 6, 6, ((x + y) / 6) % 2 ? QColor(204, 204, 204) : Qt::white);
            painter.drawImage(0, 0, preview);
            painter.end();
            item->setIcon(QIcon(QPixmap::fromImage(shown)));
        }
        item->setText(tr("%1 — %2\n%3 · zona %4×%5 en (%6, %7)")
                          .arg(p.name, p.label.isEmpty() ? tr("Cambios") : p.label, p.time.toString(QStringLiteral("HH:mm:ss")))
                          .arg(bounds.width())
                          .arg(bounds.height())
                          .arg(bounds.x())
                          .arg(bounds.y()));
        item->setData(Qt::UserRole, p.from);
        if (p.from == selected)
            m_proposals->setCurrentItem(item);
    }
    if (!m_proposals->currentItem() && m_proposals->count() > 0)
        m_proposals->setCurrentRow(0);
}

void SessionDialog::refresh()
{
    Session* s = m_collab->session();
    m_status->setText(m_collab->statusText());
    const bool active = m_collab->isActive();
    const bool host = active && s->isHost();
    const bool joined = active && s->state() == Session::State::Joined;

    m_hostInfo->setVisible(host);
    m_addresses->setVisible(!host && joined);
    const bool p2p = host && s->connection() == Mode::P2P;
    m_codeLabel->setVisible(p2p);
    m_codeView->setVisible(p2p);
    m_addressLabel->setVisible(host && !p2p);
    m_hostAddress->setVisible(host && !p2p);
    if (p2p) {
        m_joinHelp->setText(tr("Para unirse: Archivo > Colaboración > P2P > Unirse con un código, y este código (en Kaint o en Kaint "
                               "para la web, desde cualquier lugar)."));
        m_codeView->setAddress(s->sessionCode());
        for (QWidget* w : {static_cast<QWidget*>(m_webLabel), static_cast<QWidget*>(m_webAddress), static_cast<QWidget*>(m_otherLabel),
                           m_otherAddresses})
            w->setVisible(false);
    } else if (host) {
        m_joinHelp->setText(tr("Para unirse desde Kaint: Archivo > Colaboración > Red local > Unirse a una sesión. Aparece en la "
                               "lista de sesiones cercanas; si no, se escribe esta dirección."));
        const QStringList addresses = s->addresses();
        const QString first = addresses.value(0);
        m_hostAddress->setAddress(first.isEmpty() ? QString() : QStringLiteral("%1:%2").arg(first).arg(s->port()));
        m_webLabel->setVisible(s->servesWeb() && !first.isEmpty());
        m_webAddress->setVisible(s->servesWeb() && !first.isEmpty());
        m_webAddress->setAddress(QStringLiteral("http://%1:%2/").arg(first).arg(s->port()));
        // Las demás (otra tarjeta de red, una VPN...), cada una con su botón; solo se rehacen si cambian.
        const QStringList rest = addresses.mid(1);
        QStringList withPort;
        for (const QString& a : rest)
            withPort << QStringLiteral("%1:%2").arg(a).arg(s->port());
        if (withPort != m_shownOthers) {
            m_shownOthers = withPort;
            for (QWidget* old : m_otherAddresses->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
                old->deleteLater();
            for (const QString& a : withPort) {
                auto* view = new AddressView(AddressView::Kind::Address, m_otherAddresses);
                view->setAddress(a);
                m_otherAddresses->layout()->addWidget(view);
            }
        }
        m_otherLabel->setVisible(!withPort.isEmpty());
        m_otherAddresses->setVisible(!withPort.isEmpty());
    } else {
        m_addresses->setText(joined ? tr("Lo que exportes es una copia: la imagen la guarda el anfitrión.") : QString());
    }

    // Participantes y solicitudes.
    const int selectedId = m_peers->currentItem() ? m_peers->currentItem()->data(0, Qt::UserRole).toInt() : -1;
    {
        const QSignalBlocker block(m_peers);
        m_peers->clear();
        if (active) {
            for (const Peer& p : s->peers()) {
                auto* item = new QTreeWidgetItem(m_peers);
                QString name = p.name;
                if (p.id == s->myId())
                    name += QStringLiteral(" ") + tr("(tú)");
                if (p.host)
                    name += QStringLiteral(" · ") + tr("anfitrión");
                item->setText(0, name);
                item->setIcon(0, swatch(p.color));
                item->setText(1, platformText(p));
                item->setText(2, p.host ? QString() : p.mode == QLatin1String("live") ? tr("En tiempo real") : tr("Con confirmaciones"));
                item->setData(0, Qt::UserRole, p.id);
                item->setData(0, Qt::UserRole + 1, false);
                if (p.id == selectedId)
                    m_peers->setCurrentItem(item);
            }
            for (const Peer& p : s->waitingPeers()) {
                auto* item = new QTreeWidgetItem(m_peers);
                item->setText(0, p.name);
                item->setIcon(0, Icons::theme({"dialog-question", "help-about"}));
                item->setText(1, platformText(p));
                item->setText(2, tr("Espera tu permiso"));
                item->setData(0, Qt::UserRole, p.id);
                item->setData(0, Qt::UserRole + 1, true);
                if (p.id == selectedId)
                    m_peers->setCurrentItem(item);
            }
        }
    }
    m_admit->setVisible(host);
    m_deny->setVisible(host);
    m_kick->setVisible(host);
    m_admit->setEnabled(host && selectedPeer(true) > 0);
    m_deny->setEnabled(host && selectedPeer(true) > 0);
    m_kick->setEnabled(host && selectedPeer(false) > 0);

    m_proposalsBox->setVisible(host);
    if (host) {
        if (m_proposals->count() != s->proposals().size())
            refreshProposals();
        const QSignalBlocker block(m_autoAccept);
        m_autoAccept->setChecked(m_collab->settings().autoAccept);
        m_accept->setEnabled(selectedProposal() > 0);
        m_reject->setEnabled(selectedProposal() > 0);
        m_acceptAll->setEnabled(!s->proposals().isEmpty());
    }

    const bool draft = joined && !s->isHost() && !s->isLive();
    m_draftBox->setVisible(draft);
    if (draft) {
        m_draftStatus->setText(s->proposalPending() ? tr("Enviados; esperando la respuesta del anfitrión.")
                               : s->hasDraft()      ? tr("Tienes cambios sin sugerir. Su historial es solo tuyo hasta que se aceptan.")
                                                    : tr("Sin cambios. Edita la imagen y sugiere los cambios al anfitrión."));
        m_propose->setEnabled(s->hasDraft() && !s->proposalPending());
        m_discard->setEnabled(s->hasDraft());
    }
    m_leave->setText(host ? tr("Terminar la sesión") : tr("Salir de la sesión"));
    m_leave->setEnabled(active);
}

// ---------------------------------------------------------------------------

HistoryDialog::HistoryDialog(CollabController* collab, QWidget* parent)
    : QDialog(parent)
    , m_collab(collab)
{
    setWindowTitle(tr("Historial de la sesión"));
    setWindowIcon(Icons::theme(QStringLiteral("collab")));
    setAttribute(Qt::WA_DeleteOnClose);
    auto* layout = new QVBoxLayout(this);
    m_info = new QLabel(this);
    m_info->setWordWrap(true);
    layout->addWidget(m_info);
    m_steps = new QListWidget(this);
    m_steps->setMinimumHeight(220);
    layout->addWidget(m_steps, 1);
    connect(m_steps, &QListWidget::itemDoubleClicked, this, &HistoryDialog::revert);
    connect(m_steps, &QListWidget::currentRowChanged, this, [this] {
        Session* s = m_collab->session();
        m_revert->setEnabled(s && s->canRevert() && m_steps->currentRow() >= 0 && m_steps->currentRow() != s->hostHistoryIndex());
    });
    m_revert = new QPushButton(Icons::theme({"edit-undo", "document-revert"}), tr("Volver a esta versión"), this);
    m_revert->setToolTip(tr("La imagen vuelve a como estaba en ese paso, para todos. Se puede rehacer desde el mismo historial."));
    connect(m_revert, &QPushButton::clicked, this, &HistoryDialog::revert);
    m_note = new QLabel(this);
    m_note->setWordWrap(true);
    m_note->setForegroundRole(QPalette::PlaceholderText);
    auto* row = new QHBoxLayout;
    row->addWidget(m_revert);
    row->addWidget(m_note, 1);
    layout->addLayout(row);

    // Anfitrión: quién más puede volver a versiones anteriores.
    m_permissions = new QGroupBox(tr("Pueden volver a versiones anteriores"), this);
    auto* permissions = new QVBoxLayout(m_permissions);
    m_permissionList = new QWidget(m_permissions);
    auto* list = new QVBoxLayout(m_permissionList);
    list->setContentsMargins(0, 0, 0, 0);
    permissions->addWidget(m_permissionList);
    layout->addWidget(m_permissions);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::close);
    layout->addWidget(box);

    connect(collab, &CollabController::changed, this, &HistoryDialog::refresh);
    connect(collab, &CollabController::tabStateChanged, this, &HistoryDialog::refresh);
    resize(420, 520);
    refresh();
}

void HistoryDialog::refresh()
{
    Session* s = m_collab->session();
    const bool active = m_collab->isActive() && s && s->state() == Session::State::Joined;
    if (!active) {
        m_info->setText(tr("No estás en ninguna sesión."));
        m_steps->clear();
        m_revert->setEnabled(false);
        m_permissions->hide();
        return;
    }
    const bool host = s->isHost();
    m_info->setText(host ? tr("Tu historial, el de la imagen de la sesión. Volver a un paso cambia la imagen para todos.")
                         : tr("El historial de %1, el anfitrión. Volver a un paso cambia la imagen para todos.").arg(s->peer(0).name));
    const QStringList steps = s->hostHistory();
    const int current = s->hostHistoryIndex();
    const int selected = m_steps->currentRow();
    const bool followCurrent = selected < 0 || selected == m_steps->property("current").toInt();
    {
        const QSignalBlocker block(m_steps);
        m_steps->clear();
        QStringList names{tr("Imagen original")};
        names += steps;
        for (int i = 0; i < names.size(); ++i) {
            auto* item = new QListWidgetItem(names[i], m_steps);
            QFont f = item->font();
            if (i == current)
                f.setBold(true);
            // Lo que está deshecho (se puede rehacer), en cursiva y apagado.
            if (i > current) {
                f.setItalic(true);
                item->setForeground(palette().color(QPalette::PlaceholderText));
            }
            item->setFont(f);
        }
        m_steps->setProperty("current", current);
        m_steps->setCurrentRow(followCurrent ? current : std::min(selected, int(m_steps->count()) - 1));
    }
    m_revert->setEnabled(s->canRevert() && m_steps->currentRow() >= 0 && m_steps->currentRow() != current);
    m_note->setText(s->canRevert() ? QString()
                                   : tr("Solo el anfitrión y quienes tengan su permiso pueden volver a una versión anterior."));

    m_permissions->setVisible(host);
    if (!host)
        return;
    QList<int> guests;
    for (const Peer& p : s->peers())
        if (!p.host)
            guests.append(p.id);
    if (guests != m_shownGuests) {
        m_shownGuests = guests;
        for (QWidget* w : m_permissionList->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
            w->deleteLater();
        for (int id : guests) {
            auto* check = new QCheckBox(m_permissionList);
            check->setProperty("peer", id);
            m_permissionList->layout()->addWidget(check);
            connect(check, &QCheckBox::toggled, this, [this, id](bool on) {
                if (Session* session = m_collab->session())
                    session->setHistoryPermission(id, on);
            });
        }
        if (guests.isEmpty())
            m_permissionList->layout()->addWidget(new QLabel(tr("Todavía no hay invitados."), m_permissionList));
    }
    for (QCheckBox* check : m_permissionList->findChildren<QCheckBox*>()) {
        const Peer p = s->peer(check->property("peer").toInt());
        const QSignalBlocker block(check);
        check->setText(p.name);
        check->setChecked(p.history);
    }
}

void HistoryDialog::revert()
{
    Session* s = m_collab->session();
    const int row = m_steps->currentRow();
    if (!s || !s->canRevert() || row < 0 || row == s->hostHistoryIndex())
        return;
    s->revertTo(row);
}

} // namespace Collab
