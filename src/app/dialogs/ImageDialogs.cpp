#include "app/dialogs/ImageDialogs.h"

#include "app/Icons.h"
#include "app/dialogs/DialogButtons.h"
#include "app/widgets/UnitField.h"
#include "core/Gif.h"
#include "core/Raster.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>
#include <QTreeWidget>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

QDialogButtonBox* okCancel(QDialog* dlg)
{
    return okCancelButtons(dlg);
}

constexpr int kMaxSide = 65535;

QString memoryText(QSize s, int layers = 1)
{
    const double mb = double(s.width()) * s.height() * 4 * layers / (1024.0 * 1024.0);
    const QLocale locale;
    return mb >= 1024 ? QObject::tr("%1 GB").arg(locale.toString(mb / 1024.0, 'f', 2))
                      : QObject::tr("%1 MB").arg(locale.toString(mb, 'f', 1));
}

// Una flecha negra hacia (dx, dy), como las del anclaje de Paint.NET (sobre sus casillas gris
// claro): asta fina y punta triangular, en 15 px (y a 2x y 3x para pantallas con escala).
QIcon anchorArrow(int dx, int dy)
{
    constexpr int kSide = 15;
    QIcon icon;
    for (const qreal dpr : {1.0, 2.0, 3.0}) {
        QPixmap pm(QSize(kSide, kSide) * dpr);
        pm.setDevicePixelRatio(dpr);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        QPointF dir(dx, dy);
        dir /= std::hypot(dir.x(), dir.y());
        const QPointF perp(-dir.y(), dir.x());
        const QPointF center(kSide / 2.0, kSide / 2.0);
        const QPointF tip = center + dir * 6.0, tail = center - dir * 6.0;
        const QPointF base = tip - dir * 5.0;
        p.setPen(QPen(Qt::black, 1.4, Qt::SolidLine, Qt::FlatCap));
        p.drawLine(tail, base + dir * 0.5);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::black);
        p.drawPolygon(QPolygonF{tip, base + perp * 3.5, base - perp * 3.5});
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

// La casilla del ancla, como en Paint.NET: una imagen (marco gris, fondo blanco, una nube gris y
// montañas azules) en 16 px.
QIcon anchorPicture()
{
    constexpr int kSide = 16;
    QIcon icon;
    for (const qreal dpr : {1.0, 2.0, 3.0}) {
        QPixmap pm(QSize(kSide, kSide) * dpr);
        pm.setDevicePixelRatio(dpr);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(0x6d, 0x6e, 0x71), 1));
        p.setBrush(Qt::white);
        p.drawRect(QRectF(0.5, 0.5, kSide - 1, kSide - 1));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xc4, 0xc6, 0xc8));
        p.drawEllipse(QPointF(4.6, 5.0), 1.7, 1.5);
        p.drawEllipse(QPointF(6.6, 4.2), 2.1, 2.0);
        p.drawRoundedRect(QRectF(3.0, 4.6, 5.8, 2.2), 1.0, 1.0);
        p.setBrush(QColor(0x3e, 0x79, 0xb4));
        p.drawPolygon(QPolygonF{QPointF(1.5, 14.5), QPointF(4.0, 11.0), QPointF(6.6, 7.4), QPointF(8.4, 9.6),
                                QPointF(10.6, 6.4), QPointF(14.5, 14.5)});
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

} // namespace

// ---------------------------------------------------------------------------

SizeEditor::SizeEditor(QSize original, double dpi, bool allowPercent, QWidget* parent)
    : QWidget(parent)
    , m_original(original)
    , m_aspect(original.height() > 0 ? double(original.width()) / original.height() : 1.0)
    , m_dpi(dpi)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    if (allowPercent) {
        auto* row = new QHBoxLayout;
        m_byPercent = new QRadioButton(tr("Por porcentaje:"), this);
        m_byAbsolute = new QRadioButton(tr("Por tamaño absoluto"), this);
        m_percent = new QSpinBox(this);
        m_percent->setRange(1, 10000);
        m_percent->setValue(100);
        m_byPercent->setChecked(true);
        row->addWidget(m_byPercent);
        row->addWidget(withUnit(m_percent, QStringLiteral("%"), QString(), this));
        row->addStretch();
        layout->addLayout(row);
        layout->addWidget(m_byAbsolute);
        connect(m_byPercent, &QRadioButton::toggled, this, &SizeEditor::updateMode);
        connect(m_percent, &QSpinBox::valueChanged, this, &SizeEditor::onPercent);
    }
    m_keepAspect = new QCheckBox(tr("Mantener la relación de aspecto"), this);
    m_keepAspect->setChecked(true);
    layout->addWidget(m_keepAspect);

    // Como en Paint.NET: cada número en su campo y la unidad a la derecha, como texto o como
    // desplegable (la resolución y la anchura de impresión, que van a la par).
    auto unitLabel = [this](const QString& text) {
        auto* label = new QLabel(text, this);
        return label;
    };
    auto* pixelBox = new QGroupBox(tr("Tamaño en píxeles"), this);
    auto* pixelGrid = new QGridLayout(pixelBox);
    m_width = new QSpinBox(this);
    m_height = new QSpinBox(this);
    for (QSpinBox* sb : {m_width, m_height})
        sb->setRange(1, kMaxSide);
    m_width->setValue(original.width());
    m_height->setValue(original.height());
    m_resolution = new QDoubleSpinBox(this);
    m_resolution->setRange(0.01, 99999);
    m_resolution->setDecimals(2);
    m_resolutionUnit = new QComboBox(this);
    m_resolutionUnit->addItem(tr("píxeles/pulgada"));
    m_resolutionUnit->addItem(tr("píxeles/cm"));
    // Píxeles/centímetro por defecto; se recuerda la última unidad elegida.
    m_resolutionUnit->setCurrentIndex(QSettings().value(QStringLiteral("dialogs/resolutionUnit"), 1).toInt() == 0 ? 0 : 1);
    m_resolution->setValue(m_resolutionUnit->currentIndex() == 1 ? dpi / 2.54 : dpi);
    pixelGrid->addWidget(new QLabel(tr("Anchura:"), this), 0, 0);
    pixelGrid->addWidget(m_width, 0, 1);
    pixelGrid->addWidget(unitLabel(tr("píxeles")), 0, 2);
    pixelGrid->addWidget(new QLabel(tr("Altura:"), this), 1, 0);
    pixelGrid->addWidget(m_height, 1, 1);
    pixelGrid->addWidget(unitLabel(tr("píxeles")), 1, 2);
    pixelGrid->addWidget(new QLabel(tr("Resolución:"), this), 2, 0);
    pixelGrid->addWidget(m_resolution, 2, 1);
    pixelGrid->addWidget(m_resolutionUnit, 2, 2);
    pixelGrid->setColumnStretch(3, 1);
    layout->addWidget(pixelBox);

    auto* printBox = new QGroupBox(tr("Tamaño de la impresión"), this);
    auto* printGrid = new QGridLayout(printBox);
    m_printWidth = new QDoubleSpinBox(this);
    m_printHeight = new QDoubleSpinBox(this);
    for (QDoubleSpinBox* sb : {m_printWidth, m_printHeight}) {
        sb->setRange(0.01, 999999);
        sb->setDecimals(2);
    }
    m_printUnit = new QComboBox(this);
    m_printUnit->addItems({tr("pulgadas"), tr("centímetros")});
    m_printUnit->setCurrentIndex(m_resolutionUnit->currentIndex());
    m_printHeightUnit = unitLabel(QString());
    printGrid->addWidget(new QLabel(tr("Anchura:"), this), 0, 0);
    printGrid->addWidget(m_printWidth, 0, 1);
    printGrid->addWidget(m_printUnit, 0, 2);
    printGrid->addWidget(new QLabel(tr("Altura:"), this), 1, 0);
    printGrid->addWidget(m_printHeight, 1, 1);
    printGrid->addWidget(m_printHeightUnit, 1, 2);
    printGrid->setColumnStretch(3, 1);
    layout->addWidget(printBox);
    // Las dos columnas de campos con el mismo ancho, alineadas entre los dos grupos.
    const int fieldWidth = std::max(m_resolution->sizeHint().width(), m_printWidth->sizeHint().width()) + 12;
    for (QWidget* field : std::initializer_list<QWidget*>{m_width, m_height, m_resolution, m_printWidth, m_printHeight})
        field->setFixedWidth(fieldWidth);
    const int labelWidth = std::max({QLabel(tr("Resolución:")).sizeHint().width(), QLabel(tr("Anchura:")).sizeHint().width(),
                                     QLabel(tr("Altura:")).sizeHint().width()});
    pixelGrid->setColumnMinimumWidth(0, labelWidth);
    printGrid->setColumnMinimumWidth(0, labelWidth);
    connect(m_printUnit, &QComboBox::activated, m_resolutionUnit, &QComboBox::setCurrentIndex);

    connect(m_width, &QSpinBox::valueChanged, this, [this] { onPixels(true); });
    connect(m_height, &QSpinBox::valueChanged, this, [this] { onPixels(false); });
    connect(m_printWidth, &QDoubleSpinBox::valueChanged, this, [this] { onPrint(true); });
    connect(m_printHeight, &QDoubleSpinBox::valueChanged, this, [this] { onPrint(false); });
    connect(m_resolution, &QDoubleSpinBox::valueChanged, this, &SizeEditor::onResolution);
    connect(m_resolutionUnit, &QComboBox::currentIndexChanged, this, [this](int unit) {
        m_updating = true;
        m_resolution->setValue(unit == 1 ? m_dpi / 2.54 : m_dpi);
        m_updating = false;
        QSettings().setValue(QStringLiteral("dialogs/resolutionUnit"), unit);
        m_printUnit->setCurrentIndex(unit);
        syncPrint();
    });
    connect(m_keepAspect, &QCheckBox::toggled, this, [this](bool on) {
        if (on)
            onPixels(true);
    });
    syncPrint();
    updateMode();
}

double SizeEditor::unitPixels() const
{
    return m_resolutionUnit->currentIndex() == 1 ? m_dpi / 2.54 : m_dpi;
}

double SizeEditor::dpi() const
{
    return m_dpi;
}

QSize SizeEditor::value() const
{
    return QSize(m_width->value(), m_height->value());
}

void SizeEditor::focusWidth()
{
    m_width->setFocus(Qt::TabFocusReason);
    m_width->selectAll();
}

void SizeEditor::setKeepAspect(bool on)
{
    m_keepAspect->setChecked(on);
}

void SizeEditor::setValue(QSize size)
{
    m_updating = true;
    m_width->setValue(size.width());
    m_height->setValue(size.height());
    m_updating = false;
    syncPrint();
    emit changed();
}

void SizeEditor::syncPrint()
{
    const bool was = m_updating;
    m_updating = true;
    m_printHeightUnit->setText(m_printUnit->currentText());
    m_printWidth->setValue(m_width->value() / unitPixels());
    m_printHeight->setValue(m_height->value() / unitPixels());
    m_updating = was;
}

void SizeEditor::updateMode()
{
    const bool percent = m_byPercent && m_byPercent->isChecked();
    if (m_percent)
        m_percent->parentWidget()->setEnabled(percent);
    for (QWidget* w : std::initializer_list<QWidget*>{m_width, m_height, m_printWidth, m_printHeight, m_keepAspect})
        w->setEnabled(!percent);
    if (percent)
        onPercent();
}

void SizeEditor::onPercent()
{
    if (!m_percent || !m_byPercent->isChecked())
        return;
    const double f = m_percent->value() / 100.0;
    setValue(QSize(std::clamp(int(std::lround(m_original.width() * f)), 1, kMaxSide),
                   std::clamp(int(std::lround(m_original.height() * f)), 1, kMaxSide)));
}

void SizeEditor::onPixels(bool widthChanged)
{
    if (m_updating)
        return;
    m_updating = true;
    if (m_keepAspect->isChecked()) {
        if (widthChanged)
            m_height->setValue(std::clamp(int(std::lround(m_width->value() / m_aspect)), 1, kMaxSide));
        else
            m_width->setValue(std::clamp(int(std::lround(m_height->value() * m_aspect)), 1, kMaxSide));
    }
    m_updating = false;
    syncPrint();
    emit changed();
}

void SizeEditor::onPrint(bool widthChanged)
{
    if (m_updating)
        return;
    m_updating = true;
    if (widthChanged)
        m_width->setValue(std::clamp(int(std::lround(m_printWidth->value() * unitPixels())), 1, kMaxSide));
    else
        m_height->setValue(std::clamp(int(std::lround(m_printHeight->value() * unitPixels())), 1, kMaxSide));
    m_updating = false;
    onPixels(widthChanged);
}

void SizeEditor::onResolution()
{
    if (m_updating)
        return;
    // La resolución exacta solo cambia cuando se escribe otra (el campo la muestra redondeada).
    m_dpi = m_resolutionUnit->currentIndex() == 1 ? m_resolution->value() * 2.54 : m_resolution->value();
    if (m_keepAspect->isChecked() && !(m_byPercent && m_byPercent->isChecked())) {
        const double pw = m_printWidth->value();
        m_updating = true;
        m_width->setValue(std::clamp(int(std::lround(pw * unitPixels())), 1, kMaxSide));
        m_updating = false;
        onPixels(true);
    } else {
        syncPrint();
        emit changed();
    }
}

// ---------------------------------------------------------------------------

NewImageDialog::NewImageDialog(QSize initial, double dpi, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Imagen nueva"));
    setWindowIcon(Icons::theme({"document-new"}));
    m_size = new SizeEditor(initial, dpi, false, this);
    m_size->setKeepAspect(false);
    m_background = new QComboBox(this);
    m_background->addItems({tr("Blanco"), tr("Color secundario"), tr("Transparente")});
    m_memory = new QLabel(this);

    // Como en Paint.NET: el tamaño en memoria arriba; el fondo, abajo.
    auto* form = new QFormLayout;
    form->addRow(tr("Fondo:"), m_background);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_memory);
    layout->addWidget(m_size);
    layout->addLayout(form);
    layout->addWidget(okCancel(this));
    connect(m_size, &SizeEditor::changed, this, &NewImageDialog::updateMemory);
    updateMemory();
    // Como en Paint.NET: el ancho, elegido, listo para escribir el tamaño.
    QTimer::singleShot(0, this, [this] { m_size->focusWidth(); });
}

void NewImageDialog::updateMemory() { m_memory->setText(tr("Nuevo tamaño: %1").arg(memoryText(m_size->value()))); }
QSize NewImageDialog::imageSize() const { return m_size->value(); }
double NewImageDialog::dpi() const { return m_size->dpi(); }

QColor NewImageDialog::background(const QColor& secondary) const
{
    switch (m_background->currentIndex()) {
    case 1: return secondary;
    case 2: return Qt::transparent;
    default: return Qt::white;
    }
}

// ---------------------------------------------------------------------------

ResizeImageDialog::ResizeImageDialog(QSize current, double dpi, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Cambiar el tamaño"));
    setWindowIcon(Icons::theme({"transform-scale"}));
    m_size = new SizeEditor(current, dpi, true, this);
    m_resampling = new QComboBox(this);
    for (Resample::Method m : Resample::all())
        m_resampling->addItem(Resample::name(m), int(m));
    m_resampling->setCurrentIndex(m_resampling->findData(int(Resample::Method::Adaptive)));
    m_gamma = new QCheckBox(tr("Corrección gamma (en luz lineal)"), this);
    m_gamma->setToolTip(tr("Calcula el remuestreo en luz lineal: los detalles finos conservan su brillo"));
    m_memory = new QLabel(this);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    form->addRow(tr("Remuestreo:"), m_resampling);
    form->addRow(QString(), m_gamma);
    layout->addLayout(form);
    layout->addWidget(m_size);
    auto* info = new QFormLayout;
    info->addRow(tr("Tamaño actual: %1 × %2 · nuevo:").arg(current.width()).arg(current.height()), m_memory);
    layout->addLayout(info);
    layout->addWidget(okCancel(this));
    connect(m_size, &SizeEditor::changed, this, &ResizeImageDialog::updateMemory);
    updateMemory();
}

void ResizeImageDialog::updateMemory()
{
    const QSize s = m_size->value();
    m_memory->setText(tr("%1 × %2 (%3 por capa)").arg(s.width()).arg(s.height()).arg(memoryText(s)));
}

QSize ResizeImageDialog::imageSize() const { return m_size->value(); }
double ResizeImageDialog::dpi() const { return m_size->dpi(); }
Resample::Method ResizeImageDialog::method() const { return Resample::Method(m_resampling->currentData().toInt()); }
bool ResizeImageDialog::gammaCorrect() const { return m_gamma->isChecked(); }

// ---------------------------------------------------------------------------

CanvasSizeDialog::CanvasSizeDialog(QSize current, double dpi, QWidget* parent)
    : QDialog(parent)
    , m_anchor(new QButtonGroup(this))
{
    setWindowTitle(tr("Tamaño del lienzo"));
    setWindowIcon(Icons::theme({"transform-crop-and-resize"}));
    m_size = new SizeEditor(current, dpi, true, this);
    m_size->setKeepAspect(false);

    // Opciones, como en Paint.NET 5: el ancla en un desplegable y, debajo, la rejilla de casillas
    // (botones de Windows: gris claro, la del ancla en azul) con flechas hacia donde crece el
    // lienzo; y con qué se rellena lo nuevo.
    auto* options = new QGroupBox(tr("Opciones"), this);
    auto* optionsGrid = new QGridLayout(options);
    m_anchorList = new QComboBox(options);
    m_anchorList->addItems({tr("Arriba a la izquierda"), tr("Arriba"), tr("Arriba a la derecha"), tr("Izquierda"), tr("Centro"),
                            tr("Derecha"), tr("Abajo a la izquierda"), tr("Abajo"), tr("Abajo a la derecha")});
    auto* cells = new QWidget(options);
    cells->setStyleSheet(QStringLiteral(
        "QToolButton { background: #e1e1e1; border: 1px solid #adadad; border-radius: 0px; padding: 0px; }"
        "QToolButton:hover { background: #e5f1fb; border-color: #0078d7; }"
        "QToolButton:pressed, QToolButton:checked { background: #cce4f7; border-color: #005499; }"));
    auto* grid = new QGridLayout(cells);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(2);
    const Qt::Alignment rows[] = {Qt::AlignTop, Qt::AlignVCenter, Qt::AlignBottom};
    const Qt::Alignment cols[] = {Qt::AlignLeft, Qt::AlignHCenter, Qt::AlignRight};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            auto* b = new QToolButton(cells);
            b->setCheckable(true);
            b->setFixedSize(25, 25);
            b->setIconSize(QSize(16, 16));
            b->setToolTip(m_anchorList->itemText(r * 3 + c));
            m_anchor->addButton(b, int(rows[r] | cols[c]));
            grid->addWidget(b, r, c);
        }
    }
    m_anchor->buttons().at(4)->setChecked(true);
    m_anchorList->setCurrentIndex(4);
    m_fill = new QComboBox(options);
    m_fill->addItems({tr("Transparente"), tr("Color principal"), tr("Color secundario")});
    // Se recuerda el último; la primera vez, el de siempre (el color secundario).
    m_fill->setCurrentIndex(std::clamp(QSettings().value(QStringLiteral("dialogs/canvasFill"), int(Fill::Secondary)).toInt(), 0, 2));
    m_fill->setToolTip(tr("Con qué se rellena lo que se añade al lienzo: con un color, solo en la capa de abajo (las demás "
                          "quedan transparentes)"));
    optionsGrid->addWidget(new QLabel(tr("Ancla:"), options), 0, 0);
    optionsGrid->addWidget(m_anchorList, 0, 1);
    optionsGrid->addWidget(cells, 1, 1, Qt::AlignHCenter);
    optionsGrid->addWidget(new QLabel(tr("Relleno:"), options), 2, 0);
    optionsGrid->addWidget(m_fill, 2, 1);
    optionsGrid->setColumnStretch(1, 1);
    // La lista y la rejilla, a la par.
    connect(m_anchorList, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (QAbstractButton* b = m_anchor->buttons().value(i))
            b->setChecked(true);
    });
    connect(m_anchor, &QButtonGroup::idToggled, this, [this](int, bool on) {
        if (!on)
            return;
        m_anchorList->setCurrentIndex(int(m_anchor->buttons().indexOf(m_anchor->checkedButton())));
        updateArrows();
    });
    connect(m_fill, &QComboBox::currentIndexChanged, this,
            [](int i) { QSettings().setValue(QStringLiteral("dialogs/canvasFill"), i); });
    m_memory = new QLabel(this);
    connect(m_size, &SizeEditor::changed, this, [this] { m_memory->setText(memoryText(m_size->value())); });
    m_memory->setText(memoryText(current));

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("Tamaño actual: %1 × %2 píxeles").arg(current.width()).arg(current.height()), this));
    layout->addWidget(m_size);
    layout->addWidget(options);
    auto* info = new QFormLayout;
    info->addRow(tr("Memoria por capa:"), m_memory);
    layout->addLayout(info);
    layout->addWidget(okCancel(this));
    updateArrows();
}

void CanvasSizeDialog::updateArrows()
{
    // Como en Paint.NET: el ancla (la imagen) en su casilla y, en las de alrededor, flechas negras
    // que apuntan desde ella hacia donde crece el lienzo; las demás, vacías.
    const QList<QAbstractButton*> buttons = m_anchor->buttons();
    const int checked = int(buttons.indexOf(m_anchor->checkedButton()));
    const int cr = checked / 3, cc = checked % 3;
    const QIcon anchor = anchorPicture();
    for (int i = 0; i < buttons.size(); ++i) {
        const int dr = i / 3 - cr, dc = i % 3 - cc;
        QIcon icon;
        if (dr == 0 && dc == 0)
            icon = anchor;
        else if (std::abs(dr) <= 1 && std::abs(dc) <= 1)
            icon = anchorArrow(dc, dr);
        buttons[i]->setIcon(icon);
    }
}

QSize CanvasSizeDialog::canvasSize() const { return m_size->value(); }
double CanvasSizeDialog::dpi() const { return m_size->dpi(); }
Qt::Alignment CanvasSizeDialog::anchor() const { return Qt::Alignment(m_anchor->checkedId()); }
CanvasSizeDialog::Fill CanvasSizeDialog::fill() const { return Fill(m_fill->currentIndex()); }

// ---------------------------------------------------------------------------

LayerPropertiesDialog::LayerPropertiesDialog(const LayerProps& props, std::function<void(const LayerProps&)> preview,
                                             QWidget* parent)
    : QDialog(parent)
    , m_preview(std::move(preview))
    , m_base(props)
{
    setWindowTitle(tr("Propiedades de la capa"));
    setWindowIcon(Icons::theme({"layer-properties", "document-properties"}));
    m_name = new QLineEdit(props.name, this);
    m_visible = new QCheckBox(tr("Visible"), this);
    m_visible->setChecked(props.visible);
    m_alphaLock = new QCheckBox(tr("Bloquear la transparencia"), this);
    m_alphaLock->setChecked(props.alphaLock);
    m_alphaLock->setToolTip(tr("Al pintar, solo cambia el color de lo que ya hay; lo transparente sigue transparente."));
    m_blend = new QComboBox(this);
    for (BlendMode m : layerBlendModes())
        m_blend->addItem(blendModeName(m), int(m));
    m_blend->setCurrentIndex(std::max(0, m_blend->findData(int(props.blend))));
    m_opacitySlider = new QSlider(Qt::Horizontal, this);
    m_opacitySlider->setRange(0, 255);
    m_opacitySlider->setValue(props.opacity);
    m_opacity = new QSpinBox(this);
    m_opacity->setRange(0, 255);
    m_opacity->setValue(props.opacity);

    // Disposición de Paint.NET 5: Nombre, Opacidad, Modo de fusión y Visible.
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("Nombre"), this));
    layout->addWidget(m_name);
    layout->addSpacing(6);
    layout->addWidget(new QLabel(tr("Opacidad"), this));
    auto* opacityRow = new QHBoxLayout;
    opacityRow->addWidget(m_opacitySlider, 1);
    opacityRow->addWidget(m_opacity);
    layout->addLayout(opacityRow);
    layout->addSpacing(6);
    auto* blendRow = new QHBoxLayout;
    auto* blendLabel = new QLabel(tr("Modo de fusión:"), this);
    blendLabel->setBuddy(m_blend);
    blendRow->addWidget(blendLabel);
    blendRow->addWidget(m_blend, 1);
    layout->addLayout(blendRow);
    layout->addWidget(m_visible);
    layout->addWidget(m_alphaLock);
    layout->addSpacing(6);
    layout->addWidget(okCancel(this));
    setMinimumWidth(320);

    connect(m_opacitySlider, &QSlider::valueChanged, m_opacity, &QSpinBox::setValue);
    connect(m_opacity, &QSpinBox::valueChanged, m_opacitySlider, &QSlider::setValue);
    connect(m_opacity, &QSpinBox::valueChanged, this, &LayerPropertiesDialog::emitPreview);
    connect(m_visible, &QCheckBox::toggled, this, &LayerPropertiesDialog::emitPreview);
    connect(m_blend, &QComboBox::currentIndexChanged, this, &LayerPropertiesDialog::emitPreview);
    connect(m_name, &QLineEdit::textChanged, this, &LayerPropertiesDialog::emitPreview);
}

LayerProps LayerPropertiesDialog::props() const
{
    LayerProps p = m_base;
    p.name = m_name->text().isEmpty() ? m_base.name : m_name->text();
    p.visible = m_visible->isChecked();
    p.blend = BlendMode(m_blend->currentData().toInt());
    p.opacity = m_opacity->value();
    p.alphaLock = m_alphaLock->isChecked();
    return p;
}

void LayerPropertiesDialog::emitPreview()
{
    if (m_preview)
        m_preview(props());
}

// ---------------------------------------------------------------------------

QString ColorProfileDialog::describe(const QColorSpace& cs)
{
    if (!cs.isValid())
        return tr("Ninguno (sRGB)");
    if (cs == QColorSpace(QColorSpace::SRgb))
        return QStringLiteral("sRGB IEC61966-2.1");
    const QString d = cs.description();
    if (!d.isEmpty())
        return d;
    if (cs == QColorSpace(QColorSpace::AdobeRgb))
        return QStringLiteral("Adobe RGB (1998)");
    if (cs == QColorSpace(QColorSpace::DisplayP3))
        return QStringLiteral("Display P3");
    if (cs == QColorSpace(QColorSpace::ProPhotoRgb))
        return QStringLiteral("ProPhoto RGB");
    return tr("Perfil personalizado");
}

ColorProfileDialog::ColorProfileDialog(const QColorSpace& current, QWidget* parent)
    : QDialog(parent)
    , m_current(current.isValid() ? current : QColorSpace(QColorSpace::SRgb))
{
    setWindowTitle(tr("Perfil de colores"));
    setWindowIcon(Icons::theme({"color-profile", "color-management"}));

    auto* form = new QGridLayout;
    form->addWidget(new QLabel(tr("Actual:"), this), 0, 0);
    auto* currentLabel = new QLabel(describe(m_current), this);
    QFont bold = currentLabel->font();
    bold.setBold(true);
    currentLabel->setFont(bold);
    form->addWidget(currentLabel, 0, 1, 1, 3);

    form->addWidget(new QLabel(tr("Nuevo:"), this), 1, 0);
    m_profiles = new QComboBox(this);
    m_profiles->setMinimumWidth(220);
    form->addWidget(m_profiles, 1, 1);
    auto* load = new QToolButton(this);
    load->setIcon(Icons::theme({"document-open", "folder-open"}));
    load->setToolTip(tr("Cargar un perfil de color (.icc, .icm)"));
    auto* saveProfile = new QToolButton(this);
    saveProfile->setIcon(Icons::theme({"document-save"}));
    saveProfile->setToolTip(tr("Guardar el perfil elegido como archivo .icc"));
    form->addWidget(load, 1, 2);
    form->addWidget(saveProfile, 1, 3);

    // Los mismos perfiles que ofrece Paint.NET.
    for (auto named : {QColorSpace::SRgb, QColorSpace::AdobeRgb, QColorSpace::DisplayP3, QColorSpace::ProPhotoRgb})
        addProfile(QColorSpace(named), false);
    addProfile(m_current, true);

    connect(load, &QToolButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Cargar perfil de color"), QString(),
                                                          tr("Perfiles de color (*.icc *.icm);;Todos los archivos (*)"));
        if (path.isEmpty())
            return;
        QFile f(path);
        const QColorSpace cs = f.open(QIODevice::ReadOnly) ? QColorSpace::fromIccProfile(f.readAll()) : QColorSpace();
        if (!cs.isValid()) {
            QMessageBox::warning(this, tr("Perfil de colores"), tr("El archivo no contiene un perfil de color RGB válido."));
            return;
        }
        addProfile(cs, true);
    });
    connect(saveProfile, &QToolButton::clicked, this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, tr("Guardar perfil de color"),
                                                          describe(profile()) + QStringLiteral(".icc"),
                                                          tr("Perfiles de color (*.icc)"));
        if (path.isEmpty())
            return;
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write(profile().iccProfile()) <= 0)
            QMessageBox::warning(this, tr("Perfil de colores"), tr("No se pudo guardar el perfil."));
    });

    auto* box = new QDialogButtonBox(this);
    m_convert = box->addButton(tr("Convertir"), QDialogButtonBox::AcceptRole);
    m_assign = box->addButton(tr("Asignar"), QDialogButtonBox::AcceptRole);
    box->addButton(tr("Cancelar"), QDialogButtonBox::RejectRole);
    m_convert->setToolTip(tr("Convierte los colores al perfil nuevo: los píxeles cambian y la imagen se ve igual."));
    m_assign->setToolTip(tr("Asigna el perfil nuevo sin tocar los píxeles: cambia cómo se interpretan sus colores."));
    connect(m_convert, &QPushButton::clicked, this, [this] {
        m_result = Result::Convert;
        accept();
    });
    connect(m_assign, &QPushButton::clicked, this, [this] {
        m_result = Result::Assign;
        accept();
    });
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_profiles, &QComboBox::currentIndexChanged, this, &ColorProfileDialog::updateButtons);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(box);
    updateButtons();
}

void ColorProfileDialog::addProfile(const QColorSpace& cs, bool select)
{
    int index = int(m_spaces.indexOf(cs));
    if (index < 0) {
        m_spaces.append(cs);
        m_profiles->addItem(describe(cs));
        index = int(m_spaces.size()) - 1;
    }
    if (select)
        m_profiles->setCurrentIndex(index);
}

QColorSpace ColorProfileDialog::profile() const
{
    return m_spaces.value(m_profiles->currentIndex(), QColorSpace(QColorSpace::SRgb));
}

void ColorProfileDialog::updateButtons()
{
    // Igual que Paint.NET: con el mismo perfil no hay nada que hacer.
    const bool different = profile() != m_current;
    m_convert->setEnabled(different);
    m_assign->setEnabled(different);
}

// ---------------------------------------------------------------------------
// Animación (GIF)
// ---------------------------------------------------------------------------

AnimationDialog::AnimationDialog(const QList<LayerPtr>& layers, int defaultDelay, QWidget* parent)
    : QDialog(parent)
    , m_layers(layers)
{
    setWindowTitle(tr("Animación"));
    setWindowIcon(Icons::theme({"media-playback-start", "video-x-generic"}));
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("Al guardar como GIF, cada capa visible es un fotograma (la de abajo es el primero). "
                                "La duración se guarda en el nombre de la capa."),
                             this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* top = new QHBoxLayout;
    m_list = new QTreeWidget(this);
    m_list->setHeaderLabels({tr("Fotograma"), tr("Capa"), tr("Duración (ms)")});
    m_list->setRootIsDecorated(false);
    m_list->setIconSize(QSize(48, 36));
    m_list->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_list->setMinimumWidth(380);
    for (int i = 0; i < m_layers.size(); ++i) {
        const LayerPtr& layer = m_layers[i];
        if (!layer->props.visible)
            continue;
        const int frame = int(m_frameLayers.size());
        m_frameLayers << i;
        auto* item = new QTreeWidgetItem(m_list);
        item->setText(0, QString::number(frame + 1));
        QString base = Gif::nameWithDelay(layer->props.name, 0);
        base.chop(int(qstrlen(" (0ms)")));
        item->setText(1, base);
        item->setIcon(1, QIcon(QPixmap::fromImage(Raster::thumbnail(layer->image, QSize(48, 36)))));
        auto* spin = new QSpinBox(m_list);
        spin->setRange(Gif::kMinDelayMs, 60000);
        spin->setSingleStep(10);
        spin->setValue(Gif::delayFromName(layer->props.name, defaultDelay));
        m_list->setItemWidget(item, 2, spin);
        m_delays << spin;
    }
    m_list->resizeColumnToContents(0);
    m_list->setColumnWidth(2, 110);
    top->addWidget(m_list, 1);

    auto* side = new QVBoxLayout;
    m_preview = new QLabel(this);
    m_preview->setFixedSize(220, 165);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setFrameShape(QFrame::StyledPanel);
    side->addWidget(m_preview);
    auto* play = new QPushButton(Icons::theme({"media-playback-start"}), tr("Reproducir"), this);
    play->setCheckable(true);
    side->addWidget(play);
    side->addStretch();
    top->addLayout(side);
    layout->addLayout(top, 1);

    // Misma duración para todos.
    auto* row = new QHBoxLayout;
    auto* all = new QSpinBox(this);
    all->setRange(Gif::kMinDelayMs, 60000);
    all->setSingleStep(10);
    all->setValue(defaultDelay);
    auto* apply = new QPushButton(tr("Aplicar a todos"), this);
    connect(apply, &QPushButton::clicked, this, [this, all] {
        for (QSpinBox* s : std::as_const(m_delays))
            s->setValue(all->value());
    });
    row->addWidget(new QLabel(tr("Duración de todos los fotogramas:"), this));
    row->addWidget(all);
    row->addWidget(new QLabel(tr("ms"), this));
    row->addWidget(apply);
    row->addStretch();
    layout->addLayout(row);
    layout->addWidget(okCancelButtons(this));

    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &AnimationDialog::step);
    connect(play, &QPushButton::toggled, this, [this, play](bool on) {
        play->setText(on ? tr("Pausa") : tr("Reproducir"));
        play->setIcon(Icons::theme({on ? "media-playback-pause" : "media-playback-start"}));
        if (on)
            step();
        else
            m_timer->stop();
    });
    connect(m_list, &QTreeWidget::currentItemChanged, this, [this] {
        if (!m_timer->isActive())
            showFrame(m_list->indexOfTopLevelItem(m_list->currentItem()));
    });
    if (m_list->topLevelItemCount() > 0)
        m_list->setCurrentItem(m_list->topLevelItem(0));
    resize(640, 420);
}

void AnimationDialog::showFrame(int frame)
{
    if (frame < 0 || frame >= m_frameLayers.size())
        return;
    m_current = frame;
    const QImage& image = m_layers[m_frameLayers[frame]]->image;
    QPixmap pm(m_preview->size() - QSize(4, 4));
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    const QSize s = image.size().scaled(pm.size(), Qt::KeepAspectRatio);
    const QRect r(QPoint((pm.width() - s.width()) / 2, (pm.height() - s.height()) / 2), s);
    p.fillRect(r, QBrush(Raster::checkerboard(8)));
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(r, image);
    p.end();
    m_preview->setPixmap(pm);
}

void AnimationDialog::step()
{
    if (m_frameLayers.isEmpty())
        return;
    showFrame(m_current);
    const int delay = m_delays.value(m_current) ? m_delays[m_current]->value() : 100;
    m_current = (m_current + 1) % int(m_frameLayers.size());
    m_timer->start(delay);
}

QHash<int, QString> AnimationDialog::renamedLayers() const
{
    QHash<int, QString> renamed;
    for (int f = 0; f < m_frameLayers.size(); ++f) {
        const int index = m_frameLayers[f];
        const QString name = m_layers[index]->props.name;
        const QString updated = Gif::nameWithDelay(name, m_delays[f]->value());
        if (updated != name && Gif::delayFromName(name, -1) != m_delays[f]->value())
            renamed.insert(index, updated);
    }
    return renamed;
}
