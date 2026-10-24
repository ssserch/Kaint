#include "app/dialogs/ThemeDialog.h"

#include "app/Icons.h"
#include "app/dialogs/DialogButtons.h"
#include "app/widgets/EffectControls.h"
#include "app/widgets/UnitField.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

QWidget* sectionHeader(const QString& title, QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* l = new QHBoxLayout(w);
    l->setContentsMargins(0, 8, 0, 0);
    l->addWidget(new QLabel(title, w));
    auto* line = new QFrame(w);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    l->addWidget(line, 1);
    return w;
}

QComboBox* sizeCombo(const QList<int>& sizes, QWidget* parent)
{
    auto* combo = new QComboBox(parent);
    for (int size : sizes)
        combo->addItem(QStringLiteral("%1 px").arg(size), size);
    return combo;
}

// Deslizador con su valor a la derecha.
QHBoxLayout* sliderRow(QSlider* slider, QWidget* parent)
{
    auto* row = new QHBoxLayout;
    auto* value = new QLabel(parent);
    value->setMinimumWidth(value->fontMetrics().horizontalAdvance(QStringLiteral("100 %")));
    auto show = [value](int v) { value->setText(QStringLiteral("%1 %").arg(v)); };
    show(slider->value());
    QObject::connect(slider, &QSlider::valueChanged, value, show);
    slider->setMaximumWidth(260);
    row->addWidget(slider, 1);
    row->addWidget(value);
    row->addStretch();
    return row;
}

void fixedWidth(std::initializer_list<QWidget*> widgets)
{
    for (QWidget* w : widgets)
        w->setSizePolicy(QSizePolicy::Fixed, w->sizePolicy().verticalPolicy());
}

void selectData(QComboBox* combo, const QVariant& value)
{
    int i = combo->findData(value);
    if (i < 0 && value.typeId() == QMetaType::Int) {
        combo->addItem(QStringLiteral("%1 px").arg(value.toInt()), value);
        i = combo->count() - 1;
    }
    combo->setCurrentIndex(std::max(0, i));
}

// Temas de iconos instalados (los de cursores no tienen carpetas de iconos).
QStringList iconThemes()
{
    QStringList names;
    for (const QString& path : QIcon::themeSearchPaths()) {
        const QDir dir(path);
        for (const QString& name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QString index = dir.filePath(name + QStringLiteral("/index.theme"));
            if (name == QLatin1String("hicolor") || names.contains(name) || !QFile::exists(index))
                continue;
            const QSettings theme(index, QSettings::IniFormat);
            if (!theme.value(QStringLiteral("Icon Theme/Directories")).toString().isEmpty() ||
                !theme.value(QStringLiteral("Icon Theme/Directories")).toStringList().isEmpty())
                names.append(name);
        }
    }
    names.sort(Qt::CaseInsensitive);
    return names;
}

// Vista previa del fondo: el área de trabajo en pequeño, con la imagen colocada como en el lienzo
// y una hoja en medio.
class BackgroundPreview : public QWidget {
public:
    explicit BackgroundPreview(QWidget* parent)
        : QWidget(parent)
    {
        setFixedSize(240, 150);
    }
    void setSpec(const Theme::Spec& spec)
    {
        m_spec = Theme::resolved(spec);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        // Se dibuja como un área de trabajo de 1200x750 reducida, para que la escala y la
        // colocación se vean como en la ventana.
        constexpr double f = 0.2;
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.scale(f, f);
        const QRect area(0, 0, int(width() / f), int(height() / f));
        p.fillRect(area, m_spec.workspace);
        Theme::paintBackgroundImage(p, area, area, m_spec);
        const QRect page(area.width() / 2 - 300, area.height() / 2 - 200, 600, 400);
        p.fillRect(page.translated(8, 8), QColor(0, 0, 0, 60));
        p.fillRect(page, Qt::white);
        p.resetTransform();
        p.setPen(palette().color(QPalette::Mid));
        p.drawRect(rect().adjusted(0, 0, -1, -1));
    }

private:
    Theme::Spec m_spec;
};

} // namespace

ThemeDialog::ThemeDialog(QWidget* parent)
    : QDialog(parent)
    , m_spec(Theme::custom())
    , m_original(m_spec)
{
    setWindowTitle(tr("Tema personalizado"));
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget(scroll);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 4, 12, 4);
    scroll->setWidget(content);

    // Modo base: los colores sin cambiar salen del tema claro u oscuro de Paint.NET.
    layout->addWidget(sectionHeader(tr("Base"), content));
    auto* baseForm = new QFormLayout;
    auto* templateRow = new QHBoxLayout;
    m_template = new QComboBox(content);
    m_template->addItems({tr("Claro"), tr("Oscuro"), tr("Windows 98")});
    auto* applyTemplate = new QPushButton(tr("Aplicar"), content);
    applyTemplate->setToolTip(tr("Sustituye colores y diseño por los de la plantilla (la imagen de fondo se queda)."));
    templateRow->addWidget(m_template);
    templateRow->addWidget(applyTemplate);
    templateRow->addStretch();
    baseForm->addRow(tr("Partir de:"), templateRow);
    m_base = new QComboBox(content);
    m_base->addItems({tr("Claro"), tr("Oscuro")});
    baseForm->addRow(tr("Modo:"), m_base);
    connect(applyTemplate, &QPushButton::clicked, this, [this] {
        Theme::Spec spec;
        switch (m_template->currentIndex()) {
        case 2: spec = Theme::windows98(); break;
        default:
            spec.dark = m_template->currentIndex() == 1;
            spec.radius = Theme::preset(spec.dark).radius;
            break;
        }
        spec.workspaceImage = m_spec.workspaceImage;
        spec.imageMode = m_spec.imageMode;
        spec.imageOpacity = m_spec.imageOpacity;
        spec.imageScale = m_spec.imageScale;
        spec.imageAnchor = m_spec.imageAnchor;
        spec.imageBlur = m_spec.imageBlur;
        spec.imageGrayscale = m_spec.imageGrayscale;
        spec.imageTint = m_spec.imageTint;
        spec.imageTintStrength = m_spec.imageTintStrength;
        setSpec(spec);
    });
    layout->addLayout(baseForm);
    auto* note = new QLabel(tr("Los colores que no cambies siguen al modo elegido. Los cambiados aparecen en negrita."),
                            content);
    note->setWordWrap(true);
    layout->addWidget(note);
    connect(m_base, &QComboBox::activated, this, [this](int i) {
        m_spec.dark = i == 1;
        refresh();
        changed();
    });

    layout->addWidget(sectionHeader(tr("Colores"), content));
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(8);
    grid->setColumnMinimumWidth(3, 16);
    int cell = 0;
    const Theme::ColorRole* workspaceRole = nullptr;
    for (const Theme::ColorRole& role : Theme::kColorRoles) {
        if (role.color == &Theme::Spec::workspace) {
            workspaceRole = &role;
            continue;
        }
        const ColorRow row = addColor(role, content);
        const int r = cell / 2;
        const int c = (cell % 2) * 4;
        grid->addWidget(row.label, r, c);
        grid->addWidget(row.button, r, c + 1);
        grid->addWidget(row.reset, r, c + 2);
        ++cell;
    }
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(4, 1);
    layout->addLayout(grid);

    // Fondo del lienzo: color e imagen opcional con su ajuste y opacidad.
    layout->addWidget(sectionHeader(tr("Fondo del lienzo"), content));
    auto* backForm = new QFormLayout;
    if (workspaceRole) {
        const ColorRow row = addColor(*workspaceRole, content);
        row.label->setText(tr("Color:"));
        auto* colorRow = new QHBoxLayout;
        colorRow->addWidget(row.button);
        colorRow->addWidget(row.reset);
        colorRow->addStretch();
        backForm->addRow(row.label, colorRow);
    }
    auto* imageRow = new QHBoxLayout;
    m_image = new QLineEdit(content);
    m_image->setReadOnly(true);
    m_image->setPlaceholderText(tr("Sin imagen"));
    auto* chooseImage = new QPushButton(tr("Elegir…"), content);
    m_clearImage = new QPushButton(tr("Quitar"), content);
    imageRow->addWidget(m_image, 1);
    imageRow->addWidget(chooseImage);
    imageRow->addWidget(m_clearImage);
    backForm->addRow(tr("Imagen:"), imageRow);
    auto* preview = new BackgroundPreview(content);
    m_preview = preview;
    backForm->addRow(tr("Vista previa:"), preview);
    m_imageMode = new QComboBox(content);
    m_imageMode->addItems({tr("Mosaico"), tr("Tamaño original"), tr("Estirada"), tr("Ajustada"), tr("Rellenar")});
    backForm->addRow(tr("Colocación:"), m_imageMode);
    m_imageAnchor = new QComboBox(content);
    m_imageAnchor->addItems({tr("Arriba a la izquierda"), tr("Arriba"), tr("Arriba a la derecha"), tr("Izquierda"), tr("Centro"),
                             tr("Derecha"), tr("Abajo a la izquierda"), tr("Abajo"), tr("Abajo a la derecha")});
    backForm->addRow(tr("Posición:"), m_imageAnchor);
    m_imageScale = new QSpinBox(content);
    m_imageScale->setRange(10, 400);
    m_imageScale->setSingleStep(10);
    m_imageScale->setToolTip(tr("Tamaño de la imagen en mosaico o a tamaño original."));
    backForm->addRow(tr("Escala:"), withUnit(m_imageScale, QStringLiteral("%")));
    m_imageOpacity = new QSlider(Qt::Horizontal, content);
    m_imageOpacity->setRange(0, 100);
    backForm->addRow(tr("Opacidad de la imagen:"), sliderRow(m_imageOpacity, content));
    m_imageBlur = new QSlider(Qt::Horizontal, content);
    m_imageBlur->setRange(0, 40);
    auto* blurRow = new QHBoxLayout;
    auto* blurValue = new QLabel(content);
    blurValue->setMinimumWidth(blurValue->fontMetrics().horizontalAdvance(QStringLiteral("40 px")));
    connect(m_imageBlur, &QSlider::valueChanged, blurValue, [blurValue](int v) { blurValue->setText(QStringLiteral("%1 px").arg(v)); });
    m_imageBlur->setMaximumWidth(260);
    blurRow->addWidget(m_imageBlur, 1);
    blurRow->addWidget(blurValue);
    blurRow->addStretch();
    backForm->addRow(tr("Desenfoque:"), blurRow);
    m_imageGrayscale = new QCheckBox(tr("En escala de grises"), content);
    backForm->addRow(QString(), m_imageGrayscale);
    auto* tintRow = new QHBoxLayout;
    m_imageTint = new ColorButton(content);
    m_imageTintStrength = new QSlider(Qt::Horizontal, content);
    m_imageTintStrength->setRange(0, 100);
    tintRow->addWidget(m_imageTint);
    tintRow->addLayout(sliderRow(m_imageTintStrength, content), 1);
    backForm->addRow(tr("Tinte:"), tintRow);
    layout->addLayout(backForm);
    connect(chooseImage, &QPushButton::clicked, this, &ThemeDialog::chooseImage);
    connect(m_clearImage, &QPushButton::clicked, this, [this] {
        m_spec.workspaceImage.clear();
        refresh();
        changed();
    });
    auto edit = [this](auto apply) {
        return [this, apply](auto value) {
            if (m_refreshing)
                return;
            apply(value);
            refresh();
            changed();
        };
    };
    connect(m_imageMode, &QComboBox::activated, this, edit([this](int i) { m_spec.imageMode = Theme::ImageMode(i); }));
    connect(m_imageAnchor, &QComboBox::activated, this, edit([this](int i) { m_spec.imageAnchor = i; }));
    connect(m_imageScale, &QSpinBox::valueChanged, this, edit([this](int v) { m_spec.imageScale = v; }));
    connect(m_imageOpacity, &QSlider::valueChanged, this, edit([this](int v) { m_spec.imageOpacity = v; }));
    connect(m_imageBlur, &QSlider::valueChanged, this, edit([this](int v) { m_spec.imageBlur = v; }));
    connect(m_imageGrayscale, &QCheckBox::toggled, this, edit([this](bool on) { m_spec.imageGrayscale = on; }));
    connect(m_imageTint, &ColorButton::colorChanged, this, edit([this](const QColor& c) {
        m_spec.imageTint = c;
        if (m_spec.imageTintStrength == 0)
            m_spec.imageTintStrength = 30;
    }));
    connect(m_imageTintStrength, &QSlider::valueChanged, this, edit([this](int v) { m_spec.imageTintStrength = v; }));

    // Diseño: iconos, botones de las barras, espaciado, esquinas, fuente.
    layout->addWidget(sectionHeader(tr("Diseño"), content));
    auto* designForm = new QFormLayout;
    m_iconSize = sizeCombo({16, 20, 24, 32}, content);
    designForm->addRow(tr("Iconos de las barras:"), m_iconSize);
    m_toolIconSize = sizeCombo({16, 20, 22, 24, 28, 32}, content);
    designForm->addRow(tr("Iconos de la ventana Herramientas:"), m_toolIconSize);
    m_buttonStyle = new QComboBox(content);
    m_buttonStyle->addItems({tr("Planos"), tr("Con marco"), tr("Redondeados")});
    designForm->addRow(tr("Botones de las barras:"), m_buttonStyle);
    m_density = new QComboBox(content);
    m_density->addItems({tr("Compacto"), tr("Normal"), tr("Amplio")});
    designForm->addRow(tr("Espaciado:"), m_density);
    m_radius = new QSpinBox(content);
    m_radius->setRange(0, 16);
    designForm->addRow(tr("Redondeo de las esquinas:"), withUnit(m_radius, tr("px")));
    m_classic = new QCheckBox(tr("Bordes clásicos en relieve (como Windows 98)"), content);
    designForm->addRow(tr("Estilo:"), m_classic);
    connect(m_classic, &QCheckBox::toggled, this, [this](bool on) {
        if (m_refreshing)
            return;
        m_spec.classic = on;
        changed();
    });
    m_pixelFont = new QCheckBox(tr("Texto sin suavizado (pixelado, como Windows 98)"), content);
    designForm->addRow(QString(), m_pixelFont);
    connect(m_pixelFont, &QCheckBox::toggled, this, [this](bool on) {
        if (m_refreshing)
            return;
        m_spec.pixelFont = on;
        changed();
    });
    m_panelOpacity = new QSlider(Qt::Horizontal, content);
    m_panelOpacity->setRange(20, 100);
    m_panelOpacity->setToolTip(tr("Con «Ventanas translúcidas» activado, opacidad de las ventanas flotantes cuando el "
                                  "puntero no está encima."));
    designForm->addRow(tr("Opacidad de las ventanas translúcidas:"), sliderRow(m_panelOpacity, content));
    auto* fontRow = new QHBoxLayout;
    m_systemFont = new QCheckBox(tr("La del sistema"), content);
    m_font = new QFontComboBox(content);
    m_fontSize = new QSpinBox(content);
    m_fontSize->setRange(6, 24);
    fontRow->addWidget(m_systemFont);
    fontRow->addWidget(m_font, 1);
    fontRow->addWidget(withUnit(m_fontSize, tr("pt")));
    designForm->addRow(tr("Fuente:"), fontRow);
    m_iconTheme = new QComboBox(content);
    m_iconTheme->addItem(tr("Automático (Breeze)"), QString());
    for (const QString& name : iconThemes())
        m_iconTheme->addItem(name, name);
    designForm->addRow(tr("Iconos:"), m_iconTheme);
    layout->addLayout(designForm);
    fixedWidth({m_template, m_base, m_imageMode, m_imageAnchor, m_imageScale, m_iconSize, m_toolIconSize, m_buttonStyle,
                m_density, m_radius, m_iconTheme});
    layout->addStretch();

    connect(m_iconSize, &QComboBox::activated, this, [this] {
        m_spec.iconSize = m_iconSize->currentData().toInt();
        changed();
    });
    connect(m_toolIconSize, &QComboBox::activated, this, [this] {
        m_spec.toolIconSize = m_toolIconSize->currentData().toInt();
        changed();
    });
    connect(m_buttonStyle, &QComboBox::activated, this, [this](int i) {
        m_spec.buttonStyle = Theme::ButtonStyle(i);
        changed();
    });
    connect(m_density, &QComboBox::activated, this, [this](int i) {
        m_spec.density = Theme::Density(i);
        changed();
    });
    connect(m_radius, &QSpinBox::valueChanged, this, [this](int v) {
        m_spec.radius = v;
        changed();
    });
    connect(m_panelOpacity, &QSlider::valueChanged, this, [this](int v) {
        m_spec.panelOpacity = v;
        changed();
    });
    auto updateFont = [this] {
        if (m_refreshing)
            return;
        const bool system = m_systemFont->isChecked();
        m_spec.fontFamily = system ? QString() : m_font->currentFont().family();
        m_spec.fontSize = system ? 0 : m_fontSize->value();
        m_font->setEnabled(!system);
        m_fontSize->parentWidget()->setEnabled(!system);
        changed();
    };
    connect(m_systemFont, &QCheckBox::toggled, this, updateFont);
    connect(m_font, &QFontComboBox::currentFontChanged, this, updateFont);
    connect(m_fontSize, &QSpinBox::valueChanged, this, updateFont);
    connect(m_iconTheme, &QComboBox::activated, this, [this] {
        m_spec.iconTheme = m_iconTheme->currentData().toString();
        changed();
    });

    // Abajo: importar, exportar y restablecer a la izquierda; Aceptar y Cancelar a la derecha.
    auto* buttons = new QHBoxLayout;
    auto* importButton = new QPushButton(tr("Importar…"), this);
    auto* exportButton = new QPushButton(tr("Exportar…"), this);
    auto* resetButton = new QPushButton(tr("Restablecer todo"), this);
    buttons->addWidget(importButton);
    buttons->addWidget(exportButton);
    buttons->addWidget(resetButton);
    buttons->addStretch();
    buttons->addWidget(okCancelButtons(this));
    connect(importButton, &QPushButton::clicked, this, &ThemeDialog::importTheme);
    connect(exportButton, &QPushButton::clicked, this, &ThemeDialog::exportTheme);
    connect(resetButton, &QPushButton::clicked, this, [this] {
        Theme::Spec spec;
        spec.dark = m_spec.dark;
        spec.radius = Theme::preset(spec.dark).radius;
        setSpec(spec);
    });

    auto* main = new QVBoxLayout(this);
    main->addWidget(scroll, 1);
    main->addLayout(buttons);
    resize(780, 640);
    refresh();
}

ThemeDialog::ColorRow ThemeDialog::addColor(const Theme::ColorRole& role, QWidget* parent)
{
    ColorRow row{role.color, new QLabel(QCoreApplication::translate("Theme", role.label), parent), new ColorButton(parent),
                 new QToolButton(parent)};
    row.reset->setIcon(Icons::theme({"edit-undo", "edit-reset"}));
    row.reset->setToolTip(tr("Restablecer"));
    row.reset->setAutoRaise(true);
    const auto member = role.color;
    connect(row.button, &ColorButton::colorChanged, this, [this, member](const QColor& c) {
        if (m_refreshing)
            return;
        m_spec.*member = c;
        refresh();
        changed();
    });
    connect(row.reset, &QToolButton::clicked, this, [this, member] {
        m_spec.*member = QColor();
        refresh();
        changed();
    });
    m_colors.append(row);
    return row;
}

void ThemeDialog::setSpec(const Theme::Spec& spec)
{
    m_spec = spec;
    refresh();
    changed();
}

void ThemeDialog::refresh()
{
    m_refreshing = true;
    const Theme::Spec effective = Theme::resolved(m_spec);
    m_base->setCurrentIndex(m_spec.dark ? 1 : 0);
    for (const ColorRow& row : std::as_const(m_colors)) {
        row.button->setColor(effective.*row.color);
        const bool custom = (m_spec.*row.color).isValid();
        row.reset->setEnabled(custom);
        QFont font = row.label->font();
        font.setBold(custom);
        row.label->setFont(font);
    }
    m_image->setText(QDir::toNativeSeparators(m_spec.workspaceImage));
    m_clearImage->setEnabled(!m_spec.workspaceImage.isEmpty());
    m_imageMode->setCurrentIndex(int(m_spec.imageMode));
    m_imageAnchor->setCurrentIndex(m_spec.imageAnchor);
    m_imageScale->setValue(m_spec.imageScale);
    const bool hasImage = !m_spec.workspaceImage.isEmpty();
    m_imageScale->parentWidget()->setEnabled(hasImage
                             && (m_spec.imageMode == Theme::ImageMode::Tile || m_spec.imageMode == Theme::ImageMode::Center));
    m_imageAnchor->setEnabled(hasImage && m_spec.imageMode != Theme::ImageMode::Stretch);
    m_imageOpacity->setValue(m_spec.imageOpacity);
    m_imageBlur->setValue(m_spec.imageBlur);
    m_imageGrayscale->setChecked(m_spec.imageGrayscale);
    m_imageTint->setColor(m_spec.imageTint.isValid() ? m_spec.imageTint : QColor(Qt::black));
    m_imageTintStrength->setValue(m_spec.imageTintStrength);
    for (QWidget* w : std::initializer_list<QWidget*>{m_imageMode, m_imageOpacity, m_imageBlur, m_imageGrayscale, m_imageTint,
                                                      m_imageTintStrength})
        w->setEnabled(hasImage);
    m_classic->setChecked(m_spec.classic);
    m_pixelFont->setChecked(m_spec.pixelFont);
    static_cast<BackgroundPreview*>(m_preview)->setSpec(m_spec);
    selectData(m_iconSize, m_spec.iconSize);
    selectData(m_toolIconSize, m_spec.toolIconSize);
    m_buttonStyle->setCurrentIndex(int(m_spec.buttonStyle));
    m_density->setCurrentIndex(int(m_spec.density));
    m_radius->setValue(m_spec.radius);
    m_panelOpacity->setValue(m_spec.panelOpacity);
    const bool systemFont = m_spec.fontFamily.isEmpty() && m_spec.fontSize <= 0;
    m_systemFont->setChecked(systemFont);
    m_font->setCurrentFont(systemFont ? QApplication::font() : QFont(m_spec.fontFamily));
    m_fontSize->setValue(m_spec.fontSize > 0 ? m_spec.fontSize : std::max(6, QApplication::font().pointSize()));
    m_font->setEnabled(!systemFont);
    m_fontSize->parentWidget()->setEnabled(!systemFont);
    m_iconTheme->setCurrentIndex(std::max(0, m_iconTheme->findData(m_spec.iconTheme)));
    m_refreshing = false;
}

void ThemeDialog::changed()
{
    if (!m_refreshing)
        Theme::setCustom(m_spec);
}

void ThemeDialog::chooseImage()
{
    const QString start = m_spec.workspaceImage.isEmpty()
                              ? QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
                              : QFileInfo(m_spec.workspaceImage).absolutePath();
    const QString file = QFileDialog::getOpenFileName(this, tr("Imagen de fondo"), start,
                                                      tr("Imágenes (*.png *.jpg *.jpeg *.webp *.bmp *.gif *.avif *.jxl)"));
    if (file.isEmpty())
        return;
    m_spec.workspaceImage = Theme::storeBackground(file);
    refresh();
    changed();
}

void ThemeDialog::importTheme()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("Importar tema"), QString(), tr("Temas de Kaint (*.json)"));
    if (file.isEmpty())
        return;
    QFile f(file);
    const QJsonDocument doc = f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()) : QJsonDocument();
    if (!doc.isObject()) {
        QMessageBox::warning(this, windowTitle(), tr("No se pudo leer el tema de «%1».").arg(QFileInfo(file).fileName()));
        return;
    }
    setSpec(Theme::fromJson(doc.object()));
}

void ThemeDialog::exportTheme()
{
    const QString file = QFileDialog::getSaveFileName(this, tr("Exportar tema"), QStringLiteral("kaint-theme.json"),
                                                      tr("Temas de Kaint (*.json)"));
    if (file.isEmpty())
        return;
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        f.write(QJsonDocument(Theme::toJson(m_spec, true)).toJson(QJsonDocument::Indented)) < 0)
        QMessageBox::warning(this, windowTitle(), tr("No se pudo guardar «%1».").arg(QFileInfo(file).fileName()));
}

void ThemeDialog::accept()
{
    Theme::saveCustom(m_spec);
    Theme::setCustom(m_spec);
    QDialog::accept();
}

void ThemeDialog::reject()
{
    Theme::setCustom(m_original);
    QDialog::reject();
}
