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
    m_previewPane = new EncodedPreview(path, this);
    connect(m_previewPane, &EncodedPreview::statusChanged, m_size, &QLabel::setText);

    auto* side = new QVBoxLayout;
    side->addLayout(form);
    side->addStretch();
    side->addWidget(m_size);

    auto* top = new QHBoxLayout;
    top->addLayout(side);
    top->addWidget(m_previewPane, 1);

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
    updateControls();
    schedule();
}

SaveConfigDialog::~SaveConfigDialog() = default;

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
    if (m_advancedButton)
        o = withAdvanced(o, m_advanced);
    if (!m_icoSizes.isEmpty()) {
        o.icoSizes.clear();
        for (QCheckBox* c : m_icoSizes)
            if (c->isChecked())
                o.icoSizes << c->property("kaintIcoSide").toInt();
    }
    return o;
}

ImageIO::SaveOptions SaveConfigDialog::withAdvanced(ImageIO::SaveOptions o, const Advanced& a)
{
    o.quantization = a.quantization;
    o.gifGlobalPalette = a.globalPalette;
    o.gifStack = a.stack;
    o.gifEffects = a.effects;
    // Como en ezgif, el primero como fondo solo cuenta si no se apilan los fotogramas.
    if (a.stack)
        o.gifEffects.firstAsBackground = o.gifEffects.firstOnlyAsBackground = false;
    return o;
}

QList<Gif::Frame> SaveConfigDialog::framesOf(const Advanced& a) const
{
    QList<Gif::Frame> frames;
    for (const FrameEntry& e : a.entries)
        if (!e.skip && e.source >= 0 && e.source < m_frames.size())
            frames << Gif::Frame{m_frames[e.source].image, e.delayMs};
    // Con todos omitidos se guarda al menos el primero.
    if (frames.isEmpty() && !a.entries.isEmpty())
        frames << Gif::Frame{m_frames.value(a.entries.first().source).image, a.entries.first().delayMs};
    return frames;
}

QList<Gif::Frame> SaveConfigDialog::animationFrames() const
{
    if (!m_animation || !m_animation->isChecked())
        return {};
    return framesOf(m_advanced);
}

EncodedPreview::Encoder SaveConfigDialog::encoder(const ImageIO::SaveOptions& options, const QList<Gif::Frame>& frames) const
{
    return [image = m_image, path = m_path, options, frames](QString* error) {
        if (!frames.isEmpty())
            return ImageIO::encodeAnimation(frames, options, error);
        return ImageIO::encode(ImageIO::prepare(image, path, options), path, options, error);
    };
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
    const ImageIO::SaveOptions o = options();
    m_previewPane->request(encoder(o, animationFrames()), o.loops);
}

void SaveConfigDialog::showAdvanced()
{
    // Como el editor de GIF de ezgif: los fotogramas uno por uno (duración, omitir, copiar),
    // rangos, opciones del GIF y efectos, con su vista previa. Se edita una copia; Aceptar la
    // aplica.
    m_previewPane->setPlaying(false);
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

    // Vista previa con lo que hay en la ventana (aún sin aplicar), para ir viendo cada cambio.
    auto* preview = new EncodedPreview(m_path, &dlg);
    auto* status = new QLabel(&dlg);
    connect(preview, &EncodedPreview::statusChanged, status, &QLabel::setText);
    auto refresh = [&] {
        ImageIO::SaveOptions o = withAdvanced(options(), adv);
        o.frameDelay = allDelay;
        const int count = plays.toInt();
        o.loops = plays.isEmpty() ? 0 : (count <= 1 ? -1 : count - 1);
        preview->request(encoder(o, framesOf(adv)), o.loops);
    };

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
            connect(delay, &QSpinBox::valueChanged, &dlg, [&adv, &refresh, i](int ms) {
                adv.entries[i].delayMs = ms;
                refresh();
            });
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
        refresh();
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
    connect(loopsEdit, &QLineEdit::textChanged, &dlg, [&](const QString& text) {
        plays = text;
        refresh();
    });
    connect(globalPalette, &QCheckBox::toggled, &dlg, [&](bool on) {
        adv.globalPalette = on;
        refresh();
    });
    connect(quantization, &QComboBox::currentIndexChanged, &dlg, [&] {
        adv.quantization = quantization->currentData().toInt();
        refresh();
    });
    connect(crossfade, &QCheckBox::toggled, &dlg, [&](bool on) {
        adv.effects.crossfade = on;
        updateEffects();
        refresh();
    });
    connect(fadeDelay, &QSpinBox::valueChanged, &dlg, [&](int ms) {
        adv.effects.fadeDelayMs = ms;
        refresh();
    });
    connect(fadeCount, &QSpinBox::valueChanged, &dlg, [&](int n) {
        adv.effects.fadeFrames = n;
        refresh();
    });
    connect(fadeLast, &QCheckBox::toggled, &dlg, [&](bool on) {
        adv.effects.fadeLastToFirst = !on;
        refresh();
    });
    connect(noStack, &QCheckBox::toggled, &dlg, [&](bool on) {
        adv.stack = !on;
        updateEffects();
        refresh();
    });
    connect(firstBackground, &QCheckBox::toggled, &dlg, [&](bool on) {
        adv.effects.firstAsBackground = on;
        updateEffects();
        refresh();
    });
    connect(firstOnly, &QCheckBox::toggled, &dlg, [&](bool on) {
        adv.effects.firstOnlyAsBackground = on;
        refresh();
    });

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
    auto* side = new QVBoxLayout;
    side->addWidget(preview, 1);
    side->addWidget(status);
    auto* columns = new QHBoxLayout;
    columns->addWidget(page, 1);
    columns->addLayout(side);
    auto* outer = new QVBoxLayout(&dlg);
    outer->addLayout(columns, 1);
    outer->addWidget(box);
    dlg.resize(1160, 720);
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
