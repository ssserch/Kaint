#include "app/dialogs/SaveConfigDialog.h"

#include "app/Icons.h"
#include "app/dialogs/DialogButtons.h"
#include "app/widgets/BarSlider.h"
#include "core/Raster.h"

#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

bool SaveConfigDialog::hasOptions(const QString& path)
{
    return ImageIO::supportsQuality(path) || ImageIO::hasBitDepth(path) || ImageIO::isAnimation(path)
           || ImageIO::isIcon(path);
}

SaveConfigDialog::SaveConfigDialog(const QImage& flattened, const QString& path, const ImageIO::SaveOptions& initial,
                                   QWidget* parent, const QList<Gif::Frame>& frames)
    : QDialog(parent)
    , m_image(flattened)
    , m_path(path)
    , m_frames(frames)
{
    setWindowTitle(tr("Configuración de guardado: %1").arg(QFileInfo(path).suffix().toUpper()));
    setWindowIcon(Icons::theme({"document-save"}));

    auto* form = new QFormLayout;
    if (ImageIO::supportsQuality(path)) {
        m_quality = new BarSlider(1, 100, QString(), this);
        m_quality->setValue(initial.quality);
        connect(m_quality, &BarSlider::valueChanged, this, &SaveConfigDialog::schedule);
        form->addRow(tr("Calidad:"), m_quality);
    }
    if (ImageIO::hasBitDepth(path)) {
        m_depth = new QComboBox(this);
        m_depth->addItem(tr("Automática"), 0);
        if (ImageIO::supportsAlpha(path))
            m_depth->addItem(tr("32 bits"), 32);
        m_depth->addItem(tr("24 bits"), 24);
        m_depth->addItem(tr("8 bits"), 8);
        m_depth->setCurrentIndex(std::max(0, m_depth->findData(initial.bitDepth)));
        connect(m_depth, &QComboBox::currentIndexChanged, this, [this] {
            updateControls();
            schedule();
        });
        form->addRow(tr("Profundidad de bits:"), m_depth);
        m_quantization = new QComboBox(this);
        m_quantization->addItem(tr("Octree"), 1);
        m_quantization->addItem(tr("Corte a la mediana"), 0);
        m_quantization->setCurrentIndex(std::max(0, m_quantization->findData(initial.quantization)));
        connect(m_quantization, &QComboBox::currentIndexChanged, this, &SaveConfigDialog::schedule);
        form->addRow(tr("Algoritmo de cuantización:"), m_quantization);
        m_dither = new BarSlider(0, 8, QString(), this);
        m_dither->setValue(initial.dither);
        connect(m_dither, &BarSlider::valueChanged, this, &SaveConfigDialog::schedule);
        form->addRow(tr("Nivel de tramado:"), m_dither);
        if (ImageIO::supportsAlpha(path)) {
            m_threshold = new BarSlider(0, 255, QString(), this);
            m_threshold->setValue(initial.alphaThreshold);
            connect(m_threshold, &BarSlider::valueChanged, this, &SaveConfigDialog::schedule);
            form->addRow(tr("Umbral de transparencia:"), m_threshold);
            m_thresholdNote = new QLabel(tr("Los píxeles que tengan un valor del canal alfa menor que el umbral serán "
                                            "totalmente transparentes."),
                                         this);
            m_thresholdNote->setWordWrap(true);
            form->addRow(m_thresholdNote);
        }
    }
    if (ImageIO::supportsInterlace(path)) {
        m_interlaced = new QCheckBox(tr("Entrelazado"), this);
        m_interlaced->setToolTip(tr("La imagen se ve entera a baja resolución mientras se carga (Adam7)."));
        m_interlaced->setChecked(initial.interlaced);
        connect(m_interlaced, &QCheckBox::toggled, this, &SaveConfigDialog::schedule);
        form->addRow(tr("Otras opciones:"), m_interlaced);
    }

    if (ImageIO::isAnimation(path)) {
        if (m_frames.size() > 1) {
            m_animation = new QCheckBox(tr("Animación: cada capa visible es un fotograma"), this);
            m_animation->setToolTip(tr("La capa de abajo es el primer fotograma. La duración de cada uno va en el "
                                       "nombre de la capa, p. ej. «Fotograma 1 (100ms)» (Imagen > Animación)."));
            m_animation->setChecked(initial.animation);
            connect(m_animation, &QCheckBox::toggled, this, [this] {
                updateControls();
                schedule();
            });
            form->addRow(m_animation);
            m_delay = new QSpinBox(this);
            m_delay->setRange(Gif::kMinDelayMs, 60000);
            m_delay->setSingleStep(10);
            m_delay->setSuffix(tr(" ms"));
            m_delay->setValue(initial.frameDelay);
            m_delay->setToolTip(tr("Para las capas que no indican su duración en el nombre"));
            connect(m_delay, &QSpinBox::valueChanged, this, &SaveConfigDialog::schedule);
            form->addRow(tr("Duración de los fotogramas:"), m_delay);
            m_loopForever = new QCheckBox(tr("Repetir sin fin"), this);
            m_loopForever->setChecked(initial.loops == 0);
            m_loops = new QSpinBox(this);
            m_loops->setRange(0, 65535);
            m_loops->setSpecialValueText(tr("Una sola vez"));
            m_loops->setSuffix(tr(" veces más"));
            m_loops->setValue(std::max(0, initial.loops));
            connect(m_loopForever, &QCheckBox::toggled, this, [this] {
                updateControls();
                schedule();
            });
            connect(m_loops, &QSpinBox::valueChanged, this, &SaveConfigDialog::schedule);
            form->addRow(m_loopForever);
            form->addRow(tr("Repeticiones:"), m_loops);
        }
        m_dither = new BarSlider(0, 8, QString(), this);
        m_dither->setValue(initial.dither);
        connect(m_dither, &BarSlider::valueChanged, this, &SaveConfigDialog::schedule);
        form->addRow(tr("Nivel de tramado:"), m_dither);
        m_threshold = new BarSlider(0, 255, QString(), this);
        m_threshold->setValue(initial.alphaThreshold);
        connect(m_threshold, &BarSlider::valueChanged, this, &SaveConfigDialog::schedule);
        form->addRow(tr("Umbral de transparencia:"), m_threshold);
    }
    if (ImageIO::isIcon(path)) {
        auto* sizes = new QWidget(this);
        auto* grid = new QGridLayout(sizes);
        grid->setContentsMargins(0, 0, 0, 0);
        int i = 0;
        for (int s : Ico::standardSizes()) {
            auto* check = new QCheckBox(QStringLiteral("%1 × %1").arg(s), sizes);
            check->setChecked(initial.icoSizes.contains(s));
            check->setProperty("kaintIcoSide", s);
            connect(check, &QCheckBox::toggled, this, &SaveConfigDialog::schedule);
            grid->addWidget(check, i / 2, i % 2);
            m_icoSizes << check;
            ++i;
        }
        form->addRow(tr("Tamaños:"), sizes);
    }

    m_size = new QLabel(this);
    m_preview = new QLabel(this);
    m_preview->setAlignment(Qt::AlignCenter);
    auto* scroll = new QScrollArea(this);
    scroll->setWidget(m_preview);
    scroll->setWidgetResizable(true);
    scroll->setMinimumSize(360, 280);
    m_zoom = new QComboBox(this);
    m_zoom->addItem(tr("Ajustar"), 0);
    for (int z : {100, 200, 400})
        m_zoom->addItem(QStringLiteral("%1 %").arg(z), z);
    connect(m_zoom, &QComboBox::currentIndexChanged, this, [this] { onEncoded(m_lastPreview, -1, {}, m_generation); });

    auto* side = new QVBoxLayout;
    side->addLayout(form);
    side->addStretch();
    side->addWidget(m_size);

    auto* top = new QHBoxLayout;
    top->addLayout(side);
    auto* right = new QVBoxLayout;
    auto* zoomRow = new QHBoxLayout;
    zoomRow->addWidget(new QLabel(tr("Vista previa:"), this));
    zoomRow->addStretch();
    zoomRow->addWidget(m_zoom);
    right->addLayout(zoomRow);
    right->addWidget(scroll, 1);
    top->addLayout(right, 1);

    auto* box = okCancelButtons(this);
    auto* defaults = box->addButton(tr("Predeterminado"), QDialogButtonBox::ResetRole);
    connect(defaults, &QPushButton::clicked, this, [this] {
        const ImageIO::SaveOptions d;
        if (m_quality)
            m_quality->setValue(d.quality);
        if (m_depth)
            m_depth->setCurrentIndex(0);
        if (m_dither)
            m_dither->setValue(d.dither);
        if (m_threshold)
            m_threshold->setValue(d.alphaThreshold);
        if (m_quantization)
            m_quantization->setCurrentIndex(m_quantization->findData(d.quantization));
        if (m_interlaced)
            m_interlaced->setChecked(d.interlaced);
        if (m_animation)
            m_animation->setChecked(d.animation);
        if (m_delay)
            m_delay->setValue(d.frameDelay);
        if (m_loopForever)
            m_loopForever->setChecked(d.loops == 0);
        for (QCheckBox* c : std::as_const(m_icoSizes))
            c->setChecked(d.icoSizes.contains(c->property("kaintIcoSide").toInt()));
    });

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(top, 1);
    layout->addWidget(box);
    resize(720, 440);

    m_debounce.setSingleShot(true);
    m_debounce.setInterval(120);
    connect(&m_debounce, &QTimer::timeout, this, &SaveConfigDialog::start);
    updateControls();
    schedule();
}

SaveConfigDialog::~SaveConfigDialog()
{
    ++m_generation;
    if (m_thread.joinable())
        m_thread.join();
}

ImageIO::SaveOptions SaveConfigDialog::options() const
{
    ImageIO::SaveOptions o;
    if (m_quality)
        o.quality = m_quality->value();
    if (m_depth)
        o.bitDepth = m_depth->currentData().toInt();
    if (m_dither)
        o.dither = m_dither->value();
    if (m_threshold)
        o.alphaThreshold = m_threshold->value();
    if (m_quantization)
        o.quantization = m_quantization->currentData().toInt();
    if (m_interlaced)
        o.interlaced = m_interlaced->isChecked();
    if (m_animation)
        o.animation = m_animation->isChecked();
    if (m_delay)
        o.frameDelay = m_delay->value();
    if (m_loopForever)
        o.loops = m_loopForever->isChecked() ? 0 : (m_loops->value() == 0 ? -1 : m_loops->value());
    if (!m_icoSizes.isEmpty()) {
        o.icoSizes.clear();
        for (QCheckBox* c : m_icoSizes)
            if (c->isChecked())
                o.icoSizes << c->property("kaintIcoSide").toInt();
    }
    return o;
}

void SaveConfigDialog::updateControls()
{
    // El GIF siempre es de 8 bits.
    const bool eight = (m_depth && m_depth->currentData().toInt() == 8) || ImageIO::isAnimation(m_path);
    if (m_dither)
        m_dither->setEnabled(eight);
    if (m_threshold)
        m_threshold->setEnabled(eight);
    if (m_thresholdNote)
        m_thresholdNote->setEnabled(eight);
    if (m_quantization)
        m_quantization->setEnabled(eight);
    const bool animated = m_animation && m_animation->isChecked();
    if (m_delay)
        m_delay->setEnabled(animated);
    if (m_loopForever)
        m_loopForever->setEnabled(animated);
    if (m_loops)
        m_loops->setEnabled(animated && !m_loopForever->isChecked());
}

void SaveConfigDialog::schedule()
{
    m_size->setText(tr("Tamaño del archivo: calculando…"));
    m_pending = true;
    m_debounce.start();
}

void SaveConfigDialog::start()
{
    if (m_running || !m_pending)
        return;
    if (m_thread.joinable())
        m_thread.join();
    m_pending = false;
    m_running = true;
    const int generation = ++m_generation;
    QList<Gif::Frame> frames;
    if (m_animation && m_animation->isChecked())
        frames = m_frames;
    m_thread = std::thread([this, generation, image = m_image, path = m_path, opts = options(), frames]() mutable {
        QString error;
        QByteArray data;
        if (frames.size() > 1) {
            for (Gif::Frame& f : frames)
                if (f.delayMs <= 0)
                    f.delayMs = opts.frameDelay;
            Gif::Options o;
            o.dither = opts.dither;
            o.alphaThreshold = opts.alphaThreshold;
            o.loops = opts.loops;
            data = Gif::encode(frames, o, &error);
        } else {
            const QImage prepared = ImageIO::prepare(image, path, opts);
            data = ImageIO::encode(prepared, path, opts, &error);
        }
        QImage decoded;
        if (!data.isEmpty() && ImageIO::isIcon(path)) {
            QBuffer buffer(&data);
            QImageReader reader(&buffer, "ico");
            for (int i = 0; i < std::max(1, reader.imageCount()); ++i) {
                if (i > 0 && !reader.jumpToImage(i))
                    break;
                const QImage img = reader.read();
                if (img.width() > decoded.width())
                    decoded = img;
            }
        } else if (!data.isEmpty()) {
            decoded.loadFromData(data);
        }
        const qint64 bytes = data.size();
        QMetaObject::invokeMethod(
            this, [this, decoded, bytes, error, generation] { onEncoded(decoded, bytes, error, generation); },
            Qt::QueuedConnection);
    });
}

void SaveConfigDialog::onEncoded(const QImage& preview, qint64 bytes, const QString& error, int generation)
{
    if (generation != m_generation)
        return;
    if (bytes >= 0) {
        m_running = false;
        if (m_thread.joinable())
            m_thread.join();
        m_lastPreview = preview;
        if (!error.isEmpty())
            m_size->setText(tr("No se pudo codificar: %1").arg(error));
        else
            m_size->setText(tr("Tamaño del archivo: %1").arg(QLocale().formattedDataSize(bytes)));
    }
    if (!m_lastPreview.isNull()) {
        const int zoom = m_zoom->currentData().toInt();
        QSize s = m_lastPreview.size();
        if (zoom == 0)
            s = s.scaled(QSize(340, 260), Qt::KeepAspectRatio).boundedTo(m_lastPreview.size()).expandedTo(QSize(1, 1));
        else
            s = s * zoom / 100;
        QPixmap pm(s);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.fillRect(pm.rect(), QBrush(Raster::checkerboard(8)));
        p.setRenderHint(QPainter::SmoothPixmapTransform, zoom == 0);
        p.drawImage(pm.rect(), m_lastPreview);
        p.end();
        m_preview->setPixmap(pm);
    }
    if (m_pending && !m_debounce.isActive())
        start();
}
