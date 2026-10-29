#include "app/SystemCursors.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSettings>
#include <QStandardPaths>
#include <QtEndian>

#include <cmath>
#include <cstdlib>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace SystemCursors {

#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)

namespace {

// Carpetas de temas, las mismas que usa libXcursor: XCURSOR_PATH o las de iconos del usuario y
// del sistema.
QStringList searchPaths()
{
    QStringList paths;
    const QString env = qEnvironmentVariable("XCURSOR_PATH");
    if (!env.isEmpty()) {
        for (const QString& p : env.split(QLatin1Char(':'), Qt::SkipEmptyParts))
            paths << (p.startsWith(QLatin1Char('~')) ? QDir::homePath() + p.mid(1) : p);
        return paths;
    }
    paths << QDir::homePath() + QStringLiteral("/.local/share/icons") << QDir::homePath() + QStringLiteral("/.icons");
    for (const QString& dir : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation))
        paths << dir + QStringLiteral("/icons");
    paths << QStringLiteral("/usr/share/icons") << QStringLiteral("/usr/share/pixmaps");
    paths.removeDuplicates();
    return paths;
}

// Temas de los que hereda uno (Inherits= de su index.theme).
QStringList inherits(const QString& dir)
{
    QFile f(dir + QStringLiteral("/index.theme"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.startsWith(QLatin1String("Inherits"))) {
            QStringList list = line.section(QLatin1Char('='), 1).split(QLatin1Char(','), Qt::SkipEmptyParts);
            for (QString& s : list)
                s = s.trimmed();
            return list;
        }
    }
    return {};
}

QString findFile(const QString& theme, const QStringList& names, const QStringList& paths, int depth)
{
    if (theme.isEmpty() || depth > 8)
        return {};
    QStringList parents;
    for (const QString& base : paths) {
        const QString dir = base + QLatin1Char('/') + theme;
        for (const QString& name : names) {
            const QString file = dir + QStringLiteral("/cursors/") + name;
            if (QFileInfo(file).isFile())
                return file;
        }
        if (parents.isEmpty())
            parents = inherits(dir);
    }
    for (const QString& parent : std::as_const(parents))
        if (parent != theme)
            if (const QString file = findFile(parent, names, paths, depth + 1); !file.isEmpty())
                return file;
    return {};
}

// Imagen de un archivo XCursor del tamaño nominal más cercano a 'size' (el mayor si empatan).
Image parse(const QString& file, int size)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray data = f.read(32 << 20);
    auto u32 = [&data](qint64 at) -> quint32 {
        return at >= 0 && at + 4 <= data.size() ? qFromLittleEndian<quint32>(data.constData() + at) : 0;
    };
    if (!data.startsWith("Xcur"))
        return {};
    const quint32 header = u32(4), count = std::min<quint32>(u32(12), 4096);
    constexpr quint32 kImage = 0xfffd0002;
    qint64 best = -1;
    int bestSize = 0;
    for (quint32 i = 0; i < count; ++i) {
        const qint64 entry = qint64(header) + qint64(i) * 12;
        if (u32(entry) != kImage)
            continue;
        const int nominal = int(u32(entry + 4));
        const int d = std::abs(nominal - size), bestD = std::abs(bestSize - size);
        if (best < 0 || d < bestD || (d == bestD && nominal > bestSize)) {
            best = u32(entry + 8);
            bestSize = nominal;
        }
    }
    if (best < 0)
        return {};
    // Trozo de imagen: cabecera, tipo, tamaño nominal, versión, ancho, alto, punto activo (x, y),
    // retardo y los píxeles ARGB premultiplicados.
    const quint32 w = u32(best + 16), h = u32(best + 20);
    const qint64 pixels = best + u32(best);
    if (w == 0 || h == 0 || w > 1024 || h > 1024 || pixels + qint64(w) * h * 4 > data.size())
        return {};
    QImage image(int(w), int(h), QImage::Format_ARGB32_Premultiplied);
    for (quint32 y = 0; y < h; ++y) {
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(int(y)));
        for (quint32 x = 0; x < w; ++x)
            line[x] = u32(pixels + (qint64(y) * w + x) * 4);
    }
    Image result{image, QPoint(int(u32(best + 24)), int(u32(best + 28)))};
    if (bestSize > 0 && bestSize != size) {
        const qreal k = qreal(size) / bestSize;
        result.image = image.scaled(QSize(std::max(1, int(std::lround(w * k))), std::max(1, int(std::lround(h * k)))),
                                    Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        result.hotSpot = QPoint(int(std::lround(result.hotSpot.x() * k)), int(std::lround(result.hotSpot.y() * k)));
    }
    return result;
}

} // namespace

QString themeName()
{
    // El de la sesión (Plasma y otros escritorios lo exportan), el de KDE o el de GTK.
    const QString env = qEnvironmentVariable("XCURSOR_THEME");
    if (!env.isEmpty())
        return env;
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    const QSettings kde(config + QStringLiteral("/kcminputrc"), QSettings::IniFormat);
    const QString kdeTheme = kde.value(QStringLiteral("Mouse/cursorTheme")).toString();
    if (!kdeTheme.isEmpty())
        return kdeTheme;
    for (const char* gtk : {"/gtk-4.0/settings.ini", "/gtk-3.0/settings.ini"}) {
        const QSettings s(config + QLatin1String(gtk), QSettings::IniFormat);
        const QString theme = s.value(QStringLiteral("Settings/gtk-cursor-theme-name")).toString();
        if (!theme.isEmpty())
            return theme;
    }
    return QStringLiteral("default");
}

Image load(Shape shape, int pixelSize)
{
    static QHash<QString, Image> cache;
    const QString theme = themeName();
    const QString key = QStringLiteral("%1/%2/%3").arg(theme).arg(int(shape)).arg(pixelSize);
    if (const auto it = cache.constFind(key); it != cache.cend())
        return *it;
    const QStringList names = shape == Shape::IBeam ? QStringList{QStringLiteral("text"), QStringLiteral("xterm"), QStringLiteral("ibeam")}
                                                    : QStringList{QStringLiteral("default"), QStringLiteral("left_ptr"), QStringLiteral("arrow")};
    const QStringList paths = searchPaths();
    QString file;
    // Si el tema elegido no se encuentra, los habituales (como hace el propio sistema).
    for (const QString& t : {theme, QStringLiteral("default"), QStringLiteral("breeze_cursors"), QStringLiteral("Adwaita")})
        if (file.isEmpty())
            file = findFile(t, names, paths, 0);
    const Image image = file.isEmpty() ? Image() : parse(file, pixelSize);
    cache.insert(key, image);
    return image;
}

#elif defined(Q_OS_WIN)

QString themeName()
{
    // El esquema de punteros de Windows (Configuración del mouse), si se cambió.
    const QSettings reg(QStringLiteral("HKEY_CURRENT_USER\\Control Panel\\Cursors"), QSettings::NativeFormat);
    return reg.value(QStringLiteral("."), QStringLiteral("Windows")).toString();
}

Image load(Shape shape, int pixelSize)
{
    static QHash<QString, Image> cache;
    const QString key = QStringLiteral("%1/%2/%3").arg(themeName()).arg(int(shape)).arg(pixelSize);
    if (const auto it = cache.constFind(key); it != cache.cend())
        return *it;
    // El puntero actual del sistema (con el esquema, el tamaño y el color elegidos en Windows), a
    // ese tamaño; si no se puede cargar así, el de tamaño normal.
    const LPCWSTR id = MAKEINTRESOURCEW(shape == Shape::IBeam ? 32513 : 32512); // IDC_IBEAM, IDC_ARROW
    HCURSOR owned = static_cast<HCURSOR>(LoadImageW(nullptr, id, IMAGE_CURSOR, pixelSize, pixelSize, LR_DEFAULTCOLOR));
    const HCURSOR cursor = owned ? owned : LoadCursorW(nullptr, id);
    Image image;
    if (cursor) {
        image.image = QImage::fromHICON(static_cast<HICON>(cursor)).convertToFormat(QImage::Format_ARGB32_Premultiplied);
        ICONINFO info{};
        if (GetIconInfo(static_cast<HICON>(cursor), &info)) {
            image.hotSpot = QPoint(int(info.xHotspot), int(info.yHotspot));
            if (info.hbmColor)
                DeleteObject(info.hbmColor);
            if (info.hbmMask)
                DeleteObject(info.hbmMask);
        }
    }
    if (owned)
        DestroyCursor(owned);
    // Un puntero cargado a otro tamaño (el de siempre) se escala al pedido.
    if (!image.image.isNull() && image.image.width() != pixelSize && image.image.width() > 0) {
        const qreal k = qreal(pixelSize) / image.image.width();
        image.image = image.image.scaled(QSize(pixelSize, std::max(1, int(std::lround(image.image.height() * k)))),
                                         Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        image.hotSpot = QPoint(int(std::lround(image.hotSpot.x() * k)), int(std::lround(image.hotSpot.y() * k)));
    }
    cache.insert(key, image);
    return image;
}

#else

QString themeName()
{
    return {};
}

Image load(Shape, int)
{
    return {};
}

#endif

} // namespace SystemCursors
