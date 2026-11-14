#pragma once

#include <QDialog>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <memory>
class QLabel;
class QNetworkAccessManager;
class QPushButton;
class QTabWidget;
class QVBoxLayout;

// Explorar complementos: una lista limitada de complementos elegidos y probados para Kaint, por
// secciones, cada uno con su botón de instalar:
// · Kaint.HTML: el catálogo de complementos de paint.js, el mismo que usa la versión web.
// · PhotoCraft: filtros .wasm de la tienda ArtCraft.
// · Paint.NET: complementos .dll (los de BoltBait y otros autores) que funcionan en Kaint.
// Las dos últimas salen del catálogo de Kaint (plugins/catalog.json en el repositorio; si no se
// puede descargar, la copia que trae Kaint). Todo lo que se descarga se comprueba con su SHA-256
// antes de copiarlo a la carpeta de complementos. Se abre desde Configuración > Complementos (el
// botón se puede esconder en Interfaz de usuario: AppSettings::browsePlugins).
class PluginCatalogDialog : public QDialog {
    Q_OBJECT
public:
    enum class Section { KaintHtml, PhotoCraft, PaintNet };

    explicit PluginCatalogDialog(QWidget* parent = nullptr, Section initial = Section::KaintHtml);

    // El catálogo de paint.js y el de Kaint (y la carpeta de la que cuelgan sus archivos).
    static QString catalogUrl();
    static QString kaintCatalogUrl();
    static QString kaintCatalogBase();

    struct FileRef {
        QUrl url;
        QString path; // relativa a la carpeta de complementos
        QString sha256;
        qint64 size = 0;
    };
    struct Entry {
        Section section = Section::KaintHtml;
        QString id;
        QString name;
        QString version;
        QString author;
        QString description;
        QString license;
        QString website;
        QStringList effects;
        QList<FileRef> files;
        // Los integrados en Kaint (los efectos que siguen complementos de BoltBait): se activan en
        // vez de descargarse.
        QStringList builtin;
    };
    // Lee el catálogo de Kaint (para las pruebas también): las secciones PhotoCraft y Paint.NET.
    static QList<Entry> parseKaintCatalog(const QByteArray& json, QString* error = nullptr);

    // Los efectos integrados activados (AppSettings::boltBaitEffects): se dan antes de abrirlo y se
    // leen al cerrarlo.
    void setEnabledBuiltins(const QStringList& ids) { m_builtins = ids; }
    QStringList enabledBuiltins() const { return m_builtins; }

private:
    struct InstallJob;
    void loadCatalogs();
    void loadKaintCatalog(const QByteArray& data, bool bundled);
    void showEntries(Section section);
    void install(int index, QPushButton* button, QLabel* status);
    void installFiles(const Entry& e, QPushButton* button, QLabel* status);
    void downloadNext(const std::shared_ptr<InstallJob>& job);
    void finishInstall(const std::shared_ptr<InstallJob>& job);
    static void failInstall(const std::shared_ptr<InstallJob>& job, const QString& message);
    QString installedVersion(const Entry& e) const;
    QVBoxLayout* rows(Section section) const;
    QLabel* message(Section section) const;

    QNetworkAccessManager* m_net = nullptr;
    QTabWidget* m_tabs = nullptr;
    QVBoxLayout* m_rows[3] = {};
    QLabel* m_message[3] = {};
    QList<Entry> m_entries;
    QStringList m_builtins;
};
