#pragma once

#include <QElapsedTimer>
#include <QTimer>
#include <QUrl>
#include <QWidget>

// Texto con una ola suave en bucle: cada letra sube y baja un poco, desfasada de la anterior.
// La segunda parte es un enlace. Sin animaciones (Configuración), el texto queda quieto.
class WaveLabel : public QWidget {
    Q_OBJECT
public:
    WaveLabel(const QString& text, const QString& link, const QUrl& url, QWidget* parent = nullptr);

    QUrl url() const { return m_url; }
    qreal phase() const { return m_phase; }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent* e) override;
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;

private:
    qreal amplitude() const;
    QRectF linkRect() const;
    void setHover(bool hover);

    QString m_text;
    QString m_link;
    QUrl m_url;
    QTimer m_timer;
    QElapsedTimer m_clock;
    qreal m_phase = 0;
    bool m_hover = false;
};
