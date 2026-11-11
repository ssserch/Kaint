#include "app/collab/CollabProtocol.h"

#include <QBuffer>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QSettings>

#include <algorithm>

namespace Collab {

namespace {

const QStringList kBlendNames{QStringLiteral("normal"), QStringLiteral("multiply"), QStringLiteral("additive"),
                              QStringLiteral("colorburn"), QStringLiteral("colordodge"), QStringLiteral("reflect"),
                              QStringLiteral("glow"), QStringLiteral("overlay"), QStringLiteral("difference"),
                              QStringLiteral("negation"), QStringLiteral("lighten"), QStringLiteral("darken"),
                              QStringLiteral("screen"), QStringLiteral("xor")};

// Sin letras que se confundan (I, O, 0, 1).
constexpr char kCodeAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

QString newCode()
{
    QString code;
    for (int i = 0; i < 12; ++i)
        code += QLatin1Char(kCodeAlphabet[QRandomGenerator::system()->bounded(int(sizeof kCodeAlphabet) - 1)]);
    return code.left(4) + QLatin1Char('-') + code.mid(4, 4) + QLatin1Char('-') + code.mid(8, 4);
}

} // namespace

QString accessName(Access a)
{
    switch (a) {
    case Access::Free: return QStringLiteral("free");
    case Access::Approve: return QStringLiteral("approve");
    case Access::Password: return QStringLiteral("password");
    case Access::Ids: return QStringLiteral("ids");
    }
    return QStringLiteral("approve");
}

Access accessFromName(const QString& name)
{
    if (name == QLatin1String("free"))
        return Access::Free;
    if (name == QLatin1String("password"))
        return Access::Password;
    if (name == QLatin1String("ids"))
        return Access::Ids;
    return Access::Approve;
}

Settings Settings::load()
{
    QSettings s;
    s.beginGroup(QStringLiteral("collab"));
    Settings c;
    c.name = s.value(QStringLiteral("name")).toString();
    c.color = s.value(QStringLiteral("color")).value<QColor>();
    c.sessionName = s.value(QStringLiteral("sessionName")).toString();
    c.port = std::clamp(s.value(QStringLiteral("port"), int(kDefaultPort)).toInt(), 1024, 65535);
    c.access = accessFromName(s.value(QStringLiteral("access"), accessName(Access::Approve)).toString());
    c.password = s.value(QStringLiteral("password")).toString();
    c.allowedIds = s.value(QStringLiteral("allowedIds")).toStringList();
    c.realtime = s.value(QStringLiteral("realtime"), true).toBool();
    c.autoAccept = s.value(QStringLiteral("autoAccept"), false).toBool();
    c.announce = s.value(QStringLiteral("announce"), true).toBool();
    c.serveWeb = s.value(QStringLiteral("serveWeb"), true).toBool();
    c.showCursors = s.value(QStringLiteral("showCursors"), true).toBool();
    return c;
}

void Settings::save() const
{
    QSettings s;
    s.beginGroup(QStringLiteral("collab"));
    s.setValue(QStringLiteral("name"), name);
    s.setValue(QStringLiteral("color"), color);
    s.setValue(QStringLiteral("sessionName"), sessionName);
    s.setValue(QStringLiteral("port"), port);
    s.setValue(QStringLiteral("access"), accessName(access));
    s.setValue(QStringLiteral("password"), password);
    s.setValue(QStringLiteral("allowedIds"), allowedIds);
    s.setValue(QStringLiteral("realtime"), realtime);
    s.setValue(QStringLiteral("autoAccept"), autoAccept);
    s.setValue(QStringLiteral("announce"), announce);
    s.setValue(QStringLiteral("serveWeb"), serveWeb);
    s.setValue(QStringLiteral("showCursors"), showCursors);
}

QString Settings::displayName() const
{
    if (!name.trimmed().isEmpty())
        return name.trimmed().left(40);
    QString user = qEnvironmentVariable("USER");
    if (user.isEmpty())
        user = qEnvironmentVariable("USERNAME");
    return user.isEmpty() ? QObject::tr("Invitado") : user.left(40);
}

QString Settings::effectiveSessionName() const
{
    return sessionName.trimmed().isEmpty() ? QObject::tr("Sesión de %1").arg(displayName()) : sessionName.trimmed().left(60);
}

QString deviceCode()
{
    QSettings s;
    QString code = s.value(QStringLiteral("collab/deviceCode")).toString();
    if (normalizedCode(code).size() != 12) {
        code = newCode();
        s.setValue(QStringLiteral("collab/deviceCode"), code);
    }
    return code;
}

QString regenerateDeviceCode()
{
    const QString code = newCode();
    QSettings().setValue(QStringLiteral("collab/deviceCode"), code);
    return code;
}

QString normalizedCode(const QString& code)
{
    QString out;
    for (const QChar c : code.toUpper())
        if (c.isLetterOrNumber())
            out += c;
    return out;
}

QString deviceTag(const QString& code)
{
    return QString::fromLatin1(QCryptographicHash::hash(normalizedCode(code).toUtf8(), QCryptographicHash::Sha256).toHex().left(16));
}

QByteArray randomNonce()
{
    QByteArray nonce(16, '\0');
    for (char& c : nonce)
        c = char(QRandomGenerator::system()->bounded(256));
    return nonce;
}

QString proof(const QString& key, const QByteArray& nonce)
{
    return QString::fromLatin1(QMessageAuthenticationCode::hash(nonce, key.toUtf8(), QCryptographicHash::Sha256).toBase64());
}

QString blendName(BlendMode mode)
{
    return kBlendNames.value(int(mode), QStringLiteral("normal"));
}

BlendMode blendFromName(const QString& name)
{
    const int i = int(kBlendNames.indexOf(name));
    return i < 0 ? BlendMode::Normal : BlendMode(i);
}

QString encodePng(const QImage& image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG", 1);
    return QString::fromLatin1(bytes.toBase64());
}

QImage decodePng(const QString& base64)
{
    QImage image;
    image.loadFromData(QByteArray::fromBase64(base64.toLatin1()), "PNG");
    if (image.isNull())
        return image;
    image = image.convertToFormat(QImage::Format_ARGB32);
    // Como las capas: sin perfil de color.
    image.setColorSpace(QColorSpace());
    return image;
}

QString encodeMask(const QBitArray& bits)
{
    QByteArray bytes((bits.size() + 7) / 8, '\0');
    for (qsizetype i = 0; i < bits.size(); ++i)
        if (bits.testBit(i))
            bytes[i / 8] = char(bytes[i / 8] | (1 << (i % 8)));
    return QString::fromLatin1(bytes.toBase64());
}

QBitArray decodeMask(const QString& base64, int count)
{
    const QByteArray bytes = QByteArray::fromBase64(base64.toLatin1());
    QBitArray bits(count);
    for (int i = 0; i < count && i / 8 < bytes.size(); ++i)
        if (bytes[i / 8] & (1 << (i % 8)))
            bits.setBit(i);
    return bits;
}

QColor peerColor(int index)
{
    static const QColor colors[] = {QColor(0xe5, 0x39, 0x35), QColor(0x1e, 0x88, 0xe5), QColor(0x43, 0xa0, 0x47),
                                    QColor(0x8e, 0x24, 0xaa), QColor(0xfb, 0x8c, 0x00), QColor(0x00, 0xac, 0xc1),
                                    QColor(0xd8, 0x1b, 0x60), QColor(0x6d, 0x4c, 0x41), QColor(0x3f, 0x51, 0xb5),
                                    QColor(0x7c, 0xb3, 0x42)};
    return colors[((index % 10) + 10) % 10];
}

QString toolName(int toolId)
{
    static const QStringList names{
        QStringLiteral("rectangleselect"), QStringLiteral("moveselectedpixels"), QStringLiteral("lassoselect"),
        QStringLiteral("moveselection"), QStringLiteral("ellipseselect"), QStringLiteral("zoom"), QStringLiteral("magicwand"),
        QStringLiteral("pan"), QStringLiteral("paintbucket"), QStringLiteral("gradient"), QStringLiteral("paintbrush"),
        QStringLiteral("eraser"), QStringLiteral("pencil"), QStringLiteral("colorpicker"), QStringLiteral("clonestamp"),
        QStringLiteral("recolor"), QStringLiteral("text"), QStringLiteral("linecurve"), QStringLiteral("shapes")};
    return names.value(toolId);
}

} // namespace Collab
