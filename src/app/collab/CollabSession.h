#pragma once

#include "app/collab/CollabProtocol.h"
#include "app/collab/CollabServer.h"
#include "core/Document.h"
#include "core/Layer.h"

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QMap>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QTimer>

#include <functional>

class Link;
struct DocumentState;

namespace Collab {

class P2PGuest;

// Puntero de otro participante (en coordenadas de la imagen).
struct RemoteCursor {
    QPointF pos;
    QString tool;
    bool inside = false;
    qint64 time = 0;
};

// Cambio que un invitado sugiere al anfitrión.
struct Proposal {
    int pid = 0;
    int from = 0;
    QString name;
    QString label;
    QJsonArray ops;
    QDateTime time;
};

// Una sesión colaborativa sobre un documento: como anfitrión (con su servidor) o como invitado.
//
// Cada participante guarda la copia "canónica" de la imagen (la del anfitrión, lo último que
// recibió) además de su documento de trabajo. Lo que cambia en el documento se descubre
// comparándolo con esa copia tras cada paso del historial y viaja como operaciones (píxeles de
// una región con su máscara, la lista de capas o la imagen entera). Lo que llega de los demás se
// aplica a las dos copias; en el documento de trabajo solo donde no hay cambios propios sin enviar
// y, mientras una herramienta está trabajando en una capa, lo que la toca espera a que termine.
class Session : public QObject {
    Q_OBJECT
public:
    enum class Role { Host, Guest };
    enum class State { Connecting, Waiting, Joined, Ended };

    static Session* host(Document* doc, const Settings& settings, QString* error, QObject* parent = nullptr);
    static Session* join(const QString& address, quint16 port, const Settings& settings, const QString& password,
                         QObject* parent = nullptr);
    // P2P: con el código de la sesión.
    static Session* joinP2P(const QString& code, const Settings& settings, const QString& password, QObject* parent = nullptr);
    ~Session() override;

    Role role() const { return m_role; }
    bool isHost() const { return m_role == Role::Host; }
    State state() const { return m_state; }
    Document* document() const { return m_doc; }
    QString address() const { return m_address; }
    quint16 port() const;
    QStringList addresses() const;
    bool servesWeb() const;
    QString sessionName() const { return m_sessionName; }
    // P2P o red local.
    Mode connection() const { return m_connection; }
    // P2P: el código de la sesión (el anfitrión lo comparte; el invitado entró con él).
    QString sessionCode() const;
    // P2P (anfitrión): cuántos servidores de señalización responden y cuántos hay.
    int signalingReady() const;
    int signalingTotal() const;
    // Mientras se conecta (P2P: buscando la sesión, conectando con el anfitrión).
    QString progressText() const { return m_progress; }

    QList<Peer> peers() const;
    QList<Peer> waitingPeers() const { return m_waiting.values(); }
    int myId() const { return m_myId; }
    Peer peer(int id) const;
    // Modo de este invitado: "live" (en tiempo real) o "confirm" (sugerencias).
    QString mode() const { return m_mode; }
    bool isLive() const { return m_mode == QLatin1String("live"); }
    bool autoAccept() const { return m_autoAccept; }
    const QHash<int, RemoteCursor>& cursors() const { return m_cursors; }

    // Si una herramienta está a mitad de una operación y sobre qué capa (para no pisarla);
    // 'editing' puede ser nulo.
    void setBusyCheck(std::function<bool(const Layer** editing)> check) { m_busyCheck = std::move(check); }
    // Mientras se aplica lo que llega de otros (el historial no debe cambiar de capa ni de
    // herramienta por ello).
    bool applyingRemote() const { return m_applying; }

    void setCursor(QPointF imagePos, bool inside, const QString& tool);
    void rename(const QString& name);
    // Anfitrión: cambia la configuración de la sesión en marcha.
    void updateSettings(const Settings& settings);

    // Invitado con confirmaciones.
    bool hasDraft() const { return m_hasDraft; }
    bool proposalPending() const { return m_pendingPid != 0; }
    bool propose();
    void discardDraft();

    // Anfitrión.
    QList<Proposal> proposals() const { return m_proposals; }
    void acceptProposal(int from);
    void rejectProposal(int from);
    void acceptAll();
    // Vista previa de cómo quedaría la imagen con una sugerencia (recortada a lo que cambia).
    QImage preview(const Proposal& proposal, QSize maxSize, QRect* bounds = nullptr) const;
    void admit(int id, bool ok);
    void kick(int id);

    // Sale de la sesión (el anfitrión la termina para todos).
    void leave();

    static QString reasonText(const QString& reason);

signals:
    void stateChanged();
    // Invitado: el documento de la sesión, listo para abrirlo en una pestaña (lo adopta quien lo
    // recibe).
    void documentReady(Document* doc);
    void peersChanged();
    void cursorsChanged();
    void proposalsChanged();
    void draftChanged();
    void joinRequested(const Collab::Peer& peer);
    void notice(const QString& text);
    void ended(const QString& reason);

private:
    struct CanonLayer {
        QString id;
        LayerProps props;
        QImage image;
    };
    struct Canon {
        QSize size;
        double dpi = 96;
        QString name;
        QList<CanonLayer> layers;
        int indexOf(const QString& id) const;
        QStringList ids() const;
    };
    struct Incoming {
        enum Kind { Commit, Apply, Accepted } kind = Commit;
        QJsonArray ops;
        QString label;
        int from = 0;
        QString name;
        int pid = 0;
    };

    explicit Session(Role role, const Settings& settings, QObject* parent);

    void setState(State s);
    void finish(const QString& reason);
    void send(const QJsonObject& message);

    // Anfitrión.
    void onHostMessage(const QJsonObject& m);
    void onPeerJoined(const Peer& peer);
    // Invitado.
    void attachLink(Link* link);
    void onGuestText(const QByteArray& text);
    void onWelcome(const QJsonObject& m);

    void attachDocument(Document* doc);
    void onLocalStep();
    void updateDraft();

    // Copia canónica.
    QString idFor(const LayerPtr& layer);
    void adoptWorking();
    QJsonArray diff();
    QJsonObject snapshot(const Canon& canon) const;
    static bool readSnapshot(const QJsonObject& doc, Canon& out);
    QString pngFor(const QImage& image) const;

    // Lo que llega.
    void enqueue(const Incoming& item);
    void processQueue();
    bool blocked(const QJsonArray& ops) const;
    // 'canonOnly': solo la copia canónica (el eco de un cambio propio cuando ya se enviaron otros
    // más nuevos: el documento ya está más adelante).
    QJsonArray apply(const QJsonArray& ops, const QString& historyText, bool canonOnly = false);
    // Aplica una operación a la copia canónica; devuelve la operación saneada (vacía si no vale).
    static QJsonObject applyToCanon(Canon& canon, const QJsonObject& op);
    void applyToWork(const Canon& before, const Canon& after, const QJsonObject& op, DocumentState& work,
                     QHash<int, QRect>& changed, bool& structural);
    void broadcastCommit(const QJsonArray& ops, int from, const QString& name, const QString& label, int pid);

    Role m_role;
    State m_state = State::Connecting;
    Settings m_settings;
    QPointer<Document> m_doc;
    Server* m_server = nullptr;
    QPointer<Link> m_socket;
    P2PGuest* m_p2p = nullptr;
    Mode m_connection = Mode::Lan;
    QString m_code;
    QString m_progress;
    QString m_address;
    quint16 m_port = 0;
    QString m_password;
    QString m_sessionName;

    int m_myId = 0;
    QMap<int, Peer> m_peers;
    QMap<int, Peer> m_waiting;
    QHash<int, RemoteCursor> m_cursors;
    QString m_mode = QStringLiteral("confirm");
    bool m_autoAccept = false;

    Canon m_canon;
    QHash<QString, LayerPtr> m_layerById;
    QHash<const Layer*, QString> m_idOf;
    QString m_idPrefix;
    int m_idCounter = 0;
    int m_rev = 0;
    int m_lastIndex = 0;
    mutable QHash<qint64, QString> m_pngCache;

    QList<Incoming> m_queue;
    QTimer m_queueTimer;
    bool m_applying = false;
    bool m_processing = false;
    std::function<bool(const Layer**)> m_busyCheck;

    // Invitado con confirmaciones.
    bool m_hasDraft = false;
    int m_pendingPid = 0;
    // Primer paso del historial que es del borrador actual.
    int m_draftStart = 0;
    // Cambios propios enviados (en tiempo real) cuyo eco aún no ha llegado.
    int m_inFlight = 0;
    int m_nextPid = 0;
    QTimer m_draftTimer;

    // Anfitrión.
    QList<Proposal> m_proposals;

    // Puntero propio (se envía como mucho ~30 veces por segundo).
    QJsonObject m_cursorOut;
    bool m_cursorDirty = false;
    QTimer m_cursorTimer;
    QTimer m_cursorExpiry;
};

} // namespace Collab
