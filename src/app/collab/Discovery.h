#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

class QUdpSocket;

namespace Collab {

// Una sesión anunciada en la red local.
struct FoundSession {
    QString address;
    quint16 port = 0;
    QString name;
    QString host;
    QString auth;
    QString app;
    bool web = false;
    int peers = 0;
    qint64 seen = 0;
};

// Escucha los anuncios de las sesiones de la red local (los de Kaint y los del servidor de Kaint
// para la web).
class Discovery : public QObject {
    Q_OBJECT
public:
    explicit Discovery(QObject* parent = nullptr);
    bool start();
    QList<FoundSession> sessions() const { return m_sessions; }

signals:
    void changed();

private:
    void read();
    void expire();

    QUdpSocket* m_socket;
    QTimer m_expire;
    QList<FoundSession> m_sessions;
};

} // namespace Collab
