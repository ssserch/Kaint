#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QTranslator>
#include <QVariant>

#include <optional>

// Idiomas personalizados: un archivo .kaintlang (JSON, el mismo que usa la versión web) con el
// nombre del idioma, su código, quién lo tradujo (y su página) y los textos, por su original en
// español (con "comentario\u0004texto" para los que se repiten con otro sentido). Se importan,
// crean, editan, exportan y quitan desde Configuración > Interfaz de usuario y se guardan en la
// carpeta de datos de Kaint. Lo que un idioma no traduce sale en inglés.
namespace CustomLanguages {

struct Language {
    QString id;
    QString name;
    QString code;
    QString author;
    QString authorUrl;
    // Texto original -> traducción (QString, o QStringList con las formas del plural).
    QHash<QString, QVariant> strings;
};

QString folder();
QList<Language> list();
std::optional<Language> find(const QString& id);
bool save(const Language& lang, QString* error = nullptr);
bool remove(const QString& id);
std::optional<Language> parse(const QByteArray& json, QString* error = nullptr);
QByteArray serialize(const Language& lang);

// Los textos que se pueden traducir: el original (en español, con su comentario si lo tiene), el
// contexto y el inglés (de la traducción inglesa que trae Kaint).
struct Source {
    QString key;
    QString context;
    QString english;
};
const QList<Source>& sources();

// El idioma personalizado en uso (el de AppSettings::language "custom:<id>"), si hay.
void setActive(const std::optional<Language>& lang);
const Language* active();

} // namespace CustomLanguages

// Traduce con un idioma personalizado y, lo que no tiene, con la traducción inglesa ('fallback').
class CustomTranslator : public QTranslator {
public:
    CustomTranslator(CustomLanguages::Language lang, QTranslator* fallback, QObject* parent = nullptr);
    QString translate(const char* context, const char* sourceText, const char* disambiguation = nullptr, int n = -1) const override;
    bool isEmpty() const override { return false; }

private:
    CustomLanguages::Language m_lang;
    QTranslator* m_fallback;
};
