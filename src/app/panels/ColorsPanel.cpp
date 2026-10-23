#include "app/panels/ColorsPanel.h"

#include "app/AppState.h"
#include "app/Icons.h"
#include "core/Palette.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QSpinBox>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {

// Título de sección con la línea a la derecha ("RGB ————").
QWidget* sectionHeader(const QString& text, QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* l = new QHBoxLayout(w);
    l->setContentsMargins(0, 2, 0, 0);
    l->addWidget(new QLabel(text, w));
    auto* rule = new QFrame(w);
    rule->setFrameShape(QFrame::HLine);
    rule->setFrameShadow(QFrame::Sunken);
    l->addWidget(rule, 1);
    return w;
}

} // namespace

ColorsPanel::ColorsPanel(QWidget* parent)
    : QWidget(parent)
{
    AppState* app = AppState::instance();

    m_target = new QComboBox(this);
    m_target->addItems({tr("Principal"), tr("Secundario")});
    m_target->setToolTip(tr("Ranura de color que se edita (C)"));
    m_more = new QPushButton(tr("Más »"), this);
    m_more->setCheckable(true);
    m_swatches = new ColorSwatches(this);
    m_wheel = new ColorWheel(this);
    m_wheel->setToolTip(tr("Clic izquierdo: ranura activa · derecho: inactiva\n"
                           "Ctrl: solo tono · Alt: solo saturación · Mayús: radios de 15°"));
    m_value = new ColorSlider(ColorSlider::Value, Qt::Vertical, this);
    m_value->setToolTip(tr("Brillo"));
    m_palette = new PaletteWidget(this);
    m_palette->setColors(app->palette());

    m_addColor = new QToolButton(this);
    m_addColor->setCheckable(true);
    m_addColor->setAutoRaise(true);
    m_addColor->setToolTip(tr("Añadir el color activo a la paleta: haz clic después en la muestra que quieras sustituir"));
    m_paletteMenu = new QToolButton(this);
    m_paletteMenu->setAutoRaise(true);
    m_paletteMenu->setPopupMode(QToolButton::InstantPopup);
    {
        // Como en Paint.NET: nueve colores en un marco blanco.
        QPixmap pm(32, 32);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setPen(QPen(QColor(60, 60, 64), 2));
        p.setBrush(Qt::white);
        p.drawRect(QRectF(2, 2, 27, 27));
        const QColor grid[] = {QColor(220, 70, 50),  QColor(130, 50, 160), QColor(60, 70, 180),
                               QColor(240, 140, 30), Qt::white,             QColor(60, 140, 240),
                               QColor(250, 200, 40), QColor(100, 180, 70),  QColor(30, 140, 110)};
        p.setPen(Qt::NoPen);
        for (int i = 0; i < 9; ++i) {
            p.setBrush(grid[i]);
            p.drawRect(QRectF(5 + (i % 3) * 7.3, 5 + (i / 3) * 7.3, 7.3, 7.3));
        }
        p.end();
        m_paletteMenu->setIcon(QIcon(pm));
    }
    m_paletteMenu->setToolTip(tr("Paletas"));
    auto* paletteMenu = new QMenu(m_paletteMenu);
    m_paletteMenu->setMenu(paletteMenu);
    connect(paletteMenu, &QMenu::aboutToShow, this, &ColorsPanel::buildPaletteMenu);

    m_topRow = new QHBoxLayout;
    m_topRow->addWidget(m_target, 1);
    m_topRow->addStretch();
    m_topRow->addWidget(m_more, 0, Qt::AlignTop);

    auto* middle = new QHBoxLayout;
    m_swatchColumn = new QVBoxLayout;
    m_swatchColumn->addWidget(m_swatches);
    m_swatchColumn->addStretch();
    middle->addLayout(m_swatchColumn);
    middle->addWidget(m_wheel, 1);
    middle->addWidget(m_value);

    // Columna derecha de la vista ampliada, con las secciones de Paint.NET.
    m_extended = new QWidget(this);
    auto* grid = new QGridLayout(m_extended);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setVerticalSpacing(2);
    grid->setColumnStretch(1, 1);
    m_hex = new QLineEdit(m_extended);
    m_hex->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("#?[0-9A-Fa-f]{0,8}")), m_hex));
    m_hex->setToolTip(tr("RRGGBB o RRGGBBAA"));
    m_hex->setAlignment(Qt::AlignRight);
    struct Row {
        ColorSlider::Channel channel;
        QString label;
        QString section;
    };
    const QList<Row> rows{
        {ColorSlider::Red, tr("R:"), QStringLiteral("RGB")},
        {ColorSlider::Green, tr("V:"), {}},
        {ColorSlider::Blue, tr("A:"), {}},
        {ColorSlider::Hue, tr("M:"), tr("MSV (HSV)")},
        {ColorSlider::Saturation, tr("S:"), {}},
        {ColorSlider::Value, tr("V:"), {}},
        {ColorSlider::Alpha, QString(), tr("Opacidad (canal alfa)")},
    };
    int row = 0;
    for (const auto& [channel, label, section] : rows) {
        if (!section.isEmpty())
            grid->addWidget(sectionHeader(section, m_extended), row++, 0, 1, 3);
        auto* slider = new ColorSlider(channel, Qt::Horizontal, m_extended);
        slider->setMinimumWidth(90);
        auto* spin = new QSpinBox(m_extended);
        spin->setRange(0, slider->maximum());
        spin->setAlignment(Qt::AlignRight);
        if (!label.isEmpty())
            grid->addWidget(new QLabel(label, m_extended), row, 0);
        grid->addWidget(slider, row, 1);
        grid->addWidget(spin, row, 2);
        m_channels.append({slider, spin});
        ++row;
        if (channel == ColorSlider::Blue) {
            grid->addWidget(new QLabel(tr("Hex:"), m_extended), row, 0);
            grid->addWidget(m_hex, row++, 1, 1, 2, Qt::AlignRight);
            m_hex->setFixedWidth(spin->sizeHint().width() + 16);
        }

        auto apply = [this, channel](int v) {
            QColor c = current();
            switch (channel) {
            case ColorSlider::Red: c.setRed(v); break;
            case ColorSlider::Green: c.setGreen(v); break;
            case ColorSlider::Blue: c.setBlue(v); break;
            case ColorSlider::Alpha: c.setAlpha(v); break;
            case ColorSlider::Hue: c = QColor::fromHsv(v, c.hsvSaturation(), c.value(), c.alpha()); break;
            case ColorSlider::Saturation: c = QColor::fromHsv(std::max(0, c.hsvHue()), v * 255 / 100, c.value(), c.alpha()); break;
            case ColorSlider::Value: c = QColor::fromHsv(std::max(0, c.hsvHue()), c.hsvSaturation(), v * 255 / 100, c.alpha()); break;
            }
            setCurrent(c);
        };
        connect(slider, &ColorSlider::valueChanged, this, apply);
        connect(spin, &QSpinBox::valueChanged, this, apply);
    }
    grid->setRowStretch(row, 1);
    m_extended->hide();

    auto* paletteRow = new QHBoxLayout;
    paletteRow->setSpacing(0);
    paletteRow->addWidget(m_addColor);
    paletteRow->addWidget(m_paletteMenu);
    paletteRow->addStretch();

    auto* left = new QVBoxLayout;
    left->setSpacing(4);
    left->addLayout(m_topRow);
    left->addLayout(middle, 1);
    left->addLayout(paletteRow);
    left->addWidget(m_palette);

    // Ampliado, como en Paint.NET, crece a lo ancho: los deslizadores van a la derecha.
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(10);
    layout->addLayout(left);
    layout->addWidget(m_extended, 1);

    connect(m_target, &QComboBox::currentIndexChanged, this,
            [](int i) { AppState::instance()->setSecondaryActive(i == 1); });
    connect(m_more, &QPushButton::toggled, this, &ColorsPanel::setExpanded);
    connect(m_swatches, &ColorSwatches::primaryClicked, this, [] { AppState::instance()->setSecondaryActive(false); });
    connect(m_swatches, &ColorSwatches::secondaryClicked, this, [] { AppState::instance()->setSecondaryActive(true); });
    connect(m_swatches, &ColorSwatches::primaryDoubleClicked, this, [this] { editWithDialog(false); });
    connect(m_swatches, &ColorSwatches::secondaryDoubleClicked, this, [this] { editWithDialog(true); });
    connect(m_swatches, &ColorSwatches::swapClicked, app, &AppState::swapColors);
    connect(m_swatches, &ColorSwatches::resetClicked, app, &AppState::resetColors);
    connect(m_wheel, &ColorWheel::colorPicked, this, [this](const QColor& c, Qt::MouseButton b) {
        if (b == Qt::RightButton)
            AppState::instance()->setInactiveColor(c);
        else
            setCurrent(c);
    });
    connect(m_value, &ColorSlider::valueChanged, this, [this](int v) {
        const QColor c = current();
        setCurrent(QColor::fromHsv(std::max(0, c.hsvHue()), c.hsvSaturation(), v * 255 / 100, c.alpha()));
    });
    connect(m_addColor, &QToolButton::toggled, m_palette, &PaletteWidget::setAddMode);
    connect(m_palette, &PaletteWidget::colorClicked, this, [this](int index, const QColor& c, Qt::MouseButton b) {
        AppState* app = AppState::instance();
        if (m_palette->addMode()) {
            app->setPaletteColor(index, app->activeColor());
            m_addColor->setChecked(false);
            return;
        }
        if (b == Qt::RightButton)
            app->setInactiveColor(c);
        else
            app->setActiveColor(c);
    });
    connect(m_hex, &QLineEdit::editingFinished, this, [this] {
        QString t = m_hex->text().remove(QLatin1Char('#'));
        if (t.size() != 6 && t.size() != 8)
            return refresh();
        const uint v = t.toUInt(nullptr, 16);
        const QColor c = t.size() == 6 ? QColor::fromRgb(v >> 16 & 255, v >> 8 & 255, v & 255, current().alpha())
                                       : QColor::fromRgb(v >> 24 & 255, v >> 16 & 255, v >> 8 & 255, v & 255);
        setCurrent(c);
    });
    connect(app, &AppState::colorsChanged, this, &ColorsPanel::refresh);
    connect(app, &AppState::paletteChanged, this, [this] { m_palette->setColors(AppState::instance()->palette()); });
    refresh();
}

double ColorsPanel::scale() const
{
    const QSize base = sizeHint();
    return base.width() > 0 ? std::max(minimumScale(), double(width()) / base.width()) : 1.0;
}

double ColorsPanel::minimumScale() const
{
    const QSize base = sizeHint(), least = minimumSizeHint();
    if (base.isEmpty())
        return 1.0;
    return std::max({0.5, double(least.width()) / base.width(), double(least.height()) / base.height()});
}

QSize ColorsPanel::sizeForScale(double scale) const
{
    const QSize base = sizeHint();
    scale = std::clamp(scale, minimumScale(), 4.0);
    return QSize(int(std::lround(base.width() * scale)), int(std::lround(base.height() * scale)));
}

QSize ColorsPanel::fitSize(QSize proposed, Qt::Orientations changed) const
{
    const QSize base = sizeHint();
    if (base.isEmpty())
        return proposed;
    const double sx = double(proposed.width()) / base.width();
    const double sy = double(proposed.height()) / base.height();
    if (changed == Qt::Vertical)
        return sizeForScale(sy);
    if (changed == Qt::Horizontal)
        return sizeForScale(sx);
    return sizeForScale(std::max(sx, sy));
}

QSize ColorsPanel::sizeForLayout(const QVariant& state) const
{
    bool ok = false;
    const double s = state.toDouble(&ok);
    return ok && s > 0 ? sizeForScale(s) : QSize();
}

void ColorsPanel::setExpanded(bool on)
{
    // Ampliado: las muestras suben junto al botón, sin el selector de ranura ni el brillo
    // vertical (ya están los deslizadores). La escala se mantiene.
    const double keep = scale();
    m_target->setVisible(!on);
    m_value->setVisible(!on);
    if (on) {
        m_swatchColumn->removeWidget(m_swatches);
        m_topRow->insertWidget(0, m_swatches, 0, Qt::AlignTop);
    } else {
        m_topRow->removeWidget(m_swatches);
        m_swatchColumn->insertWidget(0, m_swatches);
    }
    m_extended->setVisible(on);
    m_more->setText(on ? tr("« Menos") : tr("Más »"));
    m_palette->setVisibleCount(on ? Palette::kColorCount : 32);
    emit layoutChanged(keep);
}

bool ColorsPanel::isExpanded() const
{
    return m_more->isChecked();
}

QColor ColorsPanel::current() const
{
    return AppState::instance()->activeColor();
}

void ColorsPanel::setCurrent(const QColor& c)
{
    AppState::instance()->setActiveColor(c);
}

void ColorsPanel::editWithDialog(bool secondary)
{
    AppState::instance()->setSecondaryActive(secondary);
    const QColor c = QColorDialog::getColor(current(), this, secondary ? tr("Color secundario") : tr("Color principal"),
                                            QColorDialog::ShowAlphaChannel);
    if (c.isValid())
        setCurrent(c);
}

void ColorsPanel::updateAddIcon()
{
    // Como en Paint.NET: el color activo con un "+".
    QPixmap pm(32, 32);
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(60, 60, 64), 2));
        p.setBrush(QColor(current().rgb()));
        p.drawRect(QRectF(2, 2, 24, 24));
        QPainterPath plus;
        plus.addRect(QRectF(20, 13, 5, 18));
        plus.addRect(QRectF(13.5, 19.5, 18, 5));
        p.setPen(QPen(QColor(50, 110, 100), 1.6));
        p.setBrush(Qt::white);
        p.drawPath(plus.simplified());
    }
    m_addColor->setIcon(QIcon(pm));
}

void ColorsPanel::buildPaletteMenu()
{
    QMenu* menu = m_paletteMenu->menu();
    menu->clear();
    AppState* app = AppState::instance();
    const QStringList names = Palette::available();
    for (const QString& name : names) {
        menu->addAction(name, this, [this, name] {
            QString error;
            const QList<QColor> colors = Palette::load(QDir(Palette::userFolder()).filePath(name + QStringLiteral(".txt")), &error);
            if (colors.isEmpty())
                QMessageBox::warning(this, tr("Paletas"), tr("No se pudo abrir la paleta «%1»:\n%2").arg(name, error));
            else
                AppState::instance()->setPalette(colors);
        });
    }
    if (!names.isEmpty())
        menu->addSeparator();
    menu->addAction(Icons::theme({"document-save-as"}), tr("Guardar la paleta actual como…"), this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Guardar la paleta"), tr("Nombre de la paleta:"),
                                                   QLineEdit::Normal, QString(), &ok)
                                 .trimmed();
        if (!ok || name.isEmpty())
            return;
        QString error;
        QDir().mkpath(Palette::userFolder());
        if (!Palette::save(AppState::instance()->palette(), QDir(Palette::userFolder()).filePath(name + QStringLiteral(".txt")),
                           &error))
            QMessageBox::warning(this, tr("Paletas"), tr("No se pudo guardar la paleta:\n%1").arg(error));
    });
    menu->addAction(Icons::theme({"document-open"}), tr("Abrir paleta desde archivo…"), this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Abrir paleta"), Palette::userFolder(),
                                                          tr("Paletas de Paint.NET (*.txt);;Todos los archivos (*)"));
        if (path.isEmpty())
            return;
        QString error;
        const QList<QColor> colors = Palette::load(path, &error);
        if (colors.isEmpty())
            QMessageBox::warning(this, tr("Paletas"), tr("No se pudo abrir la paleta:\n%1").arg(error));
        else
            AppState::instance()->setPalette(colors);
    });
    menu->addAction(Icons::theme({"folder-open"}), tr("Abrir la carpeta de paletas"), this, [] {
        QDir().mkpath(Palette::userFolder());
        QDesktopServices::openUrl(QUrl::fromLocalFile(Palette::userFolder()));
    });
    menu->addAction(Icons::theme({"edit-reset", "view-refresh"}), tr("Restablecer la paleta predeterminada"), app,
                    [] { AppState::instance()->setPalette(Palette::defaultColors()); });
}

void ColorsPanel::refresh()
{
    const AppState* app = AppState::instance();
    const QColor c = current();
    {
        const QSignalBlocker block(m_target);
        m_target->setCurrentIndex(app->secondaryActive() ? 1 : 0);
    }
    m_swatches->setColors(app->primary(), app->secondary());
    m_swatches->setEditingSecondary(app->secondaryActive());
    m_wheel->setColor(c);
    m_value->setColor(c);
    for (auto& [slider, spin] : m_channels) {
        slider->setColor(c);
        const QSignalBlocker block(spin);
        spin->setValue(slider->value());
    }
    if (!m_hex->hasFocus()) {
        QString hex = QStringLiteral("%1%2%3").arg(c.red(), 2, 16, QLatin1Char('0')).arg(c.green(), 2, 16, QLatin1Char('0')).arg(c.blue(), 2, 16, QLatin1Char('0'));
        if (c.alpha() < 255)
            hex += QStringLiteral("%1").arg(c.alpha(), 2, 16, QLatin1Char('0'));
        m_hex->setText(hex.toUpper());
    }
    updateAddIcon();
}
