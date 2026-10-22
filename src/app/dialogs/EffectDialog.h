#pragma once

#include "core/Document.h"
#include "core/effects/Effect.h"

#include <QDialog>
#include <QHash>
#include <QTimer>

#include <atomic>
#include <functional>
#include <memory>
#include <thread>

// Diálogo generado a partir de los parámetros del efecto, como el IndirectUI
// de Paint.NET: título con línea, deslizador + caja numérica + restablecer,
// selector de ángulo, de centro sobre la miniatura, color, semilla,
// parámetros vinculados y los editores de Curvas y Niveles.
class EffectDialog : public QDialog {
    Q_OBJECT
public:
    // 'source' y 'roi' sirven para la miniatura del selector de centro y los
    // histogramas de Niveles.
    EffectDialog(const Effect& effect, const ParamValues& initial, const QImage& source, const QRect& roi,
                 QWidget* parent = nullptr);
    ParamValues values() const { return m_values; }

signals:
    void valuesChanged(const ParamValues& values);

private:
    void set(const QString& key, const QVariant& value);
    void updateReadOnly();

    ParamValues m_values;
    QList<EffectParam> m_params;
    // Actualiza el control de un parámetro sin volver a emitir cambios.
    QHash<QString, std::function<void(const QVariant&)>> m_setters;
    bool m_linking = false;
    QHash<QString, QWidget*> m_containers; // controles de cada parámetro
    QHash<QString, QWidget*> m_bodies;     // controles bajo un título con casilla
    QList<class QPushButton*> m_extraButtons; // botones de los editores (Restablecer...)
};

// Calcula la vista previa en un hilo aparte (cancelando la anterior si el
// usuario sigue moviendo un deslizador) y la muestra en el lienzo.
class EffectPreview : public QObject {
    Q_OBJECT
public:
    EffectPreview(Document* doc, LayerPtr layer, const Effect& effect, const EffectContext& ctx);
    ~EffectPreview() override;

    void request(const ParamValues& values);
    // Resultado definitivo con estos valores (reutiliza la vista previa si
    // coincide). Muestra una ventana de progreso si tarda; nulo si se cancela.
    QImage finalResult(const ParamValues& values, QWidget* parent);

signals:
    // Avance de la vista previa en milésimas (-1 = terminado).
    void progress(int permille);

private:
    void start();
    void stop();
    void onFinished(const QImage& result, const ParamValues& values, bool cancelled, int generation);

    Document* m_doc;
    LayerPtr m_layer;
    const Effect& m_effect;
    EffectContext m_ctx;
    QImage m_source;
    Selection m_selection;

    QTimer m_debounce;
    QTimer m_progressTimer;
    ParamValues m_pending;
    bool m_hasPending = false;
    std::thread m_thread;
    std::shared_ptr<std::atomic_bool> m_cancel;
    std::shared_ptr<EffectProgress> m_progress;
    bool m_running = false;
    int m_generation = 0; // descarta resultados de trabajos ya cancelados
    QImage m_lastResult;
    ParamValues m_lastValues;
};

// Aplica un efecto en otro hilo con una ventana de progreso cancelable
// (Repetir efecto, efectos sin opciones). Devuelve nulo si se cancela.
QImage applyEffectWithProgress(const Effect& effect, const QImage& src, const Selection& selection,
                               const ParamValues& values, EffectContext ctx, QWidget* parent);
