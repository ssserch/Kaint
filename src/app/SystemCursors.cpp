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
#include <cstring>
#include <iterator>

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

// Fotogramas de un archivo XCursor del tamaño nominal más cercano a 'size' (el mayor si empatan),
// escalados a ese tamaño: varios si el puntero está animado.
QList<Image> parse(const QString& file, int size)
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
    auto entry = [header](quint32 i) { return qint64(header) + qint64(i) * 12; };
    int bestSize = -1;
    for (quint32 i = 0; i < count; ++i) {
        if (u32(entry(i)) != kImage)
            continue;
        const int nominal = int(u32(entry(i) + 4));
        const int d = std::abs(nominal - size), bestD = std::abs(bestSize - size);
        if (bestSize < 0 || d < bestD || (d == bestD && nominal > bestSize))
            bestSize = nominal;
    }
    QList<Image> frames;
    for (quint32 i = 0; i < count && bestSize >= 0 && frames.size() < 256; ++i) {
        if (u32(entry(i)) != kImage || int(u32(entry(i) + 4)) != bestSize)
            continue;
        // Trozo de imagen: cabecera, tipo, tamaño nominal, versión, ancho, alto, punto activo
        // (x, y), retardo y los píxeles ARGB premultiplicados.
        const qint64 chunk = u32(entry(i) + 8);
        const quint32 w = u32(chunk + 16), h = u32(chunk + 20);
        const qint64 pixels = chunk + u32(chunk);
        if (w == 0 || h == 0 || w > 1024 || h > 1024 || pixels + qint64(w) * h * 4 > data.size())
            continue;
        QImage image(int(w), int(h), QImage::Format_ARGB32_Premultiplied);
        for (quint32 y = 0; y < h; ++y) {
            auto* line = reinterpret_cast<QRgb*>(image.scanLine(int(y)));
            for (quint32 x = 0; x < w; ++x)
                line[x] = u32(pixels + (qint64(y) * w + x) * 4);
        }
        Image frame{image, QPoint(int(u32(chunk + 24)), int(u32(chunk + 28))), int(u32(chunk + 32))};
        if (bestSize > 0 && bestSize != size) {
            const qreal k = qreal(size) / bestSize;
            frame.image = image.scaled(QSize(std::max(1, int(std::lround(w * k))), std::max(1, int(std::lround(h * k)))),
                                       Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            frame.hotSpot = QPoint(int(std::lround(frame.hotSpot.x() * k)), int(std::lround(frame.hotSpot.y() * k)));
        }
        frames.append(frame);
    }
    return frames;
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

int desktopSize()
{
    // Plasma en Wayland no siempre exporta XCURSOR_SIZE: el de KDE o el de GTK.
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    bool ok = false;
    const QSettings kde(config + QStringLiteral("/kcminputrc"), QSettings::IniFormat);
    if (const int size = kde.value(QStringLiteral("Mouse/cursorSize")).toInt(&ok); ok && size > 0)
        return size;
    for (const char* gtk : {"/gtk-4.0/settings.ini", "/gtk-3.0/settings.ini"}) {
        const QSettings s(config + QLatin1String(gtk), QSettings::IniFormat);
        if (const int size = s.value(QStringLiteral("Settings/gtk-cursor-theme-size")).toInt(&ok); ok && size > 0)
            return size;
    }
    return 0;
}

QList<Image> load(Shape shape, int pixelSize)
{
    static QHash<QString, QList<Image>> cache;
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
    const QList<Image> frames = file.isEmpty() ? QList<Image>() : parse(file, pixelSize);
    cache.insert(key, frames);
    return frames;
}

#elif defined(Q_OS_WIN)

namespace {

// Archivo del puntero en el esquema elegido (Configuración del mouse), con las variables de
// entorno puestas (%SystemRoot%...); vacío si no se cambió.
QString cursorFile(Shape shape)
{
    const QSettings reg(QStringLiteral("HKEY_CURRENT_USER\\Control Panel\\Cursors"), QSettings::NativeFormat);
    const QString value = reg.value(shape == Shape::IBeam ? QStringLiteral("IBeam") : QStringLiteral("Arrow")).toString();
    if (value.isEmpty())
        return {};
    wchar_t expanded[1024];
    const DWORD n = ExpandEnvironmentStringsW(reinterpret_cast<LPCWSTR>(value.utf16()), expanded, DWORD(std::size(expanded)));
    return n > 0 && n <= std::size(expanded) ? QString::fromWCharArray(expanded, int(n) - 1) : value;
}

// Milisegundos de cada paso de un .ani (el puntero animado de Windows): los de la lista "rate" o
// el de la cabecera "anih", en sesentavos de segundo; vacío si no es un .ani.
QList<int> aniDelays(const QString& file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray data = f.read(16 << 20);
    auto u32 = [&data](qint64 at) -> quint32 {
        return at >= 0 && at + 4 <= data.size() ? qFromLittleEndian<quint32>(data.constData() + at) : 0;
    };
    if (!data.startsWith("RIFF") || data.mid(8, 4) != "ACON")
        return {};
    // Cabecera "anih": tamaño, fotogramas, pasos, ancho, alto, bits, planos, retardo y atributos.
    int steps = 0, rate = 0;
    QList<int> rates;
    for (qint64 at = 12; at + 8 <= data.size();) {
        const QByteArray id = data.mid(at, 4);
        const qint64 size = u32(at + 4);
        if (id == "anih" && size >= 36) {
            const quint32 frames = u32(at + 12), count = u32(at + 16);
            steps = int(std::min<quint32>(count ? count : frames, 256));
            rate = int(u32(at + 36));
        } else if (id == "rate") {
            for (qint64 i = 0; i + 4 <= size && rates.size() < 256; i += 4)
                rates.append(int(u32(at + 8 + i)));
        }
        at += 8 + size + (size & 1);
    }
    QList<int> delays;
    for (int i = 0; i < steps; ++i)
        delays.append(std::max(1, i < rates.size() ? rates.at(i) : rate) * 1000 / 60);
    return delays;
}

// Tamaño de un puntero (el de su imagen, o la mitad de la máscara en los monocromos, que lleva
// las dos) y su punto activo.
QSize cursorSize(HCURSOR cursor, QPoint* hotSpot)
{
    ICONINFO info{};
    if (!GetIconInfo(cursor, &info))
        return {};
    BITMAP bm{};
    QSize size;
    if (info.hbmColor && GetObjectW(info.hbmColor, sizeof(bm), &bm))
        size = QSize(int(bm.bmWidth), int(bm.bmHeight));
    else if (info.hbmMask && GetObjectW(info.hbmMask, sizeof(bm), &bm))
        size = QSize(int(bm.bmWidth), int(bm.bmHeight) / 2);
    *hotSpot = QPoint(int(info.xHotspot), int(info.yHotspot));
    if (info.hbmColor)
        DeleteObject(info.hbmColor);
    if (info.hbmMask)
        DeleteObject(info.hbmMask);
    return size;
}

// Un paso de un puntero (el fotograma 'step' en los animados) a su tamaño, sobre transparente y,
// si no trae alfa (los monocromos), recortado con su máscara. QImage::fromHICON no sirve: supone
// que el punto activo está en el centro, como en los iconos, y saca la imagen del doble de ese
// punto (en los punteros, pequeña y deformada).
QImage cursorFrame(HCURSOR cursor, QSize size, UINT step)
{
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = size.width();
    bmi.bmiHeader.biHeight = -size.height(); // de arriba abajo
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    const HDC dc = CreateCompatibleDC(nullptr);
    void* bits = nullptr;
    const HBITMAP bitmap = dc ? CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
    QImage image;
    if (bitmap && bits) {
        const HGDIOBJ old = SelectObject(dc, bitmap);
        auto draw = [&](UINT flags) {
            std::memset(bits, 0, size_t(size.width()) * size_t(size.height()) * 4);
            const bool ok = DrawIconEx(dc, 0, 0, cursor, size.width(), size.height(), step, nullptr, flags);
            GdiFlush();
            return ok ? QImage(static_cast<const uchar*>(bits), size.width(), size.height(), QImage::Format_ARGB32_Premultiplied).copy()
                      : QImage();
        };
        image = draw(DI_NORMAL);
        bool alpha = false;
        for (int y = 0; y < image.height() && !alpha; ++y) {
            const auto* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
            for (int x = 0; x < image.width() && !alpha; ++x)
                alpha = qAlpha(line[x]) != 0;
        }
        if (!image.isNull() && !alpha) {
            const QImage mask = draw(DI_MASK);
            for (int y = 0; y < image.height(); ++y) {
                auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
                const auto* maskLine = mask.isNull() ? nullptr : reinterpret_cast<const QRgb*>(mask.constScanLine(y));
                for (int x = 0; x < image.width(); ++x)
                    line[x] = maskLine && qRed(maskLine[x]) ? 0 : line[x] | 0xff000000;
            }
        }
        SelectObject(dc, old);
    }
    if (bitmap)
        DeleteObject(bitmap);
    if (dc)
        DeleteDC(dc);
    return image;
}

} // namespace

QString themeName()
{
    // El esquema de punteros de Windows (Configuración del mouse), si se cambió.
    const QSettings reg(QStringLiteral("HKEY_CURRENT_USER\\Control Panel\\Cursors"), QSettings::NativeFormat);
    return reg.value(QStringLiteral("."), QStringLiteral("Windows")).toString();
}

int desktopSize()
{
    return 0;
}

QList<Image> load(Shape shape, int pixelSize)
{
    static QHash<QString, QList<Image>> cache;
    const QString file = cursorFile(shape);
    const QString key = QStringLiteral("%1/%2/%3/%4").arg(themeName(), file).arg(int(shape)).arg(pixelSize);
    if (const auto it = cache.constFind(key); it != cache.cend())
        return *it;
    // El archivo del puntero a ese tamaño (Windows elige el mejor que traiga y lo escala él, como
    // al ponerlo); si no se puede, el puntero actual del sistema, al suyo.
    const HCURSOR owned = file.isEmpty() ? nullptr
                                         : static_cast<HCURSOR>(LoadImageW(nullptr, reinterpret_cast<LPCWSTR>(file.utf16()),
                                                                           IMAGE_CURSOR, pixelSize, pixelSize, LR_LOADFROMFILE));
    const HCURSOR cursor = owned ? owned : LoadCursorW(nullptr, MAKEINTRESOURCEW(shape == Shape::IBeam ? 32513 : 32512)); // IDC_IBEAM, IDC_ARROW
    QList<Image> frames;
    QPoint hotSpot;
    if (const QSize size = cursor ? cursorSize(cursor, &hotSpot) : QSize(); !size.isEmpty()) {
        // Los .ani, paso a paso (DrawIconEx dibuja el que se le pida).
        const QList<int> delays = file.isEmpty() ? QList<int>() : aniDelays(file);
        for (int i = 0; i < std::max<int>(1, int(delays.size())); ++i) {
            const QImage image = cursorFrame(cursor, size, UINT(i));
            if (image.isNull())
                break;
            frames.append({image, hotSpot, delays.isEmpty() ? 0 : delays.at(i)});
        }
    }
    if (owned)
        DestroyCursor(owned);
    // Un puntero cargado a otro tamaño (el de siempre) se escala al pedido.
    for (Image& frame : frames) {
        if (frame.image.width() == pixelSize)
            continue;
        const qreal k = qreal(pixelSize) / frame.image.width();
        frame.image = frame.image.scaled(QSize(pixelSize, std::max(1, int(std::lround(frame.image.height() * k)))),
                                         Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        frame.hotSpot = QPoint(int(std::lround(frame.hotSpot.x() * k)), int(std::lround(frame.hotSpot.y() * k)));
    }
    cache.insert(key, frames);
    return frames;
}

#else

QString themeName()
{
    return {};
}

int desktopSize()
{
    return 0;
}

QList<Image> load(Shape, int)
{
    return {};
}

#endif

} // namespace SystemCursors
