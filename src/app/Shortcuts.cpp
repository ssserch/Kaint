#include "app/Shortcuts.h"

#include <QAction>
#include <QCollator>
#include <QPointer>
#include <QSettings>

#include <algorithm>

namespace Shortcuts {

namespace {

struct Registered {
    QPointer<QAction> action;
    QList<QKeySequence> defaults;
};

QHash<QString, Registered>& registry()
{
    static QHash<QString, Registered> r;
    return r;
}

const QString kGroup = QStringLiteral("shortcuts/");

void save(const QString& id, const QAction* action)
{
    QSettings s;
    const Registered& r = registry()[id];
    if (action->shortcuts() == r.defaults)
        s.remove(kGroup + id);
    else
        s.setValue(kGroup + id, action->shortcut().toString(QKeySequence::PortableText));
}

} // namespace

void registerActions(const QHash<QString, QAction*>& actions)
{
    const QSettings s;
    for (auto it = actions.constBegin(); it != actions.constEnd(); ++it) {
        QAction* a = it.value();
        registry().insert(it.key(), {a, a->shortcuts()});
        const QString key = kGroup + it.key();
        if (s.contains(key))
            a->setShortcut(QKeySequence::fromString(s.value(key).toString(), QKeySequence::PortableText));
    }
}

QString displayName(const QAction* action)
{
    QString text = action->text();
    text.remove(QLatin1Char('&'));
    if (text.endsWith(QStringLiteral("...")))
        text.chop(3);
    else if (text.endsWith(QChar(0x2026)))
        text.chop(1);
    return text.trimmed();
}

QList<Entry> entries()
{
    QList<Entry> list;
    for (auto it = registry().constBegin(); it != registry().constEnd(); ++it)
        if (it->action && !displayName(it->action).isEmpty() && !it->action->isSeparator())
            list.append({it.key(), it->action, it->defaults});
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(list.begin(), list.end(), [&](const Entry& a, const Entry& b) {
        return collator.compare(displayName(a.action), displayName(b.action)) < 0;
    });
    return list;
}

QAction* assign(const QString& id, const QKeySequence& sequence)
{
    const auto it = registry().constFind(id);
    if (it == registry().cend() || !it->action)
        return nullptr;
    QAction* previous = nullptr;
    if (!sequence.isEmpty()) {
        for (auto other = registry().constBegin(); other != registry().constEnd(); ++other) {
            if (other.key() == id || !other->action || !other->action->shortcuts().contains(sequence))
                continue;
            QList<QKeySequence> keys = other->action->shortcuts();
            keys.removeAll(sequence);
            other->action->setShortcuts(keys);
            save(other.key(), other->action);
            previous = other->action;
        }
    }
    it->action->setShortcut(sequence);
    save(id, it->action);
    return previous;
}

void reset(const QString& id)
{
    const auto it = registry().constFind(id);
    if (it == registry().cend() || !it->action)
        return;
    it->action->setShortcuts(it->defaults);
    QSettings().remove(kGroup + id);
}

void resetAll()
{
    for (auto it = registry().constBegin(); it != registry().constEnd(); ++it)
        if (it->action)
            it->action->setShortcuts(it->defaults);
    QSettings().remove(QStringLiteral("shortcuts"));
}

bool isDefault(const QString& id)
{
    const auto it = registry().constFind(id);
    return it == registry().cend() || !it->action || it->action->shortcuts() == it->defaults;
}

} // namespace Shortcuts
