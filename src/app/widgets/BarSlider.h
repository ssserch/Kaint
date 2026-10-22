#pragma once

#include <QWidget>

// Deslizador con aspecto de barra de progreso y el valor escrito encima,
// como los de la barra de opciones de Paint.NET 5 (Dureza, Tolerancia...).
class BarSlider : public QWidget {
    Q_OBJECT
public:
    explicit BarSlider(int min, int max, const QString& suffix = QStringLiteral("%"), QWidget* parent = nullptr);

    int value() const { return m_value; }
    void setValue(int value);

    QSize sizeHint() const override { return {130, 24}; }
    QSize minimumSizeHint() const override { return {60, 20}; }

signals:
    void valueChanged(int value);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    void setFromX(qreal x);

    int m_min;
    int m_max;
    int m_value;
    QString m_suffix;
};
