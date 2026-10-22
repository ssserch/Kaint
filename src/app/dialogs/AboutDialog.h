#pragma once

#include <QDialog>

// Acerca de Kaint, como el de Paint.NET: el logo aparece desenfocado y se aclara en un segundo y
// medio cada vez que se abre; debajo, versión, autor, traducción, enlace con la firma en ola y
// las bibliotecas y aportaciones en las que se apoya.
class AboutDialog : public QDialog {
    Q_OBJECT
public:
    explicit AboutDialog(QWidget* parent = nullptr);

    // Texto de la lista "Bibliotecas de software y contribuciones" (HTML).
    static QString credits();
};
