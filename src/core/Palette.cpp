#include "core/Palette.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>
#include <QTextStream>

namespace Palette {

QList<QColor> defaultColors()
{
    static const uint32_t rgb[64] = {
        0x000000, 0x404040, 0xff0000, 0xff6a00, 0xffd800, 0xb6ff00, 0x4cff00, 0x00ff21, 0x00ff90, 0x00ffff, 0x0094ff,
        0x0026ff, 0x4800ff, 0xb200ff, 0xff00dc, 0xff006e, 0xffffff, 0x808080, 0x7f0000, 0x7f3300, 0x7f6a00, 0x5b7f00,
        0x267f00, 0x007f0e, 0x007f46, 0x007f7f, 0x004a7f, 0x00137f, 0x21007f, 0x57007f, 0x7f006e, 0x7f0037, 0xa0a0a0,
        0x303030, 0xff7f7f, 0xffb27f, 0xffe97f, 0xdaff7f, 0xa5ff7f, 0x7fff8e, 0x7fffc5, 0x7fffff, 0x7fc9ff, 0x7f92ff,
        0xa17fff, 0xd67fff, 0xff7fed, 0xff7fb6, 0xc0c0c0, 0x606060, 0x7f3f3f, 0x7f593f, 0x7f743f, 0x6d7f3f, 0x527f3f,
        0x3f7f47, 0x3f7f62, 0x3f7f7f, 0x3f647f, 0x3f497f, 0x503f7f, 0x6b3f7f, 0x7f3f76, 0x7f3f5b,
    };
    QList<QColor> colors;
    for (uint32_t c : rgb)
        colors.append(QColor::fromRgba(0xff000000u | c));
    for (int i = 0; i < 32; ++i)
        colors.append(QColor::fromRgba(0x80000000u | rgb[i]));
    return colors;
}

QList<QColor> rainbowColors()
{
    QList<QColor> colors;
    for (double lightness : {0.2, 0.32, 0.44, 0.56, 0.68, 0.8})
        for (int column = 0; column < 16; ++column)
            colors.append(QColor::fromHslF(float(column / 16.0), 1.0f, float(lightness)).toRgb());
    return colors;
}

QList<QColor> smallColors()
{
    QList<QColor> colors = defaultColors().mid(0, 32);
    while (colors.size() < kColorCount)
        colors.append(Qt::white);
    return colors;
}

QList<QColor> parse(const QString& text)
{
    QList<QColor> colors;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (QString line : lines) {
        const int comment = int(line.indexOf(QLatin1Char(';')));
        if (comment >= 0)
            line.truncate(comment);
        line = line.trimmed();
        if (line.isEmpty())
            continue;
        bool ok = false;
        const uint v = line.toUInt(&ok, 16);
        if (!ok)
            continue;
        colors.append(QColor::fromRgba(line.size() <= 6 ? (0xff000000u | v) : v));
        if (colors.size() == kColorCount)
            break;
    }
    while (colors.size() < kColorCount)
        colors.append(Qt::white);
    return colors;
}

QString serialize(const QList<QColor>& colors)
{
    QString out;
    QTextStream ts(&out);
    ts << "; Archivo de paleta de Kaint (compatible con Paint.NET)\n"
       << "; Las líneas que empiezan por punto y coma son comentarios\n"
       << "; Los colores se escriben como hexadecimal de 8 cifras: aarrggbb\n";
    for (int i = 0; i < kColorCount; ++i) {
        const QColor c = colors.value(i, Qt::white);
        ts << QStringLiteral("%1").arg(c.rgba(), 8, 16, QLatin1Char('0')).toUpper() << '\n';
    }
    return out;
}

QList<QColor> load(const QString& path, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = f.errorString();
        return {};
    }
    return parse(QString::fromUtf8(f.readAll()));
}

bool save(const QList<QColor>& colors, const QString& path, QString* error)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    f.write(serialize(colors).toUtf8());
    if (!f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}

QString userFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/Palettes");
}

QStringList available()
{
    QStringList names;
    const QDir dir(userFolder());
    for (const QFileInfo& fi : dir.entryInfoList({QStringLiteral("*.txt")}, QDir::Files, QDir::Name))
        names << fi.completeBaseName();
    return names;
}

} // namespace Palette
