#pragma once

#include <QColor>
#include <QList>
#include <QRect>
#include <QWidget>

class QPainter;

// La barra de título y el marco de Kaint (Configuración > Barra de título; por defecto en Linux sí y
// en Windows no): las ventanas sin la barra de título del sistema, con una propia como la de
// Windows 10 (del color de la franja de los menús: negra con los temas oscuros, o el color propio
// de la barra superior o el de énfasis de Windows), un borde fino y sombra alrededor (la de KWin o
// la de Windows). Se mueven y cambian de tamaño con el gestor de ventanas (startSystemMove y
// startSystemResize). Los diálogos la llevan dibujada encima de su contenido; la ventana principal,
// en su fila de arriba (CaptionTitle y CaptionButtons).
namespace WindowFrame {

// Se decide al arrancar (la opción de Configuración, salvo con el tema KDE): los diálogos que se
// creen desde entonces llevan el marco.
void setEnabled(bool on);
bool enabled();

// Prepara 'window' antes de mostrarla por primera vez: sin la decoración del sistema, translúcida
// para la sombra y con sus márgenes. 'titleBar': si el marco dibuja también la barra de título (la
// ventana principal la pone en su propia fila).
void decorate(QWidget* window, bool titleBar = true);
bool isDecorated(const QWidget* window);

// Estilo de los botones de la barra de título (minimizar, maximizar o restaurar y cerrar).
enum class Buttons { Windows10, Windows11, MacOS, Windows7, WindowsXP, Windows98 };
void setButtons(Buttons style);
Buttons buttons();
// El que se dibuja: con el tema Windows 98, «Windows 10» (el predeterminado) son los de Windows 98.
Buttons effectiveButtons();
// Los botones a la izquierda de la barra (en el orden cerrar, minimizar, maximizar) o a la derecha.
// Forma de los botones de Windows 7: colgando del borde de arriba (como siempre), unidos al marco
// como el de cerrar de los diálogos (con su contorno oscuro encima y algo separados del borde) o
// sueltos y cuadrados, con las cuatro esquinas redondeadas (como las X de Paint.NET).
enum class Win7Shape { Hanging = 0, Attached = 1, Square = 2 };
void setWin7Shape(Win7Shape shape);
Win7Shape win7Shape();
void setButtonsOnLeft(bool on);
bool buttonsOnLeft();
// Recreación de Windows 11 (estilo Paint.NET): esquinas redondeadas en modo ventana. En Linux se
// decide al arrancar, antes de crear las ventanas (necesitan fondo translúcido); en Windows las
// redondea el propio Windows 11 y cambia al momento (roundedCorners() es entonces false).
void setRoundedCorners(bool on);
bool roundedCorners();
// Las ventanas con el marco de Kaint se vuelven a dibujar (cambió el estilo o el lado de los botones).
void refreshAll();

// Rectángulos de minimizar (0), maximizar (1) y cerrar (2) dentro de la fila 'area' de la barra de
// título; con 'closeOnly' (los diálogos), solo el de cerrar.
QList<QRect> buttonRects(const QRect& area, bool closeOnly);
// Ancho que ocupan los botones (con sus márgenes) en una fila de ese alto.
int buttonsWidth(int rowHeight, bool closeOnly);
// Un botón: 'which' 0 minimizar, 1 maximizar, 2 cerrar; 'groupHot', el puntero sobre alguno (los de
// macOS muestran entonces sus dibujos); 'alone', el único botón (el de cerrar de los diálogos).
void paintButton(QPainter& p, const QRect& box, int which, bool maximized, bool hot, bool down, bool active, bool groupHot,
                 bool alone = false);

// Colores de la barra de título (activa o no) y del borde.
QColor titleColor();
QColor titleTextColor(bool active);
// Con el tema Windows 98, la barra de título es la de Windows 98: degradado azul, botones grises en
// relieve y el título en negrita; con el tema Windows XP, la de Luna.
bool classicTitle();
bool lunaTitle();
void paintClassicTitle(QPainter& p, const QRect& r, bool active);
// El botón de cerrar de las ventanas flotantes con el estilo de 'style' (Windows 98, XP o 7, suelto).
void paintPanelClose(QPainter& p, const QRect& box, Buttons style, bool hot, bool down);

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

// Minimizar, maximizar (o restaurar) y cerrar con el estilo elegido (WindowFrame::Buttons): los de
// Windows 10 u 11 (46 px de ancho, el fondo se aclara bajo el puntero y el de cerrar se vuelve
// rojo), los semáforos de macOS, los de cristal de Windows 7, los de Windows XP o los de Windows 98.
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
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent* e) override;
    bool event(QEvent* e) override;
    bool eventFilter(QObject* watched, QEvent* e) override;

private:
    void resetState();
    QRect buttonRect(int i) const;
    int buttonAt(const QPoint& pos) const;
    bool groupHot() const;
    bool m_watching = false;
    int m_hover = -1;
    int m_pressed = -1;
};
