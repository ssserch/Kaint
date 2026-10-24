#include "app/dialogs/SaveConfigDialog.h"

#include "app/Icons.h"
#include "app/Theme.h"
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
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImageReader>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>

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
            m_advanced = defaultAdvanced();
            m_advanced.quantization = initial.quantization;
            m_advanced.globalPalette = initial.gifGlobalPalette;
            m_advanced.stack = initial.gifStack;
            m_advanced.effects = initial.gifEffects;
            m_advancedButton = new QPushButton(Icons::theme({"configure", "settings-configure"}), tr("Configuración avanzada…"), this);
            m_advancedButton->setToolTip(tr("Fotogramas uno por uno (duración, omitir, copiar), paleta global, fundidos "
                                            "y fondo, como en ezgif."));
            connect(m_advancedButton, &QPushButton::clicked, this, &SaveConfigDialog::showAdvanced);
            form->addRow(m_advancedButton);
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
    connect(m_zoom, &QComboBox::currentIndexChanged, this, [this] { showFrame(m_previewIndex); });

    // Reproducir el GIF en la vista previa (con sus duraciones y repeticiones).
    m_play = new QToolButton(this);
    m_play->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_play->setAutoRaise(true);
    m_frameLabel = new QLabel(this);
    m_playTimer.setSingleShot(true);
    connect(&m_playTimer, &QTimer::timeout, this, &SaveConfigDialog::nextFrame);
    connect(m_play, &QToolButton::clicked, this, [this] { setPlaying(!m_playTimer.isActive()); });
    setPlaying(false);

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
    auto* playRow = new QHBoxLayout;
    playRow->addWidget(m_play);
    playRow->addWidget(m_frameLabel);
    playRow->addStretch();
    right->addLayout(playRow);
    m_play->setVisible(ImageIO::isAnimation(path));
    m_frameLabel->setVisible(ImageIO::isAnimation(path));
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
        if (m_advancedButton) {
            m_advanced = defaultAdvanced();
            schedule();
        }
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
    m_playTimer.stop();
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
    if (m_advancedButton) {
        o.quantization = m_advanced.quantization;
        o.gifGlobalPalette = m_advanced.globalPalette;
        o.gifStack = m_advanced.stack;
        o.gifEffects = m_advanced.effects;
        // Como en ezgif, el primero como fondo solo cuenta si no se apilan los fotogramas.
        if (m_advanced.stack)
            o.gifEffects.firstAsBackground = o.gifEffects.firstOnlyAsBackground = false;
    }
    if (!m_icoSizes.isEmpty()) {
        o.icoSizes.clear();
        for (QCheckBox* c : m_icoSizes)
            if (c->isChecked())
                o.icoSizes << c->property("kaintIcoSide").toInt();
    }
    return o;
}

QList<Gif::Frame> SaveConfigDialog::animationFrames() const
{
    if (!m_animation || !m_animation->isChecked())
        return {};
    QList<Gif::Frame> frames;
    for (const FrameEntry& e : m_advanced.entries)
        if (!e.skip && e.source >= 0 && e.source < m_frames.size())
            frames << Gif::Frame{m_frames[e.source].image, e.delayMs};
    // Con todos omitidos se guarda al menos el primero.
    if (frames.isEmpty() && !m_advanced.entries.isEmpty())
        frames << Gif::Frame{m_frames.value(m_advanced.entries.first().source).image, m_advanced.entries.first().delayMs};
    return frames;
}

SaveConfigDialog::Advanced SaveConfigDialog::defaultAdvanced() const
{
    Advanced a;
    for (int i = 0; i < m_frames.size(); ++i)
        a.entries << FrameEntry{i, m_frames[i].delayMs, false};
    return a;
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
    if (m_advancedButton)
        m_advancedButton->setEnabled(animated);
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
    const QList<Gif::Frame> frames = animationFrames();
    m_thread = std::thread([this, generation, image = m_image, path = m_path, opts = options(), frames]() mutable {
        QString error;
        QByteArray data;
        if (!frames.isEmpty()) {
            data = ImageIO::encodeAnimation(frames, opts, &error);
        } else {
            const QImage prepared = ImageIO::prepare(image, path, opts);
            data = ImageIO::encode(prepared, path, opts, &error);
        }
        // Se vuelve a leer lo codificado: así la vista previa es exactamente el archivo (en el
        // GIF, todos sus fotogramas para reproducirlo).
        QList<Gif::Frame> decodedFrames;
        QImage decoded;
        if (!data.isEmpty() && ImageIO::isAnimation(path)) {
            decodedFrames = Gif::decode(data).frames;
        } else if (!data.isEmpty() && ImageIO::isIcon(path)) {
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
        if (decodedFrames.isEmpty() && !decoded.isNull())
            decodedFrames << Gif::Frame{decoded, 100};
        const qint64 bytes = data.size();
        QMetaObject::invokeMethod(
            this, [this, decodedFrames, bytes, error, generation] { onEncoded(decodedFrames, bytes, error, generation); },
            Qt::QueuedConnection);
    });
}

void SaveConfigDialog::onEncoded(const QList<Gif::Frame>& preview, qint64 bytes, const QString& error, int generation)
{
    if (generation != m_generation)
        return;
    m_running = false;
    if (m_thread.joinable())
        m_thread.join();
    m_previewFrames = preview;
    if (!error.isEmpty())
        m_size->setText(tr("No se pudo codificar: %1").arg(error));
    else
        m_size->setText(tr("Tamaño del archivo: %1").arg(QLocale().formattedDataSize(bytes)));
    m_previewIndex = std::clamp(m_previewIndex, 0, std::max(0, int(m_previewFrames.size()) - 1));
    m_play->setEnabled(m_previewFrames.size() > 1);
    if (m_previewFrames.size() < 2)
        setPlaying(false);
    showFrame(m_previewIndex);
    if (m_pending && !m_debounce.isActive())
        start();
}

void SaveConfigDialog::showFrame(int index)
{
    m_frameLabel->setText(m_previewFrames.size() > 1
                              ? tr("Fotograma %1 de %2").arg(index + 1).arg(m_previewFrames.size())
                              : QString());
    if (index < 0 || index >= m_previewFrames.size())
        return;
    m_previewIndex = index;
    const QImage& image = m_previewFrames[index].image;
    const int zoom = m_zoom->currentData().toInt();
    QSize s = image.size();
    if (zoom == 0)
        s = s.scaled(QSize(340, 260), Qt::KeepAspectRatio).boundedTo(image.size()).expandedTo(QSize(1, 1));
    else
        s = s * zoom / 100;
    QPixmap pm(s);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.fillRect(pm.rect(), QBrush(Raster::checkerboard(8)));
    p.setRenderHint(QPainter::SmoothPixmapTransform, zoom == 0);
    p.drawImage(pm.rect(), image);
    p.end();
    m_preview->setPixmap(pm);
}

void SaveConfigDialog::setPlaying(bool playing)
{
    if (playing && m_previewFrames.size() > 1) {
        // Las repeticiones del archivo: sin fin, una vez o tantas más.
        const int loops = options().loops;
        m_playsLeft = loops == 0 ? -1 : (loops < 0 ? 1 : loops + 1);
        if (m_previewIndex >= m_previewFrames.size() - 1)
            showFrame(0);
        m_playTimer.start(std::max(Gif::kMinDelayMs, m_previewFrames[m_previewIndex].delayMs));
    } else {
        m_playTimer.stop();
    }
    const bool on = m_playTimer.isActive();
    m_play->setIcon(Icons::theme({on ? "media-playback-pause" : "media-playback-start"}));
    m_play->setText(on ? tr("Pausa") : tr("Reproducir"));
}

void SaveConfigDialog::nextFrame()
{
    if (m_previewFrames.size() < 2)
        return setPlaying(false);
    int next = m_previewIndex + 1;
    if (next >= m_previewFrames.size()) {
        if (m_playsLeft > 0 && --m_playsLeft == 0)
            return setPlaying(false);
        next = 0;
    }
    showFrame(next);
    m_playTimer.start(std::max(Gif::kMinDelayMs, m_previewFrames[next].delayMs));
}

void SaveConfigDialog::showAdvanced()
{
    // Como el editor de GIF de ezgif: los fotogramas uno por uno (duración, omitir, copiar),
    // rangos, opciones del GIF y efectos. Se edita una copia; Aceptar la aplica.
    setPlaying(false);
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Configuración avanzada del GIF"));
    dlg.setWindowIcon(Icons::theme({"configure", "settings-configure"}));
    Advanced adv = m_advanced;
    int allDelay = m_delay ? m_delay->value() : ImageIO::SaveOptions().frameDelay;
    const int loops = options().loops;
    QString plays = loops == 0 ? QString() : QString::number(loops < 0 ? 1 : loops + 1);

    QList<QPixmap> thumbs;
    for (const Gif::Frame& f : std::as_const(m_frames)) {
        QPixmap pm(f.image.size().scaled(QSize(96, 72), Qt::KeepAspectRatio).expandedTo(QSize(1, 1)));
        QPainter p(&pm);
        p.fillRect(pm.rect(), QBrush(Raster::checkerboard(6)));
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(pm.rect(), f.image);
        thumbs << pm;
    }
    // Omitir con el rojo de cerrar del tema y copiar con el acento, como los botones de ezgif.
    const Theme::Spec theme = Theme::active();
    auto buttonStyle = [](const QColor& c) {
        const QColor text = c.lightness() < 160 ? QColor(Qt::white) : QColor(Qt::black);
        return QStringLiteral("QPushButton { background: %1; color: %2; border: none; border-radius: 3px; padding: 3px 10px; }"
                              "QPushButton:hover { background: %3; }")
            .arg(c.name(), text.name(), c.lighter(115).name());
    };
    const QString skipStyle = buttonStyle(theme.panelClose);
    const QString enableStyle = buttonStyle(theme.accent);

    // Fotogramas
    auto* framesBox = new QWidget;
    auto* framesRow = new QHBoxLayout(framesBox);
    framesRow->setContentsMargins(0, 0, 0, 0);
    auto* framesScroll = new QScrollArea(&dlg);
    framesScroll->setWidget(framesBox);
    framesScroll->setWidgetResizable(true);
    framesScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    framesScroll->setFixedHeight(190);

    // Rangos
    auto* rangeBox = new QGroupBox(tr("Activar u omitir varios fotogramas"), &dlg);
    auto* rangeGrid = new QGridLayout(rangeBox);
    auto* from = new QSpinBox(rangeBox);
    auto* to = new QSpinBox(rangeBox);
    auto* every = new QSpinBox(rangeBox);
    auto* skipRange = new QPushButton(tr("Omitir rango"), rangeBox);
    auto* enableRange = new QPushButton(tr("Activar rango"), rangeBox);
    auto* skipEvery = new QPushButton(tr("Omitir cada N.º"), rangeBox);
    auto* enableEvery = new QPushButton(tr("Activar cada N.º"), rangeBox);
    skipRange->setStyleSheet(skipStyle);
    skipEvery->setStyleSheet(skipStyle);
    enableRange->setStyleSheet(enableStyle);
    enableEvery->setStyleSheet(enableStyle);
    from->setValue(1);
    to->setValue(5);
    every->setValue(2);
    rangeGrid->addWidget(new QLabel(tr("Desde:"), rangeBox), 0, 0);
    rangeGrid->addWidget(from, 0, 1);
    rangeGrid->addWidget(new QLabel(tr("Hasta:"), rangeBox), 0, 2);
    rangeGrid->addWidget(to, 0, 3);
    rangeGrid->addWidget(skipRange, 0, 4);
    rangeGrid->addWidget(enableRange, 0, 5);
    rangeGrid->addWidget(new QLabel(tr("Cada:"), rangeBox), 1, 0);
    rangeGrid->addWidget(every, 1, 1);
    rangeGrid->addWidget(skipEvery, 1, 4);
    rangeGrid->addWidget(enableEvery, 1, 5);
    rangeGrid->setColumnStretch(6, 1);

    // Opciones del GIF
    auto* gifBox = new QGroupBox(tr("Opciones del GIF"), &dlg);
    auto* gifGrid = new QGridLayout(gifBox);
    auto* delayAll = new QSpinBox(gifBox);
    delayAll->setRange(Gif::kMinDelayMs, 60000);
    delayAll->setSingleStep(10);
    delayAll->setSuffix(tr(" ms"));
    auto* fps = new QLabel(gifBox);
    auto hint = [gifBox](const QString& text) {
        auto* l = new QLabel(text, gifBox);
        l->setEnabled(false);
        l->setWordWrap(true);
        return l;
    };
    auto* loopsEdit = new QLineEdit(gifBox);
    loopsEdit->setValidator(new QIntValidator(1, 65536, loopsEdit));
    loopsEdit->setPlaceholderText(tr("Sin fin"));
    loopsEdit->setMaximumWidth(90);
    auto* globalPalette = new QCheckBox(tr("Usar una paleta global"), gifBox);
    auto* quantization = new QComboBox(gifBox);
    quantization->addItem(tr("Octree"), 1);
    quantization->addItem(tr("Corte a la mediana"), 0);
    quantization->setToolTip(tr("Cómo se eligen los 256 colores de la paleta. Octree es el de Paint.NET; el corte a la "
                                "mediana reparte mejor los degradados."));
    auto* delayRow = new QHBoxLayout;
    delayRow->addWidget(delayAll);
    delayRow->addWidget(fps);
    delayRow->addWidget(hint(tr("(cambiarla pone esta duración a todos los fotogramas)")), 1);
    gifGrid->addWidget(new QLabel(tr("Duración:"), gifBox), 0, 0);
    gifGrid->addLayout(delayRow, 0, 1);
    auto* loopsRow = new QHBoxLayout;
    loopsRow->addWidget(loopsEdit);
    loopsRow->addWidget(hint(tr("(vacío: se repite sin fin)")), 1);
    gifGrid->addWidget(new QLabel(tr("Reproducciones:"), gifBox), 1, 0);
    gifGrid->addLayout(loopsRow, 1, 1);
    auto* paletteRow = new QHBoxLayout;
    paletteRow->addWidget(globalPalette);
    paletteRow->addWidget(hint(tr("(los mismos colores en todos los fotogramas: el archivo ocupa menos)")), 1);
    gifGrid->addLayout(paletteRow, 2, 0, 1, 2);
    auto* quantRow = new QHBoxLayout;
    quantRow->addWidget(quantization);
    quantRow->addStretch();
    gifGrid->addWidget(new QLabel(tr("Cuantización:"), gifBox), 3, 0);
    gifGrid->addLayout(quantRow, 3, 1);

    // Efectos (las opciones de cada uno solo se ven con él marcado, como en ezgif)
    auto* effectsBox = new QGroupBox(tr("Efectos"), &dlg);
    auto* effects = new QVBoxLayout(effectsBox);
    auto indented = [effectsBox](QBoxLayout* into) {
        auto* frame = new QFrame(effectsBox);
        frame->setFrameShape(QFrame::StyledPanel);
        auto* l = new QVBoxLayout(frame);
        l->setContentsMargins(10, 6, 10, 6);
        auto* row = new QHBoxLayout;
        row->addSpacing(18);
        row->addWidget(frame, 1);
        into->addLayout(row);
        return std::pair{frame, l};
    };
    auto* crossfade = new QCheckBox(tr("Fundido entre fotogramas"), effectsBox);
    effects->addWidget(crossfade);
    auto [fadeFrame, fadeLayout] = indented(effects);
    auto* fadeDelay = new QSpinBox(fadeFrame);
    fadeDelay->setRange(Gif::kMinDelayMs, 2000);
    fadeDelay->setSingleStep(10);
    fadeDelay->setSuffix(tr(" ms"));
    auto* fadeCount = new QSpinBox(fadeFrame);
    fadeCount->setRange(2, 20);
    auto* fadeLast = new QCheckBox(tr("No fundir el último fotograma con el primero"), fadeFrame);
    auto* fadeForm = new QFormLayout;
    fadeForm->addRow(tr("Duración del fundido:"), fadeDelay);
    auto* countRow = new QHBoxLayout;
    countRow->addWidget(fadeCount);
    countRow->addWidget(new QLabel(QStringLiteral("(2-20)"), fadeFrame));
    countRow->addStretch();
    fadeForm->addRow(tr("Fotogramas del fundido:"), countRow);
    fadeLayout->addLayout(fadeForm);
    fadeLayout->addWidget(fadeLast);

    auto* noStack = new QCheckBox(tr("No apilar los fotogramas"), effectsBox);
    auto* noStackRow = new QHBoxLayout;
    noStackRow->addWidget(noStack);
    noStackRow->addWidget(hint(tr("(cada fotograma se borra al mostrar el siguiente; para imágenes con fondo "
                                  "transparente)")),
                          1);
    effects->addLayout(noStackRow);
    auto [bgFrame, bgLayout] = indented(effects);
    auto* firstBackground = new QCheckBox(tr("Usar el primer fotograma como fondo"), bgFrame);
    bgLayout->addWidget(firstBackground);
    auto [zeroFrame, zeroLayout] = indented(bgLayout);
    auto* firstOnly = new QCheckBox(tr("Duración 0 para el primer fotograma (no mostrarlo solo)"), zeroFrame);
    zeroLayout->addWidget(firstOnly);

    // Tarjeta de cada fotograma (se rehacen al omitir, copiar o cambiar la duración de todos).
    std::function<void()> rebuild = [&] {
        const int scroll = framesScroll->horizontalScrollBar()->value();
        while (QLayoutItem* item = framesRow->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        for (int i = 0; i < adv.entries.size(); ++i) {
            const FrameEntry& e = adv.entries[i];
            auto* card = new QFrame(framesBox);
            card->setFrameShape(QFrame::StyledPanel);
            auto* v = new QVBoxLayout(card);
            v->setContentsMargins(6, 4, 6, 6);
            v->setSpacing(4);
            auto* number = new QLabel(QString::number(i + 1), card);
            auto* thumb = new QLabel(card);
            thumb->setAlignment(Qt::AlignCenter);
            thumb->setFixedSize(100, 76);
            QPixmap pm = thumbs.value(e.source);
            if (e.skip && !pm.isNull()) {
                QPixmap faded(pm.size());
                faded.fill(Qt::transparent);
                QPainter p(&faded);
                p.setOpacity(0.25);
                p.drawPixmap(0, 0, pm);
                p.end();
                pm = faded;
            }
            thumb->setPixmap(pm);
            thumb->setToolTip(e.skip ? tr("Omitido: no va en el GIF") : QString());
            auto* delay = new QSpinBox(card);
            delay->setRange(Gif::kMinDelayMs, 60000);
            delay->setSingleStep(10);
            delay->setSuffix(tr(" ms"));
            delay->setValue(e.delayMs > 0 ? e.delayMs : allDelay);
            delay->setEnabled(!e.skip);
            connect(delay, &QSpinBox::valueChanged, &dlg, [&adv, i](int ms) { adv.entries[i].delayMs = ms; });
            auto* delayRow = new QHBoxLayout;
            delayRow->addWidget(new QLabel(tr("Duración:"), card));
            delayRow->addWidget(delay);
            auto* skip = new QPushButton(e.skip ? tr("Activar") : tr("Omitir"), card);
            skip->setStyleSheet(e.skip ? enableStyle : skipStyle);
            auto* copy = new QPushButton(tr("Copiar"), card);
            copy->setStyleSheet(enableStyle);
            copy->setToolTip(tr("Repite este fotograma justo después"));
            connect(skip, &QPushButton::clicked, &dlg, [&adv, &rebuild, &dlg, i] {
                adv.entries[i].skip = !adv.entries[i].skip;
                QTimer::singleShot(0, &dlg, [&rebuild] { rebuild(); });
            });
            connect(copy, &QPushButton::clicked, &dlg, [&adv, &rebuild, &dlg, i] {
                FrameEntry twin = adv.entries[i];
                twin.skip = false;
                adv.entries.insert(i + 1, twin);
                QTimer::singleShot(0, &dlg, [&rebuild] { rebuild(); });
            });
            auto* buttons = new QHBoxLayout;
            buttons->addWidget(skip);
            buttons->addWidget(copy);
            buttons->addStretch();
            v->addWidget(number);
            v->addWidget(thumb, 0, Qt::AlignHCenter);
            v->addLayout(delayRow);
            v->addLayout(buttons);
            framesRow->addWidget(card);
        }
        framesRow->addStretch();
        const int n = std::max(1, int(adv.entries.size()));
        from->setRange(1, n);
        to->setRange(1, n);
        every->setRange(2, std::max(2, n));
        QTimer::singleShot(0, framesScroll, [framesScroll, scroll] { framesScroll->horizontalScrollBar()->setValue(scroll); });
    };
    auto updateEffects = [&] {
        fadeFrame->setVisible(crossfade->isChecked());
        bgFrame->setVisible(noStack->isChecked());
        zeroFrame->setVisible(noStack->isChecked() && firstBackground->isChecked());
    };
    auto updateFps = [&] { fps->setText(tr("(%1 FPS)").arg(QLocale().toString(1000.0 / allDelay, 'f', 2))); };
    // Pone en los controles lo de 'adv' (al abrir y con Predeterminado).
    auto load = [&] {
        const QSignalBlocker b(delayAll);
        delayAll->setValue(allDelay);
        loopsEdit->setText(plays);
        globalPalette->setChecked(adv.globalPalette);
        quantization->setCurrentIndex(std::max(0, quantization->findData(adv.quantization)));
        crossfade->setChecked(adv.effects.crossfade);
        fadeDelay->setValue(adv.effects.fadeDelayMs);
        fadeCount->setValue(std::clamp(adv.effects.fadeFrames, 2, 20));
        fadeLast->setChecked(!adv.effects.fadeLastToFirst);
        noStack->setChecked(!adv.stack);
        firstBackground->setChecked(adv.effects.firstAsBackground);
        firstOnly->setChecked(adv.effects.firstOnlyAsBackground);
        updateEffects();
        updateFps();
        rebuild();
    };

    auto mark = [&](bool skipIt, auto pick) {
        for (int i = 0; i < adv.entries.size(); ++i)
            if (pick(i))
                adv.entries[i].skip = skipIt;
        rebuild();
    };
    auto inRange = [&](int i) { return i + 1 >= std::min(from->value(), to->value()) && i + 1 <= std::max(from->value(), to->value()); };
    auto isNth = [&](int i) { return (i + 1) % every->value() == 0; };
    connect(skipRange, &QPushButton::clicked, &dlg, [&] { mark(true, inRange); });
    connect(enableRange, &QPushButton::clicked, &dlg, [&] { mark(false, inRange); });
    connect(skipEvery, &QPushButton::clicked, &dlg, [&] { mark(true, isNth); });
    connect(enableEvery, &QPushButton::clicked, &dlg, [&] { mark(false, isNth); });
    connect(delayAll, &QSpinBox::valueChanged, &dlg, [&](int ms) {
        allDelay = ms;
        for (FrameEntry& e : adv.entries)
            e.delayMs = ms;
        updateFps();
        rebuild();
    });
    connect(loopsEdit, &QLineEdit::textChanged, &dlg, [&](const QString& text) { plays = text; });
    connect(globalPalette, &QCheckBox::toggled, &dlg, [&](bool on) { adv.globalPalette = on; });
    connect(quantization, &QComboBox::currentIndexChanged, &dlg, [&] { adv.quantization = quantization->currentData().toInt(); });
    connect(crossfade, &QCheckBox::toggled, &dlg, [&](bool on) {
        adv.effects.crossfade = on;
        updateEffects();
    });
    connect(fadeDelay, &QSpinBox::valueChanged, &dlg, [&](int ms) { adv.effects.fadeDelayMs = ms; });
    connect(fadeCount, &QSpinBox::valueChanged, &dlg, [&](int n) { adv.effects.fadeFrames = n; });
    connect(fadeLast, &QCheckBox::toggled, &dlg, [&](bool on) { adv.effects.fadeLastToFirst = !on; });
    connect(noStack, &QCheckBox::toggled, &dlg, [&](bool on) {
        adv.stack = !on;
        updateEffects();
    });
    connect(firstBackground, &QCheckBox::toggled, &dlg, [&](bool on) {
        adv.effects.firstAsBackground = on;
        updateEffects();
    });
    connect(firstOnly, &QCheckBox::toggled, &dlg, [&](bool on) { adv.effects.firstOnlyAsBackground = on; });

    auto* box = okCancelButtons(&dlg);
    auto* defaults = box->addButton(tr("Predeterminado"), QDialogButtonBox::ResetRole);
    connect(defaults, &QPushButton::clicked, &dlg, [&] {
        adv = defaultAdvanced();
        allDelay = ImageIO::SaveOptions().frameDelay;
        plays.clear();
        load();
    });

    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    layout->addWidget(framesScroll);
    layout->addWidget(rangeBox);
    layout->addWidget(gifBox);
    layout->addWidget(effectsBox);
    layout->addStretch();
    auto* page = new QScrollArea(&dlg);
    page->setWidget(content);
    page->setWidgetResizable(true);
    page->setFrameShape(QFrame::NoFrame);
    auto* outer = new QVBoxLayout(&dlg);
    outer->addWidget(page, 1);
    outer->addWidget(box);
    dlg.resize(760, 720);
    load();
    if (dlg.exec() != QDialog::Accepted)
        return;

    m_advanced = adv;
    if (m_delay)
        m_delay->setValue(allDelay);
    if (m_loopForever && m_loops) {
        const int count = plays.toInt();
        m_loopForever->setChecked(plays.isEmpty());
        if (!plays.isEmpty())
            m_loops->setValue(std::max(0, count - 1));
    }
    updateControls();
    schedule();
}
