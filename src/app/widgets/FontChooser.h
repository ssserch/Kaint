#pragma once

#include <QFontComboBox>
#include <QTimer>

class QComboBox;

// Lista desplegable con texto que se comporta como un botón (tamaño de letra): un clic abre la
// lista y solo con doble clic se puede escribir. Intro aplica y Esc o un clic fuera lo cancelan.
class ComboClickToOpen : public QObject {
    Q_OBJECT
public:
    explicit ComboClickToOpen(QComboBox* combo);
    bool isEditing() const { return m_editing; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void startEditing();
    void stopEditing(bool commit);

    QComboBox* m_combo;
    QTimer m_clickTimer;
    bool m_editing = false;
    QString m_before;
};

// Selector de fuente como el de Paint.NET 5: un clic abre la lista, que muestra cada fuente
// con una frase de ejemplo; con un doble clic sobre el nombre se puede escribir (Intro o un clic
// fuera aplican la fuente con ese nombre o la primera que empiece así; Esc lo cancela).
class FontChooser : public QFontComboBox {
    Q_OBJECT
public:
    explicit FontChooser(QWidget* parent = nullptr);
    bool isEditingName() const { return m_editing; }

signals:
    // Se eligió una fuente (de la lista o escrita): el teclado vuelve al lienzo.
    void chosen();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void startEditing();
    void stopEditing(bool commit);

    QTimer m_clickTimer;
    bool m_editing = false;
    QString m_before;
};
