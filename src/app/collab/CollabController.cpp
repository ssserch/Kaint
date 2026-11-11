#include "app/collab/CollabController.h"

#include "app/Canvas.h"
#include "app/Icons.h"
#include "app/collab/CollabDialogs.h"
#include "app/dialogs/TaskDialog.h"
#include "app/tools/ToolManager.h"
#include "core/Document.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QToolButton>

namespace {
CollabController* g_instance = nullptr;
}

CollabController::CollabController(QWidget* window, Canvas* canvas, ToolManager* tools)
    : QObject(window)
    , m_window(window)
    , m_canvas(canvas)
    , m_tools(tools)
    , m_settings(Collab::Settings::load())
{
    g_instance = this;

    // Botón de la barra de estado (solo se ve con una sesión): participantes y sugerencias; a su
    // lado, para los invitados con confirmaciones, Sugerir y Descartar.
    m_status = new QWidget;
    auto* layout = new QHBoxLayout(m_status);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    m_statusButton = new QToolButton(m_status);
    m_statusButton->setAutoRaise(true);
    m_statusButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_statusButton->setIcon(Icons::theme(QStringLiteral("collab")));
    m_statusButton->setToolTip(tr("Sesión colaborativa: participantes y sugerencias"));
    connect(m_statusButton, &QToolButton::clicked, this, &CollabController::showSessionWindow);
    m_proposeButton = new QToolButton(m_status);
    m_proposeButton->setAutoRaise(true);
    m_proposeButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_proposeButton->setIcon(Icons::theme({"mail-send", "document-send", "go-up"}));
    m_proposeButton->setText(tr("Sugerir cambios"));
    m_proposeButton->setToolTip(tr("Envía al anfitrión lo que has cambiado para que lo acepte."));
    connect(m_proposeButton, &QToolButton::clicked, this, [this] {
        if (m_session)
            m_session->propose();
    });
    m_discardButton = new QToolButton(m_status);
    m_discardButton->setAutoRaise(true);
    m_discardButton->setIcon(Icons::theme({"edit-clear-history", "edit-undo", "edit-delete"}));
    m_discardButton->setToolTip(tr("Descartar el borrador (vuelve a la imagen de la sesión)"));
    connect(m_discardButton, &QToolButton::clicked, this, [this] {
        if (!m_session)
            return;
        TaskDialog ask(tr("Descartar el borrador"), Icons::theme({"edit-delete"}), QImage(),
                       tr("Se pierden los cambios que no has sugerido y su historial."), m_window);
        const int yes = ask.addChoice(Icons::theme({"edit-delete"}), tr("Descartar"), tr("Volver a la imagen de la sesión."));
        ask.addChoice(Icons::theme({"dialog-cancel"}), tr("Cancelar"), tr("Seguir con el borrador."), true);
        if (ask.choose() == yes)
            m_session->discardDraft();
    });
    layout->addWidget(m_statusButton);
    layout->addWidget(m_proposeButton);
    layout->addWidget(m_discardButton);
    m_status->hide();

    connect(canvas, &Canvas::cursorMoved, this, &CollabController::sendCursor);
    connect(canvas, &Canvas::viewChanged, this, &CollabController::refreshPointers);
    connect(tools, &ToolManager::toolChanged, this, &CollabController::sendCursor);
}

CollabController::~CollabController()
{
    if (g_instance == this)
        g_instance = nullptr;
    if (m_session)
        m_session->leave();
    if (m_status && !m_status->parentWidget())
        delete m_status;
}

CollabController* CollabController::instance()
{
    return g_instance;
}

bool CollabController::isActive() const
{
    return m_session && m_session->state() != Collab::Session::State::Ended;
}

bool CollabController::isHosting() const
{
    return isActive() && m_session->isHost();
}

bool CollabController::isConnecting() const
{
    return isActive() && m_session->state() != Collab::Session::State::Joined;
}

bool CollabController::isGuestDocument(const Document* doc) const
{
    return doc && isActive() && !m_session->isHost() && m_session->document() == doc;
}

bool CollabController::isSessionDocument(const Document* doc) const
{
    return doc && isActive() && m_session->document() == doc;
}

bool CollabController::applyingRemote() const
{
    return m_session && m_session->applyingRemote();
}

void CollabController::applySettings(const Collab::Settings& settings)
{
    const bool renamed = settings.displayName() != m_settings.displayName();
    m_settings = settings;
    m_settings.save();
    if (isActive()) {
        if (m_session->isHost())
            m_session->updateSettings(m_settings);
        else if (renamed)
            m_session->rename(m_settings.name);
    }
    refreshPointers();
    emit changed();
}

QWidget* CollabController::statusWidget() const
{
    return m_status;
}

QIcon CollabController::toolIcon(const QString& name) const
{
    for (Tool* t : m_tools->tools())
        if (Collab::toolName(int(t->id())) == name)
            return t->icon();
    return {};
}

QString CollabController::statusText() const
{
    if (!isActive())
        return tr("No estás en ninguna sesión.");
    const Collab::Session* s = m_session;
    switch (s->state()) {
    case Collab::Session::State::Connecting:
        if (s->connection() == Collab::Mode::P2P)
            return s->progressText().isEmpty() ? tr("Buscando la sesión…") : s->progressText();
        return tr("Conectando con %1…").arg(s->address());
    case Collab::Session::State::Waiting:
        return tr("Esperando a que el anfitrión te deje entrar…");
    default:
        break;
    }
    const int people = int(s->peers().size());
    // Sin la dirección: se muestra aparte, difuminada (Configuración y la ventana de la sesión).
    const QString count = people == 1 ? tr("1 participante") : tr("%1 participantes").arg(people);
    if (s->isHost() && s->connection() == Collab::Mode::P2P) {
        // Sin servidores de señalización nadie más puede encontrar la sesión.
        if (s->signalingReady() == 0)
            return tr("Hospedando «%1» por P2P · %2 · sin conexión con los servidores de señalización (se sigue intentando)")
                .arg(s->sessionName(), count);
        return tr("Hospedando «%1» por P2P · %2").arg(s->sessionName(), count);
    }
    if (s->isHost())
        return tr("Hospedando «%1» en la red local (puerto %2) · %3").arg(s->sessionName()).arg(s->port()).arg(count);
    return tr("En «%1» de %2 · %3 · %4")
        .arg(s->sessionName(), s->peer(0).name, s->isLive() ? tr("en tiempo real") : tr("con confirmaciones"), count);
}

bool CollabController::host(Document* doc, Collab::Mode mode)
{
    if (!doc)
        return false;
    if (isActive() && !leave(true))
        return false;
    QString error;
    Collab::Settings settings = m_settings;
    settings.mode = mode;
    Collab::Session* s = Collab::Session::host(doc, settings, &error, this);
    if (!s) {
        if (mode == Collab::Mode::Lan)
            QMessageBox::warning(m_window, tr("Sesión colaborativa"),
                                 tr("No se pudo abrir la sesión en el puerto %1:\n%2\n\nPrueba con otro puerto en Configuración > "
                                    "Colaboración.")
                                     .arg(m_settings.port)
                                     .arg(error));
        else
            QMessageBox::warning(m_window, tr("Sesión colaborativa"), tr("No se pudo abrir la sesión P2P:\n%1").arg(error));
        return false;
    }
    attach(s);
    emit message(tr("Sesión colaborativa abierta: %1").arg(statusText()));
    showSessionWindow();
    return true;
}

bool CollabController::hostCurrent(Collab::Mode mode)
{
    Document* doc = m_currentDocument ? m_currentDocument() : nullptr;
    if (!doc) {
        QMessageBox::information(m_window, tr("Sesión colaborativa"), tr("Abre o crea una imagen para compartirla."));
        return false;
    }
    return host(doc, mode);
}

void CollabController::join(const QString& address, quint16 port, const QString& password)
{
    if (isActive())
        m_session->leave();
    attach(Collab::Session::join(address, port, m_settings, password, this));
}

void CollabController::joinP2P(const QString& code, const QString& password)
{
    if (isActive())
        m_session->leave();
    attach(Collab::Session::joinP2P(code, m_settings, password, this));
}

void CollabController::showJoinDialog(Collab::Mode mode)
{
    if (isActive() && !leave(true))
        return;
    Collab::JoinDialog dlg(this, mode, m_window);
    dlg.exec();
}

bool CollabController::leave(bool ask)
{
    if (!isActive())
        return true;
    if (ask) {
        const bool host = m_session->isHost();
        TaskDialog dlg(host ? tr("Terminar la sesión") : tr("Salir de la sesión"), Icons::theme(QStringLiteral("collab")), QImage(),
                       host ? tr("Los invitados dejarán de estar conectados a tu imagen.")
                            : tr("Dejarás de estar conectado. Puedes exportar una copia de la imagen antes de salir; lo que "
                                 "no hayas sugerido se pierde."),
                       m_window);
        const int yes = dlg.addChoice(Icons::theme({"application-exit", "system-log-out"}), host ? tr("Terminar") : tr("Salir"),
                                      host ? tr("Cerrar la sesión para todos.") : tr("Salir de la sesión."));
        dlg.addChoice(Icons::theme({"dialog-cancel"}), tr("Cancelar"), tr("Seguir en la sesión."), true);
        if (dlg.choose() != yes)
            return false;
    }
    m_session->leave();
    return true;
}

void CollabController::showSessionWindow()
{
    // Encima de Configuración si está abierta (si no, quedaría bloqueada por ella).
    QWidget* parent = QApplication::activeModalWidget() ? QApplication::activeModalWidget() : m_window.data();
    if (m_dialog && m_dialog->parentWidget() != parent) {
        m_dialog->close();
        m_dialog = nullptr;
    }
    if (!m_dialog)
        m_dialog = new Collab::SessionDialog(this, parent);
    m_dialog->show();
    m_dialog->raise();
    m_dialog->activateWindow();
}

void CollabController::attach(Collab::Session* session)
{
    if (m_session && m_session != session)
        m_session->deleteLater();
    m_session = session;
    session->setBusyCheck([this](const Layer** editing) {
        const Document* doc = m_session ? m_session->document() : nullptr;
        const Layer* layer = nullptr;
        bool busy = doc && doc->hasPreview();
        if (doc && m_canvas->document() == doc) {
            if (Tool* t = m_tools->current()) {
                layer = t->editingLayer();
                busy = busy || layer || t->isBusy();
            }
        }
        if (editing)
            *editing = layer;
        return busy;
    });
    connect(session, &Collab::Session::documentReady, this, [this](Document* doc) {
        emit documentReady(doc);
        emit message(tr("Te uniste a «%1».").arg(m_session ? m_session->sessionName() : QString()));
        sendCursor();
    });
    connect(session, &Collab::Session::ended, this, &CollabController::onEnded);
    connect(session, &Collab::Session::notice, this, &CollabController::message);
    connect(session, &Collab::Session::cursorsChanged, this, &CollabController::refreshPointers);
    connect(session, &Collab::Session::joinRequested, this, &CollabController::askAdmission);
    for (auto signal : {&Collab::Session::peersChanged, &Collab::Session::proposalsChanged, &Collab::Session::draftChanged,
                        &Collab::Session::stateChanged}) {
        connect(session, signal, this, [this] {
            updateStatusWidget();
            refreshPointers();
            emit changed();
        });
    }
    // La pestaña de la imagen (corona, copia propia) y el historial del anfitrión.
    auto tab = [this] {
        if (m_session && m_session->document())
            emit tabStateChanged(m_session->document());
    };
    connect(session, &Collab::Session::indicatorsChanged, this, tab);
    connect(session, &Collab::Session::stateChanged, this, tab);
    connect(session, &Collab::Session::documentReady, this, tab);
    connect(session, &Collab::Session::ended, this, [this, session] {
        if (Document* doc = session->document())
            emit tabStateChanged(doc);
    });
    connect(session, &Collab::Session::revertRequested, this, [this](int index) {
        if (m_session && m_session->document() && m_historyJumper)
            m_historyJumper(m_session->document(), index);
    });
    connect(session, &Collab::Session::peersChanged, this, [this] {
        // Solicitudes de quien ya se fue.
        if (!m_session)
            return;
        QSet<int> waiting;
        for (const Collab::Peer& p : m_session->waitingPeers())
            waiting.insert(p.id);
        for (auto it = m_requests.begin(); it != m_requests.end();) {
            if (!waiting.contains(it.key())) {
                if (*it)
                    (*it)->deleteLater();
                it = m_requests.erase(it);
            } else {
                ++it;
            }
        }
    });
    updateStatusWidget();
    emit changed();
}

CollabController::TabState CollabController::tabState(const Document* doc) const
{
    TabState st;
    if (!doc || !isActive() || m_session->document() != doc || m_session->state() != Collab::Session::State::Joined)
        return st;
    st.collab = true;
    st.hostSaved = m_session->hostSaved();
    st.guest = !m_session->isHost();
    st.copy = st.guest ? m_session->localCopy() : Collab::Session::LocalCopy::None;
    return st;
}

void CollabController::markExported(const Document* doc)
{
    if (isActive() && m_session->document() == doc && !m_session->isHost())
        m_session->markExported();
}

void CollabController::showSessionHistory()
{
    if (!isActive())
        return;
    QWidget* parent = QApplication::activeModalWidget() ? QApplication::activeModalWidget() : m_window.data();
    if (!m_historyDialog)
        m_historyDialog = new Collab::HistoryDialog(this, parent);
    m_historyDialog->show();
    m_historyDialog->raise();
    m_historyDialog->activateWindow();
}

void CollabController::onEnded(const QString& reason)
{
    for (const QPointer<QMessageBox>& box : std::as_const(m_requests))
        if (box)
            box->deleteLater();
    m_requests.clear();
    m_canvas->setRemotePointers({});
    if (!reason.isEmpty()) {
        emit message(reason);
        // Si se estaba dentro, se avisa con una ventana (al intentar entrar, el diálogo lo muestra).
        if (m_session && m_session->document() && !m_session->isHost()) {
            auto* box = new QMessageBox(QMessageBox::Information, tr("Sesión colaborativa"),
                                        reason + QStringLiteral("\n\n") + tr("La imagen sigue abierta: puedes guardarla como una copia."),
                                        QMessageBox::Ok, m_window);
            box->setAttribute(Qt::WA_DeleteOnClose);
            box->open();
        }
    } else {
        emit message(tr("La sesión colaborativa terminó."));
    }
    if (m_session)
        m_session->deleteLater();
    m_session = nullptr;
    updateStatusWidget();
    emit changed();
}

void CollabController::refreshPointers()
{
    QList<Canvas::RemotePointer> pointers;
    if (isActive() && m_settings.showCursors && m_canvas->document() && m_canvas->document() == m_session->document()) {
        const auto& cursors = m_session->cursors();
        for (auto it = cursors.cbegin(); it != cursors.cend(); ++it) {
            if (!it->inside || it.key() == m_session->myId())
                continue;
            const Collab::Peer p = m_session->peer(it.key());
            if (p.name.isEmpty())
                continue;
            pointers.append({it->pos, p.color, p.name, toolIcon(it->tool)});
        }
    }
    m_canvas->setRemotePointers(pointers);
}

void CollabController::sendCursor()
{
    if (!isActive() || m_session->state() != Collab::Session::State::Joined)
        return;
    const bool ours = m_canvas->document() && m_canvas->document() == m_session->document();
    const bool inside = ours && m_canvas->cursorInside();
    if (!inside && !m_cursorWasInside)
        return;
    m_cursorWasInside = inside;
    const Tool* t = m_tools->current();
    m_session->setCursor(m_canvas->cursorImagePos(), inside, t ? Collab::toolName(int(t->id())) : QString());
}

void CollabController::askAdmission(const Collab::Peer& peer)
{
    auto* box = new QMessageBox(QMessageBox::Question, tr("Sesión colaborativa"),
                                tr("%1 quiere unirse a tu sesión (%2).").arg(peer.name, peer.app.isEmpty() ? peer.platform : peer.app),
                                QMessageBox::NoButton, m_window);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setWindowModality(Qt::NonModal);
    QPushButton* yes = box->addButton(tr("Dejar entrar"), QMessageBox::AcceptRole);
    box->addButton(tr("Rechazar"), QMessageBox::RejectRole);
    box->setDefaultButton(yes);
    const int id = peer.id;
    connect(box, &QMessageBox::buttonClicked, this, [this, box, yes, id](QAbstractButton* b) {
        m_requests.remove(id);
        if (m_session)
            m_session->admit(id, b == yes);
        box->close();
    });
    m_requests.insert(id, box);
    box->show();
    emit message(tr("%1 quiere unirse a tu sesión.").arg(peer.name));
}

void CollabController::updateStatusWidget()
{
    if (!isActive()) {
        m_status->hide();
        return;
    }
    const Collab::Session* s = m_session;
    QString text;
    if (s->state() == Collab::Session::State::Joined) {
        const int people = int(s->peers().size());
        const int proposals = int(s->proposals().size());
        text = people == 1 ? tr("1 participante") : tr("%1 participantes").arg(people);
        if (s->isHost() && proposals > 0)
            text += QStringLiteral(" · ") + (proposals == 1 ? tr("1 sugerencia") : tr("%1 sugerencias").arg(proposals));
    } else {
        text = tr("Conectando…");
    }
    m_statusButton->setText(text);
    const bool confirmGuest = !s->isHost() && s->state() == Collab::Session::State::Joined && !s->isLive();
    m_proposeButton->setVisible(confirmGuest);
    m_discardButton->setVisible(confirmGuest);
    m_proposeButton->setEnabled(s->hasDraft() && !s->proposalPending());
    m_proposeButton->setText(s->proposalPending() ? tr("Esperando al anfitrión…") : tr("Sugerir cambios"));
    m_discardButton->setEnabled(s->hasDraft());
    m_status->show();
}
