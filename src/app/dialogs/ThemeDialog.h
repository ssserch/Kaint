#pragma once

#include "app/Theme.h"

#include <QDialog>

class ColorButton;
class QCheckBox;
class QComboBox;
class QFontComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QToolButton;

// Editor del tema personalizado: modo base, colores, fondo del lienzo y diseño, con vista previa
// en toda la aplicación. Cancelar deja el tema como estaba.
class ThemeDialog : public QDialog {
    Q_OBJECT
public:
    explicit ThemeDialog(QWidget* parent = nullptr);
    Theme::Spec spec() const { return m_spec; }
    void setSpec(const Theme::Spec& spec);

    void accept() override;
    void reject() override;

private:
    struct ColorRow {
        QColor Theme::Spec::*color;
        QLabel* label;
        ColorButton* button;
        QToolButton* reset;
    };

    ColorRow addColor(const Theme::ColorRole& role, QWidget* parent);
    void refresh();
    void changed();
    void chooseImage();
    void importTheme();
    void exportTheme();

    Theme::Spec m_spec;
    Theme::Spec m_original;
    bool m_refreshing = false;
    QComboBox* m_base = nullptr;
    QList<ColorRow> m_colors;
    QLineEdit* m_image = nullptr;
    QPushButton* m_clearImage = nullptr;
    QComboBox* m_imageMode = nullptr;
    QSlider* m_imageOpacity = nullptr;
    QComboBox* m_iconSize = nullptr;
    QComboBox* m_toolIconSize = nullptr;
    QComboBox* m_buttonStyle = nullptr;
    QComboBox* m_density = nullptr;
    QSpinBox* m_radius = nullptr;
    QSlider* m_panelOpacity = nullptr;
    QCheckBox* m_systemFont = nullptr;
    QFontComboBox* m_font = nullptr;
    QSpinBox* m_fontSize = nullptr;
    QComboBox* m_iconTheme = nullptr;
};
