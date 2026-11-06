#pragma once

#include <QDialog>
#include <QList>
#include <QString>
#include <QUrl>

class QLabel;
class QNetworkAccessManager;
class QPushButton;
class QVBoxLayout;

// Complementos de Kaint.HTML: el catálogo de complementos de paint.js, el mismo que usa la versión
// web (Explorar complementos), cada uno con su botón de instalar. Se descargan, se comprueba que
// su SHA-256 sea el del catálogo y se copian a la carpeta de complementos. Se abre desde
// Configuración > Complementos, si se activó su botón en Interfaz de usuario
// (AppSettings::kaintHtmlPlugins).
class PluginCatalogDialog : public QDialog {
    Q_OBJECT
public:
    explicit PluginCatalogDialog(QWidget* parent = nullptr);

    static QString catalogUrl();

private:
    struct Entry {
        QString id;
        QString name;
        QString version;
        QString author;
        QString description;
        QString license;
        QString sha256;
        QString file;
        QUrl url;
    };
    void loadCatalog();
    void showEntries();
    void install(int index, QPushButton* button, QLabel* status);
    bool isInstalled(const Entry& e) const;

    QNetworkAccessManager* m_net = nullptr;
    QVBoxLayout* m_rows = nullptr;
    QLabel* m_message = nullptr;
    QList<Entry> m_entries;
};
