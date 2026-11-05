#include "app/Customization.h"

#include "core/effects/Effect.h"
#include "core/plugins/PluginEffects.h"

#include <QCoreApplication>
#include <QSettings>
#include <QVariantMap>

namespace {

// Submenús de Kaint (y de Paint.NET) por su nombre en inglés.
const QStringList& builtinSubmenus()
{
    static const QStringList keys{QStringLiteral("Artistic"), QStringLiteral("Blurs"),  QStringLiteral("Color"),
                                  QStringLiteral("Distort"),  QStringLiteral("Noise"),  QStringLiteral("Object"),
                                  QStringLiteral("Photo"),    QStringLiteral("Render"), QStringLiteral("Stylize")};
    return keys;
}

const QString kAdjustments = QStringLiteral("adjustments");
const QString kEffects = QStringLiteral("effects");
const QString kSubmenu = QStringLiteral("effects/");

} // namespace

Customization* Customization::instance()
{
    static Customization* c = new Customization;
    return c;
}

Customization::Customization()
{
    QSettings s;
    s.beginGroup(QStringLiteral("customize"));
    const QVariantMap locations = s.value(QStringLiteral("effectLocations")).toMap();
    for (auto it = locations.begin(); it != locations.end(); ++it) {
        const QString loc = it.value().toString();
        if (loc == kAdjustments || loc == kEffects || (loc.startsWith(kSubmenu) && loc.size() > kSubmenu.size()))
            m_locations.insert(it.key(), loc);
    }
    for (const QString& id : s.value(QStringLiteral("hiddenEffects")).toStringList())
        m_hidden.insert(id);
    for (const QVariant& v : s.value(QStringLiteral("toolOrder")).toList())
        m_toolOrder << v.toInt();
    for (const QVariant& v : s.value(QStringLiteral("hiddenTools")).toList())
        m_hiddenTools.insert(v.toInt());
}

QString Customization::submenuKey(const QString& displayName)
{
    for (const QString& key : builtinSubmenus())
        if (PdnEffect::kaintCategory(key) == displayName)
            return key;
    return displayName;
}

QString Customization::submenuName(const QString& key)
{
    return builtinSubmenus().contains(key) ? PdnEffect::kaintCategory(key) : key;
}

QString Customization::defaultLocation(const Effect* effect)
{
    if (effect->isAdjustment())
        return kAdjustments;
    if (effect->category().isEmpty())
        return kEffects;
    return kSubmenu + submenuKey(effect->category());
}

QString Customization::location(const Effect* effect) const
{
    return m_locations.value(effect->id(), defaultLocation(effect));
}

Customization::Placement Customization::placement(const Effect* effect) const
{
    const QString loc = location(effect);
    Placement p;
    p.adjustment = loc == kAdjustments;
    if (loc.startsWith(kSubmenu))
        p.submenu = submenuName(loc.mid(kSubmenu.size()));
    return p;
}

QString Customization::locationName(const QString& location)
{
    const QString effects = QCoreApplication::translate("Customization", "Efectos");
    if (location == kAdjustments)
        return QCoreApplication::translate("Customization", "Ajustes");
    if (location.startsWith(kSubmenu))
        return QStringLiteral("%1 > %2").arg(effects, submenuName(location.mid(kSubmenu.size())));
    return effects;
}

void Customization::setLocation(const QStringList& effectIds, const QString& location)
{
    for (const QString& id : effectIds) {
        const Effect* e = Effects::find(id);
        if (location.isEmpty() || (e && location == defaultLocation(e)))
            m_locations.remove(id);
        else
            m_locations.insert(id, location);
    }
    saveEffects();
    emit effectMenusChanged();
}

void Customization::setHidden(const QStringList& effectIds, bool hidden)
{
    for (const QString& id : effectIds) {
        if (hidden)
            m_hidden.insert(id);
        else
            m_hidden.remove(id);
    }
    saveEffects();
    emit effectMenusChanged();
}

void Customization::resetEffects(const QStringList& effectIds)
{
    if (effectIds.isEmpty()) {
        m_locations.clear();
        m_hidden.clear();
    } else {
        for (const QString& id : effectIds) {
            m_locations.remove(id);
            m_hidden.remove(id);
        }
    }
    saveEffects();
    emit effectMenusChanged();
}

void Customization::saveEffects() const
{
    QSettings s;
    s.beginGroup(QStringLiteral("customize"));
    QVariantMap locations;
    for (auto it = m_locations.begin(); it != m_locations.end(); ++it)
        locations.insert(it.key(), it.value());
    if (locations.isEmpty())
        s.remove(QStringLiteral("effectLocations"));
    else
        s.setValue(QStringLiteral("effectLocations"), locations);
    QStringList hidden(m_hidden.begin(), m_hidden.end());
    hidden.sort();
    if (hidden.isEmpty())
        s.remove(QStringLiteral("hiddenEffects"));
    else
        s.setValue(QStringLiteral("hiddenEffects"), hidden);
}

QList<int> Customization::toolOrder(const QList<int>& defaults) const
{
    QList<int> order;
    for (int id : m_toolOrder)
        if (defaults.contains(id) && !order.contains(id))
            order << id;
    for (int id : defaults)
        if (!order.contains(id))
            order << id;
    return order;
}

void Customization::setToolOrder(const QList<int>& order)
{
    m_toolOrder = order;
    saveTools();
    emit toolsChanged();
}

void Customization::setToolHidden(int id, bool hidden)
{
    if (hidden)
        m_hiddenTools.insert(id);
    else
        m_hiddenTools.remove(id);
    saveTools();
    emit toolsChanged();
}

void Customization::resetTools()
{
    m_toolOrder.clear();
    m_hiddenTools.clear();
    saveTools();
    emit toolsChanged();
}

void Customization::saveTools() const
{
    QSettings s;
    s.beginGroup(QStringLiteral("customize"));
    QVariantList order, hidden;
    for (int id : m_toolOrder)
        order << id;
    QList<int> sorted(m_hiddenTools.begin(), m_hiddenTools.end());
    std::sort(sorted.begin(), sorted.end());
    for (int id : sorted)
        hidden << id;
    if (order.isEmpty())
        s.remove(QStringLiteral("toolOrder"));
    else
        s.setValue(QStringLiteral("toolOrder"), order);
    if (hidden.isEmpty())
        s.remove(QStringLiteral("hiddenTools"));
    else
        s.setValue(QStringLiteral("hiddenTools"), hidden);
}
