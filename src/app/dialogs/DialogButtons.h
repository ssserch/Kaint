#pragma once

#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>

// Botones Aceptar/Cancelar propios.
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
