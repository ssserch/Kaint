#pragma once

#include <QHBoxLayout>
#include <QLabel>
#include <QWidget>

// Caja numérica con su unidad al lado, fuera de la caja, como en Paint.NET ("800 [ ] píxeles"):
// en la caja solo se escribe el número. 'note' va detrás, en gris (p. ej. "0: nunca").
inline QWidget* withUnit(QWidget* field, const QString& unit, const QString& note = QString(), QWidget* parent = nullptr)
{
    auto* row = new QWidget(parent ? parent : field->parentWidget());
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    field->setParent(row);
    layout->addWidget(field);
    if (!unit.isEmpty())
        layout->addWidget(new QLabel(unit, row));
    if (!note.isEmpty()) {
        auto* hint = new QLabel(note, row);
        hint->setEnabled(false);
        layout->addWidget(hint);
    }
    layout->addStretch();
    return row;
}
