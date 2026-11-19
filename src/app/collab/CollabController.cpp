#include "app/collab/CollabController.h"

#include "app/AppState.h"
#include "app/Canvas.h"
#include "app/Icons.h"
#include "app/collab/CollabDialogs.h"
#include "app/dialogs/SettingsDialog.h"
#include "app/dialogs/TaskDialog.h"
#include "app/tools/ToolManager.h"
#include "core/Document.h"

#include <QApplication>
#include <QDateTime>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>

static QIcon chatBubbleIcon();

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
    // El chat, con los mensajes sin leer.
    m_chatButton = new QToolButton(m_status);
    m_chatButton->setAutoRaise(true);
    m_chatButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_chatButton->setIcon(chatBubbleIcon());
    m_chatButton->setToolTip(tr("Chat de la sesión"));
    connect(m_chatButton, &QToolButton::clicked, this, &CollabController::showChat);
    layout->addWidget(m_chatButton);
    m_status->hide();

    m_expireTimer.setSingleShot(true);
    m_expireTimer.setInterval(250);
    connect(&m_expireTimer, &QTimer::timeout, this, &CollabController::refreshPointers);

    connect(canvas, &Canvas::cursorMoved, this, &CollabController::sendCursor);
    connect(canvas, &Canvas::toolButtonChanged, this, &CollabController::sendCursor);
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

void CollabController::joinInvite(const QString& rawCode, const QString& source)
{
    const QString code = Collab::normalizedSessionCode(rawCode);
    if (code.isEmpty())
        return;
    if (m_window) {
        if (m_window->isMinimized())
            m_window->showNormal();
        m_window->raise();
        m_window->activateWindow();
    }
    // Ya se está en esa sesión.
    if (isActive() && Collab::normalizedSessionCode(m_session->sessionCode()) == code) {
        showSessionWindow();
        return;
    }
    if (isActive() && !leave(true))
        return;
    auto* dlg = new Collab::JoinDialog(this, Collab::Mode::P2P, m_window);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
    dlg->startInvite(code, source);
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
    // Desde Configuración (al hospedar o con su botón «Ventana de la sesión»): Configuración es modal y,
    // con la ventana de la sesión encima, seguía bloqueando la imagen. Se cierra antes (sus cambios ya
    // están aplicados) y la ventana de la sesión se abre sobre la principal sin bloquear nada, como
    // desde «N participantes» en la barra de estado: se puede mover y seguir editando con ella abierta.
    if (auto* settings = qobject_cast<SettingsDialog*>(QApplication::activeModalWidget())) {
        settings->accept();
        QTimer::singleShot(0, this, &CollabController::showSessionWindow);
        return;
    }
    // Encima de otra ventana modal si la hay (si no, quedaría bloqueada por ella).
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
    connect(session, &Collab::Session::chatReceived, this, &CollabController::onChat);
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
    for (const QPointer<QDialog>& box : std::as_const(m_requests))
        if (box)
            box->deleteLater();
    m_requests.clear();
    m_canvas->setRemotePointers({});
    m_bubbles.clear();
    m_unread = 0;
    if (m_chatDialog)
        m_chatDialog->close();
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

// Un globo de texto pequeño (el acceso directo al chat en la barra de estado), dibujado aquí para
// que se vea igual con cualquier tema de iconos.
static QIcon chatBubbleIcon()
{
    QIcon icon;
    for (int side : {16, 24, 32}) {
        QPixmap pm(side, side);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        const qreal u = side / 16.0;
        QPainterPath bubble;
        bubble.addRoundedRect(QRectF(1 * u, 1.5 * u, 14 * u, 9.5 * u), 3 * u, 3 * u);
        QPainterPath tail;
        tail.moveTo(4 * u, 10.5 * u);
        tail.lineTo(3.5 * u, 14.5 * u);
        tail.lineTo(8 * u, 10.5 * u);
        tail.closeSubpath();
        bubble = bubble.united(tail);
        p.setPen(QPen(QColor(40, 90, 170), 1.2 * u));
        p.setBrush(QColor(235, 244, 255));
        p.drawPath(bubble);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(40, 90, 170));
        for (int i = 0; i < 3; ++i)
            p.drawEllipse(QPointF((5 + i * 3) * u, 6.25 * u), 1.05 * u, 1.05 * u);
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

void CollabController::refreshPointers()
{
    QList<Canvas::RemotePointer> pointers;
    bool pending = false;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (isActive() && m_settings.showCursors && m_canvas->document() && m_canvas->document() == m_session->document()) {
        const auto& cursors = m_session->cursors();
        for (auto it = cursors.cbegin(); it != cursors.cend(); ++it) {
            if (!it->inside || it.key() == m_session->myId())
                continue;
            const Collab::Peer p = m_session->peer(it.key());
            if (p.name.isEmpty())
                continue;
            Canvas::RemotePointer r{it->pos, p.color, p.name, toolIcon(it->tool)};
            // Lo que está dibujando y, al soltar, un momento más (hasta que llega el cambio).
            const bool recent = it->down || (it->releasedAt > 0 && now - it->releasedAt < 1500);
            if (m_settings.liveStrokes && recent && !it->path.isEmpty()) {
                r.liveKind = Collab::liveStrokeKind(it->tool);
                r.path = it->path;
                r.liveColor = it->color.isEmpty() ? QColor() : QColor(it->color);
                r.liveSize = it->size;
                pending = pending || !it->down;
            }
            const auto bubble = m_bubbles.value(it.key());
            if (m_settings.chat && m_settings.chatBubbles && !bubble.first.isEmpty() && now < bubble.second) {
                r.bubble = bubble.first;
                pending = true;
            }
            pointers.append(r);
        }
    }
    m_canvas->setRemotePointers(pointers);
    if (pending && !m_expireTimer.isActive())
        m_expireTimer.start();
}

bool CollabController::chatAvailable() const
{
    return isActive() && m_session->state() == Collab::Session::State::Joined && m_settings.chat;
}

void CollabController::onChat(const Collab::ChatMessage& message)
{
    if (!m_settings.chat)
        return;
    appendChat(message);
    if (!m_session || message.from == m_session->myId())
        return;
    // Un globo junto a su puntero, más tiempo cuanto más largo es el mensaje.
    const qint64 ms = std::clamp<qint64>(4000 + 60 * message.text.size(), 5000, 15000);
    m_bubbles.insert(message.from, {message.text, QDateTime::currentMSecsSinceEpoch() + ms});
    refreshPointers();
    if (!m_chatDialog || !m_chatDialog->isVisible()) {
        ++m_unread;
        updateStatusWidget();
        emit this->message(QStringLiteral("%1: %2").arg(message.name, message.text));
    }
}

void CollabController::appendChat(const Collab::ChatMessage& message)
{
    if (!m_chatLog)
        return;
    const QString time = QDateTime::fromMSecsSinceEpoch(message.time).toString(QStringLiteral("HH:mm"));
    m_chatLog->append(QStringLiteral("<span style=\"color:gray\">%1</span> <b style=\"color:%2\">%3</b>: %4")
                          .arg(time, message.color.isValid() ? message.color.name() : QStringLiteral("#808080"),
                               message.name.toHtmlEscaped(), message.text.toHtmlEscaped()));
}

void CollabController::showChat()
{
    if (!chatAvailable())
        return;
    if (!m_chatDialog) {
        auto* dlg = new QDialog(m_window, Qt::Tool);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setWindowTitle(tr("Chat de la sesión"));
        dlg->resize(340, 380);
        auto* layout = new QVBoxLayout(dlg);
        m_chatLog = new QTextBrowser(dlg);
        m_chatLog->setOpenExternalLinks(false);
        auto* row = new QHBoxLayout;
        auto* input = new QLineEdit(dlg);
        input->setMaxLength(500);
        input->setPlaceholderText(tr("Escribe un mensaje…"));
        auto* send = new QPushButton(tr("Enviar"), dlg);
        send->setDefault(true);
        row->addWidget(input, 1);
        row->addWidget(send);
        layout->addWidget(m_chatLog, 1);
        layout->addLayout(row);
        auto submit = [this, input] {
            if (m_session && !input->text().trimmed().isEmpty())
                m_session->sendChat(input->text());
            input->clear();
            input->setFocus();
        };
        connect(send, &QPushButton::clicked, dlg, submit);
        connect(input, &QLineEdit::returnPressed, dlg, submit);
        m_chatDialog = dlg;
        if (m_session)
            for (const Collab::ChatMessage& msg : m_session->chat())
                appendChat(msg);
        dlg->findChild<QLineEdit*>()->setFocus();
    }
    m_unread = 0;
    updateStatusWidget();
    m_chatDialog->show();
    m_chatDialog->raise();
    m_chatDialog->activateWindow();
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
    const QString tool = t ? Collab::toolName(int(t->id())) : QString();
    // Con el botón pulsado en una herramienta de pintar, los demás ven el trazo en vivo: su color (el
    // principal o, con el derecho, el secundario; nada al borrar) y su grosor.
    const Qt::MouseButton button = m_canvas->toolButton();
    bool down = false;
    QString color;
    double size = 0;
    if (inside && button != Qt::NoButton && Collab::liveStrokeKind(tool) != 0) {
        const AppState* state = AppState::instance();
        down = true;
        if (tool != QLatin1String("eraser"))
            color = (button == Qt::RightButton ? state->secondary() : state->primary()).name(QColor::HexRgb);
        size = tool == QLatin1String("pencil") ? 1 : state->tools().brushSize;
    }
    m_session->setCursor(m_canvas->cursorImagePos(), inside, tool, down, color, size);
}

void CollabController::askAdmission(const Collab::Peer& peer)
{
    // Un diálogo normal (con el marco de Kaint; el QMessageBox salía con dos barras de título) y
    // modal: encima de la ventana modal que haya (Configuración...), que si no lo bloqueaba y no se
    // podía ni aceptar ni cerrar. Se muestra sin esperar (show), así que no detiene nada.
    QWidget* parent = QApplication::activeModalWidget() ? QApplication::activeModalWidget() : m_window.data();
    auto* box = new QDialog(parent);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setWindowTitle(tr("Sesión colaborativa"));
    box->setWindowModality(Qt::ApplicationModal);
    auto* layout = new QVBoxLayout(box);
    auto* row = new QHBoxLayout;
    auto* iconLabel = new QLabel(box);
    iconLabel->setPixmap(Icons::theme({"dialog-question", "help-about"}).pixmap(32, 32));
    iconLabel->setAlignment(Qt::AlignTop);
    auto* text = new QLabel(tr("%1 quiere unirse a tu sesión (%2).").arg(peer.name, peer.app.isEmpty() ? peer.platform : peer.app), box);
    text->setWordWrap(true);
    row->addWidget(iconLabel);
    row->addWidget(text, 1);
    layout->addLayout(row);
    auto* buttons = new QHBoxLayout;
    auto* yes = new QPushButton(tr("Dejar entrar"), box);
    auto* no = new QPushButton(tr("Rechazar"), box);
    yes->setDefault(true);
    buttons->addStretch(1);
    buttons->addWidget(yes);
    buttons->addWidget(no);
    layout->addLayout(buttons);
    const int id = peer.id;
    auto answer = [this, box, id](bool ok) {
        if (!m_requests.contains(id))
            return;
        m_requests.remove(id);
        if (m_session)
            m_session->admit(id, ok);
        box->close();
    };
    connect(yes, &QPushButton::clicked, box, [answer] { answer(true); });
    connect(no, &QPushButton::clicked, box, [answer] { answer(false); });
    // Cerrarlo con la ✕ o Esc es rechazar.
    connect(box, &QDialog::rejected, this, [answer] { answer(false); });
    m_requests.insert(id, box);
    box->show();
    box->raise();
    box->activateWindow();
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
    m_chatButton->setVisible(chatAvailable());
    // Solo el globo; con mensajes sin leer, cuántos al lado.
    m_chatButton->setToolButtonStyle(m_unread > 0 ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly);
    m_chatButton->setText(m_unread > 0 ? QString::number(m_unread) : QString());
    m_chatButton->setToolTip(m_unread > 0 ? tr("Chat de la sesión (%n sin leer)", nullptr, m_unread) : tr("Chat de la sesión"));
    m_status->show();
}
