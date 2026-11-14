#include "app/dialogs/PluginCatalogDialog.h"

#include "app/Icons.h"
#include "core/effects/Effect.h"
#include "core/plugins/PaintJs.h"
#include "core/plugins/PdnBridge.h"
#include "core/plugins/PluginManager.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
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
#include <QTabWidget>
#include <QTemporaryDir>
#include <QVBoxLayout>

#include <functional>
#include <memory>

namespace {

constexpr qint64 kCatalogLimit = 2 * 1024 * 1024;
constexpr qint64 kPackageLimit = 16 * 1024 * 1024;

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
    request.setTransferTimeout(20000);
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

QString sha256(const QByteArray& data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

// Una ruta relativa sin trucos (ni "..", ni absoluta, ni caracteres raros).
bool safeRelativePath(const QString& path)
{
    static const QRegularExpression ok(QStringLiteral("^[A-Za-z0-9 _.,'+()&-]+(/[A-Za-z0-9 _.,'+()&-]+)*$"));
    return ok.match(path).hasMatch() && !path.contains(QLatin1String("..")) && !path.startsWith(QLatin1Char('/'));
}

} // namespace

QString PluginCatalogDialog::catalogUrl()
{
    return QStringLiteral("https://labystudio.github.io/paintdotjs-plugins/catalog.json");
}

QString PluginCatalogDialog::kaintCatalogBase()
{
    return QStringLiteral("https://raw.githubusercontent.com/ssserch/Kaint/main/plugins/");
}

QString PluginCatalogDialog::kaintCatalogUrl()
{
    return kaintCatalogBase() + QStringLiteral("catalog.json");
}

QList<PluginCatalogDialog::Entry> PluginCatalogDialog::parseKaintCatalog(const QByteArray& json, QString* error)
{
    QList<Entry> out;
    const QJsonObject catalog = QJsonDocument::fromJson(json).object();
    if (catalog.value(QStringLiteral("schemaVersion")).toInt() != 1) {
        if (error)
            *error = tr("El formato del catálogo no es compatible.");
        return out;
    }
    static const QRegularExpression sha(QStringLiteral("^[a-f0-9]{64}$"));
    const QUrl base(kaintCatalogBase());
    const struct {
        const char* key;
        Section section;
    } sections[] = {{"photocraft", Section::PhotoCraft}, {"paintnet", Section::PaintNet}};
    for (const auto& s : sections) {
        for (const QJsonValue& v : catalog.value(QLatin1String(s.key)).toArray()) {
            const QJsonObject o = v.toObject();
            Entry e;
            e.section = s.section;
            e.id = o.value(QStringLiteral("id")).toString().left(120);
            e.name = text(o.value(QStringLiteral("name")), e.id, 80);
            e.version = text(o.value(QStringLiteral("version")), QString(), 32);
            // Sin créditos no se ofrece.
            e.author = text(o.value(QStringLiteral("author")), QString(), 120);
            e.description = text(o.value(QStringLiteral("description")), QString(), 500);
            e.website = text(o.value(QStringLiteral("website")), QString(), 300);
            for (const QJsonValue& fx : o.value(QStringLiteral("effects")).toArray())
                e.effects << fx.toString().left(80);
            for (const QJsonValue& b : o.value(QStringLiteral("builtin")).toArray())
                if (Effects::find(b.toString()))
                    e.builtin << b.toString();
            bool valid = !e.id.isEmpty() && !e.author.isEmpty();
            for (const QJsonValue& fv : o.value(QStringLiteral("files")).toArray()) {
                const QJsonObject f = fv.toObject();
                FileRef ref;
                ref.path = f.value(QStringLiteral("path")).toString();
                ref.sha256 = f.value(QStringLiteral("sha256")).toString();
                ref.size = qint64(f.value(QStringLiteral("size")).toDouble());
                const QString url = f.value(QStringLiteral("url")).toString();
                ref.url = url.startsWith(QLatin1String("https://")) ? QUrl(url) : base.resolved(QUrl(ref.path));
                if (!safeRelativePath(ref.path) || !sha.match(ref.sha256).hasMatch() || !ref.url.isValid())
                    valid = false;
                e.files << ref;
            }
            if (valid && (!e.files.isEmpty() || !e.builtin.isEmpty()))
                out << e;
        }
    }
    if (error && out.isEmpty())
        *error = tr("El catálogo está vacío.");
    return out;
}

PluginCatalogDialog::PluginCatalogDialog(QWidget* parent, Section initial)
    : QDialog(parent)
    , m_net(new QNetworkAccessManager(this))
{
    setWindowTitle(tr("Explorar complementos"));
    setWindowIcon(Icons::theme({"plugins", "application-x-addon"}));
    resize(620, 580);
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("Esta es una lista limitada de complementos elegidos y probados para Kaint. Cada uno es obra de "
                                "su autor, que sale junto a su nombre. Puedes instalar otros desde Configuración > "
                                "Complementos > Añadir…"),
                             this);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    m_tabs = new QTabWidget(this);
    const QString titles[3] = {tr("Kaint.HTML"), tr("PhotoCraft"), tr("Paint.NET")};
    const QString notes[3] = {
        tr("Complementos de paint.js del catálogo que usa la versión web de Kaint (Kaint.HTML). Se ejecutan sin acceso a tus "
           "archivos ni a internet. Los que abren una ventana propia hecha para el navegador solo funcionan en la versión web."),
        tr("Filtros de PhotoCraft (.wasm) de la tienda ArtCraft. Se ejecutan en el intérprete de WebAssembly de Kaint, sin acceso "
           "a nada de fuera."),
        tr("Complementos de efectos de Paint.NET (.dll) que funcionan en Kaint, con la capa de compatibilidad (mono en Linux y el "
           ".NET Framework de Windows)."),
    };
    for (int i = 0; i < 3; ++i) {
        auto* page = new QWidget(m_tabs);
        auto* v = new QVBoxLayout(page);
        auto* note = new QLabel(notes[i], page);
        note->setWordWrap(true);
        v->addWidget(note);
        if (i == int(Section::PaintNet)) {
            QString why;
            if (!PdnBridge::available(&why)) {
                auto* warn = new QLabel(QStringLiteral("⚠ ") + why, page);
                warn->setWordWrap(true);
                v->addWidget(warn);
            }
        }
        auto* scroll = new QScrollArea(page);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::StyledPanel);
        // Las filas se ajustan al ancho (los textos largos pasan a otra línea) y el botón de
        // instalar siempre se ve.
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto* holder = new QWidget(scroll);
        m_rows[i] = new QVBoxLayout(holder);
        m_rows[i]->setContentsMargins(8, 8, 8, 8);
        m_rows[i]->setSpacing(10);
        m_message[i] = new QLabel(tr("Cargando el catálogo…"), holder);
        m_message[i]->setWordWrap(true);
        m_rows[i]->addWidget(m_message[i]);
        m_rows[i]->addStretch();
        scroll->setWidget(holder);
        v->addWidget(scroll, 1);
        m_tabs->addTab(page, titles[i]);
    }
    m_tabs->setCurrentIndex(int(initial));
    layout->addWidget(m_tabs, 1);
    auto* buttons = new QDialogButtonBox(this);
    QPushButton* close = buttons->addButton(tr("Cerrar"), QDialogButtonBox::AcceptRole);
    close->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    layout->addWidget(buttons);
    loadCatalogs();
}

QVBoxLayout* PluginCatalogDialog::rows(Section section) const
{
    return m_rows[int(section)];
}

QLabel* PluginCatalogDialog::message(Section section) const
{
    return m_message[int(section)];
}

void PluginCatalogDialog::loadCatalogs()
{
    const QString refresh = QStringLiteral("?refresh=") + QString::number(QDateTime::currentMSecsSinceEpoch(), 36);
    // paint.js (Kaint.HTML).
    download(m_net, QUrl(catalogUrl() + refresh), kCatalogLimit, this, [this](const QByteArray& data, const QString& error) {
        if (!error.isEmpty()) {
            message(Section::KaintHtml)->setText(error);
            return;
        }
        const QJsonObject catalog = QJsonDocument::fromJson(data).object();
        if (catalog.value(QStringLiteral("schemaVersion")).toInt() != 1 || !catalog.value(QStringLiteral("plugins")).isArray()) {
            message(Section::KaintHtml)->setText(tr("El formato del catálogo no es compatible."));
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
            e.section = Section::KaintHtml;
            e.id = o.value(QStringLiteral("id")).toString();
            e.name = text(o.value(QStringLiteral("name")), e.id, 80);
            e.version = text(o.value(QStringLiteral("version")), QString(), 32);
            e.author = text(o.value(QStringLiteral("author")), QString(), 80);
            e.description = text(o.value(QStringLiteral("description")), QString(), 500);
            e.license = text(o.value(QStringLiteral("license")), QString(), 120);
            // Sin créditos no se ofrece.
            if (e.author.isEmpty())
                continue;
            FileRef ref;
            ref.path = QFileInfo(f).fileName();
            ref.sha256 = o.value(QStringLiteral("sha256")).toString();
            ref.url = base.resolved(QUrl(f));
            e.files << ref;
            m_entries << e;
        }
        showEntries(Section::KaintHtml);
    });
    // El de Kaint (PhotoCraft y Paint.NET); si no se puede, el que trae Kaint.
    download(m_net, QUrl(kaintCatalogUrl() + refresh), kCatalogLimit, this, [this](const QByteArray& data, const QString& error) {
        if (error.isEmpty() && !parseKaintCatalog(data).isEmpty()) {
            loadKaintCatalog(data, false);
            return;
        }
        QFile bundled(QStringLiteral(":/kaint/plugins/catalog.json"));
        loadKaintCatalog(bundled.open(QIODevice::ReadOnly) ? bundled.readAll() : QByteArray(), true);
    });
}

void PluginCatalogDialog::loadKaintCatalog(const QByteArray& data, bool)
{
    QString error;
    const QList<Entry> entries = parseKaintCatalog(data, &error);
    m_entries << entries;
    for (Section s : {Section::PhotoCraft, Section::PaintNet}) {
        if (entries.isEmpty())
            message(s)->setText(error);
        else
            showEntries(s);
    }
}

QString PluginCatalogDialog::installedVersion(const Entry& e) const
{
    if (e.files.isEmpty())
        return {};
    const QString main = QFileInfo(e.files.first().path).fileName();
    for (const PluginInfo& p : PluginManager::instance()->plugins()) {
        if (e.section == Section::KaintHtml) {
            if (p.packageId == e.id)
                return p.version.isEmpty() ? QStringLiteral("?") : p.version;
        } else if (QFileInfo(p.file).fileName().compare(main, Qt::CaseInsensitive) == 0) {
            return p.version.isEmpty() ? e.version : p.version;
        }
    }
    return {};
}

void PluginCatalogDialog::showEntries(Section section)
{
    int shown = 0;
    for (int i = 0; i < m_entries.size(); ++i) {
        const Entry& e = m_entries[i];
        if (e.section != section)
            continue;
        ++shown;
        auto* row = new QWidget;
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        auto* info = new QVBoxLayout;
        auto* title = new QLabel(QStringLiteral("<b>%1</b> %2").arg(e.name.toHtmlEscaped(), e.version.toHtmlEscaped()), row);
        QString credit = tr("De %1").arg(e.author.toHtmlEscaped());
        if (!e.license.isEmpty())
            credit += QStringLiteral(" · ") + e.license.toHtmlEscaped();
        if (e.website.startsWith(QLatin1String("http")))
            credit += QStringLiteral(" · <a href=\"%1\">%2</a>").arg(e.website.toHtmlEscaped(), tr("Página web"));
        auto* who = new QLabel(credit, row);
        who->setTextFormat(Qt::RichText);
        who->setOpenExternalLinks(true);
        QString details = e.description;
        if (e.effects.size() > 1 || (e.effects.size() == 1 && e.effects.first() != e.name))
            details += (details.isEmpty() ? QString() : QStringLiteral("\n")) + tr("Efectos: %1").arg(e.effects.join(QStringLiteral(", ")));
        auto* description = new QLabel(details, row);
        QPalette dim = description->palette();
        dim.setColor(QPalette::WindowText, dim.color(QPalette::PlaceholderText));
        description->setPalette(dim);
        description->setVisible(!details.isEmpty());
        auto* status = new QLabel(row);
        status->hide();
        for (QLabel* l : {title, who, description, status}) {
            if (l != who)
                l->setTextInteractionFlags(Qt::TextSelectableByMouse);
            l->setWordWrap(true);
            l->setMinimumWidth(120);
            l->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            info->addWidget(l);
        }
        h->addLayout(info, 1);
        QPushButton* button = nullptr;
        if (!e.builtin.isEmpty()) {
            // Integrado en Kaint: se activa o se desactiva.
            auto active = [this, e] {
                for (const QString& id : e.builtin)
                    if (!m_builtins.contains(id))
                        return false;
                return true;
            };
            button = new QPushButton(active() ? tr("Desactivar") : tr("Activar"), row);
            status->setText(tr("Integrado en Kaint."));
            status->show();
            connect(button, &QPushButton::clicked, this, [this, e, button, active] {
                const bool on = !active();
                for (const QString& id : e.builtin) {
                    m_builtins.removeAll(id);
                    if (on)
                        m_builtins << id;
                }
                button->setText(on ? tr("Desactivar") : tr("Activar"));
            });
        } else {
            const QString have = installedVersion(e);
            const bool installed = !have.isEmpty() && (have == e.version || e.version.isEmpty());
            button = new QPushButton(installed ? tr("Instalado") : have.isEmpty() ? tr("Instalar") : tr("Actualizar"), row);
            button->setEnabled(!installed);
            connect(button, &QPushButton::clicked, this, [this, i, button, status] { install(i, button, status); });
        }
        h->addWidget(button, 0, Qt::AlignTop);
        rows(section)->insertWidget(rows(section)->count() - 1, row);
    }
    if (shown)
        message(section)->hide();
    else
        message(section)->setText(tr("El catálogo está vacío."));
}

void PluginCatalogDialog::install(int index, QPushButton* button, QLabel* status)
{
    const Entry e = m_entries.value(index);
    if (e.section != Section::KaintHtml) {
        installFiles(e, button, status);
        return;
    }
    button->setEnabled(false);
    button->setText(tr("Instalando…"));
    status->hide();
    QPointer<QPushButton> guardButton(button);
    QPointer<QLabel> guardStatus(status);
    const FileRef ref = e.files.value(0);
    download(m_net, ref.url, kPackageLimit, this, [this, e, ref, guardButton, guardStatus](const QByteArray& data, const QString& error) {
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
        if (sha256(data) != ref.sha256) {
            fail(tr("El complemento descargado no coincide con el del catálogo: no se instaló nada."));
            return;
        }
        QTemporaryDir dir;
        const QString tmp = dir.filePath(ref.path);
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

// PhotoCraft y Paint.NET: se descargan todos sus archivos (uno tras otro), se comprueban y se
// copian con su estructura (las bibliotecas de apoyo en su subcarpeta).
struct PluginCatalogDialog::InstallJob {
    Entry entry;
    QTemporaryDir dir;
    int next = 0;
    QPointer<QPushButton> button;
    QPointer<QLabel> status;
    QString idleText;

    // Ruta dentro de la carpeta de complementos: sin la sección del catálogo ("paintnet/...").
    static QString target(const FileRef& f)
    {
        return f.path.startsWith(QLatin1String("paintnet/")) ? f.path.mid(9) : f.path;
    }
};

void PluginCatalogDialog::installFiles(const Entry& e, QPushButton* button, QLabel* status)
{
    button->setEnabled(false);
    button->setText(tr("Instalando…"));
    status->hide();
    auto job = std::make_shared<InstallJob>();
    job->entry = e;
    job->button = button;
    job->status = status;
    job->idleText = installedVersion(e).isEmpty() ? tr("Instalar") : tr("Actualizar");
    if (!job->dir.isValid()) {
        failInstall(job, job->dir.errorString());
        return;
    }
    downloadNext(job);
}

void PluginCatalogDialog::failInstall(const std::shared_ptr<InstallJob>& job, const QString& message)
{
    if (job->status) {
        job->status->setText(message);
        job->status->show();
    }
    if (job->button) {
        job->button->setText(job->idleText);
        job->button->setEnabled(true);
    }
}

void PluginCatalogDialog::downloadNext(const std::shared_ptr<InstallJob>& job)
{
    if (job->next >= job->entry.files.size()) {
        finishInstall(job);
        return;
    }
    const FileRef ref = job->entry.files[job->next];
    download(m_net, ref.url, kPackageLimit, this, [this, job, ref](const QByteArray& data, const QString& error) {
        if (!error.isEmpty()) {
            failInstall(job, error);
            return;
        }
        if (sha256(data) != ref.sha256) {
            failInstall(job, tr("El complemento descargado no coincide con el del catálogo: no se instaló nada."));
            return;
        }
        const QString path = job->dir.filePath(InstallJob::target(ref));
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size()) {
            failInstall(job, f.errorString());
            return;
        }
        f.close();
        ++job->next;
        downloadNext(job);
    });
}

void PluginCatalogDialog::finishInstall(const std::shared_ptr<InstallJob>& job)
{
    PluginManager* pm = PluginManager::instance();
    PluginManager::DropPlan plan;
    for (int i = 0; i < job->entry.files.size(); ++i) {
        PluginManager::DropPlan::Item item;
        item.source = job->dir.filePath(InstallJob::target(job->entry.files[i]));
        item.target = InstallJob::target(job->entry.files[i]);
        // Los demás son bibliotecas o licencias que acompañan al complemento.
        item.plugin = i == 0;
        plan.copy << item;
    }
    const bool existed = !pm->installedAlready({plan.copy.first().source}).isEmpty();
    QStringList errors;
    const QStringList copied = pm->installPlan(plan, true, &errors);
    if (copied.isEmpty() || !errors.isEmpty()) {
        failInstall(job, tr("No se pudieron copiar:\n\n%1").arg(errors.isEmpty() ? job->entry.name : errors.join(QLatin1Char('\n'))));
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    pm->scan();
    if (existed)
        pm->reload(copied.first());
    // Los de Paint.NET se inspeccionan en otro proceso: se espera a saber qué efectos tienen.
    pm->waitForInspection();
    pm->scan();
    QApplication::restoreOverrideCursor();
    if (job->button)
        job->button->setText(tr("Instalado"));
    for (const PluginInfo& p : pm->plugins()) {
        if (QFileInfo(p.file).absoluteFilePath() != QFileInfo(copied.first()).absoluteFilePath() || !job->status)
            continue;
        if (!p.errors.isEmpty()) {
            job->status->setText(p.errors.join(QLatin1Char('\n')));
            job->status->show();
        }
    }
}
