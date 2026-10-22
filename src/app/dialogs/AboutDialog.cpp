#include "app/dialogs/AboutDialog.h"

#include "app/Icons.h"
#include "app/widgets/WaveLabel.h"

#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QUrl>

AboutDialog::AboutDialog(const QString& title, const QString& text, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(title);
    auto* icon = new QLabel(this);
    icon->setPixmap(Icons::app().pixmap(64, 64));
    auto* label = new QLabel(text, this);
    label->setTextFormat(Qt::RichText);
    label->setWordWrap(true);
    label->setOpenExternalLinks(true);
    label->setTextInteractionFlags(Qt::TextBrowserInteraction);
    label->setMinimumWidth(380);
    auto* signature = new WaveLabel(QStringLiteral("By "), QStringLiteral("Y2Kerchy"), QUrl(QStringLiteral("https://ssserch.com")),
                                    this);
    auto* box = new QDialogButtonBox(this);
    box->addButton(QObject::tr("Aceptar"), QDialogButtonBox::AcceptRole)->setDefault(true);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);

    auto* layout = new QGridLayout(this);
    layout->setHorizontalSpacing(16);
    layout->addWidget(icon, 0, 0, 2, 1, Qt::AlignTop);
    layout->addWidget(label, 0, 1);
    layout->addWidget(signature, 1, 1, Qt::AlignLeft | Qt::AlignTop);
    layout->addWidget(box, 2, 0, 1, 2);
    layout->setSizeConstraint(QLayout::SetFixedSize);
}
