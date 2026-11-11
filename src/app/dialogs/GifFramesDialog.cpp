#include "app/dialogs/GifFramesDialog.h"

#include "app/Icons.h"
#include "core/Document.h"
#include "core/Gif.h"
#include "core/Raster.h"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace {

constexpr int kThumb = 96;

} // namespace

GifFramesDialog::GifFramesDialog(const Document* animation, const QString& fileName, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Elegir los fotogramas"));
    setWindowIcon(Icons::theme({"document-open"}));
    auto* layout = new QVBoxLayout(this);
    auto* text = new QLabel(tr("Elige los fotogramas de «%1» que quieres abrir (un clic marca o desmarca cada uno).").arg(fileName), this);
    text->setWordWrap(true);
    layout->addWidget(text);

    m_list = new QListWidget(this);
    m_list->setViewMode(QListView::IconMode);
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setIconSize(QSize(kThumb, kThumb));
    m_list->setGridSize(QSize(kThumb + 24, kThumb + 44));
    m_list->setSpacing(4);
    m_list->setUniformItemSizes(true);
    // Un clic marca o desmarca (sin Ctrl ni Mayús).
    m_list->setSelectionMode(QAbstractItemView::MultiSelection);
    m_list->setMinimumSize(560, 360);
    const qreal dpr = qApp->devicePixelRatio();
    for (int i = 0; i < animation->layerCount(); ++i) {
        const Layer* frame = animation->layer(i);
        QImage thumb = Raster::thumbnail(frame->image, QSize(kThumb, kThumb) * dpr);
        thumb.setDevicePixelRatio(dpr);
        // La miniatura sin el tinte de "seleccionado": sus colores tal cual.
        QIcon icon;
        icon.addPixmap(QPixmap::fromImage(thumb), QIcon::Normal);
        icon.addPixmap(QPixmap::fromImage(thumb), QIcon::Selected);
        auto* item = new QListWidgetItem(icon,
                                         tr("%1 · %2 ms").arg(i + 1).arg(Gif::delayFromName(frame->props.name, 100)), m_list);
        item->setData(Qt::UserRole, i);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        item->setCheckState(Qt::Checked);
        item->setToolTip(frame->props.name);
    }
    m_list->selectAll();
    layout->addWidget(m_list, 1);
    connect(m_list, &QListWidget::itemSelectionChanged, this, &GifFramesDialog::updateState);

    auto* row = new QHBoxLayout;
    auto button = [this, row](const QString& text, auto fn) {
        auto* b = new QPushButton(text, this);
        connect(b, &QPushButton::clicked, this, fn);
        row->addWidget(b);
    };
    button(tr("Todos"), [this] { m_list->selectAll(); });
    button(tr("Ninguno"), [this] { m_list->clearSelection(); });
    button(tr("Invertir"), [this] {
        for (int i = 0; i < m_list->count(); ++i)
            m_list->item(i)->setSelected(!m_list->item(i)->isSelected());
    });
    m_count = new QLabel(this);
    row->addWidget(m_count);
    row->addStretch();
    row->addWidget(new QLabel(tr("Abrir como:"), this));
    m_openAs = new QComboBox(this);
    m_openAs->addItem(tr("Capas de una imagen"), int(OpenAs::Layers));
    m_openAs->setItemData(0, tr("Una sola imagen con un fotograma en cada capa. Se puede guardar como GIF animado."), Qt::ToolTipRole);
    m_openAs->addItem(tr("Imágenes aparte"), int(OpenAs::Images));
    m_openAs->setItemData(1, tr("Cada fotograma se abre como una imagen."), Qt::ToolTipRole);
    row->addWidget(m_openAs);
    layout->addLayout(row);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_ok = box->button(QDialogButtonBox::Ok);
    m_ok->setText(tr("Abrir"));
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(box);
    updateState();
    resize(680, 520);
}

QList<int> GifFramesDialog::frames() const
{
    QList<int> out;
    for (int i = 0; i < m_list->count(); ++i)
        if (m_list->item(i)->isSelected())
            out << m_list->item(i)->data(Qt::UserRole).toInt();
    return out;
}

GifFramesDialog::OpenAs GifFramesDialog::openAs() const
{
    return OpenAs(m_openAs->currentData().toInt());
}

void GifFramesDialog::updateState()
{
    // La casilla de cada miniatura sigue a la selección.
    int chosen = 0;
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem* item = m_list->item(i);
        const QSignalBlocker block(m_list);
        item->setCheckState(item->isSelected() ? Qt::Checked : Qt::Unchecked);
        chosen += item->isSelected();
    }
    m_count->setText(tr("%1 de %2").arg(chosen).arg(m_list->count()));
    m_ok->setEnabled(chosen > 0);
    // Uno solo se abre como una imagen normal.
    m_openAs->setEnabled(chosen > 1);
}
