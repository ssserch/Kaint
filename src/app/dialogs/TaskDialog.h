#pragma once

#include <QDialog>
#include <QIcon>
#include <QImage>

class QVBoxLayout;

// Diálogo de decisión como los de Paint.NET 5 ("Cambios no guardados",
// "Pegar"): miniatura de la imagen, el texto de la pregunta y una opción
// grande por respuesta, con título y descripción. choose() lo muestra y
// devuelve el índice de la opción elegida (-1 si se cierra sin elegir).
class TaskDialog : public QDialog {
    Q_OBJECT
public:
    TaskDialog(const QString& title, const QIcon& icon, const QImage& thumbnail, const QString& text,
               QWidget* parent = nullptr);

    // Devuelve el índice de la opción. La predeterminada responde a Intro.
    int addChoice(const QIcon& icon, const QString& title, const QString& description, bool isDefault = false);
    int choose();
    int chosen() const { return m_chosen; }

private:
    QVBoxLayout* m_choices = nullptr;
    int m_count = 0;
    int m_chosen = -1;
};
