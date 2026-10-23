#pragma once

#include "core/Layer.h"
#include "core/Resample.h"

#include <QColorSpace>
#include <QDialog>
#include <QHash>
#include <QTimer>

#include <functional>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QSlider;
class QSpinBox;

// Tamaño como en los diálogos de Paint.NET: por porcentaje o absoluto, "Mantener la relación
// de aspecto", tamaño en píxeles y tamaño de impresión (resolución en píxeles/pulgada o
// píxeles/cm y ancho/alto en pulgadas o centímetros).
class SizeEditor : public QWidget {
    Q_OBJECT
public:
    SizeEditor(QSize original, double dpi, bool allowPercent, QWidget* parent = nullptr);
    QSize value() const;
    void setValue(QSize size);
    double dpi() const;
    void setKeepAspect(bool on);

signals:
    void changed();

private:
    void onPixels(bool widthChanged);
    void onPrint(bool widthChanged);
    void onPercent();
    void onResolution();
    void updateMode();
    void syncPrint();
    double unitPixels() const;

    QSize m_original;
    double m_aspect;
    double m_dpi;
    QRadioButton* m_byPercent = nullptr;
    QRadioButton* m_byAbsolute = nullptr;
    QSpinBox* m_percent = nullptr;
    QCheckBox* m_keepAspect;
    QSpinBox* m_width;
    QSpinBox* m_height;
    QDoubleSpinBox* m_resolution;
    QComboBox* m_resolutionUnit;
    QDoubleSpinBox* m_printWidth;
    QDoubleSpinBox* m_printHeight;
    QComboBox* m_printUnit;
    QLabel* m_printHeightUnit;
    bool m_updating = false;
};

class NewImageDialog : public QDialog {
    Q_OBJECT
public:
    NewImageDialog(QSize initial, double dpi, QWidget* parent = nullptr);
    QSize imageSize() const;
    double dpi() const;
    QColor background(const QColor& secondary) const;

private:
    void updateMemory();
    SizeEditor* m_size;
    QComboBox* m_background;
    QLabel* m_memory;
};

class ResizeImageDialog : public QDialog {
    Q_OBJECT
public:
    ResizeImageDialog(QSize current, double dpi, QWidget* parent = nullptr);
    QSize imageSize() const;
    double dpi() const;
    Resample::Method method() const;
    bool gammaCorrect() const;

private:
    void updateMemory();
    SizeEditor* m_size;
    QComboBox* m_resampling;
    QCheckBox* m_gamma;
    QLabel* m_memory;
};

class CanvasSizeDialog : public QDialog {
    Q_OBJECT
public:
    CanvasSizeDialog(QSize current, double dpi, QWidget* parent = nullptr);
    QSize canvasSize() const;
    double dpi() const;
    Qt::Alignment anchor() const;

private:
    void updateArrows();
    SizeEditor* m_size;
    QButtonGroup* m_anchor;
    QLabel* m_memory;
};

class LayerPropertiesDialog : public QDialog {
    Q_OBJECT
public:
    LayerPropertiesDialog(const LayerProps& props, std::function<void(const LayerProps&)> preview,
                          QWidget* parent = nullptr);
    LayerProps props() const;

private:
    void emitPreview();
    std::function<void(const LayerProps&)> m_preview;
    LayerProps m_base;
    QLineEdit* m_name;
    QCheckBox* m_visible;
    QComboBox* m_blend;
    QSlider* m_opacitySlider;
    QSpinBox* m_opacity;
};

// Imagen > Perfil de colores (Paint.NET 5): muestra el perfil actual y deja elegir otro de la
// lista o de un archivo .icc para convertir los colores (los píxeles cambian, el aspecto se
// mantiene) o asignarlo (solo cambia la interpretación de los mismos píxeles).
class ColorProfileDialog : public QDialog {
    Q_OBJECT
public:
    enum class Result { Cancel, Convert, Assign };
    ColorProfileDialog(const QColorSpace& current, QWidget* parent = nullptr);
    Result result() const { return m_result; }
    QColorSpace profile() const;

    // Nombre que se muestra para un perfil.
    static QString describe(const QColorSpace& cs);

private:
    void addProfile(const QColorSpace& cs, bool select);
    void updateButtons();

    QColorSpace m_current;
    QComboBox* m_profiles;
    QList<QColorSpace> m_spaces;
    QPushButton* m_convert;
    QPushButton* m_assign;
    Result m_result = Result::Cancel;
};

class QLabel;
class QSpinBox;
class QTreeWidget;

// Imagen > Animación: la duración de cada fotograma de un GIF (cada capa visible, la de abajo
// primero) con una vista previa que se reproduce. Las duraciones se guardan en el nombre de
// las capas ("Fotograma 1 (100ms)").
class AnimationDialog : public QDialog {
    Q_OBJECT
public:
    AnimationDialog(const QList<LayerPtr>& layers, int defaultDelay, QWidget* parent = nullptr);
    QHash<int, QString> renamedLayers() const;

private:
    void showFrame(int frame);
    void step();

    QList<LayerPtr> m_layers;
    QList<int> m_frameLayers;
    QList<QSpinBox*> m_delays;
    QTreeWidget* m_list;
    QLabel* m_preview;
    QTimer* m_timer;
    int m_current = 0;
};
