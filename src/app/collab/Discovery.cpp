#include "app/collab/Discovery.h"

#include "app/collab/CollabProtocol.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkDatagram>
#include <QUdpSocket>

namespace Collab {

Discovery::Discovery(QObject* parent)
    : QObject(parent)
    , m_socket(new QUdpSocket(this))
{
    connect(m_socket, &QUdpSocket::readyRead, this, &Discovery::read);
    m_expire.setInterval(1000);
    connect(&m_expire, &QTimer::timeout, this, &Discovery::expire);
}

bool Discovery::start()
{
    m_expire.start();
    return m_socket->bind(QHostAddress::AnyIPv4, kBeaconPort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
}

void Discovery::read()
{
    bool any = false;
    while (m_socket->hasPendingDatagrams()) {
        const QNetworkDatagram d = m_socket->receiveDatagram(8192);
        const QJsonObject o = QJsonDocument::fromJson(d.data()).object();
        if (o.value(QStringLiteral("kaint-collab")).toInt() != kVersion)
            continue;
        FoundSession s;
        QHostAddress sender = d.senderAddress();
        bool ok = false;
        const quint32 v4 = sender.toIPv4Address(&ok);
        s.address = ok ? QHostAddress(v4).toString() : sender.toString();
        s.port = quint16(o.value(QStringLiteral("port")).toInt());
        s.name = o.value(QStringLiteral("name")).toString().left(80);
        s.host = o.value(QStringLiteral("host")).toString().left(40);
        s.auth = o.value(QStringLiteral("auth")).toString();
        s.app = o.value(QStringLiteral("app")).toString().left(60);
        s.web = o.value(QStringLiteral("web")).toBool();
        s.peers = o.value(QStringLiteral("peers")).toInt();
        s.seen = QDateTime::currentMSecsSinceEpoch();
        if (s.port == 0)
            continue;
        // La misma sesión vista por varias interfaces (o por este mismo equipo): una vez.
        bool found = false;
        for (FoundSession& e : m_sessions) {
            if (e.port == s.port && (e.address == s.address || (e.name == s.name && e.host == s.host))) {
                const bool local = QHostAddress(e.address).isLoopback();
                if (local && !QHostAddress(s.address).isLoopback())
                    e = s;
                else
                    e.seen = s.seen, e.peers = s.peers, e.name = s.name, e.auth = s.auth;
                found = true;
                break;
            }
        }
        if (!found)
            m_sessions.append(s);
        any = true;
    }
    if (any)
        emit changed();
}

void Discovery::expire()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qsizetype before = m_sessions.size();
    m_sessions.erase(std::remove_if(m_sessions.begin(), m_sessions.end(), [now](const FoundSession& s) { return now - s.seen > 7000; }),
                     m_sessions.end());
    if (m_sessions.size() != before)
        emit changed();
}

} // namespace Collab
