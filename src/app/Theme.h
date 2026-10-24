#pragma once

#include <QColor>
#include <QJsonObject>
#include <QObject>
#include <QPalette>
#include <QString>

#include <array>

class QPainter;
class QWidget;

// Temas de la interfaz.
//
// "Seguir al sistema", "Claro" y "Oscuro" imitan Paint.NET en Windows 10 (el primero elige
// claro u oscuro según el escritorio). "KDE" usa el estilo y los colores reales de Plasma.
// "Personalizado" parte de claro u oscuro y deja cambiar colores, fondo del lienzo y diseño.
// "Windows 98" es el clásico gris con relieve (basado en 98.css); también es una plantilla del
// personalizado. Todos menos KDE ignoran la configuración del escritorio y dibujan con Fusion y
// una hoja de estilo. Eso se decide antes de crear la aplicación, así que pasar al tema KDE o
// salir de él reinicia Kaint.
namespace Theme {

enum class Scheme { System = 0, Light = 1, Dark = 2, Kde = 3, Custom = 4, Windows98 = 5 };
enum class ImageMode { Tile = 0, Center = 1, Stretch = 2, Fit = 3, Fill = 4 };
enum class ButtonStyle { Flat = 0, Framed = 1, Rounded = 2 };
enum class Density { Compact = 0, Normal = 1, Comfortable = 2 };

// Colores y diseño de un tema. En el personalizado, un color no válido toma el del tema base
// (claro u oscuro).
struct Spec {
    bool dark = false;

    QColor accent;
    QColor window;
    QColor text;
    QColor base;
    QColor button;
    QColor border;
    QColor menuBar;
    QColor menuBarText;
    QColor toolBar;
    QColor statusBar;
    QColor panel;
    QColor panelTitle;
    QColor panelTitleEnd;
    QColor panelTitleText;
    QColor panelClose;
    QColor selection;
    QColor selectionBorder;
    QColor hover;
    QColor workspace;

    QString workspaceImage;
    ImageMode imageMode = ImageMode::Tile;
    int imageOpacity = 100;
    int imageScale = 100;
    int imageAnchor = 4;
    int imageBlur = 0;
    bool imageGrayscale = false;
    QColor imageTint;
    int imageTintStrength = 0;

    // Bordes clásicos en relieve (Windows 98) en botones, campos, menús y ventanas.
    bool classic = false;
    // Texto sin suavizado, pixelado como en Windows 98.
    bool pixelFont = false;

    int iconSize = 16;
    int toolIconSize = 22;
    ButtonStyle buttonStyle = ButtonStyle::Flat;
    Density density = Density::Normal;
    int radius = 0;
    int panelOpacity = 75;
    QString fontFamily;
    int fontSize = 0;
    QString iconTheme;

    bool operator==(const Spec&) const = default;
};

// Colores editables del tema, con su clave en JSON y su nombre (contexto "Theme").
struct ColorRole {
    const char* key;
    QColor Spec::*color;
    const char* label;
};
extern const std::array<ColorRole, 19> kColorRoles;

// Temas de Paint.NET, Windows 98 y el personalizado con sus huecos rellenos.
Spec preset(bool dark);
Spec windows98();
Spec resolved(const Spec& custom);
// 'embedImage' guarda también la imagen de fondo (para exportar el tema y compartirlo).
QJsonObject toJson(const Spec& spec, bool embedImage = false);
Spec fromJson(const QJsonObject& json);
// Copia una imagen de fondo a la carpeta de Kaint (así no se pierde si se mueve el original).
QString storeBackground(const QString& file);
Spec loadCustom();
void saveCustom(const Spec& custom);

QPalette palette(const Spec& spec);
QString styleSheet(const Spec& spec);

// Arranque: prepare() antes de crear QApplication, initStyle() después y antes de las ventanas.
void captureSystemDefaults();
void prepare(Scheme scheme);
void initStyle(Scheme scheme);

void apply(Scheme scheme);
// Cambia el tema personalizado (se ve al momento si es el activo).
void setCustom(const Spec& custom);
Spec custom();
// Pasar a este tema necesita reiniciar Kaint (entrar o salir del tema KDE).
bool needsRestart(Scheme scheme);
Scheme current();
// Tema efectivo; con KDE se deduce de la paleta del sistema.
Spec active();
bool isDark();
bool systemPrefersDark();

// Color de la barra de título del sistema cuando no es la del tema (Windows con el color de
// énfasis en las barras de título), o no válido.
QColor titleBarColor();
// 'spec' con las cabeceras de las ventanas flotantes del color 'titleBar' (si es válido).
Spec withTitleBarColor(Spec spec, const QColor& titleBar);
// Barra de título nativa a juego con el tema: en Windows, modo oscuro y el color 'band' (no
// válido: el del sistema).
void styleTitleBar(QWidget* window, const QColor& band);

// Fondo del área de trabajo (alrededor de la imagen): color e imagen del tema. 'color' no válido
// usa el del tema.
QColor workspaceColor();
void paintWorkspace(QPainter& p, const QRect& area, const QRect& clip, const QColor& color = QColor());
// La imagen de fondo de un tema (con su colocación, escala, desenfoque y tinte) sobre 'area'.
void paintBackgroundImage(QPainter& p, const QRect& area, const QRect& clip, const Spec& spec);

// Avisa de los cambios de tema, también cuando el sistema pasa de claro a oscuro.
class Notifier : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

signals:
    void changed();

public slots:
    // Vuelve a leer si el sistema usa modo oscuro.
    void systemSettingChanged();
};
Notifier* notifier();

} // namespace Theme
