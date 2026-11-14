#include "app/CustomLanguages.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QXmlStreamReader>

#include <algorithm>

namespace CustomLanguages {

namespace {

std::optional<Language>& activeSlot()
{
    static std::optional<Language> slot;
    return slot;
}

QString cleanId(const QString& id)
{
    static const QRegularExpression bad(QStringLiteral("[^A-Za-z0-9_.-]+"));
    QString out = id;
    out.replace(bad, QStringLiteral("-"));
    return out.left(60);
}

} // namespace

QString folder()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/languages");
}

std::optional<Language> parse(const QByteArray& json, QString* error)
{
    QJsonParseError jsonError;
    const QJsonObject o = QJsonDocument::fromJson(json, &jsonError).object();
    if (o.value(QStringLiteral("format")).toString() != QLatin1String("kaint-language") || !o.value(QStringLiteral("strings")).isObject()) {
        if (error)
            *error = QObject::tr("No es un idioma de Kaint (.kaintlang).");
        return std::nullopt;
    }
    auto text = [&](const char* key, int max) { return o.value(QLatin1String(key)).toString().trimmed().left(max); };
    Language lang;
    lang.name = text("name", 60);
    if (lang.name.isEmpty())
        lang.name = QObject::tr("Idioma personalizado");
    lang.id = cleanId(text("id", 60));
    if (lang.id.isEmpty())
        lang.id = cleanId(lang.name.toLower()) + QLatin1Char('-') + QString::number(QDateTime::currentMSecsSinceEpoch(), 36);
    lang.code = text("code", 20);
    lang.author = text("author", 80);
    const QString url = text("authorUrl", 300);
    if (url.startsWith(QLatin1String("https://")) || url.startsWith(QLatin1String("http://")))
        lang.authorUrl = url;
    const QJsonObject strings = o.value(QStringLiteral("strings")).toObject();
    for (auto it = strings.begin(); it != strings.end(); ++it) {
        if (it.value().isString() && !it.value().toString().isEmpty()) {
            lang.strings.insert(it.key(), it.value().toString());
        } else if (it.value().isArray()) {
            QStringList forms;
            for (const QJsonValue& v : it.value().toArray())
                forms << v.toString();
            if (!forms.isEmpty())
                lang.strings.insert(it.key(), forms);
        }
    }
    return lang;
}

QByteArray serialize(const Language& lang)
{
    QJsonObject strings;
    for (auto it = lang.strings.begin(); it != lang.strings.end(); ++it) {
        if (it.value().typeId() == QMetaType::QStringList)
            strings.insert(it.key(), QJsonArray::fromStringList(it.value().toStringList()));
        else
            strings.insert(it.key(), it.value().toString());
    }
    QJsonObject o{{QStringLiteral("format"), QStringLiteral("kaint-language")},
                  {QStringLiteral("version"), 1},
                  {QStringLiteral("id"), lang.id},
                  {QStringLiteral("name"), lang.name},
                  {QStringLiteral("code"), lang.code},
                  {QStringLiteral("author"), lang.author},
                  {QStringLiteral("authorUrl"), lang.authorUrl},
                  {QStringLiteral("strings"), strings}};
    return QJsonDocument(o).toJson(QJsonDocument::Indented);
}

QList<Language> list()
{
    QList<Language> out;
    const QDir dir(folder());
    for (const QString& f : dir.entryList({QStringLiteral("*.kaintlang")}, QDir::Files, QDir::Name)) {
        QFile file(dir.filePath(f));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        if (auto lang = parse(file.read(32 * 1024 * 1024))) {
            lang->id = QFileInfo(f).completeBaseName();
            out << *lang;
        }
    }
    std::sort(out.begin(), out.end(), [](const Language& a, const Language& b) { return a.name.localeAwareCompare(b.name) < 0; });
    return out;
}

std::optional<Language> find(const QString& id)
{
    for (const Language& l : list())
        if (l.id == id)
            return l;
    return std::nullopt;
}

bool save(const Language& lang, QString* error)
{
    QDir().mkpath(folder());
    QSaveFile file(QDir(folder()).filePath(cleanId(lang.id) + QStringLiteral(".kaintlang")));
    if (!file.open(QIODevice::WriteOnly) || file.write(serialize(lang)) < 0 || !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

bool remove(const QString& id)
{
    return QFile::remove(QDir(folder()).filePath(cleanId(id) + QStringLiteral(".kaintlang")));
}

const QList<Source>& sources()
{
    static const QList<Source> list = [] {
        QList<Source> out;
        QFile file(QStringLiteral(":/i18n/kaint_en.ts"));
        if (!file.open(QIODevice::ReadOnly))
            return out;
        QXmlStreamReader xml(&file);
        QString context;
        Source current;
        QString comment;
        QStringList forms;
        bool inMessage = false;
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement()) {
                const auto name = xml.name();
                if (name == QLatin1String("name") && !inMessage) {
                    context = xml.readElementText();
                } else if (name == QLatin1String("message")) {
                    inMessage = true;
                    current = Source();
                    comment.clear();
                    forms.clear();
                } else if (name == QLatin1String("source")) {
                    current.key = xml.readElementText();
                } else if (name == QLatin1String("comment")) {
                    comment = xml.readElementText();
                } else if (name == QLatin1String("numerusform")) {
                    forms << xml.readElementText();
                } else if (name == QLatin1String("translation")) {
                    if (xml.attributes().value(QLatin1String("type")) != QLatin1String("vanished")) {
                        const QString t = xml.readElementText(QXmlStreamReader::IncludeChildElements);
                        if (forms.isEmpty())
                            current.english = t;
                    }
                }
            } else if (xml.isEndElement() && xml.name() == QLatin1String("message")) {
                inMessage = false;
                if (!forms.isEmpty())
                    current.english = forms.join(QStringLiteral(" / "));
                if (!comment.isEmpty())
                    current.key = comment + QChar(4) + current.key;
                current.context = context;
                if (!current.key.isEmpty())
                    out << current;
            }
        }
        // Un texto puede salir en varios contextos: basta con una vez.
        QSet<QString> seen;
        QList<Source> unique;
        for (const Source& s : std::as_const(out))
            if (!seen.contains(s.key)) {
                seen.insert(s.key);
                unique << s;
            }
        std::sort(unique.begin(), unique.end(), [](const Source& a, const Source& b) { return a.key.localeAwareCompare(b.key) < 0; });
        return unique;
    }();
    return list;
}

void setActive(const std::optional<Language>& lang)
{
    activeSlot() = lang;
}

const Language* active()
{
    return activeSlot() ? &*activeSlot() : nullptr;
}

} // namespace CustomLanguages

CustomTranslator::CustomTranslator(CustomLanguages::Language lang, QTranslator* fallback, QObject* parent)
    : QTranslator(parent)
    , m_lang(std::move(lang))
    , m_fallback(fallback)
{
}

QString CustomTranslator::translate(const char* context, const char* sourceText, const char* disambiguation, int n) const
{
    const QString source = QString::fromUtf8(sourceText);
    auto it = m_lang.strings.constEnd();
    if (disambiguation && *disambiguation)
        it = m_lang.strings.constFind(QString::fromUtf8(disambiguation) + QChar(4) + source);
    if (it == m_lang.strings.constEnd())
        it = m_lang.strings.constFind(source);
    if (it != m_lang.strings.constEnd()) {
        if (it.value().typeId() == QMetaType::QStringList) {
            const QStringList forms = it.value().toStringList();
            if (!forms.isEmpty())
                return forms.value(n == 1 ? 0 : std::min<qsizetype>(1, forms.size() - 1));
        } else if (!it.value().toString().isEmpty()) {
            return it.value().toString();
        }
    }
    return m_fallback ? m_fallback->translate(context, sourceText, disambiguation, n) : QString();
}
