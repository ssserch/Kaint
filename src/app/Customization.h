#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QStringList>

class Effect;

// Configuración > Personalización: dónde sale cada efecto en los menús (también los de los
// complementos: por ejemplo, uno de un complemento en Efectos > Desenfoques), qué efectos se ocultan
// y qué herramientas hay en la ventana Herramientas y en qué orden. Se guarda con QSettings.
class Customization : public QObject {
    Q_OBJECT
public:
    static Customization* instance();

    // Ubicación de un efecto en los menús: "adjustments" (Ajustes), "effects" (el menú Efectos, sin
    // submenú) o "effects/<submenú>". Los submenús de Kaint se guardan por su nombre en inglés
    // ("effects/Blurs"), así no cambian con el idioma; los nuevos, con el nombre que se les dio.
    struct Placement {
        bool adjustment = false;
        // Nombre visible del submenú de Efectos (vacío: directamente en Efectos).
        QString submenu;
    };
    Placement placement(const Effect* effect) const;
    QString location(const Effect* effect) const;
    static QString defaultLocation(const Effect* effect);
    // "Ajustes", "Efectos" o "Efectos > Desenfoques".
    static QString locationName(const QString& location);
    static QString submenuKey(const QString& displayName);
    static QString submenuName(const QString& key);
    // Mueve los efectos a otra ubicación (vacía: la suya de siempre).
    void setLocation(const QStringList& effectIds, const QString& location);
    bool isMoved(const QString& effectId) const { return m_locations.contains(effectId); }
    bool isHidden(const QString& effectId) const { return m_hidden.contains(effectId); }
    void setHidden(const QStringList& effectIds, bool hidden);
    bool hasEffectChanges() const { return !m_locations.isEmpty() || !m_hidden.isEmpty(); }
    void resetEffects(const QStringList& effectIds = {});

    // Ventana Herramientas: orden (por ToolId) y herramientas ocultas. Las que falten en el orden
    // guardado (nuevas) van al final en su orden de fábrica.
    QList<int> toolOrder(const QList<int>& defaults) const;
    void setToolOrder(const QList<int>& order);
    bool isToolHidden(int id) const { return m_hiddenTools.contains(id); }
    void setToolHidden(int id, bool hidden);
    bool hasToolChanges() const { return !m_toolOrder.isEmpty() || !m_hiddenTools.isEmpty(); }
    void resetTools();

signals:
    void effectMenusChanged();
    void toolsChanged();

private:
    Customization();
    void saveEffects() const;
    void saveTools() const;

    QHash<QString, QString> m_locations;
    QSet<QString> m_hidden;
    QList<int> m_toolOrder;
    QSet<int> m_hiddenTools;
};
