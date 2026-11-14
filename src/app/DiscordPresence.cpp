#include "app/DiscordPresence.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLocalSocket>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
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
    // Los ID de Discord son números (snowflakes).
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
    // Para las pruebas (un Discord falso, sin tocar el de verdad): solo este.
    const QString forced = qEnvironmentVariable("KAINT_DISCORD_IPC");
    if (!forced.isEmpty())
        return {forced};
#ifdef Q_OS_WIN
    // En Windows son tuberías con nombre (\\.\pipe\discord-ipc-N): QLocalSocket solo necesita
    // el nombre.
    QStringList pipes;
    for (int i = 0; i < 10; ++i)
        pipes << QStringLiteral("discord-ipc-%1").arg(i);
    return pipes;
#endif
    // Igual que la biblioteca oficial: XDG_RUNTIME_DIR (o TMPDIR, /tmp) y las carpetas de las
    // versiones Flatpak y Snap de Discord.
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

QString DiscordPresence::joinSecret(const QString& code)
{
    return code.isEmpty() ? QString() : QStringLiteral("kaint-p2p:") + code;
}

QString DiscordPresence::codeFromSecret(const QString& secret)
{
    const QLatin1String prefix("kaint-p2p:");
    if (!secret.startsWith(prefix))
        return {};
    // Solo letras, números y guiones: lo demás no es un código de sesión.
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9-]{4,40}$"));
    const QString code = secret.mid(prefix.size()).trimmed();
    return valid.match(code).hasMatch() ? code : QString();
}

bool DiscordPresence::registerLaunchCommand(const QString& clientId, QString* error)
{
    if (!isValidClientId(clientId)) {
        if (error)
            *error = tr("El ID de la aplicación de Discord no es válido.");
        return false;
    }
    const QString id = clientId.trimmed();
#if defined(Q_OS_WIN)
    // Como Discord_Register: HKCU\Software\Classes\discord-<ID> abre este Kaint.
    const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    QSettings reg(QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes\\discord-%1").arg(id), QSettings::NativeFormat);
    reg.setValue(QStringLiteral("Default"), QStringLiteral("URL:Run game %1 protocol").arg(id));
    reg.setValue(QStringLiteral("URL Protocol"), QString());
    reg.setValue(QStringLiteral("DefaultIcon/Default"), exe);
    reg.setValue(QStringLiteral("shell/open/command/Default"), QStringLiteral("\"%1\"").arg(exe));
    reg.sync();
    if (reg.status() != QSettings::NoError) {
        if (error)
            *error = tr("No se pudo escribir en el registro de Windows.");
        return false;
    }
    return true;
#elif defined(Q_OS_MACOS)
    // ~/Library/Application Support/discord/games/<ID>.json con la orden que abre Kaint.
    const QString dir = QDir::homePath() + QStringLiteral("/Library/Application Support/discord/games");
    QDir().mkpath(dir);
    QSaveFile f(dir + QStringLiteral("/%1.json").arg(id));
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(QJsonObject{{QStringLiteral("command"), QCoreApplication::applicationFilePath()}}).toJson());
    return f.commit();
#else
    // Un .desktop oculto para el protocolo discord-<ID> (con la AppImage, la AppImage; con Flatpak,
    // «flatpak run»).
    QString exec;
    if (!qEnvironmentVariable("FLATPAK_ID").isEmpty())
        exec = QStringLiteral("flatpak run %1").arg(qEnvironmentVariable("FLATPAK_ID"));
    else if (!qEnvironmentVariable("APPIMAGE").isEmpty())
        exec = QStringLiteral("\"%1\"").arg(qEnvironmentVariable("APPIMAGE"));
    else
        exec = QStringLiteral("\"%1\"").arg(QCoreApplication::applicationFilePath());
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    if (dir.isEmpty() || !QDir().mkpath(dir)) {
        if (error)
            *error = tr("No se encontró la carpeta de aplicaciones del usuario.");
        return false;
    }
    const QString name = QStringLiteral("discord-%1.desktop").arg(id);
    QSaveFile f(dir + QLatin1Char('/') + name);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    f.write(QStringLiteral("[Desktop Entry]\nName=Kaint\nExec=%1\nType=Application\nNoDisplay=true\n"
                           "Categories=Discord;Graphics;\nMimeType=x-scheme-handler/discord-%2;\n")
                .arg(exec, id)
                .toUtf8());
    if (!f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    QProcess::startDetached(QStringLiteral("xdg-mime"),
                            {QStringLiteral("default"), name, QStringLiteral("x-scheme-handler/discord-%1").arg(id)});
    return true;
#endif
}

QJsonObject DiscordPresence::buildActivity(const PresenceInfo& info, const PresenceOptions& options, qint64 startSeconds,
                                           bool minimal)
{
    QJsonObject activity;
    QString details;
    QStringList state;
    if (!info.hasDocument) {
        details = QObject::tr("Sin imágenes abiertas");
    } else if (minimal) {
        details = options.showFile && !info.fileName.isEmpty() ? QObject::tr("Editando «%1»").arg(info.fileName)
                                                               : QObject::tr("Editando una imagen");
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
    // El modo colaborativo: la sesión arriba, la imagen debajo y, en el grupo de Discord («2 de 8»),
    // cuántas personas hay. Con una sesión P2P, el secreto para unirse (el código: Discord solo se lo
    // da a quien invitas o aceptas; después Kaint pide la contraseña o espera al anfitrión).
    const bool collab = info.collab && options.showCollab;
    const bool party = collab && !info.collabWaiting && info.collabPeople > 0 && !minimal;
    if (collab) {
        const QString name = options.showCollabName ? info.collabName.trimmed() : QString();
        QString line;
        if (info.collabWaiting)
            line = QObject::tr("Esperando para entrar a una sesión colaborativa");
        else if (info.collabHost)
            line = name.isEmpty() ? QObject::tr("Hospedando una sesión colaborativa") : QObject::tr("Hospedando «%1»").arg(name);
        else
            line = name.isEmpty() ? QObject::tr("En una sesión colaborativa") : QObject::tr("En la sesión «%1»").arg(name);
        QStringList collabState;
        if (info.hasDocument)
            collabState << details;
        if (!party && info.collabPeople > 0)
            collabState << QObject::tr("%n personas en la sesión", nullptr, info.collabPeople);
        if (!info.collabWaiting)
            collabState << (info.collabP2P ? QObject::tr("por P2P") : QObject::tr("en la red local"));
        details = line;
        state = collabState;
    }
    activity.insert(QStringLiteral("details"), limited(details));
    if (!state.isEmpty())
        activity.insert(QStringLiteral("state"), limited(state.join(QStringLiteral(" · "))));
    if (options.showElapsed)
        activity.insert(QStringLiteral("timestamps"), QJsonObject{{QStringLiteral("start"), startSeconds}});
    // La herramienta va en el texto de la imagen grande: el Discord de Windows rechaza un
    // "small_text" sin imagen pequeña (y la actividad se quedaba en la primera, sin imágenes).
    QString largeText = QStringLiteral("Kaint %1").arg(QStringLiteral(KAINT_VERSION));
    if (!minimal && options.showTool && info.hasDocument && !info.tool.isEmpty())
        largeText += QStringLiteral(" · ") + info.tool;
    activity.insert(QStringLiteral("assets"), QJsonObject{{QStringLiteral("large_image"), largeImageKey()},
                                                          {QStringLiteral("large_text"), limited(largeText)}});
    if (party) {
        // El ID del grupo no revela el código: es un resumen (el mismo para todos los de la sesión).
        const QByteArray seed = (info.collabCode.isEmpty() ? info.collabName : info.collabCode).toUtf8();
        const QString id = QStringLiteral("kaint-")
                           + QString::fromLatin1(QCryptographicHash::hash("kaint-party:" + seed, QCryptographicHash::Sha256)
                                                     .toHex()
                                                     .left(24));
        const int people = info.collabPeople;
        activity.insert(QStringLiteral("party"),
                        QJsonObject{{QStringLiteral("id"), id},
                                    {QStringLiteral("size"), QJsonArray{people, std::max(std::max(options.partyMax, 2), people)}}});
        if (options.invites && info.collabP2P && !info.collabCode.isEmpty())
            activity.insert(QStringLiteral("secrets"), QJsonObject{{QStringLiteral("join"), joinSecret(info.collabCode)}});
    }
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
    case Status::Connected:
        if (!m_activityError.isEmpty())
            return tr("Conectado, pero Discord rechazó la actividad completa (se muestra una más sencilla): %1")
                .arg(m_activityError);
        return tr("Conectado: Discord muestra tu actividad en Kaint.");
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
        // Las tuberías de Windows no son archivos: se intenta conectar (falla al momento si no hay).
#ifndef Q_OS_WIN
        if (!QFileInfo::exists(path))
            continue;
#endif
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
    m_minimal = false;
    m_activityError.clear();
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
    case Frame: {
        const QString evt = msg.value(QStringLiteral("evt")).toString();
        const QString cmd = msg.value(QStringLiteral("cmd")).toString();
        if (evt == QLatin1String("READY")) {
            m_ready = true;
            setStatus(Status::Connected);
            m_dirty = true;
            subscribeInvites();
            scheduleSend();
        } else if (evt == QLatin1String("ACTIVITY_JOIN")) {
            // Se aceptó una invitación (o nos aceptaron): Discord da el secreto para unirse.
            const QString code = codeFromSecret(msg.value(QStringLiteral("data")).toObject().value(QStringLiteral("secret")).toString());
            if (m_options.invites && !code.isEmpty())
                emit joinInvite(code);
        } else if (evt == QLatin1String("ACTIVITY_JOIN_REQUEST")) {
            // Alguien pulsó «Pedir unirse» en el perfil.
            const QJsonObject user = msg.value(QStringLiteral("data")).toObject().value(QStringLiteral("user")).toObject();
            const QString userId = user.value(QStringLiteral("id")).toString();
            if (!isValidClientId(userId))
                break;
            if (!m_options.invites || m_options.joinRequests == PresenceOptions::JoinRequests::Decline) {
                replyJoinRequest(userId, false);
            } else if (m_options.joinRequests == PresenceOptions::JoinRequests::Accept) {
                replyJoinRequest(userId, true);
            } else {
                QString name = user.value(QStringLiteral("global_name")).toString().trimmed();
                if (name.isEmpty())
                    name = user.value(QStringLiteral("username")).toString().trimmed();
                const QString tag = user.value(QStringLiteral("discriminator")).toString();
                if (!tag.isEmpty() && tag != QLatin1String("0"))
                    name += QLatin1Char('#') + tag;
                emit joinRequest(userId, name.left(64));
            }
        } else if (evt == QLatin1String("ERROR")) {
            const QString message = msg.value(QStringLiteral("data")).toObject().value(QStringLiteral("message")).toString();
            if (m_ready && cmd != QLatin1String("SET_ACTIVITY")) {
                // Las invitaciones (SUBSCRIBE, responder a una solicitud): no cortan la conexión.
                qWarning("Discord: %s: %s", qPrintable(cmd), qPrintable(message));
            } else if (m_ready && cmd == QLatin1String("SET_ACTIVITY")) {
                // Se rechazó la actividad (no la conexión): Discord seguiría mostrando la anterior
                // ("Sin imágenes abiertas"). Se vuelve a enviar una más sencilla.
                if (!m_minimal) {
                    m_minimal = true;
                    m_activityError = message;
                    m_dirty = true;
                    scheduleSend();
                    emit statusChanged();
                }
            } else {
                setStatus(Status::Error, message);
            }
        }
        break;
    }
    case Close:
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

void DiscordPresence::sendCommand(const QString& cmd, const QJsonObject& args, const QString& evt)
{
    if (!m_socket || !m_ready)
        return;
    QJsonObject payload{{QStringLiteral("cmd"), cmd},
                        {QStringLiteral("args"), args},
                        {QStringLiteral("nonce"), QStringLiteral("kaint-%1").arg(++m_nonce)}};
    if (!evt.isEmpty())
        payload.insert(QStringLiteral("evt"), evt);
    m_socket->write(frame(Frame, payload));
    m_socket->flush();
}

void DiscordPresence::subscribeInvites()
{
    // Siempre se escucha (si las invitaciones están apagadas, se ignoran o se rechazan).
    sendCommand(QStringLiteral("SUBSCRIBE"), {}, QStringLiteral("ACTIVITY_JOIN"));
    sendCommand(QStringLiteral("SUBSCRIBE"), {}, QStringLiteral("ACTIVITY_JOIN_REQUEST"));
    const QString id = effectiveClientId();
    if (m_options.invites && m_options.registerLaunch && m_registered != id) {
        QString error;
        if (registerLaunchCommand(id, &error))
            m_registered = id;
        else
            qWarning("Discord: %s", qPrintable(error));
    }
}

void DiscordPresence::replyJoinRequest(const QString& userId, bool accept)
{
    if (!isValidClientId(userId))
        return;
    sendCommand(accept ? QStringLiteral("SEND_ACTIVITY_JOIN_INVITE") : QStringLiteral("CLOSE_ACTIVITY_REQUEST"),
                QJsonObject{{QStringLiteral("user_id"), userId.trimmed()}});
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
                           {QStringLiteral("activity"), buildActivity(m_info, m_options, m_start, m_minimal)}};
    const QJsonObject payload{{QStringLiteral("cmd"), QStringLiteral("SET_ACTIVITY")},
                              {QStringLiteral("args"), args},
                              {QStringLiteral("nonce"), QStringLiteral("kaint-%1").arg(++m_nonce)}};
    m_socket->write(frame(Frame, payload));
    m_socket->flush();
    m_lastSend.start();
    m_dirty = false;
}
