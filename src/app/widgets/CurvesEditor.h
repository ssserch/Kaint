#pragma once

#include "core/effects/Effect.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;

class CurveGraph : public QWidget {
    Q_OBJECT
public:
    explicit CurveGraph(QWidget* parent = nullptr);
    void setValue(const CurvesValue& v);
    const CurvesValue& value() const { return m_value; }
    void setChannels(int channel, const QList<int>& followers);
    QSize sizeHint() const override { return {260, 260}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override { return w; }

signals:
    void edited();
    void hovered(const QPointF& value, bool inside);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    QRectF area() const;
    QPointF toWidget(const QPointF& v) const;
    QPointF toValue(const QPointF& w) const;
    int hit(const QPointF& w) const;
    void propagate();

    CurvesValue m_value;
    int m_channel = 0;
    QList<int> m_followers;
    int m_drag = -1;
};

// Editor de curvas de Paint.NET: luminosidad o RGB, con casillas para editar a la vez los
// canales rojo, verde y azul que estén marcados.
class CurvesEditor : public QWidget {
    Q_OBJECT
public:
    explicit CurvesEditor(QWidget* parent = nullptr);
    void setValue(const CurvesValue& v);
    CurvesValue value() const;
    QList<class QPushButton*> dialogButtons() const { return {m_reset}; }

signals:
    void valueChanged();

private:
    void updateChannel();

    class QPushButton* m_reset;
    QComboBox* m_mode;
    QCheckBox* m_red;
    QCheckBox* m_green;
    QCheckBox* m_blue;
    QLabel* m_coords;
    CurveGraph* m_graph;
};
