#pragma once

#include <QColor>
#include <QImage>
#include <QList>
#include <QPointer>
#include <QWidget>

class QPainter;
class QVariantAnimation;

// Rueda de tono/saturación (el brillo va en un deslizador aparte).
class ColorWheel : public QWidget {
    Q_OBJECT
public:
    explicit ColorWheel(QWidget* parent = nullptr);
    void setColor(const QColor& c);
    // Tono en el sentido de las agujas del reloj (el amarillo abajo), como la rueda de Paint.NET 5;
    // si no, al revés (el amarillo arriba).
    void setClockwise(bool on);
    QColor color() const { return m_color; }
    QSize sizeHint() const override { return {150, 150}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override { return w; }

signals:
    void colorPicked(const QColor& c, Qt::MouseButton button);

protected:
    void paintEvent(QPaintEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    void paintWheel(QPainter& p);
    void pickAt(const QPointF& p, Qt::KeyboardModifiers mods);
    QRectF wheelRect() const;

    QColor m_color;
    QColor m_pressColor;
    Qt::MouseButton m_button = Qt::NoButton;
    QImage m_wheel;
    bool m_clockwise = false;
};

// Deslizador con el degradado del canal que controla de fondo.
class ColorSlider : public QWidget {
    Q_OBJECT
public:
    enum Channel { Red, Green, Blue, Hue, Saturation, Value, Alpha };
    ColorSlider(Channel channel, Qt::Orientation orientation, QWidget* parent = nullptr);

    void setColor(const QColor& c);
    int value() const;
    int maximum() const;
    QSize sizeHint() const override;

signals:
    void valueChanged(int value);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

private:
    void paintSlider(QPainter& p);
    QColor colorAt(qreal t) const;
    void setFromPos(const QPointF& p);

    Channel m_channel;
    Qt::Orientation m_orientation;
    QColor m_color;
};

// Cuadros de color principal y secundario superpuestos, con los botones de intercambiar (X) y
// restablecer a blanco y negro.
class ColorSwatches : public QWidget {
    Q_OBJECT
public:
    explicit ColorSwatches(QWidget* parent = nullptr);
    void setColors(const QColor& primary, const QColor& secondary);
    void setEditingSecondary(bool secondary);
    QSize sizeHint() const override { return {56, 56}; }
    QRectF primaryRect() const;
    QRectF secondaryRect() const;

signals:
    void primaryClicked();
    void secondaryClicked();
    void primaryDoubleClicked();
    void secondaryDoubleClicked();
    void swapClicked();
    void resetClicked();

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;

private:
    QRectF swapRect() const;
    QRectF resetRect() const;

    QColor m_primary;
    QColor m_secondary;
    bool m_editingSecondary = false;
    // Cuánto está activa la ranura principal (1) o la secundaria (0); intermedio mientras se anima.
    qreal m_primaryLevel = 1.0;
    QPointer<QVariantAnimation> m_switch;
};

// Paleta de 96 colores (32 con la ventana reducida): clic izquierdo = ranura activa, derecho =
// inactiva.
class PaletteWidget : public QWidget {
    Q_OBJECT
public:
    explicit PaletteWidget(QWidget* parent = nullptr);
    void setColors(const QList<QColor>& colors);
    void setVisibleCount(int count);
    int visibleCount() const { return m_visible; }
    void setAddMode(bool on);
    bool addMode() const { return m_addMode; }
    QSize sizeHint() const override;
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override;

signals:
    void colorClicked(int index, const QColor& c, Qt::MouseButton button);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    bool event(QEvent* e) override;
    void timerEvent(QTimerEvent* e) override;

private:
    int columns() const;
    int cellSize() const;
    QRect cellRect(int index) const;
    int indexAt(const QPoint& p) const;

    QList<QColor> m_colors;
    int m_visible = 32;
    bool m_addMode = false;
    bool m_blinkOn = false;
    int m_blinkTimer = 0;
};

void paintCheckerboard(class QPainter& p, const QRectF& r, int cell = 4);
