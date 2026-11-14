#include "app/dialogs/LanguageEditorDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
constexpr int kKeyRole = Qt::UserRole + 1;
}

LanguageEditorDialog::LanguageEditorDialog(CustomLanguages::Language lang, QWidget* parent)
    : QDialog(parent)
    , m_lang(std::move(lang))
{
    setWindowTitle(tr("Editar el idioma"));
    resize(820, 620);
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    m_name = new QLineEdit(m_lang.name, this);
    m_name->setPlaceholderText(tr("Por ejemplo: Português"));
    m_code = new QLineEdit(m_lang.code, this);
    m_code->setPlaceholderText(QStringLiteral("pt-BR"));
    m_author = new QLineEdit(m_lang.author, this);
    m_author->setPlaceholderText(tr("Tu nombre o tu usuario"));
    m_authorUrl = new QLineEdit(m_lang.authorUrl, this);
    m_authorUrl->setPlaceholderText(QStringLiteral("https://"));
    form->addRow(tr("Nombre del idioma:"), m_name);
    form->addRow(tr("Código del idioma:"), m_code);
    form->addRow(tr("Autor de la traducción:"), m_author);
    form->addRow(tr("Página del autor:"), m_authorUrl);
    layout->addLayout(form);

    auto* row = new QHBoxLayout;
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Buscar…"));
    m_search->setClearButtonEnabled(true);
    m_onlyMissing = new QCheckBox(tr("Solo los que faltan por traducir"), this);
    row->addWidget(m_search, 1);
    row->addWidget(m_onlyMissing);
    layout->addLayout(row);
    m_count = new QLabel(this);
    layout->addWidget(m_count);

    // Una fila por texto: el inglés (su original en español en la ayuda) y la traducción.
    const auto& sources = CustomLanguages::sources();
    m_table = new QTableWidget(int(sources.size()), 2, this);
    m_table->setHorizontalHeaderLabels({tr("Inglés"), tr("Traducción")});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->verticalHeader()->hide();
    m_table->setWordWrap(true);
    for (int i = 0; i < sources.size(); ++i) {
        const auto& s = sources[i];
        auto* english = new QTableWidgetItem(s.english.isEmpty() ? s.key : s.english);
        english->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        english->setToolTip(QString(s.key).replace(QChar(4), QStringLiteral(" · ")));
        english->setData(kKeyRole, s.key);
        m_table->setItem(i, 0, english);
        const QVariant v = m_lang.strings.value(s.key);
        auto* own = new QTableWidgetItem(v.typeId() == QMetaType::QStringList ? v.toStringList().join(QStringLiteral(" / ")) : v.toString());
        m_table->setItem(i, 1, own);
    }
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (item->column() == 1)
            updateCount();
    });
    layout->addWidget(m_table, 1);
    auto* hint = new QLabel(tr("Lo que dejes vacío sale en inglés. Las formas del plural se separan con « / »."), this);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    connect(m_search, &QLineEdit::textChanged, this, &LanguageEditorDialog::filter);
    connect(m_onlyMissing, &QCheckBox::toggled, this, &LanguageEditorDialog::filter);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setText(tr("Guardar"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    updateCount();
}

void LanguageEditorDialog::filter()
{
    const QString q = m_search->text().trimmed();
    const bool missing = m_onlyMissing->isChecked();
    for (int i = 0; i < m_table->rowCount(); ++i) {
        const QString own = m_table->item(i, 1)->text();
        bool show = !missing || own.trimmed().isEmpty();
        if (show && !q.isEmpty())
            show = m_table->item(i, 0)->text().contains(q, Qt::CaseInsensitive) || own.contains(q, Qt::CaseInsensitive)
                   || m_table->item(i, 0)->data(kKeyRole).toString().contains(q, Qt::CaseInsensitive);
        m_table->setRowHidden(i, !show);
    }
}

void LanguageEditorDialog::updateCount()
{
    int done = 0;
    for (int i = 0; i < m_table->rowCount(); ++i)
        done += !m_table->item(i, 1)->text().trimmed().isEmpty();
    m_count->setText(tr("%1 de %2 traducidos (lo que falta sale en inglés).").arg(done).arg(m_table->rowCount()));
}

CustomLanguages::Language LanguageEditorDialog::language() const
{
    CustomLanguages::Language out = m_lang;
    out.name = m_name->text().trimmed();
    if (out.name.isEmpty())
        out.name = tr("Idioma personalizado");
    out.code = m_code->text().trimmed();
    out.author = m_author->text().trimmed();
    const QString url = m_authorUrl->text().trimmed();
    out.authorUrl = url.startsWith(QLatin1String("https://")) || url.startsWith(QLatin1String("http://")) ? url : QString();
    out.strings.clear();
    // Los que no están en la lista (de otra versión de Kaint o de la web) se conservan.
    for (auto it = m_lang.strings.begin(); it != m_lang.strings.end(); ++it)
        out.strings.insert(it.key(), it.value());
    for (int i = 0; i < m_table->rowCount(); ++i) {
        const QString key = m_table->item(i, 0)->data(kKeyRole).toString();
        const QString text = m_table->item(i, 1)->text();
        if (text.trimmed().isEmpty()) {
            out.strings.remove(key);
        } else if (text.contains(QStringLiteral(" / ")) && m_table->item(i, 0)->text().contains(QStringLiteral(" / "))) {
            out.strings.insert(key, text.split(QStringLiteral(" / ")));
        } else {
            out.strings.insert(key, text);
        }
    }
    return out;
}
