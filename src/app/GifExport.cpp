#include "app/GifExport.h"

#include "core/Apng.h"
#include "core/Document.h"
#include "core/ImageIO.h"
#include "core/ModernFormats.h"

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QVBoxLayout>

#include <cmath>

namespace GifExport {

namespace {

bool writeFile(const QString& path, const QByteArray& data, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) {
        if (error)
            *error = QObject::tr("No se pudo escribir «%1».").arg(QDir::toNativeSeparators(path));
        return false;
    }
    return true;
}

QList<Gif::Frame> visible(const QList<Gif::Frame>& frames)
{
    QList<Gif::Frame> out;
    for (const Gif::Frame& f : frames)
        if (!f.hidden && !f.image.isNull())
            out << f;
    return out;
}

QImage prepared(const QImage& image, const VideoOptions& o, bool keepAlpha)
{
    QImage out = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (o.scale > 1)
        out = out.scaled(out.size() * o.scale, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    if (!keepAlpha) {
        QImage flat(out.size(), QImage::Format_RGB32);
        flat.fill(o.background);
        QPainter p(&flat);
        p.drawImage(0, 0, out);
        p.end();
        return flat;
    }
    return out;
}

// Con FFmpeg: los fotogramas como PNG en una carpeta temporal y una lista con la duración de cada
// uno (vídeo de duración variable, sin redondear las duraciones del GIF).
bool encodeWithFfmpeg(const QList<Gif::Frame>& frames, Format format, const QString& path, int loops, int webpQuality,
                      const VideoOptions& o, QWidget* progressParent, QString* error)
{
    const QString ffmpeg = ffmpegPath();
    if (ffmpeg.isEmpty()) {
        if (error)
            *error = QObject::tr("Para exportar en este formato hace falta FFmpeg (https://ffmpeg.org). Instálalo (en Linux, el "
                                 "paquete «ffmpeg») o pon ffmpeg junto a Kaint y vuelve a intentarlo.");
        return false;
    }
    QTemporaryDir dir;
    if (!dir.isValid()) {
        if (error)
            *error = QObject::tr("No se pudo crear una carpeta temporal.");
        return false;
    }
    const bool alpha = format != Format::Mp4;
    QString list = QStringLiteral("ffconcat version 1.0\n");
    for (int i = 0; i < frames.size(); ++i) {
        const QString name = QStringLiteral("f%1.png").arg(i, 5, 10, QLatin1Char('0'));
        if (!prepared(frames[i].image, o, alpha).save(dir.filePath(name), "PNG")) {
            if (error)
                *error = QObject::tr("No se pudo escribir «%1».").arg(dir.filePath(name));
            return false;
        }
    }
    const int repeat = format == Format::WebP ? 1 : std::max(1, o.repeat);
    QString last;
    for (int r = 0; r < repeat; ++r)
        for (int i = 0; i < frames.size(); ++i) {
            last = QStringLiteral("f%1.png").arg(i, 5, 10, QLatin1Char('0'));
            list += QStringLiteral("file '%1'\nduration %2\n").arg(last).arg(std::max(1, frames[i].delayMs) / 1000.0, 0, 'f', 3);
        }
    // (La duración del último solo cuenta si se repite su nombre al final.)
    list += QStringLiteral("file '%1'\n").arg(last);
    QFile listFile(dir.filePath(QStringLiteral("list.txt")));
    if (!listFile.open(QIODevice::WriteOnly) || listFile.write(list.toUtf8()) < 0) {
        if (error)
            *error = QObject::tr("No se pudo crear una carpeta temporal.");
        return false;
    }
    listFile.close();
    const int crf[3][3] = {{17, 23, 28}, {24, 32, 40}, {95, 85, 70}};
    const int q = std::clamp(o.quality, 0, 2);
    QStringList args{QStringLiteral("-y"), QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                     QStringLiteral("-progress"), QStringLiteral("pipe:1"), QStringLiteral("-f"), QStringLiteral("concat"),
                     QStringLiteral("-safe"), QStringLiteral("0"), QStringLiteral("-i"), listFile.fileName(),
                     QStringLiteral("-fps_mode"), QStringLiteral("vfr")};
    switch (format) {
    case Format::Mp4:
        // H.264 en 4:2:0 necesita medidas pares.
        args << QStringLiteral("-vf") << QStringLiteral("pad=ceil(iw/2)*2:ceil(ih/2)*2:color=%1").arg(o.background.name())
             << QStringLiteral("-c:v") << QStringLiteral("libx264") << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
             << QStringLiteral("-crf") << QString::number(crf[0][q]) << QStringLiteral("-preset") << QStringLiteral("medium")
             << QStringLiteral("-movflags") << QStringLiteral("+faststart");
        break;
    case Format::WebM:
        args << QStringLiteral("-c:v") << QStringLiteral("libvpx-vp9") << QStringLiteral("-pix_fmt") << QStringLiteral("yuva420p")
             << QStringLiteral("-b:v") << QStringLiteral("0") << QStringLiteral("-crf") << QString::number(crf[1][q])
             << QStringLiteral("-row-mt") << QStringLiteral("1");
        break;
    case Format::WebP:
        args << QStringLiteral("-c:v") << QStringLiteral("libwebp_anim") << QStringLiteral("-loop") << QString::number(std::max(0, loops));
        if (webpQuality >= 100)
            args << QStringLiteral("-lossless") << QStringLiteral("1");
        else
            args << QStringLiteral("-quality") << QString::number(webpQuality);
        break;
    case Format::Apng:
        break;
    }
    args << path;
    QProcess process;
    process.setProgram(ffmpeg);
    process.setArguments(args);
    process.start();
    if (!process.waitForStarted(10000)) {
        if (error)
            *error = QObject::tr("No se pudo ejecutar FFmpeg (%1).").arg(ffmpeg);
        return false;
    }
    const int total = int(frames.size()) * repeat;
    std::unique_ptr<QProgressDialog> progress;
    if (progressParent) {
        progress = std::make_unique<QProgressDialog>(QObject::tr("Exportando…"), QObject::tr("Cancelar"), 0, total, progressParent);
        progress->setWindowModality(Qt::WindowModal);
        progress->setMinimumDuration(400);
    }
    QByteArray out;
    bool canceled = false;
    while (!process.waitForFinished(50)) {
        out += process.readAllStandardOutput();
        // «frame=N» en cada informe de -progress.
        const qsizetype at = out.lastIndexOf("frame=");
        if (progress && at >= 0) {
            const qsizetype end = out.indexOf('\n', at);
            if (end > at)
                progress->setValue(std::min(total, out.mid(at + 6, end - at - 6).trimmed().toInt()));
        }
        QCoreApplication::processEvents();
        if (progress && progress->wasCanceled()) {
            canceled = true;
            process.kill();
            process.waitForFinished(3000);
            break;
        }
        if (process.state() == QProcess::NotRunning)
            break;
    }
    if (canceled) {
        QFile::remove(path);
        if (error)
            error->clear();
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 || !QFileInfo(path).size()) {
        if (error)
            *error = QObject::tr("FFmpeg no pudo exportar: %1").arg(QString::fromUtf8(process.readAllStandardError()).trimmed().left(600));
        return false;
    }
    return true;
}

} // namespace

QString ffmpegPath()
{
    const QString name = QStringLiteral("ffmpeg");
    const QString beside = QStandardPaths::findExecutable(name, {QCoreApplication::applicationDirPath()});
    if (!beside.isEmpty())
        return beside;
    return QStandardPaths::findExecutable(name);
}

QString extension(Format format)
{
    switch (format) {
    case Format::Apng: return QStringLiteral("png");
    case Format::WebP: return QStringLiteral("webp");
    case Format::Mp4: return QStringLiteral("mp4");
    case Format::WebM: return QStringLiteral("webm");
    }
    return {};
}

QString filter(Format format)
{
    switch (format) {
    case Format::Apng: return QObject::tr("PNG animado (*.png *.apng)");
    case Format::WebP: return QObject::tr("WebP animado (*.webp)");
    case Format::Mp4: return QObject::tr("Vídeo MP4 (*.mp4)");
    case Format::WebM: return QObject::tr("Vídeo WebM (*.webm)");
    }
    return {};
}

bool encode(const QList<Gif::Frame>& all, Format format, const QString& path, int loops, int webpQuality, const VideoOptions& video,
            QWidget* progressParent, QString* error)
{
    const QList<Gif::Frame> frames = visible(all);
    if (frames.isEmpty()) {
        if (error)
            *error = QObject::tr("No hay fotogramas que guardar.");
        return false;
    }
    if (format == Format::Apng) {
        QList<Gif::Frame> scaled = frames;
        if (video.scale > 1)
            for (Gif::Frame& f : scaled)
                f.image = prepared(f.image, video, true);
        const QByteArray data = Apng::encode(scaled, loops, error);
        return !data.isEmpty() && writeFile(path, data, error);
    }
    if (format == Format::WebP && ModernFormats::canWriteAnimatedWebP()) {
        QList<Gif::Frame> scaled = frames;
        if (video.scale > 1)
            for (Gif::Frame& f : scaled)
                f.image = prepared(f.image, video, true);
        const QByteArray data = ModernFormats::encodeAnimatedWebP(scaled, webpQuality, loops, error);
        return !data.isEmpty() && writeFile(path, data, error);
    }
    return encodeWithFfmpeg(frames, format, path, loops, webpQuality, video, progressParent, error);
}

QString exportAnimation(QWidget* parent, Document* doc, Format format, const QString& directory)
{
    if (!doc)
        return {};
    const QList<Gif::Frame> frames = visible(ImageIO::animationFrames(doc, 100, false));
    if (frames.isEmpty())
        return {};
    const bool video = format == Format::Mp4 || format == Format::WebM;
    if (video && ffmpegPath().isEmpty()) {
        QMessageBox::warning(parent, QObject::tr("Exportar"),
                             QObject::tr("Para exportar vídeo hace falta FFmpeg (https://ffmpeg.org). Instálalo (en Linux, el paquete "
                                         "«ffmpeg») o pon ffmpeg junto a Kaint y vuelve a intentarlo."));
        return {};
    }
    int total = 0;
    for (const Gif::Frame& f : frames)
        total += std::max(1, f.delayMs);

    // Las opciones: repeticiones, tamaño, calidad y fondo (vídeo); calidad (WebP).
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Exportar como %1").arg(format == Format::Apng   ? QObject::tr("PNG animado")
                                                           : format == Format::WebP ? QObject::tr("WebP animado")
                                                           : format == Format::Mp4  ? QStringLiteral("MP4")
                                                                                    : QStringLiteral("WebM")));
    auto* form = new QFormLayout;
    auto* repeat = new QSpinBox(&dlg);
    repeat->setRange(1, 100);
    // Que el vídeo dure unos segundos aunque el GIF sea corto.
    repeat->setValue(std::clamp(int(std::ceil(5000.0 / std::max(1, total))), 1, 20));
    repeat->setSuffix(QObject::tr(" veces"));
    auto* durationLabel = new QLabel(&dlg);
    auto syncDuration = [=] { durationLabel->setText(QObject::tr("%1 s").arg(total * repeat->value() / 1000.0, 0, 'f', 1)); };
    QObject::connect(repeat, &QSpinBox::valueChanged, &dlg, syncDuration);
    syncDuration();
    auto* scale = new QComboBox(&dlg);
    for (int s : {1, 2, 3, 4})
        scale->addItem(QStringLiteral("%1 % (%2 × %3)").arg(s * 100).arg(doc->size().width() * s).arg(doc->size().height() * s), s);
    scale->setToolTip(QObject::tr("Más grande sin suavizar (para pixel art: los vídeos pequeños se ven borrosos)."));
    auto* quality = new QComboBox(&dlg);
    quality->addItems({QObject::tr("Alta"), QObject::tr("Media"), QObject::tr("Baja (archivo más pequeño)")});
    auto* webpQuality = new QSpinBox(&dlg);
    webpQuality->setRange(1, 100);
    webpQuality->setValue(90);
    webpQuality->setToolTip(QObject::tr("100: sin pérdida."));
    QColor background = Qt::white;
    auto* backgroundButton = new QPushButton(&dlg);
    auto paintBackground = [&] {
        QPixmap pm(16, 16);
        pm.fill(background);
        backgroundButton->setIcon(QIcon(pm));
        backgroundButton->setText(background.name());
    };
    paintBackground();
    // Blanco o negro (un clic cambia de uno a otro).
    QObject::connect(backgroundButton, &QPushButton::clicked, &dlg, [&] {
        background = background == QColor(Qt::white) ? QColor(Qt::black) : QColor(Qt::white);
        paintBackground();
    });
    if (video) {
        form->addRow(QObject::tr("Repetir la animación:"), repeat);
        form->addRow(QObject::tr("Duración del vídeo:"), durationLabel);
        form->addRow(QObject::tr("Calidad:"), quality);
        if (format == Format::Mp4) {
            backgroundButton->setToolTip(QObject::tr("El MP4 no tiene transparencia: lo transparente va sobre este color (blanco o negro)."));
            form->addRow(QObject::tr("Fondo:"), backgroundButton);
        }
    }
    if (format == Format::WebP)
        form->addRow(QObject::tr("Calidad:"), webpQuality);
    form->addRow(QObject::tr("Tamaño:"), scale);
    auto* note = new QLabel(&dlg);
    note->setWordWrap(true);
    note->setText(format == Format::Apng   ? QObject::tr("Sin pérdida y con transparencia de 8 bits; lo abren los navegadores y Discord.")
                  : format == Format::WebP ? QObject::tr("Con transparencia y mucho más ligero que el GIF.")
                  : format == Format::Mp4  ? QObject::tr("H.264: se ve en cualquier teléfono, navegador y red social.")
                                           : QObject::tr("VP9 con transparencia (para la web)."));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    auto* layout = new QVBoxLayout(&dlg);
    layout->addLayout(form);
    layout->addWidget(note);
    layout->addWidget(buttons);
    if (dlg.exec() != QDialog::Accepted)
        return {};

    const QString base = doc->displayName().section(QLatin1Char('.'), 0, 0);
    QString path = QFileDialog::getSaveFileName(parent, QObject::tr("Exportar"),
                                                QDir(directory).filePath(QStringLiteral("%1.%2").arg(base, extension(format))), filter(format));
    if (path.isEmpty())
        return {};
    if (QFileInfo(path).suffix().isEmpty())
        path += QLatin1Char('.') + extension(format);
    VideoOptions options;
    options.repeat = repeat->value();
    options.scale = scale->currentData().toInt();
    options.quality = quality->currentIndex();
    options.background = background;
    QString error;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = encode(frames, format, path, doc->gifLoops(), webpQuality->value(), options, parent, &error);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        if (!error.isEmpty())
            QMessageBox::warning(parent, QObject::tr("Exportar"), error);
        return {};
    }
    return path;
}

} // namespace GifExport
