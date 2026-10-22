#pragma once

#include "core/BlendMode.h"

#include <QColor>
#include <QColorSpace>
#include <QImage>
#include <QString>

#include <memory>

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
    }
};

using LayerPtr = std::shared_ptr<Layer>;
