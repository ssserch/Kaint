#include "app/dialogs/TaskDialog.h"

#include <QCommandLinkButton>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>

namespace {

// Miniatura sobre tablero de ajedrez (se ve la transparencia), con borde.
QPixmap thumbnailPixmap(const QImage& image, QSize box, const QPalette& pal)
{
    const QSize size = image.size().scaled(box, Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
    QPixmap pm(size + QSize(2, 2));
    pm.fill(pal.color(QPalette::Mid));
    QPainter p(&pm);
    const QRect inner(QPoint(1, 1), size);
    constexpr int cell = 6;
    for (int y = 0; y < size.height(); y += cell)
        for (int x = 0; x < size.width(); x += cell)
            p.fillRect(QRect(inner.topLeft() + QPoint(x, y), QSize(cell, cell)).intersected(inner),
                       ((x / cell + y / cell) & 1) ? QColor(204, 204, 204) : QColor(255, 255, 255));
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(inner, image);
    return pm;
}

} // namespace

TaskDialog::TaskDialog(const QString& title, const QIcon& icon, const QImage& thumbnail, const QString& text,
                       QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(title);
    if (!icon.isNull())
        setWindowIcon(icon);

    auto* layout = new QVBoxLayout(this);
    layout->setSizeConstraint(QLayout::SetFixedSize);
    auto* top = new QHBoxLayout;
    top->setSpacing(12);
    if (!thumbnail.isNull()) {
        auto* thumb = new QLabel(this);
        thumb->setPixmap(thumbnailPixmap(thumbnail, QSize(120, 80), palette()));
        thumb->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        top->addWidget(thumb, 0, Qt::AlignTop);
    }
    auto* message = new QLabel(text, this);
    message->setWordWrap(true);
    message->setMinimumWidth(260);
    message->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    top->addWidget(message, 1);
    layout->addLayout(top);
    layout->addSpacing(6);

    m_choices = new QVBoxLayout;
    m_choices->setSpacing(2);
    layout->addLayout(m_choices);
}

int TaskDialog::addChoice(const QIcon& icon, const QString& title, const QString& description, bool isDefault)
{
    const int index = m_count++;
    auto* button = new QCommandLinkButton(title, description, this);
    button->setIcon(icon);
    button->setIconSize(QSize(16, 16));
    button->setMinimumWidth(440); // como en Paint.NET: la descripción en una o dos líneas
    button->setObjectName(QStringLiteral("choice%1").arg(index));
    if (isDefault) {
        button->setDefault(true);
        button->setFocus();
    }
    connect(button, &QAbstractButton::clicked, this, [this, index] {
        m_chosen = index;
        accept();
    });
    m_choices->addWidget(button);
    return index;
}

int TaskDialog::choose()
{
    m_chosen = -1;
    exec();
    return m_chosen;
}
