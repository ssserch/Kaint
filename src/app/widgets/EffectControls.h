#pragma once

#include "core/effects/Effect.h"

#include <QColor>
#include <QVector3D>
#include <QImage>
#include <QWidget>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;

// Selector circular de ángulo (AngleChooserControl de Paint.NET): arrastrar fija el ángulo;
// Mayús lo ajusta de 15 en 15°.
class AngleChooser : public QWidget {
    Q_OBJECT
public:
    AngleChooser(double min, double max, QWidget* parent = nullptr);
    double value() const { return m_value; }
    void setValue(double degrees);
    QSize sizeHint() const override { return {64, 64}; }

signals:
    void valueChanged(double degrees);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;

private:
    void setFromPos(const QPointF& p, bool snap);

    double m_min;
    double m_max;
    double m_value = 0;
};

// Selector de centro/desplazamiento sobre una miniatura de la imagen (PanControl de
// Paint.NET). El valor va de -1 a 1 en cada eje.
class PanControl : public QWidget {
    Q_OBJECT
public:
    explicit PanControl(QWidget* parent = nullptr);
    void setImage(const QImage& thumbnail);
    QPointF value() const { return m_value; }
    void setValue(const QPointF& v);
    QSize sizeHint() const override { return {180, 120}; }

signals:
    void valueChanged(const QPointF& v);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;

private:
    QRectF imageRect() const;
    void setFromPos(const QPointF& p);

    QImage m_image;
    QPointF m_value;
};

// Globo de "Rotación y giro" (control RollBallAndSliders de Paint.NET): muestra en 3D el
// ángulo y la inclinación.
class RollBallControl : public QWidget {
    Q_OBJECT
public:
    explicit RollBallControl(double maxTilt, QWidget* parent = nullptr);
    QVector3D value() const { return m_value; }
    void setValue(const QVector3D& v);
    QSize sizeHint() const override { return {96, 96}; }

signals:
    void valueChanged(const QVector3D& v);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;

private:
    QPointF center() const;
    qreal radius() const;
    void dragTo(const QPointF& p);

    QVector3D m_value;
    double m_maxTilt;
    bool m_rotating = false;
    QPointF m_last;
};

// Botón con una muestra del color; abre el selector de color con transparencia.
class ColorButton : public QWidget {
    Q_OBJECT
public:
    explicit ColorButton(QWidget* parent = nullptr);
    QColor color() const { return m_color; }
    void setColor(const QColor& c);
    QSize sizeHint() const override { return {72, 26}; }

signals:
    void colorChanged(const QColor& c);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;

private:
    QColor m_color = Qt::black;
};

// Histograma de los canales R, G y B.
class HistogramView : public QWidget {
    Q_OBJECT
public:
    explicit HistogramView(QWidget* parent = nullptr);
    void setImage(const QImage& image, const QRect& rect);
    void setChannels(const std::array<bool, 3>& shown);
    void setVertical(bool vertical, bool towardsRight);
    QSize sizeHint() const override { return m_vertical ? QSize(90, 256) : QSize(256, 90); }

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    std::array<std::array<qint64, 256>, 3> m_hist{};
    std::array<bool, 3> m_shown{true, true, true};
    bool m_vertical = false;
    bool m_towardsRight = false;
};

// Barra de degradado con tiradores triangulares (los de Niveles).
class LevelsGradient : public QWidget {
    Q_OBJECT
public:
    LevelsGradient(int handles, QWidget* parent = nullptr);
    void setValues(const QList<double>& values);
    QList<double> values() const { return m_values; }
    void setTint(const QColor& c);
    void setVertical(bool vertical);
    QSize sizeHint() const override { return m_vertical ? QSize(30, 256) : QSize(256, 26); }

signals:
    void handleMoved(int index, double value);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    QRectF barRect() const;
    double xFor(double v) const;
    double valueAt(const QPointF& p) const;

    QList<double> m_values;
    QColor m_tint = Qt::white;
    int m_drag = -1;
    bool m_vertical = false;
};

// Editor de Niveles de Paint.NET: histogramas de entrada y salida, entrada negro/blanco,
// salida negro/gris(gamma)/blanco, Automático, Restablecer y casillas R, G, B para elegir qué
// canales se ajustan.
class LevelsEditor : public QWidget {
    Q_OBJECT
public:
    explicit LevelsEditor(QWidget* parent = nullptr);
    void setSource(const QImage& image, const QRect& rect);
    void setValue(const LevelsValue& v);
    LevelsValue value() const { return m_value; }
    QList<class QPushButton*> dialogButtons() const { return {m_auto, m_reset}; }

signals:
    void valueChanged();

private:
    void syncControls();
    QList<int> channels() const;
    template <typename F>
    void edit(F f);
    void refreshOutputHistogram();

    LevelsValue m_value;
    QImage m_source;
    QRect m_rect;
    HistogramView* m_inHist;
    HistogramView* m_outHist;
    LevelsGradient* m_inBar;
    LevelsGradient* m_outBar;
    QSpinBox* m_inLow;
    QSpinBox* m_inHigh;
    QSpinBox* m_outLow;
    QDoubleSpinBox* m_gamma;
    QSpinBox* m_outHigh;
    std::array<QCheckBox*, 3> m_checks{};
    std::array<class QFrame*, 5> m_swatches{};
    class QPushButton* m_auto;
    class QPushButton* m_reset;
    bool m_syncing = false;
};
