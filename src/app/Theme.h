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
// "Seguir al sistema", "Claro", "Oscuro" y "Azul" imitan Paint.NET en Windows 10 (el primero
// elige claro u oscuro según el escritorio). "KDE" usa el estilo y los colores reales de Plasma.
// "Personalizado" parte de claro u oscuro y deja cambiar colores, fondo del lienzo y diseño.
// "Windows 98" es el clásico gris con relieve (basado en 98.css); también es una plantilla del
// personalizado. "Windows 98 ALT" es el clásico de Windows 2000 y XP (Paint.NET en Windows XP con
// el estilo clásico), "Windows XP" el de Luna (basado en XP.css) y "Windows 7" el de Aero (7.css).
// Todos menos KDE ignoran la configuración del escritorio y dibujan con Fusion y una hoja de
// estilo. Eso se decide antes de crear la aplicación, así que pasar al tema KDE o salir de él
// reinicia Kaint.
namespace Theme {

enum class Scheme { System = 0, Light = 1, Dark = 2, Kde = 3, Custom = 4, Windows98 = 5, Blue = 6, Windows7 = 7, WindowsXP = 8,
                    Windows98Alt = 9, Windows7Dark = 10 };
constexpr int kLastScheme = 10;
enum class ImageMode { Tile = 0, Center = 1, Stretch = 2, Fit = 3, Fill = 4 };
enum class ButtonStyle { Flat = 0, Framed = 1, Rounded = 2 };
enum class Density { Compact = 0, Normal = 1, Comfortable = 2 };
// Aspecto de la interfaz: el de Kaint o una recreación de Paint.NET 5, más compacta (menús y
// botones más juntos, ventanas sin borde ni esquinas redondeadas...). Solo cambia lo visual; no
// se aplica con el tema KDE.
enum class Look { Kaint = 0, PaintNet = 1 };

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
    // El cristal de las ventanas con el estilo de Windows 7 (su «Color de la ventana»); los demás
    // temas no lo usan.
    QColor glass;

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
    // Con el aspecto de Paint.NET (lo pone apply(), no se guarda en el tema).
    bool paintNet = false;
    // Tema Azul de Paint.NET: barras, reglas y menús en degradado azulado (no se guarda).
    bool blue = false;
    // Estilo de Windows 7 (7.css): botones, menús y barras con los degradados y el cristal de Aero.
    // Lo llevan sus temas y, si se elige, el personalizado (que lo guarda con sus colores).
    bool win7 = false;
    // Estilo de Windows XP (XP.css): botones, campos, menús y ventanas de Luna. Como win7.
    bool xp = false;
    // Con el color de selección propio de los temas Claro y Oscuro (no se guarda): la pestaña de
    // la imagen y los botones de las ventanas también lo llevan.
    bool ownSelection = false;

    bool operator==(const Spec&) const = default;
};

// Colores editables del tema, con su clave en JSON y su nombre (contexto "Theme").
struct ColorRole {
    const char* key;
    QColor Spec::*color;
    const char* label;
};
extern const std::array<ColorRole, 20> kColorRoles;

// Temas de Paint.NET, Windows 98 y el personalizado con sus huecos rellenos.
Spec preset(bool dark);
Spec windows98();
// Windows 7, con los colores y degradados de 7.css, y su versión oscura (el cristal ahumado).
Spec windows7();
Spec windows7Dark();
// Windows XP (Luna), con los colores y degradados de XP.css.
Spec windowsXP();
// Windows 98 ALT: el clásico de Windows 2000 y XP (gris #d4d0c8, cabeceras de azul oscuro a celeste).
Spec windows98Alt();
// El tema Azul de Paint.NET 5: el claro con las barras en degradado azulado.
Spec blue();
// El tema del que salen los colores sin cambiar del personalizado: el claro o el oscuro de
// Paint.NET o, con su estilo, Windows 7 (claro u oscuro) o Windows XP.
Spec customBase(const Spec& custom);
Spec resolved(const Spec& custom);
// 'embedImage' guarda también la imagen de fondo (para exportar el tema y compartirlo).
QJsonObject toJson(const Spec& spec, bool embedImage = false);
Spec fromJson(const QJsonObject& json);
// Copia una imagen (o un GIF animado, o un vídeo) de fondo a la carpeta de Kaint (así no se pierde
// si se mueve el original).
QString storeBackground(const QString& file);
// Lo mismo con los datos del fondo (al importar un tema); 'name', el nombre que tenía.
QString storeBackgroundData(const QByteArray& data, const QString& name);
// Exportar e importar el tema personalizado, también entre Kaint y Kaint.HTML: con fondo propio, un
// .zip con theme.json y el archivo del fondo (background.png, .gif, .mp4...); sin él, el .json
// solo. Importar acepta los dos (y los .json de antes, con la imagen dentro).
bool themeHasBackground(const Spec& spec);
bool exportTheme(const Spec& spec, const QString& path, QString* error);
bool importTheme(const QString& path, Spec* spec, QString* error);
// Si el fondo es animado: un GIF, WebP o PNG animado, o un vídeo (MP4, WebM...).
bool isAnimatedBackground(const QString& file);
// El filtro del diálogo para elegir el fondo (imágenes, animaciones y vídeos).
QString backgroundFileFilter();
Spec loadCustom();
void saveCustom(const Spec& custom);

QPalette palette(const Spec& spec);
QString styleSheet(const Spec& spec);

// Arranque: prepare() antes de crear QApplication, initStyle() después y antes de las ventanas.
void captureSystemDefaults();
void prepare(Scheme scheme);
void initStyle(Scheme scheme);

void apply(Scheme scheme);
// Aspecto de la interfaz (se ve al volver a aplicar el tema).
void setLook(Look look);
Look look();
// Con el aspecto de Paint.NET activo (nunca con el tema KDE).
bool paintNetLook();
// Subrayar siempre las letras de acceso de los menús y botones. Si no (lo de fábrica, como en
// Windows), solo se subrayan mientras se pulsa Alt o se usan los menús con el teclado.
void setUnderlineShortcuts(bool always);
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

// Color de la barra de título cuando no es la del tema (el propio que se elige en Configuración
// o, en Windows, el de énfasis si se muestra en las barras de título), o no válido. Los temas
// Windows 98, 98 ALT y XP no llevan ninguno.
QColor titleBarColor();
// Windows 7: «Habilitar transparencia», como en Windows 7: el cristal de los marcos de las
// ventanas deja ver lo de detrás (difuminado, donde el sistema lo permite). Solo con sus temas.
// Devuelve si cambió (entonces hay que volver a aplicar el tema).
bool setAeroTransparency(bool on);
bool aeroTransparency();
// Color de selección propio de los temas Claro y Oscuro (y Seguir al sistema, según el modo):
// el fondo de lo elegido en las ventanas (herramientas, capas, historial...), en la barra
// superior y en las listas; el borde y lo de bajo el puntero salen de él. No válido: el azul de
// siempre. Devuelve si cambió (entonces hay que volver a aplicar el tema).
bool setSelectionColors(const QColor& light, const QColor& dark);
// Los de fábrica (para el botón de color en Configuración).
QColor defaultSelectionColor(bool dark);
// El color propio elegido (no válido: ninguno).
QColor customTitleBarColor();
// El de la barra de título inactiva (al pasar a otro programa) cuando titleBarColor() es válido:
// como en Windows, sin el color de énfasis.
QColor inactiveTitleBarColor();
// Color propio de la barra superior y de las cabeceras de las ventanas, como el de énfasis de
// Windows (no válido: el del tema; en Windows, solo con la barra de título de Kaint). Se ve al
// volver a aplicar el tema.
void setCustomTitleBarColor(const QColor& color);
// Fuente de la interfaz elegida en Configuración (familia vacía o tamaño 0: los del sistema o del
// tema). Se ve al volver a aplicar el tema.
void setUiFont(const QString& family, int pointSize);
QString uiFontFamily();
int uiFontSize();
// 'spec' con las cabeceras de las ventanas flotantes del color 'titleBar' (si es válido).
Spec withTitleBarColor(Spec spec, const QColor& titleBar);
// Barra de título nativa a juego con el tema: en Windows, modo oscuro y el color 'band' (no
// válido: el del sistema).
void styleTitleBar(QWidget* window, const QColor& band);
// Windows, como Paint.NET: la barra de título de 'window' se sigue viendo activa (con el color de
// énfasis, no gris) mientras la ventana activa sea otra de Kaint. Para nativeEvent(): verdadero si
// se ocupó del mensaje (entonces hay que llamar después a syncTitleBarActivation, ya cambiada la
// activación); en las demás plataformas no hace nada.
bool keepTitleBarActive(QWidget* window, const QByteArray& type, void* message, qintptr* result);
// La pinta inactiva si la ventana activa ya no es de Kaint (también al pasar a otro programa).
void syncTitleBarActivation(QWidget* window);

// El cristal de Aero (Windows 7): el color de las ventanas del tema activo (Spec::glass), con o sin
// «Habilitar transparencia» y con el foco o sin él. Con el color de siempre, los de 7.css.
QColor glassColor(bool translucent, bool focused);

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
    // El fondo animado del lienzo (GIF o vídeo) tiene un fotograma nuevo: se vuelve a dibujar.
    void backgroundFrame();

public slots:
    // Vuelve a leer si el sistema usa modo oscuro.
    void systemSettingChanged();
};
Notifier* notifier();

} // namespace Theme
