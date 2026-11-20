#pragma once

#include <QImage>
#include <QJsonObject>
#include <QList>
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
};

enum Prop { PosX = 0, PosY, ScaleX, ScaleY, Rotation, Opacity, PropCount };

struct Key {
    int t = 0;  // ms desde el principio del fotograma
    double value = 0;
    Interp interp = Interp::Linear;
    bool easeIn = false;   // llega al punto despacio
    bool easeOut = false;  // sale del punto despacio

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
    bool isIdentity() const;
};

struct Clip {
    std::array<Track, PropCount> tracks;
    int stepMs = 50;  // cada cuánto se calcula un fotograma al exportar (20 por segundo)
    bool lockScale = true;

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
