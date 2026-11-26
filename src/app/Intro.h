#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QTimer>
#include <QWidget>

#include <functional>
#include <memory>

struct IntroFrames;

// La intro de Kaint (video/intro.webm) encima de la ventana: la primera vez que se abre o, si se
// pide en Configuración, siempre. Al terminar se desvanece desde su último fotograma. Con la opción
// de siempre, mantener pulsada Intro (o Espacio) llena una línea abajo y, al llegar al otro extremo, la salta
// (al soltarla, la línea vuelve rápido). Necesita Qt Multimedia; sin él no se muestra.
class Intro : public QWidget {
public:
    // Si toca mostrarla, la pone encima de 'window' y llama a 'done' al irse; si no, llama a 'done'.
    // 'customVideo': con 'always', un vídeo propio (si existe); si no, el de Kaint.
    static void start(QWidget* window, bool always, const QString& customVideo, std::function<void()> done);
    ~Intro() override;

protected:
    void paintEvent(QPaintEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    bool eventFilter(QObject* watched, QEvent* e) override;

private:
    Intro(QWidget* window, bool skippable, const QString& video, std::function<void()> done);
    void finish();
    void step();

    QImage m_frame;
    // Los fotogramas, convertidos y escalados en otro hilo (Intro.cpp).
    std::shared_ptr<IntroFrames> m_frames;
    bool m_skippable = false;
    bool m_holding = false;
    bool m_finishing = false;
    double m_progress = 0;
    QTimer m_ticker;
    QElapsedTimer m_clock;
    std::function<void()> m_done;
    QObject* m_player = nullptr;
};
