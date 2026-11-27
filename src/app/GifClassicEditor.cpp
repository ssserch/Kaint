#include "app/GifClassicEditor.h"

#include "app/GifEditor.h"
#include "app/GifExport.h"
#include "app/Icons.h"
#include "app/widgets/EncodedPreview.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/Gif.h"
#include "core/ImageIO.h"
#include "core/Layer.h"
#include "core/Raster.h"

#include <QComboBox>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QSplitter>
#include <QStyle>
#include <QToolButton>
#include <QUndoStack>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

// Los botones, grises como los del estudio (los colores fuertes cansaban la vista); el de omitir
// algo más oscuro y el de activar algo más claro, para distinguirlos.
const QColor kSkip(0x22, 0x24, 0x29);
const QColor kAccent(0x2e, 0x31, 0x38);
constexpr int kMaxDelay = 600000;

QString buttonStyle(const QColor& c)
{
    return QStringLiteral("QPushButton { background: %1; color: #ecedef; border: 1px solid #3a3d44; border-radius: 7px; "
                          "padding: 4px 10px; }"
                          "QPushButton:hover { background: %2; border-color: #4a4d55; }"
                          "QPushButton:pressed { background: #0d4a43; border-color: #1fd1b9; }"
                          "QPushButton:disabled { background: #17181c; color: #5b5e65; }")
        .arg(c.name(), c.lighter(130).name());
}

QPixmap thumbOf(const QImage& image, bool dimmed)
{
    QPixmap pm(image.size().scaled(QSize(112, 80), Qt::KeepAspectRatio).expandedTo(QSize(1, 1)));
    QPainter p(&pm);
    p.fillRect(pm.rect(), QBrush(Raster::checkerboard(6)));
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(pm.rect(), image);
    if (dimmed)
        p.fillRect(pm.rect(), QColor(0, 0, 0, 140));
    return pm;
}

} // namespace

GifClassicEditor::GifClassicEditor(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("GifClassicEditor"));
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(0);
    connect(&m_refreshTimer, &QTimer::timeout, this, &GifClassicEditor::refresh);
    m_previewTimer.setSingleShot(true);
    m_previewTimer.setInterval(250);
    connect(&m_previewTimer, &QTimer::timeout, this, &GifClassicEditor::updatePreview);
    buildUi();
    applyLook();
}

GifClassicEditor::~GifClassicEditor() = default;

void GifClassicEditor::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 4, 8, 8);
    root->setSpacing(6);

    // Cabecera (compacta, como la del estudio): el nombre, cambiar al estudio, volver al editor de
    // imágenes y exportar.
    auto* headBox = new QFrame(this);
    headBox->setProperty("gifPanel", true);
    headBox->setFrameShape(QFrame::StyledPanel);
    auto* head = new QHBoxLayout(headBox);
    head->setContentsMargins(8, 2, 6, 2);
    head->setSpacing(6);
    auto* icon = new QLabel(this);
    icon->setPixmap(Icons::theme({"image-gif", "video-x-generic", "media-playback-start"}).pixmap(20, 20));
    m_title = new QLabel(this);
    QFont titleFont = m_title->font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.1);
    m_title->setFont(titleFont);
    auto* studio = new QPushButton(Icons::theme({"video-x-generic", "media-playback-start"}), tr("Estudio"), this);
    studio->setToolTip(tr("Pasa al estudio: la línea de tiempo con pistas, puntos clave y audio."));
    studio->setMinimumHeight(28);
    connect(studio, &QPushButton::clicked, this, &GifClassicEditor::studioRequested);
    auto* back = new QPushButton(Icons::theme({"draw-brush", "applications-graphics"}), tr("Editor de imágenes"), this);
    back->setToolTip(tr("Vuelve a editar esta imagen con las herramientas (cada fotograma es una capa)."));
    back->setMinimumHeight(28);
    connect(back, &QPushButton::clicked, this, &GifClassicEditor::imageEditorRequested);
    auto* exportButton = new QToolButton(this);
    exportButton->setIcon(Icons::theme({"document-export", "document-save-as"}));
    exportButton->setMinimumHeight(28);
    exportButton->setText(tr("Exportar GIF..."));
    exportButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    exportButton->setPopupMode(QToolButton::MenuButtonPopup);
    exportButton->setToolTip(tr("Guarda la animación como GIF, con la configuración de siempre (paleta, cuantización, efectos...). "
                                "En la flecha, otros formatos: PNG animado, WebP animado y vídeo MP4 o WebM."));
    connect(exportButton, &QToolButton::clicked, this, [this] { emit exportRequested(QStringLiteral("gif")); });
    auto* exportMenu = new QMenu(exportButton);
    for (const auto& [format, text] : {std::pair{QStringLiteral("gif"), tr("GIF...")},
                                       std::pair{QStringLiteral("apng"), tr("PNG animado (APNG)...")},
                                       std::pair{QStringLiteral("webp"), tr("WebP animado...")},
                                       std::pair{QStringLiteral("mp4"), tr("Vídeo MP4...")},
                                       std::pair{QStringLiteral("webm"), tr("Vídeo WebM...")}})
        exportMenu->addAction(text, this, [this, format] { emit exportRequested(format); });
    exportButton->setMenu(exportMenu);
    head->addWidget(icon);
    head->addWidget(m_title, 1);
    head->addWidget(studio);
    head->addWidget(back);
    head->addWidget(exportButton);
    root->addWidget(headBox);

    // Los fotogramas, en tarjetas (se desplazan a lo ancho), con la del «+» al final.
    m_framesBox = new QWidget;
    m_framesBox->setObjectName(QStringLiteral("classicFrames"));
    m_framesRow = new QHBoxLayout(m_framesBox);
    m_framesRow->setContentsMargins(0, 0, 0, 0);
    m_framesRow->setSpacing(8);
    m_framesScroll = new QScrollArea(this);
    m_framesScroll->setWidget(m_framesBox);
    m_framesScroll->setWidgetResizable(true);
    m_framesScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_framesScroll->setFixedHeight(232);
    m_add = new QToolButton(m_framesBox);
    m_add->setObjectName(QStringLiteral("addFrames"));
    m_add->setText(QStringLiteral("+"));
    m_add->setToolTip(tr("Añadir fotogramas"));
    QFont plus = m_add->font();
    plus.setPointSizeF(plus.pointSizeF() * 2.6);
    m_add->setFont(plus);
    m_add->setFixedSize(96, 200);
    m_add->setPopupMode(QToolButton::InstantPopup);
    auto* addMenu = new QMenu(m_add);
    addMenu->addAction(Icons::theme({"document-import", "document-open"}), tr("Desde archivos (imágenes, GIF o vídeo)..."), this,
                       &GifClassicEditor::addFromFiles);
    addMenu->addAction(Icons::theme({"document-new"}), tr("Fotograma vacío"), this, &GifClassicEditor::addBlank);
    addMenu->addAction(Icons::theme({"edit-copy", "edit-duplicate"}), tr("Copia del último"), this, [this] {
        if (m_doc && m_doc->layerCount() > 0)
            duplicate(m_doc->layerCount() - 1);
    });
    m_add->setMenu(addMenu);

    auto* page = new QWidget;
    page->setObjectName(QStringLiteral("classicPage"));
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 4, 0);
    layout->setSpacing(8);
    layout->addWidget(m_framesScroll);

    // Rangos
    auto* rangeBox = new QGroupBox(tr("Activar u omitir varios fotogramas"), page);
    auto* rangeGrid = new QGridLayout(rangeBox);
    m_from = new QSpinBox(rangeBox);
    m_to = new QSpinBox(rangeBox);
    m_every = new QSpinBox(rangeBox);
    m_from->setValue(1);
    m_to->setValue(5);
    m_every->setValue(2);
    auto styled = [](QPushButton* b, const QColor& c) {
        b->setStyleSheet(buttonStyle(c));
        return b;
    };
    auto* skipRange = styled(new QPushButton(tr("Omitir rango"), rangeBox), kSkip);
    auto* enableRange = styled(new QPushButton(tr("Activar rango"), rangeBox), kAccent);
    auto* skipEvery = styled(new QPushButton(tr("Omitir cada N.º"), rangeBox), kSkip);
    auto* enableEvery = styled(new QPushButton(tr("Activar cada N.º"), rangeBox), kAccent);
    rangeGrid->addWidget(new QLabel(tr("Desde:"), rangeBox), 0, 0);
    rangeGrid->addWidget(m_from, 0, 1);
    rangeGrid->addWidget(new QLabel(tr("Hasta:"), rangeBox), 0, 2);
    rangeGrid->addWidget(m_to, 0, 3);
    rangeGrid->addWidget(skipRange, 0, 4);
    rangeGrid->addWidget(enableRange, 0, 5);
    rangeGrid->addWidget(new QLabel(tr("Cada:"), rangeBox), 1, 0);
    rangeGrid->addWidget(m_every, 1, 1);
    rangeGrid->addWidget(skipEvery, 1, 4);
    rangeGrid->addWidget(enableEvery, 1, 5);
    rangeGrid->setColumnStretch(6, 1);
    auto inRange = [this](int i) { return i + 1 >= std::min(m_from->value(), m_to->value()) && i + 1 <= std::max(m_from->value(), m_to->value()); };
    auto isNth = [this](int i) { return (i + 1) % std::max(1, m_every->value()) == 0; };
    auto pick = [this](auto test) {
        QList<int> frames;
        for (int i = 0; m_doc && i < m_doc->layerCount(); ++i)
            if (test(i))
                frames << i;
        return frames;
    };
    connect(skipRange, &QPushButton::clicked, this, [=, this] { setShown(pick(inRange), false, tr("Omitir rango")); });
    connect(enableRange, &QPushButton::clicked, this, [=, this] { setShown(pick(inRange), true, tr("Activar rango")); });
    connect(skipEvery, &QPushButton::clicked, this, [=, this] { setShown(pick(isNth), false, tr("Omitir cada N.º")); });
    connect(enableEvery, &QPushButton::clicked, this, [=, this] { setShown(pick(isNth), true, tr("Activar cada N.º")); });
    layout->addWidget(rangeBox);

    // Orden y velocidad
    auto* orderBox = new QGroupBox(tr("Orden y velocidad"), page);
    auto* orderRow = new QHBoxLayout(orderBox);
    auto* reverseButton = new QPushButton(Icons::theme({"media-seek-backward", "go-previous"}), tr("Invertir el orden"), orderBox);
    reverseButton->setToolTip(tr("El último fotograma pasa a ser el primero."));
    auto* boomerangButton = new QPushButton(Icons::theme({"media-playlist-repeat", "view-refresh"}), tr("Ida y vuelta"), orderBox);
    boomerangButton->setToolTip(tr("Añade los fotogramas al revés al final (sin repetir el último ni el primero): la animación va "
                                   "y vuelve, como un bumerán."));
    auto* faster = new QPushButton(tr("Más rápido (×2)"), orderBox);
    faster->setToolTip(tr("La mitad de duración en cada fotograma (cada uno conserva su proporción)."));
    auto* slower = new QPushButton(tr("Más lento (×½)"), orderBox);
    slower->setToolTip(tr("El doble de duración en cada fotograma."));
    connect(reverseButton, &QPushButton::clicked, this, &GifClassicEditor::reverse);
    connect(boomerangButton, &QPushButton::clicked, this, &GifClassicEditor::boomerang);
    connect(faster, &QPushButton::clicked, this,
            [this] { setAllDelays([](int d) { return int(std::lround(d * 0.5)); }, tr("Más rápido (×2)")); });
    connect(slower, &QPushButton::clicked, this, [this] { setAllDelays([](int d) { return d * 2; }, tr("Más lento (×½)")); });
    orderRow->addWidget(reverseButton);
    orderRow->addWidget(boomerangButton);
    orderRow->addSpacing(12);
    orderRow->addWidget(faster);
    orderRow->addWidget(slower);
    orderRow->addStretch();
    layout->addWidget(orderBox);

    // Opciones del GIF: la duración de todos (o los fotogramas por segundo) y las repeticiones.
    auto* gifBox = new QGroupBox(tr("Opciones del GIF"), page);
    auto* gifGrid = new QGridLayout(gifBox);
    auto hint = [gifBox](const QString& text) {
        auto* l = new QLabel(text, gifBox);
        l->setEnabled(false);
        l->setWordWrap(true);
        return l;
    };
    m_delayAll = new QSpinBox(gifBox);
    m_delayAll->setRange(Gif::kMinEditDelayMs, kMaxDelay);
    m_delayAll->setSingleStep(10);
    m_delayAll->setSuffix(tr(" ms"));
    m_delayAll->setKeyboardTracking(false);
    m_fpsLabel = new QLabel(gifBox);
    m_fps = new QComboBox(gifBox);
    m_fps->addItem(tr("Fotogramas por segundo..."), 0);
    for (int fps : {5, 10, 12, 15, 20, 24, 25, 30, 50, 60})
        m_fps->addItem(tr("%1 fps").arg(fps), fps);
    m_fps->setToolTip(tr("La misma duración para todos los fotogramas según los fotogramas por segundo. El GIF guarda como "
                         "mucho 50 por segundo (20 ms); con más, los vídeos y el PNG animado los conservan."));
    connect(m_delayAll, &QSpinBox::valueChanged, this, [this](int ms) {
        setAllDelays([ms](int) { return ms; }, tr("Duración de los fotogramas"));
    });
    connect(m_fps, &QComboBox::activated, this, [this](int i) {
        const int fps = m_fps->itemData(i).toInt();
        m_fps->setCurrentIndex(0);
        if (fps > 0) {
            const int ms = std::max(Gif::kMinEditDelayMs, int(std::lround(1000.0 / fps)));
            setAllDelays([ms](int) { return ms; }, tr("Fotogramas por segundo"));
        }
    });
    auto* delayRow = new QHBoxLayout;
    delayRow->addWidget(m_delayAll);
    delayRow->addWidget(m_fpsLabel);
    delayRow->addWidget(m_fps);
    delayRow->addWidget(hint(tr("(cambiarla pone esta duración a todos los fotogramas)")), 1);
    gifGrid->addWidget(new QLabel(tr("Duración:"), gifBox), 0, 0);
    gifGrid->addLayout(delayRow, 0, 1);
    m_plays = new QLineEdit(gifBox);
    m_plays->setValidator(new QIntValidator(1, 65536, m_plays));
    m_plays->setMaximumWidth(90);
    connect(m_plays, &QLineEdit::editingFinished, this, [this] {
        if (!m_doc)
            return;
        // Vacío: sin fin (0); 1 vez: -1; N veces: N - 1 repeticiones, como al guardar el GIF.
        const int count = m_plays->text().toInt();
        m_doc->setGifLoops(m_plays->text().isEmpty() ? 0 : (count <= 1 ? -1 : count - 1));
        m_previewTimer.start();
    });
    auto* loopsRow = new QHBoxLayout;
    loopsRow->addWidget(m_plays);
    loopsRow->addWidget(hint(tr("(vacío: se repite sin fin)")), 1);
    gifGrid->addWidget(new QLabel(tr("Reproducciones:"), gifBox), 1, 0);
    gifGrid->addLayout(loopsRow, 1, 1);
    gifGrid->addWidget(hint(tr("La paleta global, la cuantización y los efectos (fundido, no apilar) se eligen al exportar el GIF.")), 2,
                       0, 1, 2);
    layout->addWidget(gifBox);
    layout->addStretch();

    auto* pageScroll = new QScrollArea(this);
    pageScroll->setWidget(page);
    pageScroll->setWidgetResizable(true);
    pageScroll->setFrameShape(QFrame::NoFrame);

    // A la derecha, la vista previa del GIF (codificado de verdad: con su peso).
    auto* side = new QFrame(this);
    side->setProperty("gifPanel", true);
    side->setFrameShape(QFrame::StyledPanel);
    auto* sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(8, 8, 8, 8);
    m_preview = new EncodedPreview(QStringLiteral("animacion.gif"), side);
    m_status = new QLabel(side);
    connect(m_preview, &EncodedPreview::statusChanged, m_status, &QLabel::setText);
    sideLayout->addWidget(m_preview, 1);
    sideLayout->addWidget(m_status);

    auto* split = new QSplitter(Qt::Horizontal, this);
    split->setChildrenCollapsible(false);
    split->setHandleWidth(8);
    split->addWidget(pageScroll);
    split->addWidget(side);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    root->addWidget(split, 1);
}

void GifClassicEditor::applyLook()
{
    setStyleSheet(GifEdit::amoledStyleSheet(QStringLiteral("GifClassicEditor"))
                  + QStringLiteral("GifClassicEditor #classicPage, GifClassicEditor #classicFrames { background: #000000; }"
                                   "GifClassicEditor QGroupBox { color: #8b8f97; font-weight: bold; border: 1px solid #1b1c20; "
                                   "border-radius: 10px; margin-top: 0px; padding: 30px 8px 8px 8px; background: #0c0d0f; }"
                                   "GifClassicEditor QGroupBox::title { subcontrol-origin: padding; subcontrol-position: top left; "
                                   "left: 12px; top: 8px; padding: 0px; background: transparent; }"
                                   "GifClassicEditor QGroupBox * { font-weight: normal; }"
                                   "GifClassicEditor QScrollArea > QWidget#qt_scrollarea_viewport { background: #000000; }"
                                   "GifClassicEditor QFrame[gifCard=\"true\"] { background: #0c0d0f; border: 1px solid #1b1c20; "
                                   "border-radius: 10px; }"
                                   "GifClassicEditor QFrame[gifCard=\"true\"][skipped=\"true\"] { background: #050506; }"
                                   "GifClassicEditor QLineEdit { background: #08090a; color: #ecedef; border: 1px solid #2a2c32; "
                                   "border-radius: 7px; padding: 3px 6px; }"
                                   "GifClassicEditor QToolButton#addFrames { border: 2px dashed #2a2c32; border-radius: 12px; "
                                   "color: #1fd1b9; background: transparent; }"
                                   "GifClassicEditor QToolButton#addFrames:hover { border-color: #1fd1b9; }"
                                   "GifClassicEditor QToolButton#addFrames::menu-indicator { image: none; }"));
    const QPalette pal = GifEdit::amoledPalette(palette());
    setPalette(pal);
    setAutoFillBackground(true);
    for (QAbstractScrollArea* area : findChildren<QAbstractScrollArea*>()) {
        area->viewport()->setPalette(pal);
        area->viewport()->setAutoFillBackground(false);
    }
    if (m_preview)
        m_preview->setPalette(pal);
}

void GifClassicEditor::changeEvent(QEvent* e)
{
    // Otro tema (otra hoja de estilo): la paleta del editor otra vez.
    if (e->type() == QEvent::StyleChange)
        QTimer::singleShot(0, this, [this] {
            const QPalette pal = GifEdit::amoledPalette(palette());
            setPalette(pal);
        });
    QWidget::changeEvent(e);
}

void GifClassicEditor::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    refresh();
    m_previewTimer.start();
}

void GifClassicEditor::setDocument(Document* doc)
{
    if (doc == m_doc)
        return;
    if (m_doc)
        m_doc->disconnect(this);
    m_doc = doc;
    if (m_doc) {
        auto changed = [this] {
            m_refreshTimer.start();
            m_previewTimer.start();
        };
        connect(m_doc, &Document::layersChanged, this, changed);
        connect(m_doc, &Document::layerPixelsChanged, this, changed);
        connect(m_doc, &Document::pixelsChanged, this, changed);
        connect(m_doc, &Document::sizeChanged, this, changed);
        connect(m_doc, &QObject::destroyed, this, [this] { setDocument(nullptr); });
        const int loops = m_doc->gifLoops();
        m_plays->setText(loops == 0 ? QString() : QString::number(loops < 0 ? 1 : loops + 1));
    }
    rebuildCards();
    updateTitle();
    m_previewTimer.start();
}

void GifClassicEditor::updateTitle()
{
    m_title->setText(m_doc ? tr("Editor de GIF clásico: %1").arg(m_doc->displayName()) : tr("Editor de GIF clásico"));
}

// ---------------------------------------------------------------------------
// Tarjetas
// ---------------------------------------------------------------------------

void GifClassicEditor::refresh()
{
    if (!isVisible())
        return;
    updateTitle();
    const int n = m_doc ? m_doc->layerCount() : 0;
    bool same = n == m_cards.size();
    for (int i = 0; same && i < n; ++i)
        same = m_cards[i].layer == m_doc->layer(i);
    if (!same) {
        rebuildCards();
        return;
    }
    for (int i = 0; i < n; ++i)
        updateCard(m_cards[i], i);
}

void GifClassicEditor::rebuildCards()
{
    const int scroll = m_framesScroll->horizontalScrollBar()->value();
    m_framesRow->removeWidget(m_add);
    while (QLayoutItem* item = m_framesRow->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    m_cards.clear();
    const int n = m_doc ? m_doc->layerCount() : 0;
    for (int i = 0; i < n; ++i) {
        Card c;
        c.frame = new QFrame(m_framesBox);
        c.frame->setProperty("gifCard", true);
        c.frame->setFrameShape(QFrame::StyledPanel);
        c.frame->setFixedWidth(270);
        auto* v = new QVBoxLayout(c.frame);
        v->setContentsMargins(8, 6, 8, 8);
        v->setSpacing(4);
        c.number = new QLabel(c.frame);
        // El ojo, como en la lista de capas: abierto se muestra, cerrado no va en el GIF.
        c.eye = new QToolButton(c.frame);
        c.eye->setAutoRaise(true);
        auto* editButton = new QToolButton(c.frame);
        editButton->setAutoRaise(true);
        editButton->setIcon(Icons::theme({"draw-brush", "edit-image"}));
        editButton->setToolTip(tr("Editar el fotograma con las herramientas"));
        auto* head = new QHBoxLayout;
        head->addWidget(c.number);
        head->addStretch();
        head->addWidget(editButton);
        head->addWidget(c.eye);
        c.thumb = new QLabel(c.frame);
        c.thumb->setAlignment(Qt::AlignCenter);
        c.thumb->setFixedSize(116, 84);
        c.delay = new QSpinBox(c.frame);
        c.delay->setRange(Gif::kMinEditDelayMs, kMaxDelay);
        c.delay->setSingleStep(10);
        c.delay->setKeyboardTracking(false);
        auto* delayRow = new QHBoxLayout;
        delayRow->addWidget(new QLabel(tr("Duración:"), c.frame));
        delayRow->addWidget(c.delay, 1);
        delayRow->addWidget(new QLabel(tr("ms"), c.frame));
        c.skip = new QPushButton(c.frame);
        auto* copy = new QPushButton(tr("Copiar"), c.frame);
        copy->setStyleSheet(buttonStyle(kAccent));
        copy->setToolTip(tr("Repite este fotograma justo después"));
        // Mover el fotograma a un lado (cambia el orden del GIF) y quitarlo.
        c.left = new QToolButton(c.frame);
        c.left->setArrowType(Qt::LeftArrow);
        c.left->setAutoRaise(true);
        c.left->setToolTip(tr("Mover este fotograma antes"));
        c.right = new QToolButton(c.frame);
        c.right->setArrowType(Qt::RightArrow);
        c.right->setAutoRaise(true);
        c.right->setToolTip(tr("Mover este fotograma después"));
        c.remove = new QToolButton(c.frame);
        c.remove->setIcon(Icons::theme({"edit-delete", "list-remove"}));
        c.remove->setAutoRaise(true);
        c.remove->setToolTip(tr("Quitar este fotograma"));
        auto* buttons = new QHBoxLayout;
        buttons->setSpacing(4);
        buttons->addWidget(c.left);
        buttons->addWidget(c.skip);
        buttons->addWidget(copy);
        buttons->addWidget(c.remove);
        buttons->addWidget(c.right);
        v->addLayout(head);
        v->addWidget(c.thumb, 0, Qt::AlignHCenter);
        v->addLayout(delayRow);
        v->addLayout(buttons);
        // (Las tarjetas se buscan por su capa: el índice cambia al mover o quitar fotogramas.)
        const Layer* layer = m_doc->layer(i);
        auto indexOf = [this, layer] {
            for (int j = 0; m_doc && j < m_doc->layerCount(); ++j)
                if (m_doc->layer(j) == layer)
                    return j;
            return -1;
        };
        auto toggle = [this, indexOf] {
            const int at = indexOf();
            if (at >= 0)
                setShown({at}, !m_doc->layer(at)->props.visible,
                         m_doc->layer(at)->props.visible ? tr("Omitir el fotograma") : tr("Activar el fotograma"));
        };
        connect(c.eye, &QToolButton::clicked, this, toggle);
        connect(c.skip, &QPushButton::clicked, this, toggle);
        connect(copy, &QPushButton::clicked, this, [this, indexOf] {
            if (const int at = indexOf(); at >= 0)
                duplicate(at);
        });
        connect(c.remove, &QToolButton::clicked, this, [this, indexOf] {
            if (const int at = indexOf(); at >= 0)
                remove(at);
        });
        connect(c.left, &QToolButton::clicked, this, [this, indexOf] {
            if (const int at = indexOf(); at > 0)
                move(at, at - 1);
        });
        connect(c.right, &QToolButton::clicked, this, [this, indexOf] {
            if (const int at = indexOf(); at >= 0)
                move(at, at + 1);
        });
        connect(editButton, &QToolButton::clicked, this, [this, indexOf] {
            if (const int at = indexOf(); at >= 0)
                emit editFrameRequested(at);
        });
        connect(c.delay, &QSpinBox::valueChanged, this, [this, indexOf](int ms) {
            if (const int at = indexOf(); at >= 0 && ms != delayOf(at))
                setDelay(at, ms);
        });
        c.layer = layer;
        m_framesRow->addWidget(c.frame);
        m_cards << c;
        updateCard(m_cards.last(), i);
    }
    m_framesRow->addWidget(m_add);
    m_add->setEnabled(m_doc != nullptr);
    m_framesRow->addStretch();
    const int count = std::max(1, n);
    m_from->setRange(1, count);
    m_to->setRange(1, count);
    m_every->setRange(2, std::max(2, count));
    QTimer::singleShot(0, m_framesScroll, [this, scroll] { m_framesScroll->horizontalScrollBar()->setValue(scroll); });
    if (n > 0) {
        const QSignalBlocker block(m_delayAll);
        m_delayAll->setValue(delayOf(0));
        m_fpsLabel->setText(tr("(%1 FPS)").arg(QLocale().toString(1000.0 / std::max(1, delayOf(0)), 'f', 2)));
    }
}

void GifClassicEditor::updateCard(Card& c, int index)
{
    const Layer* layer = m_doc->layer(index);
    const bool shown = layer->props.visible;
    c.number->setText(QString::number(index + 1));
    c.eye->setIcon(Icons::eye(shown));
    c.eye->setToolTip(shown ? tr("Se muestra: clic para ocultarlo") : tr("No se muestra: clic para mostrarlo"));
    c.frame->setProperty("skipped", !shown);
    c.frame->style()->unpolish(c.frame);
    c.frame->style()->polish(c.frame);
    if (c.imageKey != layer->image.cacheKey() || c.thumb->property("dimmed").toBool() == shown) {
        c.imageKey = layer->image.cacheKey();
        c.thumb->setPixmap(thumbOf(layer->image, !shown));
        c.thumb->setProperty("dimmed", !shown);
    }
    c.thumb->setToolTip(shown ? QString() : tr("Omitido: no va en el GIF"));
    if (!c.delay->hasFocus()) {
        const QSignalBlocker block(c.delay);
        c.delay->setValue(delayOf(index));
    }
    c.delay->setEnabled(shown);
    c.skip->setText(shown ? tr("Omitir") : tr("Activar"));
    c.skip->setStyleSheet(buttonStyle(shown ? kSkip : kAccent));
    c.left->setEnabled(index > 0);
    c.right->setEnabled(index + 1 < m_doc->layerCount());
    c.remove->setEnabled(m_doc->layerCount() > 1);
    if (index == 0 && !m_delayAll->hasFocus()) {
        const QSignalBlocker block(m_delayAll);
        m_delayAll->setValue(delayOf(0));
        m_fpsLabel->setText(tr("(%1 FPS)").arg(QLocale().toString(1000.0 / std::max(1, delayOf(0)), 'f', 2)));
    }
}

void GifClassicEditor::updatePreview()
{
    if (!m_doc || !isVisible())
        return;
    const QList<Gif::Frame> frames = ImageIO::animationFrames(m_doc, 100, false);
    ImageIO::SaveOptions options;
    options.loops = m_doc->gifLoops();
    m_preview->request([frames, options](QString* error) { return ImageIO::encodeAnimation(frames, options, error); }, options.loops);
}

// ---------------------------------------------------------------------------
// Cambios (cada uno, un paso del historial del editor de GIF)
// ---------------------------------------------------------------------------

void GifClassicEditor::apply(const QList<LayerPtr>& layers, const QList<LayerProps>& props, int active, const QString& text,
                             const QString& icon)
{
    if (!m_doc || layers.isEmpty())
        return;
    const DocumentState before = DocumentState::capture(m_doc);
    DocumentState after = before;
    after.layers = layers;
    after.images.clear();
    for (const LayerPtr& l : layers)
        after.images << l->image;
    after.props = props;
    after.active = std::clamp(active, 0, std::max(0, int(layers.size()) - 1));
    HistoryIcons::registerIcon(text, icon);
    auto* step = new GifEditCommand(text);
    new DocumentStateCommand(m_doc, before, after, text, step);
    m_doc->undoStack()->push(step);
}

QList<LayerProps> GifClassicEditor::currentProps() const
{
    QList<LayerProps> props;
    for (const LayerPtr& l : m_doc->layers())
        props << l->props;
    return props;
}

int GifClassicEditor::delayOf(int index) const
{
    return m_doc && index >= 0 && index < m_doc->layerCount() ? GifEdit::delayOf(m_doc->layer(index)) : 100;
}

void GifClassicEditor::setDelay(int index, int delayMs)
{
    if (!m_doc)
        return;
    QList<LayerProps> props = currentProps();
    props[index].name = Gif::nameWithDelay(props[index].name, std::clamp(delayMs, Gif::kMinEditDelayMs, kMaxDelay));
    apply(m_doc->layers(), props, m_doc->activeIndex(), tr("Duración del fotograma"), QStringLiteral("chronometer"));
}

void GifClassicEditor::setAllDelays(const std::function<int(int)>& fn, const QString& text)
{
    if (!m_doc)
        return;
    QList<LayerProps> props = currentProps();
    bool changed = false;
    for (int i = 0; i < props.size(); ++i) {
        const int next = std::clamp(fn(delayOf(i)), Gif::kMinEditDelayMs, kMaxDelay);
        if (next != delayOf(i)) {
            props[i].name = Gif::nameWithDelay(props[i].name, next);
            changed = true;
        }
    }
    if (changed)
        apply(m_doc->layers(), props, m_doc->activeIndex(), text, QStringLiteral("chronometer"));
}

void GifClassicEditor::setShown(const QList<int>& frames, bool shown, const QString& text)
{
    if (!m_doc || frames.isEmpty())
        return;
    QList<LayerProps> props = currentProps();
    bool changed = false;
    for (int i : frames)
        if (i >= 0 && i < props.size() && props[i].visible != shown) {
            props[i].visible = shown;
            changed = true;
        }
    if (changed)
        apply(m_doc->layers(), props, m_doc->activeIndex(), text, shown ? QStringLiteral("visibility") : QStringLiteral("view-hidden"));
}

void GifClassicEditor::duplicate(int index)
{
    if (!m_doc || index < 0 || index >= m_doc->layerCount())
        return;
    QList<LayerPtr> layers = m_doc->layers();
    QList<LayerProps> props = currentProps();
    const LayerPtr& source = layers[index];
    auto copy = std::make_shared<Layer>(source->props.name, source->image);
    copy->props = source->props;
    copy->props.visible = true;
    layers.insert(index + 1, copy);
    props.insert(index + 1, copy->props);
    apply(layers, props, index + 1, tr("Copiar el fotograma"), QStringLiteral("edit-copy"));
}

void GifClassicEditor::remove(int index)
{
    if (!m_doc || m_doc->layerCount() <= 1 || index < 0 || index >= m_doc->layerCount())
        return;
    QList<LayerPtr> layers = m_doc->layers();
    QList<LayerProps> props = currentProps();
    layers.removeAt(index);
    props.removeAt(index);
    apply(layers, props, std::min(index, int(layers.size()) - 1), tr("Quitar el fotograma"), QStringLiteral("edit-delete"));
}

void GifClassicEditor::move(int from, int to)
{
    if (!m_doc || from < 0 || to < 0 || from >= m_doc->layerCount() || to >= m_doc->layerCount() || from == to)
        return;
    QList<LayerPtr> layers = m_doc->layers();
    QList<LayerProps> props = currentProps();
    layers.move(from, to);
    props.move(from, to);
    apply(layers, props, to, tr("Mover el fotograma"), QStringLiteral("go-next"));
}

void GifClassicEditor::reverse()
{
    if (!m_doc || m_doc->layerCount() < 2)
        return;
    QList<LayerPtr> layers = m_doc->layers();
    std::reverse(layers.begin(), layers.end());
    QList<LayerProps> props;
    for (const LayerPtr& l : std::as_const(layers))
        props << l->props;
    apply(layers, props, m_doc->layerCount() - 1 - m_doc->activeIndex(), tr("Invertir el orden"), QStringLiteral("media-seek-backward"));
}

void GifClassicEditor::boomerang()
{
    if (!m_doc || m_doc->layerCount() < 3)
        return;
    QList<LayerPtr> layers = m_doc->layers();
    QList<LayerProps> props = currentProps();
    const QList<LayerPtr> forward = layers;
    for (qsizetype i = forward.size() - 2; i >= 1; --i) {
        auto copy = std::make_shared<Layer>(forward[i]->props.name, forward[i]->image);
        copy->props = forward[i]->props;
        layers << copy;
        props << copy->props;
    }
    apply(layers, props, m_doc->activeIndex(), tr("Ida y vuelta"), QStringLiteral("media-playlist-repeat"));
}

void GifClassicEditor::append(const QList<QImage>& images, const QList<int>& delays, const QString& text)
{
    if (!m_doc || images.isEmpty())
        return;
    QList<LayerPtr> layers = m_doc->layers();
    QList<LayerProps> props = currentProps();
    // En la pista del último (el clásico no usa las pistas del estudio, pero las respeta).
    const int track = layers.isEmpty() ? 0 : layers.last()->props.track;
    for (int i = 0; i < images.size(); ++i) {
        const QString name = Gif::nameWithDelay(tr("Fotograma %1").arg(layers.size() + 1), std::max(Gif::kMinEditDelayMs, delays.value(i, 100)));
        auto layer = std::make_shared<Layer>(name, GifEdit::fitted(images[i], m_doc->size()));
        layer->props.track = track;
        layers << layer;
        props << layer->props;
    }
    apply(layers, props, int(layers.size()) - 1, text, QStringLiteral("list-add"));
    // Que se vea la tarjeta nueva.
    QTimer::singleShot(50, m_framesScroll, [this] {
        m_framesScroll->horizontalScrollBar()->setValue(m_framesScroll->horizontalScrollBar()->maximum());
    });
}

void GifClassicEditor::addFromFiles()
{
    if (!m_doc)
        return;
    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Añadir fotogramas"), QString(),
                                                            ImageIO::openFilter() + QStringLiteral(";;")
                                                                + tr("Vídeo (%1)").arg(GifExport::videoPatterns()));
    QList<QImage> images;
    QList<int> delays;
    QStringList failed;
    for (const QString& path : paths) {
        GifEdit::Media media;
        QString error;
        if (GifEdit::loadMedia(path, &media, this, &error)) {
            images << media.frames;
            delays << media.delays;
        } else {
            failed << QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), error.isEmpty() ? tr("no es una imagen") : error);
        }
    }
    append(images, delays, images.size() == 1 ? tr("Añadir un fotograma") : tr("Añadir fotogramas"));
    if (!failed.isEmpty())
        QMessageBox::warning(this, tr("Añadir fotogramas"), tr("No se pudieron importar:\n%1").arg(failed.join(QLatin1Char('\n'))));
}

void GifClassicEditor::addBlank()
{
    if (!m_doc)
        return;
    QImage blank(m_doc->size(), QImage::Format_ARGB32_Premultiplied);
    blank.fill(Qt::transparent);
    append({blank}, {delayOf(m_doc->layerCount() - 1)}, tr("Añadir un fotograma"));
}
