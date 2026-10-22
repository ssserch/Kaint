#pragma once

#include "core/effects/Effect.h"
#include "core/plugins/KaintPlugin.h"

#include <QHash>
#include <QJsonObject>

#include <mutex>

// Efecto de un complemento de Paint.NET: los parámetros salen de la
// descripción de IndirectUI que envía el proceso auxiliar y el cálculo se
// hace allí (PdnBridge).
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
    // Categoría de Kaint para un submenú de Paint.NET ("Blurs" -> "Desenfoques").
    static QString kaintCategory(const QString& subMenu);

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
    QHash<QString, bool> m_colorAlpha; // colores Int32 con alfa (ARGB) o sin él (RGB)
    mutable std::mutex m_errorMutex;
    mutable QString m_error;
};

// Efecto de un complemento nativo (.so con la API de KaintPlugin.h).
class NativeEffect : public Effect {
public:
    NativeEffect(const KaintEffectInfo& info, const QString& pluginName);

    QString id() const override { return m_id; }
    QString name() const override { return m_name; }
    QString category() const override { return m_category; }
    bool isAdjustment() const override { return m_category.isEmpty(); }
    QList<EffectParam> params() const override { return m_params; }
    bool straightAlpha() const override { return true; }
    QString pluginName() const override { return m_pluginName; }
    void render(const RenderArgs& args) const override;

private:
    QString m_id;
    QString m_name;
    QString m_category;
    QString m_pluginName;
    QList<EffectParam> m_params;
    void (*m_render)(const KaintRenderArgs*) = nullptr;
};
