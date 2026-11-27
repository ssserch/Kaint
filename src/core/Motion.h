#pragma once

#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QTransform>
#include <QSize>
#include <QString>

#include <array>
#include <memory>

// Animación por puntos clave de un fotograma del editor de GIF (como los de Filmora): la imagen de
// la capa se mueve, se escala, gira y cambia de opacidad a lo largo de la duración del fotograma.
// Al exportar (ImageIO::animationFrames) el fotograma se convierte en varios, uno cada stepMs.
namespace Motion {

// Cómo se pasa de un punto clave al siguiente (lo dice el primero de los dos).
enum class Interp {
    Linear = 0,      // en línea recta
    Curve = 1,       // curva suave en cada tramo (se detiene un instante en cada punto)
    Continuous = 2,  // curva continua que pasa por todos los puntos sin detenerse
    Freeze = 3,      // se queda en el valor hasta el siguiente punto
    Bezier = 4,      // con su propia curva: las dos asas del tramo (Key::bezier), como cubic-bezier()
};

// La curva de un tramo Bézier por defecto (entrada y salida suaves, como «ease-in-out»).
constexpr std::array<double, 4> kDefaultBezier{0.42, 0.0, 0.58, 1.0};

// Lo que avanza (0 al principio del tramo, 1 al final; puede pasarse) a la fracción 'u' del tiempo
// del tramo, con la curva cúbica de asas (x1, y1) y (x2, y2), como cubic-bezier() de CSS.
double bezierProgress(double u, const std::array<double, 4>& b);

// RotX y RotY: el giro en 3D (la imagen como una lámina fina, con perspectiva), arriba y abajo y a los
// lados. (Al final: así no cambian los números de los de antes.)
enum Prop { PosX = 0, PosY, ScaleX, ScaleY, Rotation, Opacity, RotX, RotY, PropCount };

struct Key {
    int t = 0;  // ms desde el principio del fotograma
    double value = 0;
    Interp interp = Interp::Linear;
    bool easeIn = false;   // llega al punto despacio
    bool easeOut = false;  // sale del punto despacio
    // Con Interp::Bezier, las dos asas del tramo que empieza aquí: x (fracción del tiempo del tramo,
    // de 0 a 1) e y (lo que avanza: 0 el valor de este punto, 1 el del siguiente).
    std::array<double, 4> bezier = kDefaultBezier;

    bool operator==(const Key&) const = default;
};

struct Track {
    double base = 0;   // el valor si no hay puntos clave
    QList<Key> keys;   // ordenados por tiempo

    bool animated() const { return !keys.isEmpty(); }
    double value(int t) const;
    // El punto en 't' (-1 si no hay).
    int keyAt(int t) const;
    // Pone (o cambia) el punto en 't'.
    void setKey(int t, double value);
    bool operator==(const Track&) const = default;
};

struct Transform {
    double x = 0, y = 0;            // desplazamiento en píxeles
    double scaleX = 100, scaleY = 100;  // %
    double rotation = 0;            // grados, alrededor del centro de la imagen
    double opacity = 100;           // %
    double rotX = 0, rotY = 0;      // giro en 3D, grados
    // Deformar: lo que se mueve cada esquina (arriba izquierda, arriba derecha, abajo derecha,
    // abajo izquierda), en píxeles de la imagen; la imagen queda en perspectiva.
    std::array<QPointF, 4> warp{};
    // Al girar o escalar, suavizada; si no, con sus píxeles nítidos (para el pixel art).
    bool smooth = false;
    bool isIdentity() const;
};

// La inclinación en 3D de 'ax' grados (arriba y abajo) y 'ay' (a los lados) de 'rect', alrededor de
// su centro y con la cámara a 1,5 veces su tamaño (como QTransform::rotate con Qt::XAxis y
// Qt::YAxis).
QTransform tiltTransform(const QRectF& rect, double ax, double ay);
// Todo lo que se le hace a una imagen de tamaño 'size': deformar, el giro en 3D, escalar, girar y
// mover (de sus coordenadas a las del lienzo).
QTransform matrix(QSize size, const Transform& transform);

struct Clip {
    std::array<Track, PropCount> tracks;
    int stepMs = 50;  // cada cuánto se calcula un fotograma al exportar (20 por segundo)
    bool lockScale = true;
    // Deformar (lo que se mueve cada esquina; no cambia a lo largo del fotograma) y suavizar.
    std::array<QPointF, 4> warp{};
    bool smooth = false;

    Clip();
    // Sin puntos clave y con los valores de siempre: no hace nada.
    bool isEmpty() const;
    bool animated() const;
    Transform sample(int t) const;
    // Los tiempos con algún punto clave (sin repetir, ordenados).
    QList<int> keyTimes() const;
    // Mueve todos los puntos de 'from' a 'to' (si en 'to' ya había uno, lo sustituye).
    void moveKeys(int from, int to);
    void removeKeys(int t);
    // La forma de pasar al siguiente punto, para todos los puntos de 't'.
    void setInterp(int t, Interp interp);
    void setEase(int t, bool in, bool on);
    // El último tiempo con un punto (0 si no hay).
    int lastKeyTime() const;

    QJsonObject toJson() const;
    static Clip fromJson(const QJsonObject& o);
    bool operator==(const Clip&) const = default;
};

using ClipPtr = std::shared_ptr<const Clip>;

double defaultValue(Prop p);
QString propName(Prop p);

// La imagen de la capa con la transformación, en un lienzo transparente del mismo tamaño.
QImage render(const QImage& source, const Transform& transform);

// Los fotogramas de un fotograma animado: uno cada stepMs hasta 'durationMs' (el último, lo que
// quede). Cada par: la imagen y su duración.
QList<std::pair<QImage, int>> expand(const QImage& source, const Clip& clip, int durationMs);

}  // namespace Motion
