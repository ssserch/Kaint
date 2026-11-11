#include "app/dialogs/IconPackEditor.h"

#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

constexpr int kSide = 24;
constexpr int kMaxSide = 256;

enum Column { Original = 0, Mine = 1, File = 2, Names = 3 };

QIcon iconFrom(const QImage& image)
{
    if (image.isNull())
        return {};
    // Centrada en un cuadrado: todas las filas iguales aunque la imagen no sea cuadrada.
    const qreal dpr = qApp->devicePixelRatio();
    const int side = int(kSide * dpr);
    QImage square(side, side, QImage::Format_ARGB32_Premultiplied);
    square.fill(Qt::transparent);
    const QImage scaled = image.scaled(side, side, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPainter p(&square);
    p.drawImage((side - scaled.width()) / 2, (side - scaled.height()) / 2, scaled);
    p.end();
    QPixmap pm = QPixmap::fromImage(square);
    pm.setDevicePixelRatio(dpr);
    return QIcon(pm);
}

QStringList imageFilters()
{
    QStringList out;
    for (const QByteArray& f : QImageReader::supportedImageFormats())
        out << QStringLiteral("*.") + QString::fromLatin1(f);
    return out;
}

} // namespace

IconPackEditor::IconPackEditor(const QString& packId, QWidget* parent)
    : QDialog(parent)
    , m_slots(Icons::packSlots())
{
    setWindowTitle(tr("Editor de paquetes de iconos"));
    setWindowIcon(Icons::pdn(QStringLiteral("paintbrush")));
    auto* layout = new QVBoxLayout(this);

    // Qué paquete se edita.
    auto* top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Paquete:"), this));
    m_pack = new QComboBox(this);
    m_pack->addItem(Icons::theme({"document-new"}), tr("Nuevo paquete"), QString());
    for (const IconPacks::Pack& p : IconPacks::installed())
        if (!p.folder.isEmpty())
            m_pack->addItem(p.name, p.id);
    top->addWidget(m_pack, 1);
    layout->addLayout(top);

    // Sus datos (los de asset-pack.json).
    auto* form = new QFormLayout;
    m_name = new QLineEdit(this);
    m_name->setPlaceholderText(tr("Mis iconos"));
    m_id = new QLineEdit(this);
    m_id->setToolTip(tr("Lo distingue de los demás paquetes: letras, números, «.», «_» y «-». Si se instala otro paquete con el "
                        "mismo identificador, sustituye a este."));
    m_author = new QLineEdit(this);
    m_version = new QLineEdit(this);
    m_version->setPlaceholderText(QStringLiteral("1.0"));
    m_description = new QLineEdit(this);
    form->addRow(tr("Nombre:"), m_name);
    form->addRow(tr("Identificador:"), m_id);
    form->addRow(tr("Autor:"), m_author);
    form->addRow(tr("Versión:"), m_version);
    form->addRow(tr("Descripción:"), m_description);
    layout->addLayout(form);
    connect(m_name, &QLineEdit::textEdited, this, [this](const QString& text) {
        // El identificador sigue al nombre hasta que se escribe uno.
        if (!m_idTouched && m_loadedId.isEmpty())
            m_id->setText(text.trimmed().isEmpty() ? QString() : IconPacks::idFromName(text));
        markDirty();
    });
    connect(m_id, &QLineEdit::textEdited, this, [this] {
        m_idTouched = true;
        markDirty();
    });
    for (QLineEdit* e : {m_author, m_version, m_description})
        connect(e, &QLineEdit::textEdited, this, &IconPackEditor::markDirty);

    // Los iconos.
    auto* filterRow = new QHBoxLayout;
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Buscar un icono…"));
    m_search->setClearButtonEnabled(true);
    m_filter = new QComboBox(this);
    m_filter->addItem(tr("Todos"));
    m_filter->addItem(tr("Cambiados"));
    m_filter->addItem(tr("Sin cambiar"));
    m_count = new QLabel(this);
    filterRow->addWidget(m_search, 1);
    filterRow->addWidget(m_filter);
    filterRow->addWidget(m_count);
    layout->addLayout(filterRow);
    connect(m_search, &QLineEdit::textChanged, this, &IconPackEditor::applyFilter);
    connect(m_filter, &QComboBox::currentIndexChanged, this, &IconPackEditor::applyFilter);

    m_list = new QTreeWidget(this);
    m_list->setColumnCount(4);
    m_list->setHeaderLabels({tr("Kaint"), tr("Paquete"), tr("Archivo"), tr("Se usa en")});
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setIconSize(QSize(kSide, kSide));
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setAlternatingRowColors(true);
    m_list->setMinimumHeight(260);
    m_list->header()->setSectionResizeMode(Original, QHeaderView::ResizeToContents);
    m_list->header()->setSectionResizeMode(Mine, QHeaderView::ResizeToContents);
    m_list->header()->setSectionResizeMode(File, QHeaderView::ResizeToContents);
    m_list->header()->setStretchLastSection(true);
    m_list->setAcceptDrops(true);
    m_list->viewport()->setAcceptDrops(true);
    m_list->viewport()->installEventFilter(this);
    m_list->setToolTip(tr("Arrastra imágenes aquí: sobre un icono para cambiarlo, o varias a la vez con el nombre de archivo de "
                          "cada icono."));
    layout->addWidget(m_list, 1);
    connect(m_list, &QTreeWidget::itemSelectionChanged, this, &IconPackEditor::updateButtons);
    connect(m_list, &QTreeWidget::itemDoubleClicked, this, &IconPackEditor::chooseImage);

    auto* actions = new QHBoxLayout;
    m_choose = new QPushButton(Icons::theme({"document-open"}), tr("Elegir imagen…"), this);
    m_choose->setToolTip(tr("Cambia el icono elegido por una imagen (PNG, WebP…). Las de más de 256 px se reducen."));
    m_paste = new QPushButton(Icons::theme({"edit-paste"}), tr("Pegar"), this);
    m_paste->setToolTip(tr("Usa la imagen del portapapeles para los iconos elegidos."));
    m_reset = new QPushButton(Icons::theme({"edit-undo", "edit-reset"}), tr("Usar el de Kaint"), this);
    m_reset->setToolTip(tr("Quita la imagen del paquete: vuelve a verse el icono de Kaint."));
    m_original = new QPushButton(Icons::theme({"document-save-as"}), tr("Guardar el de Kaint…"), this);
    m_original->setToolTip(tr("Guarda el icono de Kaint como PNG para dibujar el tuyo encima (en Kaint o en otro programa)."));
    auto* import = new QPushButton(Icons::theme({"document-import", "list-add"}), tr("Importar imágenes…"), this);
    import->setToolTip(tr("Varias imágenes a la vez: cada una cambia el icono que tiene su mismo nombre de archivo "
                          "(por ejemplo, menu_file_save_icon.png)."));
    for (QPushButton* b : {m_choose, m_paste, m_reset, m_original})
        actions->addWidget(b);
    actions->addStretch();
    actions->addWidget(import);
    layout->addLayout(actions);
    connect(m_choose, &QPushButton::clicked, this, &IconPackEditor::chooseImage);
    connect(m_paste, &QPushButton::clicked, this, &IconPackEditor::paste);
    connect(m_reset, &QPushButton::clicked, this, &IconPackEditor::resetSelected);
    connect(m_original, &QPushButton::clicked, this, &IconPackEditor::saveOriginal);
    connect(import, &QPushButton::clicked, this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(this, tr("Importar imágenes"), QDir::homePath(),
                                                                tr("Imágenes (%1)").arg(imageFilters().join(QLatin1Char(' '))));
        if (!files.isEmpty())
            importImages(files);
    });

    m_use = new QCheckBox(tr("Usar este paquete al guardarlo"), this);
    m_use->setChecked(true);
    layout->addWidget(m_use);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    auto* exportButton = box->addButton(tr("Exportar ZIP…"), QDialogButtonBox::ActionRole);
    exportButton->setIcon(Icons::theme({"document-export", "document-save-as"}));
    exportButton->setToolTip(tr("Guarda el paquete como un archivo .zip para compartirlo (se instala con Paquetes de iconos > Añadir)."));
    connect(exportButton, &QPushButton::clicked, this, &IconPackEditor::exportZip);
    connect(box, &QDialogButtonBox::accepted, this, [this] {
        if (save())
            accept();
    });
    connect(box, &QDialogButtonBox::rejected, this, &IconPackEditor::reject);
    layout->addWidget(box);

    fillList();
    const int start = packId.isEmpty() || packId == IconPacks::defaultId() ? 0 : std::max(0, m_pack->findData(packId));
    m_pack->setCurrentIndex(start);
    load(m_pack->itemData(start).toString());
    connect(m_pack, &QComboBox::activated, this, [this](int i) {
        const QString id = m_pack->itemData(i).toString();
        if (id == m_loadedId)
            return;
        if (!confirmDiscard()) {
            const QSignalBlocker block(m_pack);
            m_pack->setCurrentIndex(std::max(0, m_pack->findData(m_loadedId)));
            return;
        }
        load(id);
    });
    resize(760, 680);
}

QByteArray IconPackEditor::encode(const QImage& source)
{
    if (source.isNull())
        return {};
    QImage image = source.convertToFormat(QImage::Format_ARGB32);
    if (image.width() > kMaxSide || image.height() > kMaxSide)
        image = image.scaled(kMaxSide, kMaxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return data;
}

void IconPackEditor::load(const QString& id)
{
    m_loading = true;
    m_loadedId = id;
    m_images.clear();
    m_idTouched = false;
    IconPacks::Pack pack;
    for (const IconPacks::Pack& p : IconPacks::installed())
        if (p.id == id && !id.isEmpty())
            pack = p;
    m_name->setText(pack.name);
    m_id->setText(pack.id);
    m_author->setText(pack.author);
    m_version->setText(pack.version);
    m_description->setText(pack.description);
    if (!id.isEmpty()) {
        const QHash<QString, QString> files = IconPacks::images(id);
        for (auto it = files.cbegin(); it != files.cend(); ++it) {
            QFile f(*it);
            if (f.open(QIODevice::ReadOnly))
                m_images.insert(it.key(), f.readAll());
        }
    }
    // Uno nuevo lleva como autor el nombre del modo colaborativo, si se puso uno.
    if (id.isEmpty())
        m_author->setText(QSettings().value(QStringLiteral("collab/name")).toString().trimmed());
    for (int i = 0; i < m_list->topLevelItemCount(); ++i)
        updateItem(m_list->topLevelItem(i));
    m_dirty = false;
    m_loading = false;
    applyFilter();
    updateButtons();
}

void IconPackEditor::fillList()
{
    m_list->clear();
    for (const Icons::PackSlot& slot : std::as_const(m_slots)) {
        auto* item = new QTreeWidgetItem(m_list);
        item->setData(File, Qt::UserRole, slot.folder + QLatin1Char('/') + slot.file);
        item->setIcon(Original, iconFrom(Icons::originalPackImage(slot.folder, slot.file, kSide * 2)));
        item->setText(File, slot.folder == QLatin1String("icons") ? slot.file : slot.folder + QLatin1Char('/') + slot.file);
        item->setText(Names, slot.names.join(QStringLiteral(", ")));
        item->setToolTip(File, slot.folder + QLatin1Char('/') + slot.file + QStringLiteral(".png"));
    }
}

QString IconPackEditor::keyOf(const QTreeWidgetItem* item) const
{
    return item ? item->data(File, Qt::UserRole).toString() : QString();
}

void IconPackEditor::updateItem(QTreeWidgetItem* item)
{
    const QByteArray data = m_images.value(keyOf(item));
    const QImage image = data.isEmpty() ? QImage() : QImage::fromData(data);
    item->setIcon(Mine, iconFrom(image));
    item->setText(Mine, image.isNull() ? QStringLiteral("—") : QString());
    item->setToolTip(Mine, image.isNull() ? tr("Sin imagen: se ve el de Kaint.") : tr("%1 × %2 px").arg(image.width()).arg(image.height()));
    QFont f = item->font(File);
    f.setBold(!image.isNull());
    item->setFont(File, f);
}

void IconPackEditor::applyFilter()
{
    const QString text = m_search->text().trimmed();
    int shown = 0;
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = m_list->topLevelItem(i);
        const bool changed = m_images.contains(keyOf(item));
        bool visible = text.isEmpty() || item->text(File).contains(text, Qt::CaseInsensitive)
                       || item->text(Names).contains(text, Qt::CaseInsensitive);
        if (m_filter->currentIndex() == 1)
            visible = visible && changed;
        else if (m_filter->currentIndex() == 2)
            visible = visible && !changed;
        item->setHidden(!visible);
        shown += visible;
    }
    m_count->setText(tr("%1 de %2 · %3 cambiados").arg(shown).arg(m_list->topLevelItemCount()).arg(m_images.size()));
}

QList<QTreeWidgetItem*> IconPackEditor::selectedItems() const
{
    QList<QTreeWidgetItem*> out;
    for (QTreeWidgetItem* item : m_list->selectedItems())
        if (!item->isHidden())
            out << item;
    return out;
}

void IconPackEditor::updateButtons()
{
    const QList<QTreeWidgetItem*> items = selectedItems();
    bool anyChanged = false;
    for (QTreeWidgetItem* item : items)
        anyChanged = anyChanged || m_images.contains(keyOf(item));
    m_choose->setEnabled(!items.isEmpty());
    m_paste->setEnabled(!items.isEmpty());
    m_reset->setEnabled(anyChanged);
    m_original->setEnabled(items.size() == 1);
}

void IconPackEditor::markDirty()
{
    if (!m_loading)
        m_dirty = true;
}

void IconPackEditor::setImage(const QString& key, const QImage& image)
{
    const QByteArray data = encode(image);
    if (data.isEmpty())
        return;
    m_images.insert(key, data);
    for (int i = 0; i < m_list->topLevelItemCount(); ++i)
        if (keyOf(m_list->topLevelItem(i)) == key)
            updateItem(m_list->topLevelItem(i));
    markDirty();
}

void IconPackEditor::chooseImage()
{
    const QList<QTreeWidgetItem*> items = selectedItems();
    if (items.isEmpty())
        return;
    const QString file = QFileDialog::getOpenFileName(this, tr("Elegir imagen"), QDir::homePath(),
                                                      tr("Imágenes (%1)").arg(imageFilters().join(QLatin1Char(' '))));
    if (file.isEmpty())
        return;
    const QImage image(file);
    if (image.isNull()) {
        QMessageBox::warning(this, tr("Elegir imagen"), tr("No se pudo abrir «%1».").arg(QFileInfo(file).fileName()));
        return;
    }
    for (QTreeWidgetItem* item : items)
        setImage(keyOf(item), image);
    applyFilter();
    updateButtons();
}

void IconPackEditor::importImages(const QStringList& files)
{
    // Por el nombre de archivo (sin extensión), en "icons" o en "cursors".
    QHash<QString, QString> byFile;
    for (const Icons::PackSlot& slot : std::as_const(m_slots))
        byFile.insert(slot.file, slot.folder + QLatin1Char('/') + slot.file);
    int used = 0;
    QStringList unknown;
    for (const QString& path : files) {
        const QString key = byFile.value(QFileInfo(path).completeBaseName());
        const QImage image(path);
        if (key.isEmpty() || image.isNull()) {
            unknown << QFileInfo(path).fileName();
            continue;
        }
        setImage(key, image);
        ++used;
    }
    applyFilter();
    updateButtons();
    if (!unknown.isEmpty())
        QMessageBox::information(this, tr("Importar imágenes"),
                                 tr("Se usaron %1 imágenes. Estas no tienen el nombre de ningún icono (o no se pudieron abrir):\n%2")
                                     .arg(used)
                                     .arg(unknown.mid(0, 20).join(QLatin1Char('\n'))));
}

void IconPackEditor::paste()
{
    const QImage image = QApplication::clipboard()->image();
    if (image.isNull()) {
        QMessageBox::information(this, tr("Pegar"), tr("El portapapeles no tiene ninguna imagen."));
        return;
    }
    for (QTreeWidgetItem* item : selectedItems())
        setImage(keyOf(item), image);
    applyFilter();
    updateButtons();
}

void IconPackEditor::resetSelected()
{
    for (QTreeWidgetItem* item : selectedItems()) {
        if (m_images.remove(keyOf(item)) > 0) {
            updateItem(item);
            markDirty();
        }
    }
    applyFilter();
    updateButtons();
}

void IconPackEditor::saveOriginal()
{
    const QList<QTreeWidgetItem*> items = selectedItems();
    if (items.size() != 1)
        return;
    const QString key = keyOf(items.first());
    const QString folder = key.section(QLatin1Char('/'), 0, 0), file = key.section(QLatin1Char('/'), 1);
    const QString path = QFileDialog::getSaveFileName(this, tr("Guardar el icono de Kaint"), QDir::homePath() + QLatin1Char('/') + file
                                                          + QStringLiteral(".png"),
                                                      tr("Imagen PNG (*.png)"));
    if (path.isEmpty())
        return;
    // El PNG tal cual o, si es un dibujo, a 64 px.
    QImage image(QStringLiteral(":/kaint/ui-icons/%1.png").arg(file));
    if (image.isNull() || folder != QLatin1String("icons"))
        image = Icons::originalPackImage(folder, file, 64);
    if (image.isNull() || !image.save(path, "PNG"))
        QMessageBox::warning(this, tr("Guardar el icono de Kaint"), tr("No se pudo guardar «%1».").arg(QDir::toNativeSeparators(path)));
}

IconPacks::Pack IconPackEditor::info() const
{
    IconPacks::Pack p;
    p.name = m_name->text().trimmed();
    p.id = m_id->text().trimmed();
    if (p.id.isEmpty())
        p.id = IconPacks::idFromName(p.name);
    if (p.name.isEmpty())
        p.name = p.id;
    p.author = m_author->text().trimmed();
    p.version = m_version->text().trimmed();
    p.description = m_description->text().trimmed();
    return p;
}

void IconPackEditor::exportZip()
{
    const IconPacks::Pack p = info();
    if (!IconPacks::isValidId(p.id)) {
        QMessageBox::warning(this, tr("Exportar ZIP"), tr("El identificador del paquete no es válido."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("Exportar el paquete de iconos"),
                                                      QDir::homePath() + QLatin1Char('/') + p.id + QStringLiteral(".zip"),
                                                      tr("Paquetes de iconos (*.zip)"));
    if (path.isEmpty())
        return;
    QSaveFile out(path);
    const QByteArray data = IconPacks::zip(p, m_images);
    if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit())
        QMessageBox::warning(this, tr("Exportar ZIP"), tr("No se pudo guardar «%1».").arg(QDir::toNativeSeparators(path)));
}

bool IconPackEditor::save()
{
    const IconPacks::Pack p = info();
    if (p.name.isEmpty() && m_name->text().trimmed().isEmpty() && m_id->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, windowTitle(), tr("Ponle un nombre al paquete."));
        m_name->setFocus();
        return false;
    }
    if (!IconPacks::isValidId(p.id)) {
        QMessageBox::warning(this, windowTitle(), tr("El identificador del paquete no es válido: usa letras, números, «.», «_» y «-»."));
        m_id->setFocus();
        return false;
    }
    // Otro paquete instalado con ese identificador se sustituye: se pregunta antes.
    if (p.id != m_loadedId) {
        for (const IconPacks::Pack& other : IconPacks::installed()) {
            if (other.id == p.id && !other.folder.isEmpty()
                && QMessageBox::question(this, windowTitle(),
                                         tr("Ya hay un paquete instalado con el identificador «%1» («%2»). ¿Quieres sustituirlo?")
                                             .arg(p.id, other.name))
                       != QMessageBox::Yes)
                return false;
        }
    }
    QString error;
    if (!IconPacks::save(p, m_images, m_loadedId, &error)) {
        QMessageBox::warning(this, windowTitle(), error);
        return false;
    }
    if (m_use->isChecked())
        IconPacks::activate(p.id);
    m_savedId = p.id;
    m_loadedId = p.id;
    m_dirty = false;
    return true;
}

bool IconPackEditor::confirmDiscard()
{
    if (!m_dirty)
        return true;
    const auto answer = QMessageBox::question(this, windowTitle(), tr("El paquete tiene cambios sin guardar. ¿Quieres guardarlos?"),
                                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Save)
        return save();
    return answer == QMessageBox::Discard;
}

void IconPackEditor::reject()
{
    if (confirmDiscard())
        QDialog::reject();
}

bool IconPackEditor::eventFilter(QObject* obj, QEvent* e)
{
    if (obj != m_list->viewport())
        return QDialog::eventFilter(obj, e);
    if (e->type() == QEvent::DragEnter || e->type() == QEvent::DragMove) {
        auto* drag = static_cast<QDropEvent*>(e);
        if (drag->mimeData()->hasUrls() || drag->mimeData()->hasImage()) {
            drag->acceptProposedAction();
            return true;
        }
    } else if (e->type() == QEvent::Drop) {
        auto* drop = static_cast<QDropEvent*>(e);
        QStringList files;
        for (const QUrl& url : drop->mimeData()->urls())
            if (url.isLocalFile())
                files << url.toLocalFile();
        QTreeWidgetItem* target = m_list->itemAt(drop->position().toPoint());
        // Una imagen sobre un icono lo cambia; varias, cada una por su nombre de archivo.
        if (target && (files.size() == 1 || (files.isEmpty() && drop->mimeData()->hasImage()))) {
            const QImage image = files.isEmpty() ? qvariant_cast<QImage>(drop->mimeData()->imageData()) : QImage(files.first());
            if (!image.isNull()) {
                setImage(keyOf(target), image);
                m_list->setCurrentItem(target);
                applyFilter();
                updateButtons();
            }
        } else if (!files.isEmpty()) {
            importImages(files);
        }
        drop->acceptProposedAction();
        return true;
    }
    return QDialog::eventFilter(obj, e);
}
