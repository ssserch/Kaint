#pragma once

#include "app/CustomLanguages.h"

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QTableWidget;

// Editor de un idioma personalizado: su nombre, su código, quién lo traduce (y su página) y cada
// texto de Kaint, con el inglés al lado. Lo que se deja vacío sale en inglés.
class LanguageEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit LanguageEditorDialog(CustomLanguages::Language lang, QWidget* parent = nullptr);
    CustomLanguages::Language language() const;

private:
    void filter();
    void updateCount();

    CustomLanguages::Language m_lang;
    QLineEdit* m_name = nullptr;
    QLineEdit* m_code = nullptr;
    QLineEdit* m_author = nullptr;
    QLineEdit* m_authorUrl = nullptr;
    QLineEdit* m_search = nullptr;
    QCheckBox* m_onlyMissing = nullptr;
    QLabel* m_count = nullptr;
    QTableWidget* m_table = nullptr;
};
