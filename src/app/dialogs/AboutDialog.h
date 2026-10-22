#pragma once

#include <QDialog>

// Acerca de Kaint: el texto del programa con su icono, como el cuadro "Acerca de" de Qt, y debajo
// del enlace la firma del autor con una ola.
class AboutDialog : public QDialog {
    Q_OBJECT
public:
    AboutDialog(const QString& title, const QString& text, QWidget* parent = nullptr);
};
