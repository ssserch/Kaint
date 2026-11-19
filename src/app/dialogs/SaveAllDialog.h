#pragma once

#include <QDialog>
#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QVBoxLayout;

namespace SaveAll {
class Row;
class NameEdit;
}

// "Guardar todos": las imágenes abiertas en una lista (como la de las imágenes del GIF), cada una
// con su nombre y su formato. Se puede poner el mismo nombre a todas con un número que las
// distingue (desde cuál empieza, con cuántas cifras, si la primera lo lleva...), el mismo formato
// a todas y el orden en que se numeran (arrastrando los puntos que salen al pasar por encima).
// Las que ya están guardadas empiezan sin marcar.
class SaveAllDialog : public QDialog {
    Q_OBJECT
public:
    struct Entry {
        QString name;
        // Archivo actual ("" si nunca se ha guardado).
        QString filePath;
        // Formato inicial (extensión sin punto).
        QString suffix;
        QImage image;
        bool checked = true;
    };
    struct Result {
        // Índice en la lista de entradas.
        int index = 0;
        QString path;
    };

    // 'suffixes': los formatos en que se puede guardar; 'folder', la carpeta de las imágenes que
    // no tienen archivo.
    SaveAllDialog(const QList<Entry>& entries, const QStringList& suffixes, const QStringList& labels, const QString& folder,
                  QWidget* parent = nullptr);
    ~SaveAllDialog() override;

    // Las marcadas, en el orden de la lista.
    QList<Result> results() const;
    QString folder() const;

    // Color con que se muestra la extensión de cada formato.
    static QColor extensionColor(const QString& suffix);
    // El número de una imagen tal como queda en el nombre ('decoration' con %1 en su sitio).
    static QString numberText(int number, int digits, const QString& decoration);
    // El nombre común con el número en el '#' (o al final, si no hay); 'number' < 0: sin número.
    // Cada '*' se cambia por 'base' (el nombre de la imagen sin su número: «puerta_*» deja
    // puerta_hola1, puerta_hola2...). 'numberStart'/'numberLength': dónde queda el número en el
    // nombre (para pintarlo en amarillo).
    static QString composeName(const QString& pattern, int number, int digits, const QString& decoration,
                               int* numberStart = nullptr, int* numberLength = nullptr, const QString& base = {});
    // Un nombre en secuencia separado en su parte común y su número: «hola12» → «hola» y 12,
    // «foto (3)» → «foto» y 3, «fondo» → «fondo» y -1.
    static QString sequenceBase(const QString& name, int* number = nullptr);
    // El amarillo de los números (y de los '#'): más oscuro sobre fondo claro, para que se lea.
    static QColor numberColor(const QColor& background);

    void accept() override;

private:
    void applyPattern(bool resetCustom);
    void applyFormatToAll();
    void syncAllControls();
    void startDrag(SaveAll::Row* row, int globalY);
    void moveRow(SaveAll::Row* row, int globalY);
    void endDrag(SaveAll::Row* row);
    // Lleva cada fila (menos la que se arrastra) a su sitio en la lista, con una animación corta.
    void animateRows(SaveAll::Row* except);
    // Las filas, en el orden de m_rows, dentro del QVBoxLayout.
    void syncLayoutOrder();
    void refreshHover();
    QString pathFor(const SaveAll::Row* row) const;
    void saveSettings() const;

    QList<Entry> m_entries;
    QStringList m_suffixes;
    QStringList m_labels;
    // Filas en el orden de la lista.
    QList<SaveAll::Row*> m_rows;
    QVBoxLayout* m_rowLayout = nullptr;
    QCheckBox* m_all = nullptr;
    SaveAll::NameEdit* m_pattern = nullptr;
    QComboBox* m_allFormat = nullptr;
    QSpinBox* m_start = nullptr;
    QSpinBox* m_digits = nullptr;
    QComboBox* m_decoration = nullptr;
    QCheckBox* m_numberFirst = nullptr;
    // Secuencias: cada grupo de imágenes con el mismo nombre y un número (hola1, hola2...) se numera
    // aparte y, si se pide, con sus propios números.
    QCheckBox* m_bySequence = nullptr;
    QCheckBox* m_keepNumbers = nullptr;
    QLabel* m_sequences = nullptr;
    // Arrastre: dónde empieza cada sitio de la lista (y de dónde se agarró la fila).
    QList<int> m_slotTops;
    int m_dragGrab = 0;
    SaveAll::Row* m_dragRow = nullptr;
    QLineEdit* m_folder = nullptr;
    QCheckBox* m_ownFolder = nullptr;
    QLabel* m_count = nullptr;
    QPushButton* m_ok = nullptr;
    bool m_syncing = false;
};
