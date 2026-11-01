#pragma once

#include <QColor>
#include <QWidget>

class QPainter;

// Linux, con el estilo de Paint.NET: las ventanas sin la barra de título del escritorio, con una
// propia como la de Windows 10 (del color de la franja de los menús: negra con los temas oscuros,
// o el color propio de la barra superior), un borde fino y sombra alrededor. Se mueven y cambian de
// tamaño con el gestor de ventanas (startSystemMove y startSystemResize). Los diálogos la llevan
// dibujada encima de su contenido; la ventana principal, en su fila de arriba (CaptionTitle y
// CaptionButtons).
namespace WindowFrame {

// Se decide al arrancar (Linux, estilo Paint.NET y la opción de Configuración): los diálogos que se
// creen desde entonces llevan el marco mientras siga el estilo de Paint.NET.
void setEnabled(bool on);
bool enabled();

// Prepara 'window' antes de mostrarla por primera vez: sin la decoración del sistema, translúcida
// para la sombra y con sus márgenes. 'titleBar': si el marco dibuja también la barra de título (la
// ventana principal la pone en su propia fila).
void decorate(QWidget* window, bool titleBar = true);
bool isDecorated(const QWidget* window);

// Colores de la barra de título (activa o no) y del borde.
QColor titleColor();
QColor titleTextColor(bool active);
// Con el tema Windows 98, la barra de título es la de Windows 98: degradado azul, botones grises en
// relieve y el título en negrita.
bool classicTitle();
void paintClassicTitle(QPainter& p, const QRect& r, bool active);

} // namespace WindowFrame

// Icono y título de la ventana principal en la barra de título simulada (con «…» si no cabe). Un
// arrastre la mueve y un doble clic la maximiza o la restaura.
class CaptionTitle : public QWidget {
public:
    explicit CaptionTitle(QWidget* parent = nullptr);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    bool event(QEvent* e) override;
    bool eventFilter(QObject* watched, QEvent* e) override;

private:
    bool m_watching = false;
};

// Minimizar, maximizar (o restaurar) y cerrar, como los de Windows 10: 46 px de ancho, el fondo se
// aclara bajo el puntero y el de cerrar se vuelve rojo (con el tema Windows 98, los suyos).
class CaptionButtons : public QWidget {
public:
    explicit CaptionButtons(QWidget* parent = nullptr);
    QSize sizeHint() const override;
    // Alto de la fila de la barra de título.
    static int rowHeight();

    static constexpr int kWidth = 46;

protected:
    void paintEvent(QPaintEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent* e) override;
    bool event(QEvent* e) override;
    bool eventFilter(QObject* watched, QEvent* e) override;

private:
    QRect buttonRect(int i) const;
    int buttonAt(const QPoint& pos) const;
    bool m_watching = false;
    int m_hover = -1;
    int m_pressed = -1;
};
