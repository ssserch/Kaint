#pragma once

#include <QDialog>
#include <QIcon>
#include <QImage>

class QVBoxLayout;

// Diálogo de decisión como los de Paint.NET 5 ("Cambios no guardados", "Pegar"): miniatura de
// la imagen, el texto de la pregunta y una opción grande por respuesta, con título y
// descripción.
class TaskDialog : public QDialog {
    Q_OBJECT
public:
    TaskDialog(const QString& title, const QIcon& icon, const QImage& thumbnail, const QString& text,
               QWidget* parent = nullptr);

    // Devuelve el índice de la opción. La predeterminada responde a Intro.
    int addChoice(const QIcon& icon, const QString& title, const QString& description, bool isDefault = false);
    // Contenido extra entre el texto y las opciones (las miniaturas de "Cambios no guardados").
    void addContent(QWidget* widget);
    int choose();
    int chosen() const { return m_chosen; }

private:
    QVBoxLayout* m_layout = nullptr;
    QVBoxLayout* m_choices = nullptr;
    int m_count = 0;
    int m_chosen = -1;
};
