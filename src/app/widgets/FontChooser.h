#pragma once

#include <QFontComboBox>
#include <QTimer>

// Selector de fuente como el de Paint.NET 5: un clic abre la lista, que
// muestra cada fuente con una frase de ejemplo; con un doble clic sobre el
// nombre se puede escribir (Intro lo aplica; Esc o un clic fuera lo cancela).
class FontChooser : public QFontComboBox {
    Q_OBJECT
public:
    explicit FontChooser(QWidget* parent = nullptr);
    bool isEditingName() const { return m_editing; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void startEditing();
    void stopEditing(bool commit);

    QTimer m_clickTimer; // distingue el clic (abre la lista) del doble clic (escribir)
    bool m_editing = false;
    QString m_before;
};
