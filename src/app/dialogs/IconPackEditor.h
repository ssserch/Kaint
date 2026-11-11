#pragma once

#include "app/IconPacks.h"
#include "app/Icons.h"

#include <QByteArray>
#include <QDialog>
#include <QHash>
#include <QImage>
#include <QList>
#include <QString>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

// Configuración > Paquetes de iconos > Editor: crear un paquete de iconos propio (o cambiar uno
// instalado). Arriba, sus datos (nombre, identificador, autor, versión y descripción); debajo,
// todos los iconos que un paquete puede cambiar, cada uno con el de Kaint y el del paquete. A cada
// uno se le pone una imagen (un archivo, el portapapeles o arrastrándola), se le quita (vuelve el
// de Kaint) o se guarda el de Kaint para dibujar encima. «Importar imágenes» toma varias a la vez
// por su nombre de archivo. Se guarda como un paquete instalado y se puede exportar el ZIP.
class IconPackEditor : public QDialog {
    Q_OBJECT
public:
    // 'packId': el paquete instalado que se edita; vacío (o el predeterminado), uno nuevo.
    explicit IconPackEditor(const QString& packId, QWidget* parent = nullptr);
    // El paquete que se guardó (vacío si no se guardó).
    QString savedId() const { return m_savedId; }

    // Una imagen tal como se guarda en el paquete (PNG de 256 px como mucho).
    static QByteArray encode(const QImage& image);

protected:
    bool eventFilter(QObject* obj, QEvent* e) override;
    void reject() override;

private:
    void load(const QString& id);
    void fillList();
    void updateItem(QTreeWidgetItem* item);
    void applyFilter();
    void updateButtons();
    QString keyOf(const QTreeWidgetItem* item) const;
    QList<QTreeWidgetItem*> selectedItems() const;
    void setImage(const QString& key, const QImage& image);
    void chooseImage();
    void importImages(const QStringList& files);
    void paste();
    void resetSelected();
    void saveOriginal();
    void exportZip();
    bool save();
    IconPacks::Pack info() const;
    bool confirmDiscard();
    void markDirty();

    QComboBox* m_pack = nullptr;
    QLineEdit* m_name = nullptr;
    QLineEdit* m_id = nullptr;
    QLineEdit* m_author = nullptr;
    QLineEdit* m_version = nullptr;
    QLineEdit* m_description = nullptr;
    QLineEdit* m_search = nullptr;
    QComboBox* m_filter = nullptr;
    QTreeWidget* m_list = nullptr;
    QLabel* m_count = nullptr;
    QPushButton* m_choose = nullptr;
    QPushButton* m_paste = nullptr;
    QPushButton* m_reset = nullptr;
    QPushButton* m_original = nullptr;
    QCheckBox* m_use = nullptr;

    QList<Icons::PackSlot> m_slots;
    // "icons/archivo" → la imagen del paquete (PNG o WebP).
    QHash<QString, QByteArray> m_images;
    QString m_loadedId;
    QString m_savedId;
    bool m_idTouched = false;
    bool m_dirty = false;
    bool m_loading = false;
};
