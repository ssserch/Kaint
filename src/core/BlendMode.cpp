#include "core/BlendMode.h"

#include "core/Parallel.h"
#include "core/Pixels.h"

#include <QObject>
#include <QPainter>

#include <cstdlib>
#include <cstring>

namespace {

struct BlendInfo {
    BlendMode mode;
    const char* ora;
};

const BlendInfo kBlendTable[] = {
    {BlendMode::Normal, "svg:src-over"},
    {BlendMode::Multiply, "svg:multiply"},
    {BlendMode::Additive, "svg:plus"},
    {BlendMode::ColorBurn, "svg:color-burn"},
    {BlendMode::ColorDodge, "svg:color-dodge"},
    {BlendMode::Reflect, "pdn:reflect"},
    {BlendMode::Glow, "pdn:glow"},
    {BlendMode::Overlay, "svg:overlay"},
    {BlendMode::Difference, "svg:difference"},
    {BlendMode::Negation, "pdn:negation"},
    {BlendMode::Lighten, "svg:lighten"},
    {BlendMode::Darken, "svg:darken"},
    {BlendMode::Screen, "svg:screen"},
    {BlendMode::Xor, "pdn:xor"},
    {BlendMode::Overwrite, "svg:src"},
};

// Funciones F(A, B) de Paint.NET sobre colores sin premultiplicar (0..255).
// A = fondo, B = capa superior.
inline int fNormal(int, int b) { return b; }
inline int fMultiply(int a, int b) { return (a * b + 127) / 255; }
inline int fAdditive(int a, int b) { return std::min(255, a + b); }
inline int fColorBurn(int a, int b) { return b == 0 ? 0 : std::max(0, 255 - ((255 - a) * 255) / b); }
inline int fColorDodge(int a, int b) { return b == 255 ? 255 : std::min(255, (a * 255) / (255 - b)); }
inline int fReflect(int a, int b) { return b == 255 ? 255 : std::min(255, (a * a) / (255 - b)); }
inline int fGlow(int a, int b) { return fReflect(b, a); }
inline int fOverlay(int a, int b)
{
    return a < 128 ? (2 * a * b + 127) / 255 : 255 - (2 * (255 - a) * (255 - b) + 127) / 255;
}
inline int fDifference(int a, int b) { return std::abs(a - b); }
inline int fNegation(int a, int b) { return 255 - std::abs(255 - a - b); }
inline int fLighten(int a, int b) { return std::max(a, b); }
inline int fDarken(int a, int b) { return std::min(a, b); }
inline int fScreen(int a, int b) { return a + b - (a * b + 127) / 255; }
inline int fXor(int a, int b) { return a ^ b; }

// Destino premultiplicado (caché de pantalla): resultado premultiplicado.
template <int (*F)(int, int), bool SrcPremul>
void blendRowGeneric(QRgb* d, const QRgb* s, int n, int opacity)
{
    for (int i = 0; i < n; ++i) {
        const QRgb sp = s[i];
        int sb = qAlpha(sp);
        if (opacity < 255)
            sb = (sb * opacity + 127) / 255;
        if (sb == 0)
            continue;
        const QRgb dp = d[i];
        const int da = qAlpha(dp);
        const QRgb su = SrcPremul ? qUnpremultiply(sp) : sp;
        if (da == 0) {
            d[i] = qPremultiply(qRgba(qRed(su), qGreen(su), qBlue(su), sb));
            continue;
        }
        const QRgb du = qUnpremultiply(dp);
        // Fórmula generalizada: x = ab, y = a - ab, z = b - ab (en 0..255).
        const int x = (da * sb + 127) / 255;
        const int y = da - x;
        const int z = sb - x;
        const int total = y + sb;
        auto ch = [&](int A, int B) {
            const int v = (A * y + B * z + F(A, B) * x + 127) / 255;
            return std::min(v, total);
        };
        d[i] = qRgba(ch(qRed(du), qRed(su)), ch(qGreen(du), qGreen(su)), ch(qBlue(du), qBlue(su)), total);
    }
}

// Destino con alfa directo (capas): la misma fórmula, pero el resultado se
// divide por el alfa total (UserBlendOps de Paint.NET). Donde la fuente es
// transparente el destino no se toca, así conserva su color.
template <int (*F)(int, int), bool SrcPremul>
void blendRowStraight(QRgb* d, const QRgb* s, int n, int opacity)
{
    for (int i = 0; i < n; ++i) {
        const QRgb sp = s[i];
        int sb = qAlpha(sp);
        if (opacity < 255)
            sb = (sb * opacity + 127) / 255;
        if (sb == 0)
            continue;
        const QRgb su = SrcPremul ? qUnpremultiply(sp) : sp;
        const QRgb dp = d[i];
        const int da = qAlpha(dp);
        if (da == 0) {
            d[i] = qRgba(qRed(su), qGreen(su), qBlue(su), sb);
            continue;
        }
        if (F == fNormal && da == 255) {
            // Caso más frecuente (pintar sobre un fondo opaco): una interpolación.
            const int ia = 255 - sb;
            auto mix = [&](int A, int B) { return (A * ia + B * sb + 127) / 255; };
            d[i] = qRgba(mix(qRed(dp), qRed(su)), mix(qGreen(dp), qGreen(su)), mix(qBlue(dp), qBlue(su)), 255);
            continue;
        }
        const int x = (da * sb + 127) / 255;
        const int y = da - x;
        const int z = sb - x;
        const int total = y + sb;
        auto ch = [&](int A, int B) { return std::min(255, (A * y + B * z + F(A, B) * x + total / 2) / total); };
        d[i] = qRgba(ch(qRed(dp), qRed(su)), ch(qGreen(dp), qGreen(su)), ch(qBlue(dp), qBlue(su)), total);
    }
}

template <bool SrcPremul>
void overwriteRow(QRgb* d, const QRgb* s, int n, int opacity)
{
    if (opacity >= 255 && !SrcPremul) {
        std::memcpy(d, s, size_t(n) * 4);
        return;
    }
    for (int i = 0; i < n; ++i)
        d[i] = lerpStraight(d[i], SrcPremul ? qUnpremultiply(s[i]) : s[i], opacity);
}

template <bool SrcPremul>
void overwriteRowPremul(QRgb* d, const QRgb* s, int n, int opacity)
{
    for (int i = 0; i < n; ++i)
        d[i] = lerpPixel(d[i], SrcPremul ? s[i] : qPremultiply(s[i]), opacity);
}

using RowFn = void (*)(QRgb*, const QRgb*, int, int);

template <bool SrcPremul>
RowFn straightRow(BlendMode mode)
{
    switch (mode) {
    case BlendMode::Normal: return blendRowStraight<fNormal, SrcPremul>;
    case BlendMode::Multiply: return blendRowStraight<fMultiply, SrcPremul>;
    case BlendMode::Additive: return blendRowStraight<fAdditive, SrcPremul>;
    case BlendMode::ColorBurn: return blendRowStraight<fColorBurn, SrcPremul>;
    case BlendMode::ColorDodge: return blendRowStraight<fColorDodge, SrcPremul>;
    case BlendMode::Reflect: return blendRowStraight<fReflect, SrcPremul>;
    case BlendMode::Glow: return blendRowStraight<fGlow, SrcPremul>;
    case BlendMode::Overlay: return blendRowStraight<fOverlay, SrcPremul>;
    case BlendMode::Difference: return blendRowStraight<fDifference, SrcPremul>;
    case BlendMode::Negation: return blendRowStraight<fNegation, SrcPremul>;
    case BlendMode::Lighten: return blendRowStraight<fLighten, SrcPremul>;
    case BlendMode::Darken: return blendRowStraight<fDarken, SrcPremul>;
    case BlendMode::Screen: return blendRowStraight<fScreen, SrcPremul>;
    case BlendMode::Xor: return blendRowStraight<fXor, SrcPremul>;
    case BlendMode::Overwrite: return overwriteRow<SrcPremul>;
    }
    return blendRowStraight<fNormal, SrcPremul>;
}

template <bool SrcPremul>
RowFn premulRow(BlendMode mode)
{
    switch (mode) {
    case BlendMode::Additive: return blendRowGeneric<fAdditive, SrcPremul>;
    case BlendMode::ColorBurn: return blendRowGeneric<fColorBurn, SrcPremul>;
    case BlendMode::ColorDodge: return blendRowGeneric<fColorDodge, SrcPremul>;
    case BlendMode::Reflect: return blendRowGeneric<fReflect, SrcPremul>;
    case BlendMode::Glow: return blendRowGeneric<fGlow, SrcPremul>;
    case BlendMode::Negation: return blendRowGeneric<fNegation, SrcPremul>;
    case BlendMode::Xor: return blendRowGeneric<fXor, SrcPremul>;
    case BlendMode::Overwrite: return overwriteRowPremul<SrcPremul>;
    default: return nullptr;
    }
}

// Modos de QPainter que coinciden con la fórmula generalizada de Paint.NET.
bool painterMode(BlendMode mode, QPainter::CompositionMode* out)
{
    switch (mode) {
    case BlendMode::Normal: *out = QPainter::CompositionMode_SourceOver; return true;
    case BlendMode::Multiply: *out = QPainter::CompositionMode_Multiply; return true;
    case BlendMode::Overlay: *out = QPainter::CompositionMode_Overlay; return true;
    case BlendMode::Difference: *out = QPainter::CompositionMode_Difference; return true;
    case BlendMode::Lighten: *out = QPainter::CompositionMode_Lighten; return true;
    case BlendMode::Darken: *out = QPainter::CompositionMode_Darken; return true;
    case BlendMode::Screen: *out = QPainter::CompositionMode_Screen; return true;
    default: return false;
    }
}

} // namespace

QString blendModeName(BlendMode mode)
{
    switch (mode) {
    case BlendMode::Normal: return QObject::tr("Normal");
    case BlendMode::Multiply: return QObject::tr("Multiplicar");
    case BlendMode::Additive: return QObject::tr("Adición");
    case BlendMode::ColorBurn: return QObject::tr("Sobreexposición de color");
    case BlendMode::ColorDodge: return QObject::tr("Subexposición de color");
    case BlendMode::Reflect: return QObject::tr("Reflejo");
    case BlendMode::Glow: return QObject::tr("Brillo");
    case BlendMode::Overlay: return QObject::tr("Superponer");
    case BlendMode::Difference: return QObject::tr("Diferencia");
    case BlendMode::Negation: return QObject::tr("Negativo");
    case BlendMode::Lighten: return QObject::tr("Aclarar");
    case BlendMode::Darken: return QObject::tr("Oscurecer");
    case BlendMode::Screen: return QObject::tr("Pantalla");
    case BlendMode::Xor: return QObject::tr("O exclusivo");
    case BlendMode::Overwrite: return QObject::tr("Sobrescribir");
    }
    return {};
}

const QList<BlendMode>& layerBlendModes()
{
    static const QList<BlendMode> modes = [] {
        QList<BlendMode> list;
        for (const auto& entry : kBlendTable)
            if (entry.mode != BlendMode::Overwrite)
                list.append(entry.mode);
        return list;
    }();
    return modes;
}

QString blendModeToolName(BlendMode mode)
{
    switch (mode) {
    case BlendMode::Normal: return QObject::tr("Normal");
    case BlendMode::Multiply: return QObject::tr("Multiplicar");
    case BlendMode::Additive: return QObject::tr("Aditivo");
    case BlendMode::ColorBurn: return QObject::tr("Sobreexposición de color");
    case BlendMode::ColorDodge: return QObject::tr("Subexposición de color");
    case BlendMode::Reflect: return QObject::tr("Reflejar");
    case BlendMode::Glow: return QObject::tr("Resplandor");
    case BlendMode::Overlay: return QObject::tr("Superposición");
    case BlendMode::Difference: return QObject::tr("Diferencia");
    case BlendMode::Negation: return QObject::tr("Negación");
    case BlendMode::Lighten: return QObject::tr("Aclarar");
    case BlendMode::Darken: return QObject::tr("Oscurecer");
    case BlendMode::Screen: return QObject::tr("Trama");
    case BlendMode::Xor: return QObject::tr("O exclusivo");
    case BlendMode::Overwrite: return QObject::tr("Sobrescribir");
    }
    return {};
}

const QList<BlendMode>& toolBlendModes()
{
    static const QList<BlendMode> modes = [] {
        QList<BlendMode> list;
        for (const auto& entry : kBlendTable)
            list.append(entry.mode);
        return list;
    }();
    return modes;
}

QString blendModeOraName(BlendMode mode)
{
    for (const auto& entry : kBlendTable)
        if (entry.mode == mode)
            return QString::fromLatin1(entry.ora);
    return QStringLiteral("svg:src-over");
}

BlendMode blendModeFromOra(const QString& name)
{
    for (const auto& entry : kBlendTable)
        if (name == QLatin1String(entry.ora) && entry.mode != BlendMode::Overwrite)
            return entry.mode;
    return BlendMode::Normal;
}

void blendImage(QImage& dst, QPoint at, const QImage& src, const QRect& srcRect, BlendMode mode, int opacity)
{
    if (opacity <= 0)
        return;
    // Recorta la región a ambas imágenes.
    QRect s = srcRect.intersected(src.rect());
    QRect d(at + (s.topLeft() - srcRect.topLeft()), s.size());
    const QRect clipped = d.intersected(dst.rect());
    if (clipped.isEmpty())
        return;
    s.translate(clipped.topLeft() - d.topLeft());
    s.setSize(clipped.size());
    d = clipped;

    const bool dstPremul = isPremultiplied(dst);
    const bool srcPremul = isPremultiplied(src);
    RowFn fn = nullptr;
    if (dstPremul) {
        // Caché de pantalla: QPainter compone (y convierte la fuente con alfa
        // directo) en los modos que coinciden con los de Paint.NET.
        QPainter::CompositionMode cm;
        if (painterMode(mode, &cm)) {
            QPainter p(&dst);
            p.setCompositionMode(cm);
            p.setOpacity(std::min(opacity, 255) / 255.0);
            p.drawImage(d.topLeft(), src, s);
            return;
        }
        fn = srcPremul ? premulRow<true>(mode) : premulRow<false>(mode);
    } else {
        fn = srcPremul ? straightRow<true>(mode) : straightRow<false>(mode);
    }
    if (!fn)
        return;

    const ConstPixels in(src);
    const Pixels out(dst);
    auto rows = [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
            fn(out.row(d.top() + y) + d.left(), in.row(s.top() + y) + s.left(), d.width(), std::min(opacity, 255));
    };
    if (qint64(d.width()) * d.height() > 65536)
        parallelRows(0, d.height(), rows, 16);
    else
        rows(0, d.height());
}
