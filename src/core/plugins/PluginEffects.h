#pragma once

#include "core/effects/Effect.h"
#include "core/plugins/KaintPlugin.h"

#include <QHash>
#include <QJsonObject>

#include <mutex>

// Efecto de un complemento de Paint.NET: los parámetros salen de la descripción de IndirectUI
// que envía el proceso auxiliar y el cálculo se hace allí (PdnBridge).
class PdnEffect : public Effect {
public:
    PdnEffect(const QString& assembly, const QString& pluginName, const QJsonObject& description);

    QString id() const override { return m_id; }
    QString name() const override { return m_name; }
    QString category() const override { return m_category; }
    bool isAdjustment() const override { return m_adjustment; }
    bool hidden() const override { return m_hidden; }
    QList<EffectParam> params() const override { return m_params; }
    bool straightAlpha() const override { return true; }
    QString pluginName() const override { return m_pluginName; }
    QImage iconImage() const override { return m_icon; }
    QString helpText() const override { return m_help; }
    bool hasCustomDialog() const override { return m_customDialog; }
    bool showCustomDialog(const QImage& src, const QRect& roi, const EffectContext& ctx, QString* error) const override;
    QString takeError() const override;
    void render(const RenderArgs& args) const override;

    // Valores para el proceso auxiliar (nombres y tipos de Paint.NET).
    QJsonObject toHostValues(const ParamValues& values) const;
    static QString kaintCategory(const QString& subMenu);
    // Identificador de un efecto de un ensamblado (también sin cargarlo).
    static QString makeId(const QString& assembly, const QString& typeId);

private:
    QString m_assembly;
    QString m_typeId;
    QString m_id;
    QString m_name;
    QString m_category;
    bool m_adjustment = false;
    bool m_hidden = false;
    bool m_customDialog = false;
    QString m_pluginName;
    QImage m_icon;
    QString m_help;
    QList<EffectParam> m_params;
    QHash<QString, bool> m_colorAlpha;
    mutable std::mutex m_errorMutex;
    mutable QString m_error;
};

// Efecto 'index' de un complemento nativo: con la versión 1 de la API la tabla tiene la
// estructura antigua (sin icono) y se lee con su tamaño; los campos nuevos quedan a cero.
KaintEffectInfo nativeEffectAt(const KaintPluginInfo* plugin, int index);

// Efecto de un complemento nativo (.so con la API de KaintPlugin.h). 'folder' es la carpeta del
// complemento (para su icono en un archivo).
class NativeEffect : public Effect {
public:
    NativeEffect(const KaintEffectInfo& info, const QString& folder, const QString& pluginName);

    QString id() const override { return m_id; }
    QString name() const override { return m_name; }
    QString category() const override { return m_category; }
    bool isAdjustment() const override { return m_category.isEmpty(); }
    QList<EffectParam> params() const override { return m_params; }
    bool straightAlpha() const override { return true; }
    QString pluginName() const override { return m_pluginName; }
    QImage iconImage() const override { return m_icon; }
    void render(const RenderArgs& args) const override;

private:
    QString m_id;
    QString m_name;
    QString m_category;
    QString m_pluginName;
    QImage m_icon;
    QList<EffectParam> m_params;
    void (*m_render)(const KaintRenderArgs*) = nullptr;
};
