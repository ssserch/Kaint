#include "app/dialogs/SaveAllDialog.h"

#include "app/Icons.h"
#include "core/Raster.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputMethodEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QStyleOptionFrame>
#include <QTextCharFormat>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace SaveAll {

namespace {

constexpr int kThumb = 56;

QPixmap thumbnail(const QImage& image)
{
    const QSize size = image.size().scaled(QSize(kThumb, kThumb), Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
    QPixmap pm(size);
    QPainter p(&pm);
    p.fillRect(pm.rect(), QBrush(Raster::checkerboard(6)));
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(pm.rect(), image);
    return pm;
}

// Caracteres que no puede llevar un nombre de archivo (en Windows tampoco los de la segunda lista).
bool validName(const QString& name)
{
    if (name.trimmed().isEmpty() || name == QLatin1String(".") || name == QLatin1String(".."))
        return false;
    static const QString forbidden = QStringLiteral("/\\:*?\"<>|");
    for (const QChar c : name)
        if (forbidden.contains(c) || c.unicode() < 32)
            return false;
    return true;
}

} // namespace

// Campo del nombre con la extensión del formato detrás, en su color (como «prueba.png»).
class NameEdit : public QLineEdit {
public:
    explicit NameEdit(QWidget* parent = nullptr)
        : QLineEdit(parent)
    {
    }

    // Partes del texto en amarillo (el número de cada nombre, los '#' del nombre común).
    void setHighlights(const QList<QPair<int, int>>& ranges)
    {
        m_highlights = ranges;
        applyHighlights();
    }
    const QList<QPair<int, int>>& highlights() const { return m_highlights; }

    void applyHighlights()
    {
        // QLineEdit no tiene texto enriquecido: los formatos le llegan como los de un método de
        // entrada (posiciones respecto al cursor), sin cambiar el texto.
        QList<QInputMethodEvent::Attribute> attributes;
        QTextCharFormat yellow;
        yellow.setForeground(SaveAllDialog::numberColor(palette().color(QPalette::Base)));
        for (const auto& [start, length] : std::as_const(m_highlights))
            if (length > 0 && start >= 0 && start + length <= text().size())
                attributes << QInputMethodEvent::Attribute(QInputMethodEvent::TextFormat, start - cursorPosition(), length, yellow);
        QInputMethodEvent event(QString(), attributes);
        QCoreApplication::sendEvent(this, &event);
    }

    void setExtension(const QString& suffix)
    {
        m_extension = suffix.isEmpty() ? QString() : QLatin1Char('.') + suffix;
        m_color = SaveAllDialog::extensionColor(suffix);
        setTextMargins(0, 0, m_extension.isEmpty() ? 0 : fontMetrics().horizontalAdvance(m_extension) + 2, 0);
        update();
    }

protected:
    void paintEvent(QPaintEvent* e) override
    {
        QLineEdit::paintEvent(e);
        if (m_extension.isEmpty() || text().isEmpty())
            return;
        QStyleOptionFrame opt;
        initStyleOption(&opt);
        QRect r = style()->subElementRect(QStyle::SE_LineEditContents, &opt, this);
        r = r.marginsRemoved(textMargins());
        // QLineEdit deja 2 píxeles a cada lado del texto.
        const int x = std::min(r.x() + 2 + fontMetrics().horizontalAdvance(text()), r.right() - 1);
        QPainter p(this);
        p.setPen(isEnabled() ? m_color : palette().color(QPalette::Disabled, QPalette::Text));
        p.drawText(QRect(x, r.y(), width() - x, r.height()), Qt::AlignLeft | Qt::AlignVCenter, m_extension);
    }

private:
    QString m_extension;
    QColor m_color;
    QList<QPair<int, int>> m_highlights;
};

// Los puntos para arrastrar una fila: solo se ven con el cursor encima de ella.
class DragHandle : public QWidget {
public:
    explicit DragHandle(QWidget* parent)
        : QWidget(parent)
    {
        setFixedWidth(12);
        setCursor(Qt::OpenHandCursor);
        setToolTip(SaveAllDialog::tr("Arrastra para cambiar el orden"));
    }

    void setShown(bool shown)
    {
        if (m_shown != shown) {
            m_shown = shown;
            update();
        }
    }

    std::function<void(int)> dragged;
    std::function<void(bool)> dragging;

protected:
    void paintEvent(QPaintEvent*) override
    {
        if (!m_shown)
            return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(palette().color(QPalette::WindowText));
        const int rows = 4;
        const int gap = 5;
        const int top = (height() - (rows - 1) * gap) / 2;
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < 2; ++c)
                p.drawEllipse(QPointF(3.5 + c * 5, top + r * gap), 1.5, 1.5);
    }
    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() != Qt::LeftButton)
            return;
        setCursor(Qt::ClosedHandCursor);
        if (dragging)
            dragging(true);
    }
    void mouseMoveEvent(QMouseEvent* e) override
    {
        if ((e->buttons() & Qt::LeftButton) && dragged)
            dragged(int(e->globalPosition().y()));
    }
    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (e->button() != Qt::LeftButton)
            return;
        setCursor(Qt::OpenHandCursor);
        if (dragging)
            dragging(false);
    }

private:
    bool m_shown = false;
};

// Una imagen de la lista: los puntos para moverla, si se guarda, su miniatura, el nombre y el formato.
class Row : public QWidget {
public:
    Row(int index, const QPixmap& thumb, QWidget* parent)
        : QWidget(parent)
        , index(index)
    {
        auto* h = new QHBoxLayout(this);
        h->setContentsMargins(2, 3, 6, 3);
        h->setSpacing(8);
        handle = new DragHandle(this);
        check = new QCheckBox(this);
        check->setToolTip(SaveAllDialog::tr("Guardar esta imagen"));
        auto* image = new QLabel(this);
        image->setPixmap(thumb);
        image->setAlignment(Qt::AlignCenter);
        image->setFixedSize(kThumb + 4, kThumb + 4);
        image->setFrameShape(QFrame::StyledPanel);
        name = new NameEdit(this);
        name->setMinimumWidth(220);
        format = new QComboBox(this);
        format->setMinimumWidth(84);
        h->addWidget(handle);
        h->addWidget(check);
        h->addWidget(image);
        h->addWidget(name, 1);
        h->addWidget(format);
        setAttribute(Qt::WA_Hover);
    }

    QString suffix() const { return format->currentData().toString(); }
    void setDragging(bool on)
    {
        m_dragging = on;
        handle->setShown(on || underMouse());
        update();
    }

    int index;
    // El nombre se cambió a mano: el nombre común no lo pisa (hasta que se vuelva a escribir).
    bool custom = false;
    DragHandle* handle;
    QCheckBox* check;
    NameEdit* name;
    QComboBox* format;

protected:
    void enterEvent(QEnterEvent* e) override
    {
        handle->setShown(true);
        update();
        QWidget::enterEvent(e);
    }
    void leaveEvent(QEvent* e) override
    {
        handle->setShown(m_dragging);
        update();
        QWidget::leaveEvent(e);
    }
    void paintEvent(QPaintEvent*) override
    {
        if (!m_dragging && !underMouse())
            return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QColor c = palette().color(QPalette::Highlight);
        c.setAlpha(m_dragging ? 70 : 32);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
    }

private:
    bool m_dragging = false;
};

} // namespace SaveAll

using SaveAll::Row;

QColor SaveAllDialog::extensionColor(const QString& suffix)
{
    const QString s = suffix.toLower();
    if (s == QLatin1String("png"))
        return QColor(229, 57, 53);
    if (s == QLatin1String("jpg") || s == QLatin1String("jpeg") || s == QLatin1String("jpe") || s == QLatin1String("jfif"))
        return QColor(41, 98, 255);
    if (s == QLatin1String("webp"))
        return QColor(46, 160, 67);
    if (s == QLatin1String("gif"))
        return QColor(156, 39, 176);
    if (s == QLatin1String("bmp"))
        return QColor(239, 108, 0);
    if (s == QLatin1String("ora"))
        return QColor(0, 150, 136);
    if (s == QLatin1String("pdn"))
        return QColor(63, 81, 181);
    if (s == QLatin1String("tif") || s == QLatin1String("tiff"))
        return QColor(121, 85, 72);
    if (s == QLatin1String("ico"))
        return QColor(0, 151, 167);
    // El resto, un tono fijo sacado de la extensión.
    return QColor::fromHsv(int(qHash(s) % 360), 190, 210);
}

QString SaveAllDialog::numberText(int number, int digits, const QString& decoration)
{
    const QString n = QStringLiteral("%1").arg(number, std::max(1, digits), 10, QLatin1Char('0'));
    return decoration.contains(QLatin1String("%1")) ? QString(decoration).replace(QLatin1String("%1"), n) : decoration + n;
}

QString SaveAllDialog::composeName(const QString& pattern, int number, int digits, const QString& decoration, int* numberStart,
                                   int* numberLength)
{
    // '#' (o '##', '###'...: también son las cifras mínimas) marca dónde va el número.
    const int hash = int(pattern.indexOf(QLatin1Char('#')));
    int end = hash;
    while (hash >= 0 && end < pattern.size() && pattern[end] == QLatin1Char('#'))
        ++end;
    const int minDigits = hash < 0 ? digits : std::max(digits, end - hash);
    const QString token = number < 0 ? QString() : numberText(number, minDigits, decoration);
    // Solo las cifras (sin lo que las rodea, como " (" y ")").
    const int at = hash < 0 ? int(pattern.size()) : hash;
    const int inToken = decoration.contains(QLatin1String("%1")) ? int(decoration.indexOf(QLatin1String("%1"))) : int(decoration.size());
    const int length = number < 0 ? 0 : int(QStringLiteral("%1").arg(number, std::max(1, minDigits), 10, QLatin1Char('0')).size());
    if (numberStart)
        *numberStart = at + (number < 0 ? 0 : inToken);
    if (numberLength)
        *numberLength = length;
    return hash < 0 ? pattern + token : pattern.left(hash) + token + pattern.mid(end);
}

QColor SaveAllDialog::numberColor(const QColor& background)
{
    return background.lightnessF() < 0.5 ? QColor(0xff, 0xd5, 0x4a) : QColor(0xb8, 0x8a, 0x00);
}

SaveAllDialog::SaveAllDialog(const QList<Entry>& entries, const QStringList& suffixes, const QStringList& labels,
                             const QString& folder, QWidget* parent)
    : QDialog(parent)
    , m_entries(entries)
    , m_suffixes(suffixes)
    , m_labels(labels)
{
    setWindowTitle(tr("Guardar todos"));
    setWindowIcon(Icons::theme({"document-save-all", "document-save"}));
    QSettings settings;

    auto* intro = new QLabel(tr("Marca las imágenes que quieres guardar. Puedes darles a todas el mismo nombre con un "
                                "número (# marca dónde va; si no, va al final) y el mismo formato, o cambiar cada una. "
                                "Arrastra los puntos de una imagen para cambiar el orden en que se numeran."),
                             this);
    intro->setWordWrap(true);

    // Fila de "todas": el nombre y el formato comunes, en las mismas columnas que las imágenes.
    auto* allRow = new QWidget(this);
    auto* allLayout = new QHBoxLayout(allRow);
    allLayout->setContentsMargins(2, 0, 6, 0);
    allLayout->setSpacing(8);
    auto* handleSpace = new QWidget(allRow);
    handleSpace->setFixedWidth(12);
    m_all = new QCheckBox(allRow);
    m_all->setTristate(true);
    m_all->setToolTip(tr("Marcar o desmarcar todas"));
    auto* allLabel = new QLabel(tr("Todas"), allRow);
    allLabel->setFixedWidth(SaveAll::kThumb + 4);
    allLabel->setAlignment(Qt::AlignCenter);
    m_pattern = new SaveAll::NameEdit(allRow);
    m_pattern->setPlaceholderText(tr("Nombre para todas (por ejemplo, foto#)"));
    m_pattern->setClearButtonEnabled(true);
    m_pattern->setToolTip(tr("El mismo nombre para todas las imágenes marcadas, con un número que las distingue. "
                             "# marca dónde va el número; si no hay, va al final."));
    m_allFormat = new QComboBox(allRow);
    m_allFormat->setMinimumWidth(84);
    m_allFormat->setToolTip(tr("El mismo formato para todas"));
    m_allFormat->addItem(tr("Varios"), QString());
    for (int i = 0; i < m_suffixes.size(); ++i) {
        m_allFormat->addItem(m_suffixes[i], m_suffixes[i]);
        m_allFormat->setItemData(m_allFormat->count() - 1, m_labels.value(i), Qt::ToolTipRole);
    }
    allLayout->addWidget(handleSpace);
    allLayout->addWidget(m_all);
    allLayout->addWidget(allLabel);
    allLayout->addWidget(m_pattern, 1);
    allLayout->addWidget(m_allFormat);

    // Cómo es el número.
    auto* numbers = new QHBoxLayout;
    numbers->setSpacing(6);
    m_start = new QSpinBox(this);
    m_start->setRange(0, 999999);
    m_start->setValue(settings.value(QStringLiteral("saveAll/start"), 1).toInt());
    m_digits = new QSpinBox(this);
    m_digits->setRange(1, 8);
    m_digits->setValue(settings.value(QStringLiteral("saveAll/digits"), 1).toInt());
    m_digits->setToolTip(tr("Cifras mínimas del número: con 3, 001, 002..."));
    m_decoration = new QComboBox(this);
    const QString sample = tr("nombre");
    const QStringList decorations{QStringLiteral("%1"), QStringLiteral(" %1"), QStringLiteral("_%1"), QStringLiteral("-%1"),
                                  QStringLiteral(" (%1)"), QStringLiteral("(%1)"), QStringLiteral(" [%1]")};
    for (const QString& d : decorations)
        m_decoration->addItem(sample + QString(d).replace(QLatin1String("%1"), QStringLiteral("1")), d);
    m_decoration->setCurrentIndex(
        std::max(0, int(decorations.indexOf(settings.value(QStringLiteral("saveAll/decoration"), decorations[0]).toString()))));
    m_decoration->setToolTip(tr("Formato del número"));
    m_numberFirst = new QCheckBox(tr("La primera también lleva número"), this);
    m_numberFirst->setChecked(settings.value(QStringLiteral("saveAll/numberFirst"), false).toBool());
    m_numberFirst->setToolTip(tr("Sin marcar, la primera se llama como el nombre común (foto, foto1, foto2...)."));
    numbers->addWidget(new QLabel(tr("Número:"), this));
    numbers->addWidget(m_decoration);
    numbers->addSpacing(8);
    numbers->addWidget(new QLabel(tr("Empieza en"), this));
    numbers->addWidget(m_start);
    numbers->addSpacing(8);
    numbers->addWidget(new QLabel(tr("Cifras"), this));
    numbers->addWidget(m_digits);
    numbers->addSpacing(8);
    numbers->addWidget(m_numberFirst);
    numbers->addStretch();

    // Las imágenes.
    auto* list = new QWidget;
    m_rowLayout = new QVBoxLayout(list);
    m_rowLayout->setContentsMargins(0, 0, 0, 0);
    m_rowLayout->setSpacing(2);
    for (int i = 0; i < m_entries.size(); ++i) {
        const Entry& e = m_entries[i];
        auto* row = new Row(i, SaveAll::thumbnail(e.image), list);
        row->check->setChecked(e.checked);
        row->name->setText(e.name);
        row->name->setToolTip(e.filePath.isEmpty() ? tr("Sin guardar") : QDir::toNativeSeparators(e.filePath));
        for (int k = 0; k < m_suffixes.size(); ++k) {
            row->format->addItem(m_suffixes[k], m_suffixes[k]);
            row->format->setItemData(k, m_labels.value(k), Qt::ToolTipRole);
        }
        int at = int(m_suffixes.indexOf(e.suffix));
        if (at < 0 && !e.suffix.isEmpty()) {
            // Una variante de la extensión (jpeg, tif...): se queda tal cual en su imagen.
            row->format->addItem(e.suffix, e.suffix);
            at = row->format->count() - 1;
        }
        row->format->setCurrentIndex(std::max(0, at));
        row->name->setExtension(row->suffix());
        m_rows << row;
        m_rowLayout->addWidget(row);

        connect(row->check, &QCheckBox::toggled, this, [this] {
            syncAllControls();
            applyPattern(false);
        });
        connect(row->name, &QLineEdit::textEdited, this, [row] {
            row->custom = true;
            // Escrito a mano: el número deja de marcarse.
            row->name->setHighlights({});
        });
        connect(row->format, &QComboBox::currentIndexChanged, this, [this, row] {
            row->name->setExtension(row->suffix());
            syncAllControls();
        });
        row->handle->dragging = [this, row](bool on) {
            row->setDragging(on);
            if (!on)
                applyPattern(false);
        };
        row->handle->dragged = [this, row](int y) { moveRow(row, y); };
    }
    m_rowLayout->addStretch();
    auto* scroll = new QScrollArea(this);
    scroll->setWidget(list);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setMinimumHeight(std::min(int(m_entries.size()), 4) * (SaveAll::kThumb + 12) + 8);

    // Dónde se guardan.
    auto* where = new QHBoxLayout;
    m_folder = new QLineEdit(QDir::toNativeSeparators(folder), this);
    auto* browse = new QPushButton(tr("Examinar..."), this);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Carpeta"), QDir::fromNativeSeparators(m_folder->text()));
        if (!dir.isEmpty())
            m_folder->setText(QDir::toNativeSeparators(dir));
    });
    where->addWidget(new QLabel(tr("Carpeta:"), this));
    where->addWidget(m_folder, 1);
    where->addWidget(browse);
    m_ownFolder = new QCheckBox(tr("Las que ya tienen archivo se guardan en su misma carpeta"), this);
    m_ownFolder->setChecked(settings.value(QStringLiteral("saveAll/ownFolder"), true).toBool());

    m_count = new QLabel(this);
    auto* buttons = new QDialogButtonBox(this);
    m_ok = buttons->addButton(tr("Guardar"), QDialogButtonBox::AcceptRole);
    buttons->addButton(tr("Cancelar"), QDialogButtonBox::RejectRole);
    m_ok->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* bottom = new QHBoxLayout;
    bottom->addWidget(m_count, 1);
    bottom->addWidget(buttons);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addSpacing(4);
    layout->addWidget(allRow);
    layout->addLayout(numbers);
    layout->addWidget(scroll, 1);
    layout->addLayout(where);
    layout->addWidget(m_ownFolder);
    layout->addLayout(bottom);

    connect(m_all, &QCheckBox::clicked, this, [this] {
        // Un clic en la casilla de todas: si no estaban todas, todas; si sí, ninguna.
        const bool every = std::all_of(m_rows.cbegin(), m_rows.cend(), [](const Row* r) { return r->check->isChecked(); });
        m_syncing = true;
        for (Row* r : std::as_const(m_rows))
            r->check->setChecked(!every);
        m_syncing = false;
        syncAllControls();
        applyPattern(false);
    });
    connect(m_pattern, &QLineEdit::textChanged, this, [this](const QString& text) {
        // Los '#' del nombre común, en amarillo como los números que serán.
        QList<QPair<int, int>> hashes;
        for (int i = 0; i < text.size(); ++i) {
            if (text[i] != QLatin1Char('#'))
                continue;
            int end = i;
            while (end < text.size() && text[end] == QLatin1Char('#'))
                ++end;
            hashes.append({i, end - i});
            i = end;
        }
        m_pattern->setHighlights(hashes);
        applyPattern(true);
    });
    connect(m_allFormat, &QComboBox::activated, this, [this] { applyFormatToAll(); });
    connect(m_start, &QSpinBox::valueChanged, this, [this] { applyPattern(false); });
    connect(m_digits, &QSpinBox::valueChanged, this, [this] { applyPattern(false); });
    connect(m_decoration, &QComboBox::currentIndexChanged, this, [this] { applyPattern(false); });
    connect(m_numberFirst, &QCheckBox::toggled, this, [this] { applyPattern(false); });

    syncAllControls();
    resize(720, std::min(640, sizeHint().height() + 40));
}

SaveAllDialog::~SaveAllDialog() = default;

void SaveAllDialog::applyPattern(bool resetCustom)
{
    if (m_syncing)
        return;
    const QString pattern = m_pattern->text();
    if (resetCustom)
        for (Row* r : std::as_const(m_rows))
            r->custom = false;
    m_pattern->setExtension(m_allFormat->currentData().toString());
    if (pattern.isEmpty())
        return;
    const bool numberFirst = m_numberFirst->isChecked();
    const QString decoration = m_decoration->currentData().toString();
    int k = 0;
    for (Row* r : std::as_const(m_rows)) {
        if (!r->check->isChecked())
            continue;
        const int number = numberFirst ? m_start->value() + k : (k == 0 ? -1 : m_start->value() + k - 1);
        if (!r->custom) {
            int start = 0, length = 0;
            r->name->setText(composeName(pattern, number, m_digits->value(), decoration, &start, &length));
            r->name->setHighlights({{start, length}});
        }
        ++k;
    }
}

void SaveAllDialog::applyFormatToAll()
{
    const QString suffix = m_allFormat->currentData().toString();
    if (suffix.isEmpty())
        return syncAllControls();
    m_syncing = true;
    for (Row* r : std::as_const(m_rows)) {
        const int at = r->format->findData(suffix);
        if (at >= 0)
            r->format->setCurrentIndex(at);
    }
    m_syncing = false;
    syncAllControls();
}

void SaveAllDialog::syncAllControls()
{
    if (m_syncing)
        return;
    int checked = 0;
    QString common;
    bool same = true;
    for (const Row* r : std::as_const(m_rows)) {
        if (r->check->isChecked())
            ++checked;
        if (common.isEmpty())
            common = r->suffix();
        else if (common != r->suffix())
            same = false;
    }
    m_all->setCheckState(checked == 0 ? Qt::Unchecked : checked == m_rows.size() ? Qt::Checked : Qt::PartiallyChecked);
    const int at = same ? m_allFormat->findData(common) : 0;
    m_allFormat->setCurrentIndex(std::max(0, at));
    m_pattern->setExtension(m_allFormat->currentData().toString());
    m_count->setText(tr("%n imagen(es) para guardar", nullptr, checked));
    m_ok->setEnabled(checked > 0);
}

void SaveAllDialog::moveRow(Row* row, int globalY)
{
    const int from = int(m_rows.indexOf(row));
    if (from < 0)
        return;
    QWidget* list = row->parentWidget();
    const int y = list->mapFromGlobal(QPoint(0, globalY)).y();
    // Va delante de la primera fila (sin contar ella misma) cuyo centro queda por debajo del cursor.
    int to = 0;
    for (const Row* r : std::as_const(m_rows))
        if (r != row && r->geometry().center().y() < y)
            ++to;
    if (to == from)
        return;
    m_rows.move(from, to);
    m_rowLayout->removeWidget(row);
    m_rowLayout->insertWidget(to, row);
    // Que se siga viendo la fila al arrastrarla hasta el borde de la lista.
    for (QWidget* w = list->parentWidget(); w; w = w->parentWidget())
        if (auto* scroll = qobject_cast<QScrollArea*>(w)) {
            scroll->ensureWidgetVisible(row, 0, 8);
            break;
        }
    applyPattern(false);
}

QString SaveAllDialog::folder() const
{
    return QDir::fromNativeSeparators(m_folder->text().trimmed());
}

QString SaveAllDialog::pathFor(const Row* row) const
{
    const Entry& e = m_entries[row->index];
    QString dir = folder();
    if (m_ownFolder->isChecked() && !e.filePath.isEmpty())
        dir = QFileInfo(e.filePath).absolutePath();
    QString name = row->name->text().trimmed();
    // Si se escribió la extensión en el nombre, no se repite.
    const QString ext = QLatin1Char('.') + row->suffix();
    if (name.endsWith(ext, Qt::CaseInsensitive))
        name.chop(ext.size());
    return QDir(dir).absoluteFilePath(name + ext);
}

QList<SaveAllDialog::Result> SaveAllDialog::results() const
{
    QList<Result> out;
    for (const Row* r : std::as_const(m_rows))
        if (r->check->isChecked())
            out << Result{r->index, pathFor(r)};
    return out;
}

void SaveAllDialog::accept()
{
    const QList<Result> chosen = results();
    if (chosen.isEmpty())
        return;
    QStringList problems;
    bool needsFolder = false;
    for (const Row* r : std::as_const(m_rows)) {
        if (!r->check->isChecked())
            continue;
        if (!SaveAll::validName(r->name->text().trimmed()))
            problems << tr("«%1» no es un nombre de archivo válido.").arg(r->name->text());
        if (!m_ownFolder->isChecked() || m_entries[r->index].filePath.isEmpty())
            needsFolder = true;
    }
    if (needsFolder && folder().isEmpty())
        problems << tr("Elige la carpeta donde se guardan.");
    // Dos imágenes con el mismo archivo.
    QStringList seen;
    for (const Result& r : chosen) {
        const QString key = QDir::cleanPath(r.path);
#ifdef Q_OS_WIN
        const QString cmp = key.toLower();
#else
        const QString& cmp = key;
#endif
        if (seen.contains(cmp)) {
            problems << tr("Hay varias imágenes que se guardarían como «%1».").arg(QFileInfo(r.path).fileName());
            continue;
        }
        seen << cmp;
    }
    if (!problems.isEmpty()) {
        problems.removeDuplicates();
        QMessageBox::warning(this, tr("Guardar todos"), problems.join(QLatin1Char('\n')));
        return;
    }
    if (needsFolder && !QDir().mkpath(folder())) {
        QMessageBox::warning(this, tr("Guardar todos"), tr("No se pudo crear la carpeta «%1».").arg(m_folder->text()));
        return;
    }
    // Archivos que ya existen (y no son el de la propia imagen).
    QStringList existing;
    for (const Result& r : chosen) {
        const QString own = m_entries[r.index].filePath;
        if (QFileInfo::exists(r.path) && QFileInfo(own).absoluteFilePath() != QFileInfo(r.path).absoluteFilePath())
            existing << QFileInfo(r.path).fileName();
    }
    if (!existing.isEmpty()
        && QMessageBox::question(this, tr("Guardar todos"),
                                 tr("Ya existen estos archivos:\n\n%1\n\n¿Quieres reemplazarlos?").arg(existing.join(QLatin1Char('\n'))))
               != QMessageBox::Yes)
        return;
    saveSettings();
    QDialog::accept();
}

void SaveAllDialog::saveSettings() const
{
    QSettings s;
    s.setValue(QStringLiteral("saveAll/start"), m_start->value());
    s.setValue(QStringLiteral("saveAll/digits"), m_digits->value());
    s.setValue(QStringLiteral("saveAll/decoration"), m_decoration->currentData().toString());
    s.setValue(QStringLiteral("saveAll/numberFirst"), m_numberFirst->isChecked());
    s.setValue(QStringLiteral("saveAll/ownFolder"), m_ownFolder->isChecked());
}
