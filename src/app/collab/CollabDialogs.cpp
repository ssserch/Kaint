#include "app/collab/CollabDialogs.h"

#include "app/Icons.h"
#include "app/collab/CollabController.h"
#include "app/collab/Discovery.h"
#include "app/dialogs/TaskDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
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

JoinDialog::JoinDialog(CollabController* collab, QWidget* parent)
    : QDialog(parent)
    , m_collab(collab)
    , m_discovery(new Discovery(this))
{
    setWindowTitle(tr("Unirse a una sesión colaborativa"));
    setWindowIcon(Icons::theme(QStringLiteral("collab")));
    const Settings settings = collab->settings();

    auto* layout = new QVBoxLayout(this);
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

    auto* form = new QFormLayout;
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

    connect(m_discovery, &Discovery::changed, this, &JoinDialog::refreshFound);
    if (!m_discovery->start())
        m_status->setText(tr("No se pueden buscar sesiones (el puerto %1 está ocupado). Escribe la dirección del anfitrión.")
                              .arg(kBeaconPort));
    refreshFound();
    connect(collab, &CollabController::documentReady, this, [this] {
        m_connecting = false;
        accept();
    });
    resize(620, 480);
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

    if (host) {
        QStringList lines;
        const QStringList addresses = s->addresses();
        const QString first = addresses.value(0, QStringLiteral("127.0.0.1"));
        lines << tr("Para unirse desde Kaint: Archivo > Colaboración > Unirse, dirección %1, puerto %2.").arg(first).arg(s->port());
        if (s->servesWeb())
            lines << tr("Desde un navegador de la red: http://%1:%2/").arg(first).arg(s->port());
        if (addresses.size() > 1)
            lines << tr("Otras direcciones de este equipo: %1").arg(addresses.mid(1).join(QStringLiteral(", ")));
        m_addresses->setText(lines.join(QLatin1Char('\n')));
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

} // namespace Collab
