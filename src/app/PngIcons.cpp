#include "app/Icons.h"

#include <QApplication>
#include <QFile>
#include <QHash>
#include <QIconEngine>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QPixmapCache>

#include <algorithm>
#include <cmath>

// Iconos de la interfaz en PNG (data/ui-icons), los del fork de paint.js de SSSerch. Se dibujan
// desde el PNG grande al tamaño pedido; los desactivados pasan a gris, como en paint.js.
namespace Icons {

namespace {

// Nombre que usa Kaint → archivo de data/ui-icons (sin ".png").
const QHash<QString, QString>& files()
{
    static const QHash<QString, QString> table{
        // Herramientas (el pincel, el cubo, cambiar color, el degradado y la mano se dibujan:
        // los cuatro primeros llevan los colores activos)
        {QStringLiteral("rectangleselect"), QStringLiteral("rectangle_select_tool_icon")},
        {QStringLiteral("ellipseselect"), QStringLiteral("ellipse_select_tool_icon")},
        {QStringLiteral("lassoselect"), QStringLiteral("lasso_select_tool_icon")},
        {QStringLiteral("magicwand"), QStringLiteral("magic_wand_tool_icon")},
        {QStringLiteral("moveselectedpixels"), QStringLiteral("move_tool_icon")},
        {QStringLiteral("moveselection"), QStringLiteral("move_selection_tool_icon")},
        {QStringLiteral("zoom"), QStringLiteral("zoom_tool_icon")},
        {QStringLiteral("eraser"), QStringLiteral("eraser_tool_icon")},
        {QStringLiteral("pencil"), QStringLiteral("pencil_tool_icon")},
        {QStringLiteral("colorpicker"), QStringLiteral("color_picker_tool_icon")},
        {QStringLiteral("clonestamp"), QStringLiteral("clone_stamp_tool_icon")},
        {QStringLiteral("text"), QStringLiteral("text_tool_icon")},
        {QStringLiteral("linecurve"), QStringLiteral("line_tool_icon")},
        {QStringLiteral("shapes"), QStringLiteral("shapes_tool_icon_16")},
        // Archivo
        {QStringLiteral("document-new"), QStringLiteral("menu_file_new_icon")},
        {QStringLiteral("document-open"), QStringLiteral("menu_file_open_icon")},
        {QStringLiteral("document-save"), QStringLiteral("menu_file_save_icon")},
        {QStringLiteral("document-save-as"), QStringLiteral("menu_file_save_as_icon")},
        {QStringLiteral("document-save-all"), QStringLiteral("menu_file_save_all_icon")},
        {QStringLiteral("document-print"), QStringLiteral("menu_file_print_icon")},
        {QStringLiteral("document-close"), QStringLiteral("menu_file_close_icon")},
        {QStringLiteral("application-exit"), QStringLiteral("menu_file_exit_icon")},
        // Edición
        {QStringLiteral("edit-undo"), QStringLiteral("menu_edit_undo_icon")},
        {QStringLiteral("edit-redo"), QStringLiteral("menu_edit_redo_icon")},
        {QStringLiteral("edit-cut"), QStringLiteral("menu_edit_cut_icon")},
        {QStringLiteral("edit-copy"), QStringLiteral("menu_edit_copy_icon")},
        {QStringLiteral("edit-copy-merged"), QStringLiteral("menu_edit_copy_merged_icon")},
        {QStringLiteral("edit-copy-selection"), QStringLiteral("menu_edit_copy_selection_icon")},
        {QStringLiteral("edit-copy-path"), QStringLiteral("menu_tab_copy_path_icon")},
        {QStringLiteral("edit-paste"), QStringLiteral("menu_edit_paste_icon")},
        {QStringLiteral("edit-paste-new-layer"), QStringLiteral("menu_edit_paste_in_to_new_layer_icon")},
        {QStringLiteral("edit-paste-new-image"), QStringLiteral("menu_edit_paste_in_to_new_image_icon")},
        {QStringLiteral("edit-paste-selection"), QStringLiteral("menu_edit_paste_selection_replace_icon")},
        {QStringLiteral("edit-erase-selection"), QStringLiteral("menu_edit_erase_selection_icon")},
        {QStringLiteral("edit-fill-selection"), QStringLiteral("menu_edit_fill_selection_icon")},
        {QStringLiteral("edit-select-invert"), QStringLiteral("menu_edit_invert_selection_icon")},
        {QStringLiteral("edit-select-all"), QStringLiteral("menu_edit_select_all_icon")},
        {QStringLiteral("edit-select-none"), QStringLiteral("menu_edit_deselect_icon")},
        {QStringLiteral("edit-delete"), QStringLiteral("trash_can")},
        {QStringLiteral("user-trash"), QStringLiteral("trash_can")},
        {QStringLiteral("edit-reset"), QStringLiteral("reset_icon")},
        // Ver
        {QStringLiteral("zoom-in"), QStringLiteral("menu_view_zoom_in_icon")},
        {QStringLiteral("zoom-out"), QStringLiteral("menu_view_zoom_out_icon")},
        {QStringLiteral("zoom-fit-best"), QStringLiteral("menu_view_zoom_to_window_icon")},
        {QStringLiteral("zoom-select"), QStringLiteral("menu_view_zoom_to_selection_icon")},
        {QStringLiteral("zoom-original"), QStringLiteral("menu_view_actual_size_icon")},
        {QStringLiteral("view-grid"), QStringLiteral("menu_view_grid_icon")},
        {QStringLiteral("measure"), QStringLiteral("menu_view_rulers_icon")},
        // Imagen
        {QStringLiteral("transform-crop"), QStringLiteral("menu_image_crop_icon")},
        {QStringLiteral("transform-scale"), QStringLiteral("menu_image_resize_icon")},
        {QStringLiteral("transform-crop-and-resize"), QStringLiteral("menu_image_canvas_size_icon")},
        {QStringLiteral("object-flip-horizontal"), QStringLiteral("menu_image_flip_horizontal_icon")},
        {QStringLiteral("object-flip-vertical"), QStringLiteral("menu_image_flip_vertical_icon")},
        {QStringLiteral("object-rotate-right"), QStringLiteral("menu_image_rotate90_c_w_icon")},
        {QStringLiteral("object-rotate-left"), QStringLiteral("menu_image_rotate90_c_c_w_icon")},
        {QStringLiteral("image-rotate-180"), QStringLiteral("menu_image_rotate180_icon")},
        {QStringLiteral("image-flatten"), QStringLiteral("menu_image_flatten_icon")},
        {QStringLiteral("color-profile"), QStringLiteral("menu_image_color_profile_icon")},
        // Capas
        {QStringLiteral("layer-new"), QStringLiteral("menu_layers_add_new_layer_icon")},
        {QStringLiteral("layer-delete"), QStringLiteral("menu_layers_delete_layer_icon")},
        {QStringLiteral("layer-duplicate"), QStringLiteral("menu_layers_duplicate_layer_icon")},
        {QStringLiteral("layer-merge-down"), QStringLiteral("menu_layers_merge_layer_down_icon")},
        {QStringLiteral("layer-visible-on"), QStringLiteral("menu_layers_toggle_layer_visibility_icon")},
        {QStringLiteral("layer-import"), QStringLiteral("menu_layers_import_from_file_icon")},
        {QStringLiteral("layer-flip-horizontal"), QStringLiteral("menu_layers_flip_horizontal_icon")},
        {QStringLiteral("layer-flip-vertical"), QStringLiteral("menu_layers_flip_vertical_icon")},
        {QStringLiteral("layer-rotate-180"), QStringLiteral("menu_layers_rotate180_icon")},
        {QStringLiteral("layer-rotate-zoom"), QStringLiteral("menu_layers_rotate_zoom_icon")},
        {QStringLiteral("layer-go-top"), QStringLiteral("menu_layers_go_to_top_layer_icon")},
        {QStringLiteral("layer-go-up"), QStringLiteral("menu_layers_go_to_layer_above_icon")},
        {QStringLiteral("layer-go-down"), QStringLiteral("menu_layers_go_to_layer_below_icon")},
        {QStringLiteral("layer-go-bottom"), QStringLiteral("menu_layers_go_to_bottom_layer_icon")},
        {QStringLiteral("layer-top"), QStringLiteral("menu_layers_move_layer_to_top_icon")},
        {QStringLiteral("layer-raise"), QStringLiteral("menu_layers_move_layer_up_icon")},
        {QStringLiteral("layer-lower"), QStringLiteral("menu_layers_move_layer_down_icon")},
        {QStringLiteral("layer-bottom"), QStringLiteral("menu_layers_move_layer_to_bottom_icon")},
        {QStringLiteral("layer-properties"), QStringLiteral("menu_layers_layer_properties_icon")},
        // Ventanas, configuración y ayuda
        {QStringLiteral("window-tools"), QStringLiteral("settings_tools_16")},
        {QStringLiteral("window-history"), QStringLiteral("menu_window_history_icon")},
        {QStringLiteral("window-layers"), QStringLiteral("menu_window_layers_icon")},
        {QStringLiteral("window-colors"), QStringLiteral("menu_window_colors_icon")},
        {QStringLiteral("configure"), QStringLiteral("menu_utilities_settings_icon")},
        {QStringLiteral("plugins"), QStringLiteral("menu_help_plugins_icon")},
        {QStringLiteral("help-contents"), QStringLiteral("menu_help_icon")},
        {QStringLiteral("help-about"), QStringLiteral("menu_help_about_icon")},
        {QStringLiteral("help-website"), QStringLiteral("menu_help_pdn_website_icon")},
        {QStringLiteral("help-docs"), QStringLiteral("menu_help_help_topics_icon")},
        {QStringLiteral("help-forum"), QStringLiteral("menu_help_forum_icon")},
        {QStringLiteral("help-tutorials"), QStringLiteral("menu_help_tutorials_icon")},
        {QStringLiteral("help-feedback"), QStringLiteral("menu_help_send_feedback_icon")},
        {QStringLiteral("settings-interface"), QStringLiteral("settings_u_i_24")},
        {QStringLiteral("settings-canvas"), QStringLiteral("settings_canvas_24")},
        {QStringLiteral("settings-tools"), QStringLiteral("settings_tools_24")},
        {QStringLiteral("settings-tablet"), QStringLiteral("settings_pen_and_tablet_24")},
        {QStringLiteral("settings-performance"), QStringLiteral("settings_graphics_24")},
        {QStringLiteral("settings-plugins"), QStringLiteral("settings_plugins_24")},
        {QStringLiteral("settings-updates"), QStringLiteral("settings_updates_24")},
        {QStringLiteral("settings-diagnostics"), QStringLiteral("settings_diagnostics_24")},
        // Barra de estado, colores y diálogos
        {QStringLiteral("status-selection"), QStringLiteral("selection_icon")},
        {QStringLiteral("status-cursor"), QStringLiteral("cursor_x_y_icon")},
        {QStringLiteral("palette"), QStringLiteral("swatch_icon")},
        {QStringLiteral("color-swap"), QStringLiteral("swap_icon")},
        {QStringLiteral("dialog-warning"), QStringLiteral("warning_icon")},
        {QStringLiteral("dialog-cancel"), QStringLiteral("cancel_icon")},
        // Modos de selección (menú Edición > Pegar selección)
        {QStringLiteral("selection-replace"), QStringLiteral("enum_selection_combine_mode_replace")},
        {QStringLiteral("selection-union"), QStringLiteral("enum_selection_combine_mode_union")},
        {QStringLiteral("selection-exclude"), QStringLiteral("enum_selection_combine_mode_exclude")},
        {QStringLiteral("selection-intersect"), QStringLiteral("enum_selection_combine_mode_intersect")},
        {QStringLiteral("selection-xor"), QStringLiteral("enum_selection_combine_mode_xor")},
        // Ajustes y efectos ("effect-" + su identificador)
        {QStringLiteral("effect-addnoise"), QStringLiteral("add_noise_effect")},
        {QStringLiteral("effect-autolevel"), QStringLiteral("auto_level")},
        {QStringLiteral("effect-blackwhite"), QStringLiteral("black_and_white_icon")},
        {QStringLiteral("effect-bokeh"), QStringLiteral("bokeh_effect_icon")},
        {QStringLiteral("effect-brightness"), QStringLiteral("brightness_and_contrast_adjustment")},
        {QStringLiteral("effect-bulge"), QStringLiteral("bulge_effect")},
        {QStringLiteral("effect-clouds"), QStringLiteral("clouds_effect")},
        {QStringLiteral("effect-crystalize"), QStringLiteral("crystalize_effect_icon")},
        {QStringLiteral("effect-curves"), QStringLiteral("curves_effect")},
        {QStringLiteral("effect-dents"), QStringLiteral("dents_effect_icon")},
        {QStringLiteral("effect-dropshadow"), QStringLiteral("drop_shadow_effect")},
        {QStringLiteral("effect-edgedetect"), QStringLiteral("edge_detect_effect")},
        {QStringLiteral("effect-emboss"), QStringLiteral("emboss_effect")},
        {QStringLiteral("effect-exposure"), QStringLiteral("exposure_effect")},
        {QStringLiteral("effect-fragment"), QStringLiteral("fragment_effect_icon")},
        {QStringLiteral("effect-frostedglass"), QStringLiteral("frosted_glass_effect")},
        {QStringLiteral("effect-gaussianblur"), QStringLiteral("blur_effect")},
        {QStringLiteral("effect-glow"), QStringLiteral("glow_effect")},
        {QStringLiteral("effect-highlightsshadows"), QStringLiteral("highlights_and_shadows_effect")},
        {QStringLiteral("effect-huesat"), QStringLiteral("hue_and_saturation_adjustment")},
        {QStringLiteral("effect-inksketch"), QStringLiteral("ink_sketch_effect_icon")},
        {QStringLiteral("effect-invert"), QStringLiteral("invert_colors_effect")},
        {QStringLiteral("effect-invertalpha"), QStringLiteral("invert_alpha_effect")},
        {QStringLiteral("effect-julia"), QStringLiteral("julia_fractal_effect_icon")},
        {QStringLiteral("effect-levels"), QStringLiteral("levels_effect")},
        {QStringLiteral("effect-mandelbrot"), QStringLiteral("mandelbrot_fractal_effect_icon")},
        {QStringLiteral("effect-median"), QStringLiteral("median_effect_icon")},
        {QStringLiteral("effect-morphology"), QStringLiteral("morphology_effect_icon")},
        {QStringLiteral("effect-motionblur"), QStringLiteral("motion_blur_effect")},
        {QStringLiteral("effect-oilpainting"), QStringLiteral("oil_painting_effect")},
        {QStringLiteral("effect-outline"), QStringLiteral("outline_effect_icon")},
        {QStringLiteral("effect-pencilsketch"), QStringLiteral("pencil_sketch_effect_icon")},
        {QStringLiteral("effect-pixelate"), QStringLiteral("pixelate_effect")},
        {QStringLiteral("effect-polarinversion"), QStringLiteral("polar_inversion_effect")},
        {QStringLiteral("effect-posterize"), QStringLiteral("posterize_effect_icon")},
        {QStringLiteral("effect-quantize"), QStringLiteral("quantize_effect_icon")},
        {QStringLiteral("effect-radialblur"), QStringLiteral("radial_blur_effect")},
        {QStringLiteral("effect-redeye"), QStringLiteral("red_eye_remove_effect")},
        {QStringLiteral("effect-reducenoise"), QStringLiteral("reduce_noise_effect")},
        {QStringLiteral("effect-relief"), QStringLiteral("relief_effect")},
        {QStringLiteral("effect-rotatezoom"), QStringLiteral("menu_layers_rotate_zoom_icon")},
        {QStringLiteral("effect-sepia"), QStringLiteral("sepia_effect")},
        {QStringLiteral("effect-sharpen"), QStringLiteral("sharpen_effect")},
        {QStringLiteral("effect-sketchblur"), QStringLiteral("sketch_blur_effect_icon")},
        {QStringLiteral("effect-softenportrait"), QStringLiteral("soften_portrait_effect_icon")},
        {QStringLiteral("effect-squareblur"), QStringLiteral("square_blur_effect")},
        {QStringLiteral("effect-straighten"), QStringLiteral("straighten_effect")},
        {QStringLiteral("effect-surfaceblur"), QStringLiteral("surface_blur_effect_icon")},
        {QStringLiteral("effect-temperature"), QStringLiteral("temperature_and_tint_effect")},
        {QStringLiteral("effect-tilereflection"), QStringLiteral("tile_effect")},
        {QStringLiteral("effect-turbulence"), QStringLiteral("turbulence_effect_icon")},
        {QStringLiteral("effect-twist"), QStringLiteral("twist_effect")},
        {QStringLiteral("effect-vignette"), QStringLiteral("vignette_effect_icon")},
        {QStringLiteral("effect-zoomblur"), QStringLiteral("zoom_blur_effect")},
    };
    return table;
}

QString resourcePath(const QString& file)
{
    return QStringLiteral(":/kaint/ui-icons/%1.png").arg(file);
}

// Gris y medio transparente: el aspecto de las opciones desactivadas.
QImage disabledImage(QImage image)
{
    image = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const int gray = qGray(line[x]);
            line[x] = qRgba(gray, gray, gray, qAlpha(line[x]) * 45 / 100);
        }
    }
    return image;
}

class PngIconEngine : public QIconEngine {
public:
    explicit PngIconEngine(const QString& file)
        : m_file(file)
    {
    }

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override
    {
        const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
        painter->drawPixmap(rect, scaledPixmap(rect.size(), mode, state, dpr));
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State, qreal scale) override
    {
        const qreal dpr = std::max<qreal>(1.0, scale);
        const QSize pixels = (QSizeF(size) * dpr).toSize();
        if (pixels.isEmpty())
            return {};
        // Sin el tinte de "seleccionado" de algunos estilos: el icono es el mismo en una fila
        // marcada y en una normal.
        const bool disabled = mode == QIcon::Disabled;
        const QString key = QStringLiteral("kaint-png/%1/%2x%3/%4").arg(m_file).arg(pixels.width()).arg(pixels.height()).arg(disabled);
        QPixmap pm;
        if (!QPixmapCache::find(key, &pm)) {
            const QImage source(resourcePath(m_file));
            QImage image(pixels, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            if (!source.isNull()) {
                // Centrado y sin deformar (los de estilos de trazo son más anchos que altos).
                const QImage scaled = source.scaled(pixels, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                QPainter p(&image);
                p.drawImage(QPoint((pixels.width() - scaled.width()) / 2, (pixels.height() - scaled.height()) / 2), scaled);
            }
            pm = QPixmap::fromImage(disabled ? disabledImage(image) : image);
            QPixmapCache::insert(key, pm);
        }
        pm.setDevicePixelRatio(dpr);
        return pm;
    }

    QSize actualSize(const QSize& size, QIcon::Mode, QIcon::State) override { return size; }
    QIconEngine* clone() const override { return new PngIconEngine(m_file); }
    QString key() const override { return QStringLiteral("KaintPng"); }

private:
    QString m_file;
};

QIcon png(const QString& file)
{
    if (!QFile::exists(resourcePath(file)))
        return {};
    return QIcon(new PngIconEngine(file));
}

} // namespace

QIcon named(const QString& name)
{
    const auto it = files().constFind(name);
    return it == files().constEnd() ? QIcon() : png(*it);
}

QStringList missingNamedIcons()
{
    QStringList missing;
    for (auto it = files().constBegin(); it != files().constEnd(); ++it)
        if (!QFile::exists(resourcePath(*it)))
            missing << it.key();
    return missing;
}

QString effectIcon(const QString& id, const QString& fallback)
{
    const QString name = QStringLiteral("effect-") + id;
    return files().contains(name) ? name : fallback;
}

} // namespace Icons
