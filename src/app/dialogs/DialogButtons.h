#pragma once

#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>

// Botones Aceptar/Cancelar propios. Los estándar de QDialogButtonBox se
// "retraducen" a los textos de Qt cuando algún complemento instala un
// traductor (p. ej. los formatos de imagen de KDE) y volverían al inglés.
inline QDialogButtonBox* okCancelButtons(QDialog* dlg)
{
    auto* box = new QDialogButtonBox(dlg);
    QPushButton* ok = box->addButton(QObject::tr("Aceptar"), QDialogButtonBox::AcceptRole);
    box->addButton(QObject::tr("Cancelar"), QDialogButtonBox::RejectRole);
    ok->setDefault(true);
    QObject::connect(box, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    return box;
}
