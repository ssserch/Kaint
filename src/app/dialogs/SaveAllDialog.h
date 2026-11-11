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
    // 'numberStart'/'numberLength': dónde queda el número en el nombre (para pintarlo en amarillo).
    static QString composeName(const QString& pattern, int number, int digits, const QString& decoration,
                               int* numberStart = nullptr, int* numberLength = nullptr);
    // El amarillo de los números (y de los '#'): más oscuro sobre fondo claro, para que se lea.
    static QColor numberColor(const QColor& background);

    void accept() override;

private:
    void applyPattern(bool resetCustom);
    void applyFormatToAll();
    void syncAllControls();
    void moveRow(SaveAll::Row* row, int globalY);
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
    QLineEdit* m_folder = nullptr;
    QCheckBox* m_ownFolder = nullptr;
    QLabel* m_count = nullptr;
    QPushButton* m_ok = nullptr;
    bool m_syncing = false;
};
