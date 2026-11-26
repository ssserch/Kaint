#include "core/Audio.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>

#include <algorithm>
#include <cmath>

namespace Audio {

double Clip::volumeAt(int t) const
{
    double v = volume.value(t);
    if (fadeIn > 0 && t < fadeIn)
        v *= std::max(0, t) / double(fadeIn);
    if (fadeOut > 0 && t > duration - fadeOut)
        v *= std::max(0, duration - t) / double(fadeOut);
    return std::clamp(v, 0.0, 200.0);
}

QJsonObject Clip::toJson() const
{
    QJsonArray keys;
    for (const Motion::Key& k : volume.keys)
        keys.append(QJsonArray{k.t, k.value});
    return QJsonObject{{QStringLiteral("name"), name},
                       {QStringLiteral("suffix"), suffix},
                       {QStringLiteral("row"), row},
                       {QStringLiteral("start"), start},
                       {QStringLiteral("offset"), offset},
                       {QStringLiteral("duration"), duration},
                       {QStringLiteral("length"), length},
                       {QStringLiteral("volume"), volume.base},
                       {QStringLiteral("keys"), keys},
                       {QStringLiteral("fadeIn"), fadeIn},
                       {QStringLiteral("fadeOut"), fadeOut},
                       {QStringLiteral("muted"), muted},
                       {QStringLiteral("peaks"), QString::fromLatin1(peaks.toBase64())}};
}

Clip Clip::fromJson(const QJsonObject& o)
{
    Clip c;
    c.name = o.value(QStringLiteral("name")).toString();
    c.suffix = o.value(QStringLiteral("suffix")).toString();
    c.row = std::clamp(o.value(QStringLiteral("row")).toInt(), 0, 63);
    c.start = std::max(0, o.value(QStringLiteral("start")).toInt());
    c.offset = std::max(0, o.value(QStringLiteral("offset")).toInt());
    c.length = std::max(1, o.value(QStringLiteral("length")).toInt());
    c.duration = std::clamp(o.value(QStringLiteral("duration")).toInt(), 1, c.length);
    c.volume.base = std::clamp(o.value(QStringLiteral("volume")).toDouble(100), 0.0, 200.0);
    for (const QJsonValue& k : o.value(QStringLiteral("keys")).toArray()) {
        const QJsonArray a = k.toArray();
        if (a.size() >= 2)
            c.volume.setKey(a[0].toInt(), std::clamp(a[1].toDouble(), 0.0, 200.0));
    }
    c.fadeIn = std::max(0, o.value(QStringLiteral("fadeIn")).toInt());
    c.fadeOut = std::max(0, o.value(QStringLiteral("fadeOut")).toInt());
    c.muted = o.value(QStringLiteral("muted")).toBool();
    c.peaks = QByteArray::fromBase64(o.value(QStringLiteral("peaks")).toString().toLatin1());
    return c;
}

QStringList fileExtensions()
{
    return {QStringLiteral("mp3"), QStringLiteral("wav"), QStringLiteral("ogg"), QStringLiteral("oga"), QStringLiteral("opus"),
            QStringLiteral("flac"), QStringLiteral("m4a"), QStringLiteral("aac"), QStringLiteral("wma"), QStringLiteral("aiff"),
            QStringLiteral("aif")};
}

bool isAudioFile(const QString& path)
{
    return fileExtensions().contains(QFileInfo(path).suffix().toLower());
}

namespace {

// La envolvente de unas muestras (mezcladas a mono, -1..1) a 'rate' muestras por segundo.
QByteArray peaksOf(const std::vector<float>& mono, int rate)
{
    const int window = std::max(1, rate * kPeakMs / 1000);
    QByteArray out;
    out.reserve(int(mono.size() / window) + 1);
    for (size_t i = 0; i < mono.size(); i += window) {
        float peak = 0;
        for (size_t j = i; j < std::min(mono.size(), i + window); ++j)
            peak = std::max(peak, std::abs(mono[j]));
        out.append(char(std::clamp(int(std::lround(std::sqrt(peak) * 255)), 0, 255)));
    }
    return out;
}

bool analyzeWav(const QByteArray& data, int* lengthMs, QByteArray* peaks)
{
    if (data.size() < 44 || !data.startsWith("RIFF") || data.mid(8, 4) != "WAVE")
        return false;
    int format = 0, channels = 0, rate = 0, bits = 0;
    qsizetype at = 12;
    while (at + 8 <= data.size()) {
        const QByteArray id = data.mid(at, 4);
        const quint32 size = qFromLittleEndian<quint32>(data.constData() + at + 4);
        const qsizetype body = at + 8;
        if (id == "fmt " && body + 16 <= data.size()) {
            format = qFromLittleEndian<quint16>(data.constData() + body);
            channels = qFromLittleEndian<quint16>(data.constData() + body + 2);
            rate = int(qFromLittleEndian<quint32>(data.constData() + body + 4));
            bits = qFromLittleEndian<quint16>(data.constData() + body + 14);
            if (format == 0xfffe && size >= 26)  // WAVE_FORMAT_EXTENSIBLE: el formato, en el subtipo
                format = qFromLittleEndian<quint16>(data.constData() + body + 24);
        } else if (id == "data" && channels > 0 && rate > 0 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) {
            const qsizetype end = std::min<qsizetype>(data.size(), body + qsizetype(size));
            const int bytes = bits / 8;
            const qsizetype frames = (end - body) / (bytes * channels);
            *lengthMs = int(frames * 1000 / rate);
            std::vector<float> mono;
            mono.reserve(size_t(frames));
            const char* p = data.constData() + body;
            for (qsizetype f = 0; f < frames; ++f) {
                float sum = 0;
                for (int c = 0; c < channels; ++c, p += bytes) {
                    float s = 0;
                    if (format == 3 && bits == 32)
                        s = qFromLittleEndian<float>(p);
                    else if (bits == 8)
                        s = (quint8(*p) - 128) / 128.0f;
                    else if (bits == 16)
                        s = qFromLittleEndian<qint16>(p) / 32768.0f;
                    else if (bits == 24)
                        s = float(qint32((quint32(quint8(p[0])) << 8) | (quint32(quint8(p[1])) << 16) | (quint32(quint8(p[2])) << 24)) >> 8)
                            / 8388608.0f;
                    else
                        s = qFromLittleEndian<qint32>(p) / 2147483648.0f;
                    sum += s;
                }
                mono.push_back(sum / channels);
            }
            *peaks = peaksOf(mono, rate);
            return *lengthMs > 0;
        }
        at = body + qsizetype(size) + (size & 1);
    }
    return false;
}

} // namespace

bool analyze(const QByteArray& data, const QString& suffix, const QString& ffmpeg, int* lengthMs, QByteArray* peaks,
             QString* error)
{
    if (analyzeWav(data, lengthMs, peaks))
        return true;
    if (ffmpeg.isEmpty()) {
        if (error)
            *error = QObject::tr("Para leer este audio hace falta FFmpeg (https://ffmpeg.org). Los WAV se leen sin él.");
        return false;
    }
    // FFmpeg lo pasa a muestras de 16 bits, mono y 8000 por segundo (para la onda basta).
    QTemporaryDir dir;
    const QString input = dir.filePath(QStringLiteral("in.") + (suffix.isEmpty() ? QStringLiteral("bin") : suffix));
    QFile file(input);
    if (!dir.isValid() || !file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) {
        if (error)
            *error = QObject::tr("No se pudo crear una carpeta temporal.");
        return false;
    }
    file.close();
    QProcess process;
    process.start(ffmpeg, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-i"), input, QStringLiteral("-ac"),
                           QStringLiteral("1"), QStringLiteral("-ar"), QStringLiteral("8000"), QStringLiteral("-f"),
                           QStringLiteral("s16le"), QStringLiteral("-")});
    if (!process.waitForStarted(10000)) {
        if (error)
            *error = QObject::tr("No se pudo ejecutar FFmpeg (%1).").arg(ffmpeg);
        return false;
    }
    QByteArray pcm;
    while (process.state() != QProcess::NotRunning && process.waitForReadyRead(30000))
        pcm += process.readAllStandardOutput();
    process.waitForFinished(30000);
    pcm += process.readAllStandardOutput();
    const qsizetype samples = pcm.size() / 2;
    if (samples <= 0) {
        if (error)
            *error = QObject::tr("No se pudo leer el audio: %1").arg(QString::fromUtf8(process.readAllStandardError()).trimmed().left(300));
        return false;
    }
    std::vector<float> mono(static_cast<size_t>(samples));
    for (qsizetype i = 0; i < samples; ++i)
        mono[size_t(i)] = qFromLittleEndian<qint16>(pcm.constData() + 2 * i) / 32768.0f;
    *lengthMs = int(samples * 1000 / 8000);
    *peaks = peaksOf(mono, 8000);
    return *lengthMs > 0;
}

QString ffmpegVolumeExpression(const Clip& clip)
{
    const QList<Motion::Key>& keys = clip.volume.keys;
    auto num = [](double v) { return QString::number(v, 'f', 4); };
    if (keys.isEmpty())
        return num(clip.volume.base / 100.0);
    // De atrás adelante: if(lt(t,T0),V0, if(lt(t,T1), V0+(V1-V0)*(t-T0)/(T1-T0), ... Vn)).
    QString expr = num(keys.last().value / 100.0);
    for (int i = int(keys.size()) - 1; i >= 1; --i) {
        const double t0 = keys[i - 1].t / 1000.0, t1 = keys[i].t / 1000.0;
        const double v0 = keys[i - 1].value / 100.0, v1 = keys[i].value / 100.0;
        const QString segment = t1 > t0 ? QStringLiteral("%1+(%2)*(t-%3)/%4").arg(num(v0), num(v1 - v0), num(t0), num(t1 - t0)) : num(v1);
        expr = QStringLiteral("if(lt(t,%1),%2,%3)").arg(num(t1), segment, expr);
    }
    return QStringLiteral("if(lt(t,%1),%2,%3)").arg(num(keys.first().t / 1000.0), num(keys.first().value / 100.0), expr);
}

} // namespace Audio
