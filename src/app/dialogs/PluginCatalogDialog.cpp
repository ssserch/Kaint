#include "app/dialogs/PluginCatalogDialog.h"

#include "app/Icons.h"
#include "core/plugins/PaintJs.h"
#include "core/plugins/PluginManager.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QTemporaryDir>
#include <QVBoxLayout>

#include <functional>

namespace {

constexpr qint64 kCatalogLimit = 1024 * 1024;
constexpr qint64 kPackageLimit = 5 * 1024 * 1024;

QString text(const QJsonValue& v, const QString& fallback, int max)
{
    const QString s = v.toString().trimmed();
    return s.isEmpty() ? fallback : s.left(max);
}

// Descarga sin redirecciones, con un límite de tiempo y de tamaño (como Plugins.download de la
// web); 'done' recibe los bytes o el error.
void download(QNetworkAccessManager* net, const QUrl& url, qint64 limit, QObject* context,
              std::function<void(const QByteArray&, const QString&)> done)
{
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(15000);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Kaint"));
    QNetworkReply* reply = net->get(request);
    QObject::connect(reply, &QNetworkReply::downloadProgress, reply, [reply, limit](qint64 received, qint64) {
        if (received > limit)
            reply->abort();
    });
    QObject::connect(reply, &QNetworkReply::finished, context, [reply, limit, done] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() == QNetworkReply::OperationCanceledError) {
            done({}, PluginCatalogDialog::tr("El catálogo o el complemento pasa del tamaño máximo."));
            return;
        }
        if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
            done({}, status ? PluginCatalogDialog::tr("No se pudo descargar (HTTP %1).").arg(status) : reply->errorString());
            return;
        }
        const QByteArray data = reply->readAll();
        if (data.size() > limit) {
            done({}, PluginCatalogDialog::tr("El catálogo o el complemento pasa del tamaño máximo."));
            return;
        }
        done(data, QString());
    });
}

} // namespace

QString PluginCatalogDialog::catalogUrl()
{
    return QStringLiteral("https://labystudio.github.io/paintdotjs-plugins/catalog.json");
}

PluginCatalogDialog::PluginCatalogDialog(QWidget* parent)
    : QDialog(parent)
    , m_net(new QNetworkAccessManager(this))
{
    setWindowTitle(tr("Complementos de Kaint.HTML"));
    setWindowIcon(Icons::theme({"plugins", "application-x-addon"}));
    resize(560, 520);
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("Complementos de paint.js del catálogo que usa la versión web de Kaint (Kaint.HTML). Se "
                                "ejecutan sin acceso a tus archivos ni a internet. Los que abren una ventana propia hecha para el "
                                "navegador solo funcionan en la versión web."),
                             this);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::StyledPanel);
    // Las filas se ajustan al ancho (los textos largos pasan a otra línea) y el botón de instalar
    // siempre se ve.
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* holder = new QWidget(scroll);
    m_rows = new QVBoxLayout(holder);
    m_rows->setContentsMargins(8, 8, 8, 8);
    m_rows->setSpacing(10);
    m_message = new QLabel(tr("Cargando el catálogo…"), holder);
    m_message->setWordWrap(true);
    m_rows->addWidget(m_message);
    m_rows->addStretch();
    scroll->setWidget(holder);
    layout->addWidget(scroll, 1);
    auto* buttons = new QDialogButtonBox(this);
    QPushButton* close = buttons->addButton(tr("Cerrar"), QDialogButtonBox::AcceptRole);
    close->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    layout->addWidget(buttons);
    loadCatalog();
}

void PluginCatalogDialog::loadCatalog()
{
    const QUrl url(catalogUrl() + QStringLiteral("?refresh=") + QString::number(QDateTime::currentMSecsSinceEpoch(), 36));
    download(m_net, url, kCatalogLimit, this, [this](const QByteArray& data, const QString& error) {
        if (!error.isEmpty()) {
            m_message->setText(error);
            return;
        }
        const QJsonObject catalog = QJsonDocument::fromJson(data).object();
        if (catalog.value(QStringLiteral("schemaVersion")).toInt() != 1 || !catalog.value(QStringLiteral("plugins")).isArray()) {
            m_message->setText(tr("El formato del catálogo no es compatible."));
            return;
        }
        // Como en la web: solo los de esta versión de la API, con su SHA-256 y un nombre de
        // archivo sin rutas raras.
        static const QRegularExpression sha(QStringLiteral("^[a-f0-9]{64}$"));
        static const QRegularExpression file(QStringLiteral("^[a-zA-Z0-9_./-]+\\.paintjs$"));
        const QUrl base(catalogUrl());
        for (const QJsonValue& v : catalog.value(QStringLiteral("plugins")).toArray()) {
            const QJsonObject o = v.toObject();
            const QString f = o.value(QStringLiteral("file")).toString();
            if (o.value(QStringLiteral("apiVersion")).toInt() != PaintJs::kApiVersion
                || !sha.match(o.value(QStringLiteral("sha256")).toString()).hasMatch() || !file.match(f).hasMatch()
                || f.contains(QLatin1String("..")))
                continue;
            Entry e;
            e.id = o.value(QStringLiteral("id")).toString();
            e.name = text(o.value(QStringLiteral("name")), e.id, 80);
            e.version = text(o.value(QStringLiteral("version")), QString(), 32);
            e.author = text(o.value(QStringLiteral("author")), tr("Desconocido"), 80);
            e.description = text(o.value(QStringLiteral("description")), QString(), 500);
            e.license = text(o.value(QStringLiteral("license")), QString(), 120);
            e.sha256 = o.value(QStringLiteral("sha256")).toString();
            e.file = QFileInfo(f).fileName();
            e.url = base.resolved(QUrl(f));
            m_entries << e;
        }
        showEntries();
    });
}

bool PluginCatalogDialog::isInstalled(const Entry& e) const
{
    for (const PluginInfo& p : PluginManager::instance()->plugins())
        if (p.packageId == e.id && p.version == e.version)
            return true;
    return false;
}

void PluginCatalogDialog::showEntries()
{
    if (m_entries.isEmpty()) {
        m_message->setText(tr("El catálogo está vacío."));
        return;
    }
    m_message->hide();
    for (int i = 0; i < m_entries.size(); ++i) {
        const Entry& e = m_entries[i];
        auto* row = new QWidget;
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        auto* info = new QVBoxLayout;
        auto* title = new QLabel(QStringLiteral("<b>%1</b> %2").arg(e.name.toHtmlEscaped(), e.version.toHtmlEscaped()), row);
        auto* who = new QLabel(e.license.isEmpty() ? e.author : QStringLiteral("%1 · %2").arg(e.author, e.license), row);
        auto* description = new QLabel(e.description, row);
        description->setWordWrap(true);
        QPalette dim = description->palette();
        dim.setColor(QPalette::WindowText, dim.color(QPalette::PlaceholderText));
        description->setPalette(dim);
        auto* status = new QLabel(row);
        status->setWordWrap(true);
        status->hide();
        for (QLabel* l : {title, who, description, status}) {
            l->setTextInteractionFlags(Qt::TextSelectableByMouse);
            l->setWordWrap(true);
            l->setMinimumWidth(120);
            l->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            info->addWidget(l);
        }
        h->addLayout(info, 1);
        const bool installed = isInstalled(e);
        auto* button = new QPushButton(installed ? tr("Instalado") : tr("Instalar"), row);
        button->setEnabled(!installed);
        connect(button, &QPushButton::clicked, this, [this, i, button, status] { install(i, button, status); });
        h->addWidget(button, 0, Qt::AlignTop);
        m_rows->insertWidget(m_rows->count() - 1, row);
    }
}

void PluginCatalogDialog::install(int index, QPushButton* button, QLabel* status)
{
    const Entry e = m_entries.value(index);
    button->setEnabled(false);
    button->setText(tr("Instalando…"));
    status->hide();
    QPointer<QPushButton> guardButton(button);
    QPointer<QLabel> guardStatus(status);
    download(m_net, e.url, kPackageLimit, this, [this, e, guardButton, guardStatus](const QByteArray& data, const QString& error) {
        auto fail = [&](const QString& message) {
            if (guardStatus) {
                guardStatus->setText(message);
                guardStatus->show();
            }
            if (guardButton) {
                guardButton->setText(tr("Instalar"));
                guardButton->setEnabled(true);
            }
        };
        if (!error.isEmpty()) {
            fail(error);
            return;
        }
        if (QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex()) != e.sha256) {
            fail(tr("El complemento descargado no coincide con el del catálogo: no se instaló nada."));
            return;
        }
        QTemporaryDir dir;
        const QString tmp = dir.filePath(e.file);
        QFile f(tmp);
        if (!dir.isValid() || !f.open(QIODevice::WriteOnly) || f.write(data) != data.size()) {
            fail(f.errorString());
            return;
        }
        f.close();
        // Que sea el que dice el catálogo antes de copiarlo.
        PaintJs::Info check = PaintJs::load(tmp);
        qDeleteAll(check.effects);
        if (check.id != e.id || check.version != e.version) {
            fail(check.errors.isEmpty() ? tr("El complemento no coincide con el del catálogo.") : check.errors.join(QLatin1Char('\n')));
            return;
        }
        PluginManager* pm = PluginManager::instance();
        const bool existed = !pm->installedAlready({tmp}).isEmpty();
        QStringList errors;
        const QStringList copied = pm->install({tmp}, true, &errors);
        if (copied.isEmpty()) {
            fail(errors.isEmpty() ? tr("No se pudo copiar el complemento.") : tr("No se pudieron copiar:\n\n%1").arg(errors.join(QLatin1Char('\n'))));
            return;
        }
        QApplication::setOverrideCursor(Qt::WaitCursor);
        pm->scan();
        if (existed)
            pm->reload(copied.first());
        QApplication::restoreOverrideCursor();
        if (guardButton)
            guardButton->setText(tr("Instalado"));
        for (const PluginInfo& p : pm->plugins()) {
            if (p.id != copied.first() || !guardStatus)
                continue;
            if (!p.webOnlyEffects.isEmpty()) {
                guardStatus->setText(p.effects.isEmpty() && p.fileTypes.isEmpty()
                                         ? tr("Instalado, pero solo funciona en la versión web de Kaint: abre una ventana propia "
                                              "hecha para el navegador.")
                                         : tr("Solo en la versión web: %1").arg(p.webOnlyEffects.join(QStringLiteral(", "))));
                guardStatus->show();
            } else if (!p.errors.isEmpty()) {
                guardStatus->setText(p.errors.join(QLatin1Char('\n')));
                guardStatus->show();
            }
        }
    });
}
