#pragma once

#include "core/BlendMode.h"

#include <QColor>
#include <QColorSpace>
#include <QImage>
#include <QString>

#include <memory>

namespace Motion {
struct Clip;
}

// Formato interno de todas las capas: RGBA de 8 bits con alfa directo (sin premultiplicar),
// como ColorBgra de Paint.NET.
constexpr QImage::Format kLayerFormat = QImage::Format_ARGB32;
constexpr QImage::Format kPremulFormat = QImage::Format_ARGB32_Premultiplied;

// "Transparente" de Paint.NET (ColorBgra.Transparent): blanco con alfa 0. Es el contenido de
// las capas nuevas y de lo que se borra.
constexpr QRgb kTransparent = 0x00ffffffu;

struct LayerProps {
    QString name;
    bool visible = true;
    int opacity = 255;
    BlendMode blend = BlendMode::Normal;
    // Editor de GIF: la animación por puntos clave de este fotograma (nula: ninguna). Inmutable:
    // para cambiarla se pone otra (así entra en el historial como el resto).
    std::shared_ptr<const Motion::Clip> motion;
    // Bloquear la transparencia (Alpha-Lock): lo que se pinta solo cambia el color, no el alfa.
    bool alphaLock = false;
    // Editor de GIF: la pista (capa de la línea de tiempo, como en Filmora) de este fotograma. Cada
    // pista es una sucesión de fotogramas; las de número más alto se ven encima de las otras.
    int track = 0;
    // Editor de GIF: el espacio vacío (ms) en su pista antes de este fotograma (0: pegado al anterior).
    int gap = 0;

    bool operator==(const LayerProps&) const = default;
};

struct Layer {
    LayerProps props;
    QImage image;

    // Sin color de relleno (o con uno totalmente transparente): kTransparent.
    Layer(const QString& name, QSize size, QColor fill = QColor())
    {
        props.name = name;
        image = QImage(size, kLayerFormat);
        if (fill.isValid() && fill.alpha() > 0)
            image.fill(fill);
        else
            image.fill(kTransparent);
    }

    Layer(const QString& name, const QImage& source)
    {
        props.name = name;
        image = source.convertToFormat(kLayerFormat);
        image.setColorSpace(QColorSpace());
        // Qt lee «nombre@2x.png» como imagen de doble densidad (devicePixelRatio 2) y se dibujaba a
        // la mitad: en una capa, cada píxel es un píxel.
        image.setDevicePixelRatio(1.0);
    }
};

using LayerPtr = std::shared_ptr<Layer>;
