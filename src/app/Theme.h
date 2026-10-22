#pragma once

#include <QColor>
#include <QJsonObject>
#include <QObject>
#include <QPalette>
#include <QString>

#include <array>

class QPainter;

// Temas de la interfaz.
//
// "Seguir al sistema", "Claro" y "Oscuro" imitan Paint.NET en Windows 10 (el primero elige
// claro u oscuro según el escritorio). "KDE" usa el estilo y los colores reales de Plasma.
// "Personalizado" parte de claro u oscuro y deja cambiar colores, fondo del lienzo y diseño.
// Todos menos KDE ignoran la configuración del escritorio y dibujan con Fusion y una hoja de
// estilo. Eso se decide antes de crear la aplicación: si Kaint arrancó con otro tema, el de KDE
// se arma leyendo la configuración de Plasma (estilo, colores, acento e iconos).
namespace Theme {

enum class Scheme { System = 0, Light = 1, Dark = 2, Kde = 3, Custom = 4 };
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
    QColor panelTitleText;
    QColor panelClose;
    QColor selection;
    QColor selectionBorder;
    QColor hover;
    QColor workspace;

    QString workspaceImage;
    ImageMode imageMode = ImageMode::Tile;
    int imageOpacity = 100;

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
extern const std::array<ColorRole, 18> kColorRoles;

// Temas de Paint.NET y el personalizado con sus huecos rellenos.
Spec preset(bool dark);
Spec resolved(const Spec& custom);
QJsonObject toJson(const Spec& spec);
Spec fromJson(const QJsonObject& json);
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
Scheme current();
// Tema efectivo; con KDE se deduce de la paleta del sistema.
Spec active();
bool isDark();
bool systemPrefersDark();

// Fondo del área de trabajo (alrededor de la imagen): color e imagen del tema. 'color' no válido
// usa el del tema.
QColor workspaceColor();
void paintWorkspace(QPainter& p, const QRect& area, const QRect& clip, const QColor& color = QColor());

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
