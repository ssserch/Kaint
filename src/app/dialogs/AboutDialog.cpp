#include "app/dialogs/AboutDialog.h"

#include "app/Animations.h"
#include "app/Icons.h"
#include "app/widgets/WaveLabel.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QGraphicsBlurEffect>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariantAnimation>

namespace {

// Logo: el icono de Kaint y "kaint.qt6" ("kaint.kde" dentro de Plasma) con la fuente fina del
// logo de Paint.NET (Segoe UI Light si está instalada; si no, la del sistema en su grosor más fino).
class AboutLogo : public QWidget {
public:
    explicit AboutLogo(QWidget* parent)
        : QWidget(parent)
    {
        m_font = QFontDatabase::families().contains(QStringLiteral("Segoe UI Light")) ? QFont(QStringLiteral("Segoe UI Light"))
                                                                                   : QFont(font().family());
        m_font.setWeight(QFont::Light);
        m_font.setPixelSize(44);
    }

    QSize sizeHint() const override
    {
        const QFontMetrics fm(m_font);
        return QSize(kIcon + kGap + fm.horizontalAdvance(text()) + 2 * kMargin, std::max(kIcon, fm.height()) + 2 * kMargin);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const int top = (height() - kIcon) / 2;
        Icons::app().paint(&p, QRect(kMargin, top, kIcon, kIcon));
        p.setFont(m_font);
        p.setPen(palette().color(QPalette::WindowText));
        p.drawText(QRect(kMargin + kIcon + kGap, 0, width(), height()), Qt::AlignLeft | Qt::AlignVCenter, text());
    }

private:
    static QString text()
    {
        const bool kde = qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains(QLatin1String("KDE"), Qt::CaseInsensitive)
                         || !qEnvironmentVariableIsEmpty("KDE_FULL_SESSION");
        return kde ? QStringLiteral("kaint.kde") : QStringLiteral("kaint.qt6");
    }
    static constexpr int kIcon = 56;
    static constexpr int kGap = 10;
    static constexpr int kMargin = 4;
    QFont m_font;
};

} // namespace

QString AboutDialog::credits()
{
    struct Credit {
        const char* name;
        const char* author;
        const char* url;
    };
    static const Credit list[] = {
        {"Qt 6", "The Qt Company", "https://www.qt.io/"},
        {"zlib", "Jean-loup Gailly, Mark Adler", "https://zlib.net/"},
        {"Paint.NET 3.36 (MIT)", "dotPDN LLC, Rick Brewster", "https://www.getpaint.net/"},
        {"Paint.NET API", "dotPDN LLC", "https://paintdotnet.github.io/apidocs/"},
        {"Breeze Icons", "KDE", "https://invent.kde.org/frameworks/breeze-icons"},
        {"KImageFormats", "KDE", "https://invent.kde.org/frameworks/kimageformats"},
        {"Mono", "Mono Project", "https://www.mono-project.com/"},
        {"libgdiplus", "Mono Project", "https://github.com/mono/libgdiplus"},
        {"Discord Rich Presence", "Discord", "https://discord.com/developers/docs/rich-presence/overview"},
        {"CMake", "Kitware", "https://cmake.org/"},
        {"Ninja", "Ninja Authors", "https://ninja-build.org/"},
        {"GCC", "GNU Project", "https://gcc.gnu.org/"},
    };
    QString html;
    for (const Credit& c : list)
        html += QStringLiteral("• %1 – %2 <a href=\"%3\">%3</a><br>")
                    .arg(QString::fromUtf8(c.name), QString::fromUtf8(c.author), QString::fromLatin1(c.url));
    return html;
}

QWidget* AboutDialog::createLogo(QWidget* parent)
{
    auto* logo = new AboutLogo(parent);
    // Se aclara desde muy desenfocado cada vez que se abre (sin animaciones, ya nítido).
    if (Animations::enabled()) {
        auto* blur = new QGraphicsBlurEffect(logo);
        blur->setBlurHints(QGraphicsBlurEffect::QualityHint);
        blur->setBlurRadius(28);
        logo->setGraphicsEffect(blur);
        auto* reveal = new QVariantAnimation(logo);
        reveal->setDuration(1500);
        reveal->setStartValue(28.0);
        reveal->setEndValue(0.0);
        reveal->setEasingCurve(QEasingCurve::OutCubic);
        QObject::connect(reveal, &QVariantAnimation::valueChanged, blur,
                         [blur](const QVariant& v) { blur->setBlurRadius(v.toReal()); });
        QObject::connect(reveal, &QVariantAnimation::finished, logo, [logo] { logo->setGraphicsEffect(nullptr); });
        reveal->start(QAbstractAnimation::DeleteWhenStopped);
    }
    return logo;
}

AboutDialog::AboutDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Acerca de Kaint"));
    setWindowIcon(Icons::theme({"help-about"}));

    QWidget* logo = createLogo(this);
    auto* version = new QLabel(QStringLiteral("Kaint v%1").arg(QStringLiteral(KAINT_VERSION)), this);
    auto* copyright = new QLabel(tr("Copyright © 2026 SSSerch (aka Y2Kerchy) y colaboradores.") + QLatin1Char('\n')
                                     + tr("Software libre con licencia GPL-3.0."),
                                 this);
    copyright->setWordWrap(true);
    auto* link = new QLabel(QStringLiteral("<a href=\"%1\">%1</a>").arg(QStringLiteral(KAINT_REPOSITORY)), this);
    link->setOpenExternalLinks(true);
    link->setTextInteractionFlags(Qt::TextBrowserInteraction);
    auto* signature = new WaveLabel(QStringLiteral("By "), QStringLiteral("Y2Kerchy"), QUrl(QStringLiteral("https://ssserch.com")),
                                    this);
    auto* translation = new QLabel(tr("Traducido al español por SSSerch"), this);
    auto* creditsTitle = new QLabel(tr("Bibliotecas de software y contribuciones:"), this);
    auto* creditsList = new QTextBrowser(this);
    creditsList->setOpenExternalLinks(true);
    creditsList->setHtml(credits());
    creditsList->setMinimumHeight(170);

    auto* box = new QDialogButtonBox(this);
    box->addButton(tr("Cerrar"), QDialogButtonBox::RejectRole)->setDefault(true);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(logo, 0, Qt::AlignLeft);
    layout->addSpacing(4);
    layout->addWidget(version);
    layout->addWidget(copyright);
    layout->addWidget(link);
    layout->addWidget(signature, 0, Qt::AlignLeft);
    layout->addSpacing(6);
    layout->addWidget(translation);
    layout->addSpacing(6);
    layout->addWidget(creditsTitle);
    layout->addWidget(creditsList, 1);
    layout->addWidget(box);
    resize(520, 500);
}
