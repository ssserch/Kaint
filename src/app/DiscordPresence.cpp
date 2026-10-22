#include "app/DiscordPresence.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QtEndian>

namespace {

// Códigos de operación del IPC de Discord.
enum Opcode { Handshake = 0, Frame = 1, Close = 2, Ping = 3, Pong = 4 };

// Discord corta los textos de más de 128 caracteres.
QString limited(const QString& text)
{
    return text.size() <= 128 ? text : text.left(127) + QChar(0x2026);
}

QJsonObject userConfig()
{
    QFile f(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/Kaint/discord.json"));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

} // namespace

DiscordPresence::DiscordPresence(QObject* parent)
    : QObject(parent)
    , m_start(QDateTime::currentSecsSinceEpoch())
{
    // El primer setOptions() decide si se conecta.
    m_options.enabled = false;
    m_retry.setSingleShot(true);
    m_retry.setInterval(15000);
    connect(&m_retry, &QTimer::timeout, this, &DiscordPresence::connectToDiscord);
    m_send.setSingleShot(true);
    connect(&m_send, &QTimer::timeout, this, &DiscordPresence::sendActivity);
}

DiscordPresence::~DiscordPresence()
{
    disconnectFromDiscord();
}

void DiscordPresence::setIntervals(int minSendMs, int retryMs)
{
    m_minSendMs = minSendMs;
    m_retry.setInterval(retryMs);
}

bool DiscordPresence::isValidClientId(const QString& id)
{
    // Los ID de Discord son números (snowflakes); "PON___AQUI" no vale.
    static const QRegularExpression digits(QStringLiteral("^[0-9]{5,25}$"));
    return digits.match(id.trimmed()).hasMatch();
}

QString DiscordPresence::configuredClientId()
{
    const QString user = userConfig().value(QStringLiteral("client_id")).toString();
    if (isValidClientId(user))
        return user.trimmed();
    const QString built = QStringLiteral(KAINT_DISCORD_CLIENT_ID);
    return isValidClientId(built) ? built : QString();
}

QString DiscordPresence::largeImageKey()
{
    const QString user = userConfig().value(QStringLiteral("large_image")).toString();
    return user.isEmpty() ? QStringLiteral(KAINT_DISCORD_LARGE_IMAGE) : user;
}

QString DiscordPresence::effectiveClientId() const
{
    return isValidClientId(m_options.clientId) ? m_options.clientId.trimmed() : configuredClientId();
}

QStringList DiscordPresence::socketPaths()
{
    // Igual que la biblioteca oficial: XDG_RUNTIME_DIR (o TMPDIR, /tmp) y las
    // carpetas de las versiones Flatpak y Snap de Discord.
    QStringList bases;
    for (const char* var : {"XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"}) {
        const QString v = qEnvironmentVariable(var);
        if (!v.isEmpty() && !bases.contains(v))
            bases << v;
    }
    if (!bases.contains(QStringLiteral("/tmp")))
        bases << QStringLiteral("/tmp");
    const QStringList subdirs{QString(), QStringLiteral("/app/com.discordapp.Discord"),
                              QStringLiteral("/app/com.discordapp.DiscordCanary"), QStringLiteral("/snap.discord"),
                              QStringLiteral("/.flatpak/dev.vencord.Vesktop/xdg-run")};
    QStringList paths;
    for (const QString& base : std::as_const(bases))
        for (const QString& sub : subdirs)
            for (int i = 0; i < 10; ++i)
                paths << QStringLiteral("%1%2/discord-ipc-%3").arg(base, sub).arg(i);
    return paths;
}

QByteArray DiscordPresence::frame(int opcode, const QJsonObject& payload)
{
    const QByteArray json = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QByteArray out(8, Qt::Uninitialized);
    qToLittleEndian<quint32>(quint32(opcode), out.data());
    qToLittleEndian<quint32>(quint32(json.size()), out.data() + 4);
    return out + json;
}

QJsonObject DiscordPresence::buildActivity(const PresenceInfo& info, const PresenceOptions& options, qint64 startSeconds)
{
    QJsonObject activity;
    QString details;
    QStringList state;
    if (!info.hasDocument) {
        details = QObject::tr("Sin imágenes abiertas");
    } else {
        if (options.showFile)
            details = info.fileName.isEmpty() ? QObject::tr("Editando una imagen sin título")
                                              : QObject::tr("Editando «%1»").arg(info.fileName);
        else
            details = QObject::tr("Editando una imagen");
        if (options.showLayer && info.layerCount > 0) {
            state << (info.layerName.isEmpty()
                          ? QObject::tr("Capa %1 de %2").arg(info.layerIndex).arg(info.layerCount)
                          : QObject::tr("Capa «%1» (%2 de %3)").arg(info.layerName).arg(info.layerIndex).arg(info.layerCount));
        }
        if (options.showFileInfo) {
            if (!info.fileType.isEmpty())
                state << info.fileType;
            if (info.size.isValid())
                state << QStringLiteral("%1 × %2").arg(info.size.width()).arg(info.size.height());
        }
        if (info.documents > 1)
            state << QObject::tr("%n imágenes abiertas", nullptr, info.documents);
    }
    activity.insert(QStringLiteral("details"), limited(details));
    if (!state.isEmpty())
        activity.insert(QStringLiteral("state"), limited(state.join(QStringLiteral(" · "))));
    if (options.showElapsed)
        activity.insert(QStringLiteral("timestamps"), QJsonObject{{QStringLiteral("start"), startSeconds}});
    QJsonObject assets{{QStringLiteral("large_image"), largeImageKey()},
                       {QStringLiteral("large_text"), QStringLiteral("Kaint %1").arg(QStringLiteral(KAINT_VERSION))}};
    if (options.showTool && info.hasDocument && !info.tool.isEmpty())
        assets.insert(QStringLiteral("small_text"), limited(info.tool));
    activity.insert(QStringLiteral("assets"), assets);
    return activity;
}

QString DiscordPresence::statusText() const
{
    switch (m_status) {
    case Status::Disabled: return tr("Desactivado");
    case Status::NotConfigured:
        return tr("Sin configurar: falta el ID de la aplicación de Discord (discord.json o el campo de abajo).");
    case Status::NoDiscord: return tr("Discord no está abierto en este equipo; se volverá a intentar.");
    case Status::Connecting: return tr("Conectando con Discord…");
    case Status::Connected: return tr("Conectado: Discord muestra tu actividad en Kaint.");
    case Status::Error: return tr("Discord rechazó la conexión: %1").arg(m_error);
    }
    return {};
}

void DiscordPresence::setStatus(Status status, const QString& error)
{
    if (m_status == status && m_error == error)
        return;
    m_status = status;
    m_error = error;
    emit statusChanged();
}

void DiscordPresence::setOptions(const PresenceOptions& options)
{
    const bool reconnect = options.enabled != m_options.enabled || options.clientId != m_options.clientId;
    m_options = options;
    m_dirty = true;
    if (!reconnect) {
        scheduleSend();
        return;
    }
    disconnectFromDiscord();
    if (!m_options.enabled) {
        setStatus(Status::Disabled);
        return;
    }
    if (effectiveClientId().isEmpty()) {
        setStatus(Status::NotConfigured);
        return;
    }
    connectToDiscord();
}

void DiscordPresence::update(const PresenceInfo& info)
{
    if (info == m_info)
        return;
    m_info = info;
    m_dirty = true;
    scheduleSend();
}

void DiscordPresence::connectToDiscord()
{
    m_retry.stop();
    if (!m_options.enabled || effectiveClientId().isEmpty())
        return;
    disconnectFromDiscord();
    setStatus(Status::Connecting);
    for (const QString& path : socketPaths()) {
        if (!QFileInfo::exists(path))
            continue;
        auto* socket = new QLocalSocket(this);
        socket->connectToServer(path);
        if (!socket->waitForConnected(500)) {
            delete socket;
            continue;
        }
        m_socket = socket;
        break;
    }
    if (!m_socket) {
        setStatus(Status::NoDiscord);
        m_retry.start();
        return;
    }
    connect(m_socket, &QLocalSocket::readyRead, this, &DiscordPresence::onReadyRead);
    connect(m_socket, &QLocalSocket::disconnected, this, [this] {
        // Discord se cerró: se vuelve a intentar más tarde.
        if (m_socket) {
            m_socket->deleteLater();
            m_socket = nullptr;
        }
        m_ready = false;
        m_dirty = true;
        if (m_status != Status::Error)
            setStatus(Status::NoDiscord);
        m_retry.start();
    });
    m_buffer.clear();
    m_socket->write(frame(Handshake, QJsonObject{{QStringLiteral("v"), 1}, {QStringLiteral("client_id"), effectiveClientId()}}));
    m_socket->flush();
}

void DiscordPresence::disconnectFromDiscord()
{
    m_send.stop();
    if (!m_socket)
        return;
    QLocalSocket* socket = m_socket;
    m_socket = nullptr;
    socket->disconnect(this);
    if (m_ready && socket->state() == QLocalSocket::ConnectedState) {
        // Se borra la actividad antes de cerrar.
        socket->write(frame(Close, QJsonObject{}));
        socket->flush();
        socket->waitForBytesWritten(200);
    }
    socket->disconnectFromServer();
    socket->deleteLater();
    m_ready = false;
    m_dirty = true;
}

void DiscordPresence::onReadyRead()
{
    if (!m_socket)
        return;
    m_buffer += m_socket->readAll();
    while (m_buffer.size() >= 8) {
        // data_ptr() en vez de constData(): evita un falso aviso de -Warray-bounds.
        const auto* head = reinterpret_cast<const uchar*>(m_buffer.data_ptr().data());
        auto le32 = [head](int at) {
            return quint32(head[at]) | quint32(head[at + 1]) << 8 | quint32(head[at + 2]) << 16 | quint32(head[at + 3]) << 24;
        };
        const int opcode = int(le32(0));
        const qsizetype length = qsizetype(le32(4));
        if (m_buffer.size() < 8 + length)
            return;
        const QByteArray data = m_buffer.mid(8, length);
        m_buffer.remove(0, 8 + length);
        handleFrame(opcode, data);
        if (!m_socket)
            return;
    }
}

void DiscordPresence::handleFrame(int opcode, const QByteArray& data)
{
    const QJsonObject msg = QJsonDocument::fromJson(data).object();
    switch (opcode) {
    case Frame:
        if (msg.value(QStringLiteral("evt")).toString() == QLatin1String("READY")) {
            m_ready = true;
            setStatus(Status::Connected);
            m_dirty = true;
            scheduleSend();
        } else if (msg.value(QStringLiteral("evt")).toString() == QLatin1String("ERROR")) {
            setStatus(Status::Error, msg.value(QStringLiteral("data")).toObject().value(QStringLiteral("message")).toString());
        }
        break;
    case Close:
        // P. ej. "Invalid Client ID": no tiene sentido reintentar enseguida.
        setStatus(Status::Error, msg.value(QStringLiteral("message")).toString());
        disconnectFromDiscord();
        m_retry.start(m_retry.interval() * 4);
        break;
    case Ping:
        m_socket->write(frame(Pong, msg));
        break;
    default:
        break;
    }
}

void DiscordPresence::scheduleSend()
{
    if (!m_ready || !m_dirty || m_send.isActive())
        return;
    const qint64 wait = m_lastSend.isValid() ? std::max<qint64>(0, m_minSendMs - m_lastSend.elapsed()) : 0;
    m_send.start(int(wait));
}

void DiscordPresence::sendActivity()
{
    if (!m_socket || !m_ready || !m_dirty)
        return;
    const QJsonObject args{{QStringLiteral("pid"), qint64(QCoreApplication::applicationPid())},
                           {QStringLiteral("activity"), buildActivity(m_info, m_options, m_start)}};
    const QJsonObject payload{{QStringLiteral("cmd"), QStringLiteral("SET_ACTIVITY")},
                              {QStringLiteral("args"), args},
                              {QStringLiteral("nonce"), QStringLiteral("kaint-%1").arg(++m_nonce)}};
    m_socket->write(frame(Frame, payload));
    m_socket->flush();
    m_lastSend.start();
    m_dirty = false;
}
