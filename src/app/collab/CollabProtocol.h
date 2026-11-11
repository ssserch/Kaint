#pragma once

#include "core/BlendMode.h"

#include <QBitArray>
#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QRect>
#include <QString>
#include <QStringList>

// Protocolo de colaboración entre Kaint y Kaint para la web (docs/collaboration.md).
namespace Collab {

constexpr int kVersion = 1;
constexpr quint16 kDefaultPort = 47913;
constexpr quint16 kBeaconPort = 47914;

// Quién puede entrar a una sesión.
enum class Access { Free = 0, Approve = 1, Password = 2, Ids = 3 };
QString accessName(Access a);
Access accessFromName(const QString& name);

// Cómo se conectan: P2P por Internet (WebRTC, con un código de sesión; la predeterminada) o en la
// red local (el anfitrión abre un servidor y los demás entran por su dirección).
enum class Mode { P2P = 0, Lan = 1 };
QString modeName(Mode m);
Mode modeFromName(const QString& name);

// Servidores de señalización (rastreadores de WebTorrent: solo presentan a los equipos; la imagen
// va directa de uno a otro) y de STUN/TURN predeterminados.
QStringList defaultSignalServers();
QStringList defaultIceServers();
// Código de una sesión P2P: "ABCD-EFGH-JKLM-NPQR" (lo que se comparte para entrar).
QString newSessionCode();
// Sin guiones ni espacios, en mayúsculas; vacío si no tiene la forma de un código.
QString normalizedSessionCode(const QString& code);

// Configuración > Colaboración (se guarda aparte, en el grupo "collab").
struct Settings {
    QString name;               // nombre en pantalla (vacío: el del usuario del sistema)
    QColor color;               // color propio (no válido: el que dé el anfitrión)
    QString sessionName;        // vacío: «Sesión de <nombre>»
    int port = kDefaultPort;
    Access access = Access::Approve;
    QString password;
    // Equipos autorizados (modo Ids): «CÓDIGO nombre opcional» por línea.
    QStringList allowedIds;
    // Invitados con la misma aplicación: en tiempo real (si no, con confirmaciones).
    bool realtime = true;
    // Los cambios que sugieren los invitados se aplican sin preguntar.
    bool autoAccept = false;
    // Se anuncia en la red local para que la encuentren al buscar.
    bool announce = true;
    // Los navegadores de la red pueden abrir Kaint para la web desde esta sesión.
    bool serveWeb = true;
    bool showCursors = true;
    // Configuración > Colaboración: la vista compacta (todo en un formulario) en vez de la simplificada.
    bool compactView = false;
    // P2P o red local (lo que usan «Hospedar» y «Unirse» sin más).
    Mode mode = Mode::P2P;
    QStringList signalServers = defaultSignalServers();
    // "stun:servidor:puerto" o "turn:usuario:contraseña@servidor:puerto".
    QStringList iceServers = defaultIceServers();

    static Settings load();
    void save() const;
    QString displayName() const;
    QString effectiveSessionName() const;
};

// ID de este equipo: un código que se le da al anfitrión para entrar en las sesiones con equipos
// autorizados. No viaja por la red: solo su huella (deviceTag) y la prueba de que se tiene.
QString deviceCode();
QString regenerateDeviceCode();
// "ABCD-EFGH-JKLM" → "ABCDEFGHJKLM".
QString normalizedCode(const QString& code);
QString deviceTag(const QString& code);

QByteArray randomNonce();
// HMAC-SHA256 del nonce con la clave (contraseña o código), en base64.
QString proof(const QString& key, const QByteArray& nonce);

QString blendName(BlendMode mode);
BlendMode blendFromName(const QString& name);

// Imagen ↔ PNG en base64 (alfa directo, el formato de las capas).
QString encodePng(const QImage& image);
QImage decodePng(const QString& base64);

// Máscara de bits de una región (1 = píxel cambiado), fila a fila, el bit menos significativo
// primero.
QString encodeMask(const QBitArray& bits);
QBitArray decodeMask(const QString& base64, int count);

// Colores de los participantes.
QColor peerColor(int index);

// Nombres de las herramientas (los de los iconos, iguales en la web).
QString toolName(int toolId);

} // namespace Collab
