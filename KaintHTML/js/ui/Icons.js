// Iconos de Kaint: los PNG de la interfaz (assets/icons, la misma tabla que src/app/PngIcons.cpp)
// y los dibujados por código (src/app/PdnIcons.cpp e Icons.cpp): las herramientas (las que llevan
// los colores activos, la mano y las demás con sus dibujos de siempre) y las formas. Los dibujados
// se vuelven a pintar al cambiar de tema o de colores.
import { IconPacks } from "../IconPacks.js";
import { Settings } from "../Settings.js";

// Nombre de los PNG de las herramientas en los paquetes de iconos de paint.js: si el activo trae
// uno, sustituye al dibujo de Kaint en el botón y en el cursor.
const toolFiles = {
    rectangleselect: "rectangle_select_tool_icon", ellipseselect: "ellipse_select_tool_icon",
    lassoselect: "lasso_select_tool_icon", magicwand: "magic_wand_tool_icon", moveselectedpixels: "move_tool_icon",
    moveselection: "move_selection_tool_icon", zoom: "zoom_tool_icon", eraser: "eraser_tool_icon",
    pencil: "pencil_tool_icon", colorpicker: "color_picker_tool_icon", clonestamp: "clone_stamp_tool_icon",
    text: "text_tool_icon", linecurve: "line_tool_icon", shapes: "shapes_tool_icon",
    paintbrush: "paint_brush_tool_icon", paintbucket: "paint_bucket_icon", recolor: "recoloring_tool_icon",
    gradient: "gradient_tool_icon", pan: "pan_tool_icon",
};

const files = {
    "document-new": "menu_file_new_icon", "document-open": "menu_file_open_icon", "document-save": "menu_file_save_icon",
    "document-save-as": "menu_file_save_as_icon", "document-save-all": "menu_file_save_all_icon",
    "document-print": "menu_file_print_icon", "document-close": "menu_file_close_icon", "application-exit": "menu_file_exit_icon",
    "edit-undo": "menu_edit_undo_icon", "edit-redo": "menu_edit_redo_icon", "edit-cut": "menu_edit_cut_icon",
    "edit-copy": "menu_edit_copy_icon", "edit-copy-merged": "menu_edit_copy_merged_icon",
    "edit-copy-selection": "menu_edit_copy_selection_icon", "edit-copy-path": "menu_tab_copy_path_icon",
    "edit-paste": "menu_edit_paste_icon", "edit-paste-new-layer": "menu_edit_paste_in_to_new_layer_icon",
    "edit-paste-new-image": "menu_edit_paste_in_to_new_image_icon", "edit-paste-selection": "menu_edit_paste_selection_replace_icon",
    "edit-erase-selection": "menu_edit_erase_selection_icon", "edit-fill-selection": "menu_edit_fill_selection_icon",
    "edit-select-invert": "menu_edit_invert_selection_icon", "edit-select-all": "menu_edit_select_all_icon",
    "edit-select-none": "menu_edit_deselect_icon", "edit-delete": "trash_can", "edit-reset": "reset_icon",
    "zoom-in": "menu_view_zoom_in_icon", "zoom-out": "menu_view_zoom_out_icon", "zoom-fit-best": "menu_view_zoom_to_window_icon",
    "zoom-select": "menu_view_zoom_to_selection_icon", "zoom-original": "menu_view_actual_size_icon",
    "view-grid": "menu_view_grid_icon", measure: "menu_view_rulers_icon",
    "transform-crop": "menu_image_crop_icon", "transform-scale": "menu_image_resize_icon",
    "transform-crop-and-resize": "menu_image_canvas_size_icon", "object-flip-horizontal": "menu_image_flip_horizontal_icon",
    "object-flip-vertical": "menu_image_flip_vertical_icon", "object-rotate-right": "menu_image_rotate90_c_w_icon",
    "object-rotate-left": "menu_image_rotate90_c_c_w_icon", "image-rotate-180": "menu_image_rotate180_icon",
    "image-flatten": "menu_image_flatten_icon", "color-profile": "menu_image_color_profile_icon",
    "layer-new": "menu_layers_add_new_layer_icon", "layer-delete": "menu_layers_delete_layer_icon",
    "layer-duplicate": "menu_layers_duplicate_layer_icon", "layer-merge-down": "menu_layers_merge_layer_down_icon",
    "layer-visible-on": "menu_layers_toggle_layer_visibility_icon", "layer-import": "menu_layers_import_from_file_icon",
    "layer-flip-horizontal": "menu_layers_flip_horizontal_icon", "layer-flip-vertical": "menu_layers_flip_vertical_icon",
    "layer-rotate-180": "menu_layers_rotate180_icon", "layer-rotate-zoom": "menu_layers_rotate_zoom_icon",
    "layer-go-top": "menu_layers_go_to_top_layer_icon", "layer-go-up": "menu_layers_go_to_layer_above_icon",
    "layer-go-down": "menu_layers_go_to_layer_below_icon", "layer-go-bottom": "menu_layers_go_to_bottom_layer_icon",
    "layer-top": "menu_layers_move_layer_to_top_icon", "layer-raise": "menu_layers_move_layer_up_icon",
    "layer-lower": "menu_layers_move_layer_down_icon", "layer-bottom": "menu_layers_move_layer_to_bottom_icon",
    "layer-properties": "menu_layers_layer_properties_icon",
    "window-tools": "settings_tools_16", "window-history": "menu_window_history_icon", "window-layers": "menu_window_layers_icon",
    "window-colors": "menu_window_colors_icon", configure: "menu_utilities_settings_icon", plugins: "menu_help_plugins_icon",
    "help-contents": "menu_help_icon", "help-about": "menu_help_about_icon", "help-website": "menu_help_pdn_website_icon",
    "face-smile": "face-smile.svg", "help-feedback": "menu_help_send_feedback_icon",
    "help-docs": "menu_help_help_topics_icon", "help-forum": "menu_help_forum_icon", "help-tutorials": "menu_help_tutorials_icon",
    "settings-interface": "settings_u_i_24", "settings-canvas": "settings_canvas_24", "settings-tools": "settings_tools_24",
    "settings-diagnostics": "settings_diagnostics_24", "settings-tablet": "settings_pen_and_tablet_24",
    "settings-plugins": "settings_plugins_24", "settings-updates": "settings_updates_24",
    "status-selection": "selection_icon", "status-cursor": "cursor_x_y_icon", palette: "swatch_icon", "color-swap": "swap_icon",
    "dialog-warning": "warning_icon", "dialog-cancel": "cancel_icon", "dialog-ok-apply": "tool_strip_checked",
    "image-list-menu": "tool_bar_image_list_menu_open_button", "anchor": "anchor_chooser_control_anchor_image",
    "selection-replace": "enum_selection_combine_mode_replace", "selection-union": "enum_selection_combine_mode_union",
    "selection-exclude": "enum_selection_combine_mode_exclude", "selection-intersect": "enum_selection_combine_mode_intersect",
    "selection-xor": "enum_selection_combine_mode_xor",
    "effect-addnoise": "add_noise_effect", "effect-autolevel": "auto_level", "effect-blackwhite": "black_and_white_icon",
    "effect-bokeh": "bokeh_effect_icon", "effect-brightness": "brightness_and_contrast_adjustment",
    "effect-bulge": "bulge_effect", "effect-clouds": "clouds_effect", "effect-crystalize": "crystalize_effect_icon",
    "effect-curves": "curves_effect", "effect-dents": "dents_effect_icon", "effect-dropshadow": "drop_shadow_effect",
    "effect-edgedetect": "edge_detect_effect", "effect-emboss": "emboss_effect", "effect-exposure": "exposure_effect",
    "effect-fragment": "fragment_effect_icon", "effect-frostedglass": "frosted_glass_effect",
    "effect-gaussianblur": "blur_effect", "effect-glow": "glow_effect", "effect-highlightsshadows": "highlights_and_shadows_effect",
    "effect-huesat": "hue_and_saturation_adjustment", "effect-inksketch": "ink_sketch_effect_icon",
    "effect-invert": "invert_colors_effect", "effect-invertalpha": "invert_alpha_effect",
    "effect-julia": "julia_fractal_effect_icon", "effect-levels": "levels_effect",
    "effect-mandelbrot": "mandelbrot_fractal_effect_icon", "effect-median": "median_effect_icon",
    "effect-morphology": "morphology_effect_icon", "effect-motionblur": "motion_blur_effect",
    "effect-oilpainting": "oil_painting_effect", "effect-outline": "outline_effect_icon",
    "effect-pencilsketch": "pencil_sketch_effect_icon", "effect-pixelate": "pixelate_effect",
    "effect-polarinversion": "polar_inversion_effect", "effect-posterize": "posterize_effect_icon",
    "effect-radialblur": "radial_blur_effect", "effect-redeye": "red_eye_remove_effect",
    "effect-reducenoise": "reduce_noise_effect", "effect-relief": "relief_effect", "effect-sepia": "sepia_effect",
    "effect-sharpen": "sharpen_effect", "effect-softenportrait": "soften_portrait_effect_icon",
    "effect-squareblur": "square_blur_effect", "effect-surfaceblur": "surface_blur_effect_icon",
    "effect-temperature": "temperature_and_tint_effect", "effect-tilereflection": "tile_effect",
    "effect-turbulence": "turbulence_effect_icon", "effect-twist": "twist_effect", "effect-vignette": "vignette_effect_icon",
    "effect-zoomblur": "zoom_blur_effect",
};

// Opciones de la barra de herramientas con PNG de paint.js (como Icons.cpp); las formas se
// dibujan.
const optionFiles = {
    "opt-gradient:linear": "enum_gradient_type_linear_clamped", "opt-gradient:reflected": "enum_gradient_type_linear_reflected",
    "opt-gradient:diamond": "enum_gradient_type_linear_diamond", "opt-gradient:radial": "enum_gradient_type_radial",
    "opt-gradient:conical": "enum_gradient_type_conical",
    "opt-style:outline": "enum_shape_draw_type_outline_dark", "opt-style:fill": "enum_shape_draw_type_interior",
    "opt-style:both": "enum_shape_draw_type_both_dark",
    "opt-curve:straight": "enum_curve_type_straight", "opt-curve:spline": "enum_curve_type_spline",
    "opt-curve:bezier": "enum_curve_type_bezier",
    "opt-aa:1": "anti_aliasing_true", "opt-aa:0": "anti_aliasing_false",
    "opt-selquality:0": "enum_selection_rendering_quality_aliased",
    "opt-selquality:1": "enum_selection_rendering_quality_high_quality_antialiased",
    "opt-selmode:replace": "enum_selection_combine_mode_replace", "opt-selmode:union": "enum_selection_combine_mode_union",
    "opt-selmode:exclude": "enum_selection_combine_mode_exclude", "opt-selmode:intersect": "enum_selection_combine_mode_intersect",
    "opt-selmode:xor": "enum_selection_combine_mode_xor",
    "opt-dash:solid": "enum_dash_style_solid", "opt-dash:dash": "enum_dash_style_dash", "opt-dash:dot": "enum_dash_style_dot",
    "opt-dash:dashdot": "enum_dash_style_dash_dot", "opt-dash:dashdotdot": "enum_dash_style_dash_dot_dot",
    "opt-flood:0": "enum_flood_mode_local", "opt-flood:1": "enum_flood_mode_global",
    "opt-rectmode:normal": "enum_selection_draw_mode_normal", "opt-rectmode:ratio": "enum_selection_draw_mode_fixed_ratio",
    "opt-rectmode:size": "enum_selection_draw_mode_fixed_size",
    "opt-stepper:plus": "plus_button_icon", "opt-stepper:minus": "minus_button_icon", "opt-blend": "alpha_blending_true",
    "opt-align:left": "enum_text_alignment_left", "opt-align:center": "enum_text_alignment_center",
    "opt-align:right": "enum_text_alignment_right",
    "opt-sample:1": "enum_pixel_sample_mode_point_sample",
    "opt-smoothing:1": "enum_input_path_smoothing_enabled", "opt-smoothing:0": "enum_input_path_smoothing_disabled",
    "opt-pressure:1": "tool_config_strip_brush_enable_pressure_sensitivity_true",
    "opt-pressure:0": "tool_config_strip_brush_enable_pressure_sensitivity_false",
    "opt-pressure": "tool_config_strip_brush_enable_pressure_sensitivity_true",
    "opt-recolor:once": "enum_recolor_tool_sampling_mode_once",
    "opt-recolor:secondary": "enum_recolor_tool_sampling_mode_secondary_color",
};
for (const cap of ["flat", "round", "arrow", "filled"])
    for (const end of ["start", "end"])
        optionFiles[`opt-cap:${cap}:${end}`] = `enum_line_curve_cap_${{ round: "rounded", filled: "arrow_filled" }[cap] || cap}_${end}`;
for (const n of [3, 5, 11, 31, 51])
    optionFiles[`opt-sample:${n}`] = `enum_pixel_sample_mode_average${n}x${n}`;
for (const [k, v] of Object.entries(optionFiles))
    files[k] = v;

// --- Colores ------------------------------------------------------------------------------

let primary = null, secondary = null;

export function setActiveColors(p, s) {
    primary = p;
    secondary = s;
    refreshIcons();
}

function cssVar(name) {
    return getComputedStyle(document.documentElement).getPropertyValue(name).trim() || "#000";
}
const fg = () => cssVar("--text");
const accent = () => cssVar("--accent");

const rgb = (r, g, b, a = 1) => `rgba(${r},${g},${b},${a})`;
const css = (c) => rgb(c.r, c.g, c.b);
const mix = (a, b, t) => ({ r: a.r + (b.r - a.r) * t, g: a.g + (b.g - a.g) * t, b: a.b + (b.b - a.b) * t });
const W = { r: 255, g: 255, b: 255 }, K = { r: 0, g: 0, b: 0 };
const tint = (active, fallback) => (active ? { r: active.r, g: active.g, b: active.b } : fallback);
const lightness = (c) => (Math.max(c.r, c.g, c.b) + Math.min(c.r, c.g, c.b)) / 510;
const outlineFor = (c) => css(lightness(c) < 0.25 ? mix(c, W, 0.45) : mix(c, K, 0.55));

function linear(ctx, x0, y0, x1, y1, stops) {
    const g = ctx.createLinearGradient(x0, y0, x1, y1);
    for (const [t, c] of stops) g.addColorStop(t, c);
    return g;
}

function poly(ctx, pts, close = true) {
    ctx.beginPath();
    pts.forEach(([x, y], i) => (i ? ctx.lineTo(x, y) : ctx.moveTo(x, y)));
    if (close) ctx.closePath();
}

function stroke(ctx, color, width, cap = "butt", join = "miter") {
    ctx.strokeStyle = color;
    ctx.lineWidth = width;
    ctx.lineCap = cap;
    ctx.lineJoin = join;
    ctx.stroke();
}

function roundRect(ctx, x, y, w, h, r) {
    ctx.beginPath();
    ctx.roundRect(x, y, w, h, r);
}

// --- Herramientas con los colores activos y la mano (rejilla de 20 px) ------------------

function drawBrush(ctx) {
    const paint = tint(primary, { r: 46, g: 110, b: 210 });
    ctx.save();
    ctx.translate(2, 18);
    ctx.rotate(-Math.PI / 4);
    ctx.beginPath();
    ctx.moveTo(0, 0.9);
    ctx.bezierCurveTo(1.4, -1.6, 4.8, -2.9, 8.2, -2.5);
    ctx.lineTo(8.2, 2.5);
    ctx.bezierCurveTo(5, 2.9, 2, 2.4, 0, 0.9);
    ctx.fillStyle = linear(ctx, 0, -2.6, 0, 2.6, [[0, css(mix(paint, W, 0.35))], [1, css(paint)]]);
    ctx.fill();
    stroke(ctx, outlineFor(paint), 0.8);
    ctx.fillStyle = linear(ctx, 0, -2.6, 0, 2.6, [[0, rgb(236, 240, 245)], [1, rgb(140, 150, 162)]]);
    ctx.fillRect(8.2, -2.6, 3.2, 5.2);
    ctx.strokeStyle = rgb(80, 88, 98);
    ctx.lineWidth = 0.8;
    ctx.strokeRect(8.2, -2.6, 3.2, 5.2);
    ctx.beginPath();
    ctx.moveTo(11.4, -2);
    ctx.lineTo(20.6, -1.2);
    ctx.quadraticCurveTo(22.2, 0, 20.6, 1.2);
    ctx.lineTo(11.4, 2);
    ctx.closePath();
    ctx.fillStyle = linear(ctx, 0, -2, 0, 2, [[0, rgb(112, 118, 128)], [1, rgb(52, 56, 64)]]);
    ctx.fill();
    stroke(ctx, rgb(30, 32, 38), 0.8);
    ctx.restore();
}

function drawFill(ctx) {
    const color = tint(primary, { r: 40, g: 110, b: 215 });
    ctx.beginPath();
    ctx.moveTo(7.6, 8.4);
    ctx.bezierCurveTo(3, 8, 0.8, 11, 1.2, 15);
    ctx.bezierCurveTo(1.4, 17.4, 2.6, 18.6, 4, 18.6);
    ctx.bezierCurveTo(4.6, 16, 3.6, 13.4, 5.4, 12);
    ctx.bezierCurveTo(6.4, 11.2, 7.6, 11.2, 8.6, 11.4);
    ctx.closePath();
    ctx.fillStyle = linear(ctx, 0, 8, 0, 19, [[0, css(mix(color, W, 0.3))], [1, css(color)]]);
    ctx.fill();
    stroke(ctx, outlineFor(color), 0.8);
    ctx.beginPath();
    ctx.moveTo(8.2, 9.8);
    ctx.bezierCurveTo(6.6, 4.4, 12.2, 1.4, 15.8, 8.6);
    stroke(ctx, rgb(60, 64, 72), 1.1, "round");
    poly(ctx, [[12.6, 5.4], [18.6, 11.4], [12.6, 17.4], [6.6, 11.4]]);
    ctx.fillStyle = linear(ctx, 7, 6, 18, 17, [[0, rgb(222, 226, 232)], [1, rgb(120, 128, 140)]]);
    ctx.fill();
    stroke(ctx, rgb(64, 70, 80), 0.9, "round", "round");
    poly(ctx, [[12.6, 5.4], [15, 7.8], [9, 13.8], [6.6, 11.4]]);
    ctx.fillStyle = rgb(84, 90, 100);
    ctx.fill();
    stroke(ctx, rgb(64, 70, 80), 0.9, "round", "round");
    poly(ctx, [[11.4, 7], [13, 8.6], [8.6, 13], [7, 11.4]]);
    ctx.fillStyle = css(color);
    ctx.fill();
}

function drawRecolor(ctx) {
    const p = tint(primary, { r: 50, g: 120, b: 220 }), s = tint(secondary, { r: 225, g: 80, b: 50 });
    const disc = (x, y, c) => {
        ctx.beginPath();
        ctx.arc(x, y, 4.4, 0, Math.PI * 2);
        ctx.fillStyle = css(c);
        ctx.fill();
        stroke(ctx, outlineFor(c), 0.9);
    };
    disc(14, 6, s);
    disc(6, 14, p);
    ctx.beginPath();
    ctx.moveTo(13.6, 6.4);
    ctx.lineTo(8.6, 11.4);
    stroke(ctx, rgb(40, 110, 92), 4.4);
    ctx.stroke();
    stroke(ctx, rgb(236, 248, 244), 2.6);
    poly(ctx, [[6, 14], [6.4, 8.6], [11.4, 13.6]]);
    ctx.fillStyle = rgb(236, 248, 244);
    ctx.fill();
    stroke(ctx, rgb(40, 110, 92), 0.9, "round", "round");
}

function drawGradientTool(ctx) {
    ctx.fillStyle = rgb(232, 235, 239);
    ctx.fillRect(2.4, 2.4, 15.2, 15.2);
    ctx.strokeStyle = rgb(96, 102, 112);
    ctx.lineWidth = 0.8;
    ctx.strokeRect(2.4, 2.4, 15.2, 15.2);
    ctx.fillStyle = linear(ctx, 4.5, 4.5, 15.5, 15.5, [[0, css(tint(primary, { r: 56, g: 120, b: 230 }))],
                                                      [1, css(tint(secondary, { r: 160, g: 72, b: 205 }))]]);
    ctx.fillRect(4.4, 4.4, 11.2, 11.2);
}

const handOutline = rgb(110, 66, 28);
const skin = (ctx, y0, y1) => linear(ctx, 0, y0, 0, y1, [[0, rgb(246, 206, 148)], [1, rgb(222, 160, 88)]]);

function drawPan(ctx) {
    // Mano abierta: cuatro dedos rectos y el pulgar en diagonal a la izquierda.
    ctx.beginPath();
    ctx.roundRect(5.5, 4.5, 3, 9, 1.5);
    ctx.roundRect(8.5, 2.5, 3, 11, 1.5);
    ctx.roundRect(11.5, 3.5, 3, 10, 1.5);
    ctx.roundRect(14.5, 5.5, 3, 8, 1.5);
    ctx.roundRect(5.5, 9.5, 12, 8.5, 3);
    ctx.save();
    ctx.translate(6.4, 14.2);
    ctx.rotate((50 * Math.PI) / 180);
    ctx.roundRect(-6, -1.6, 7.5, 3.2, 1.6);
    ctx.restore();
    // El contorno de la unión: trazo grueso por debajo y relleno encima.
    ctx.lineJoin = "round";
    ctx.strokeStyle = handOutline;
    ctx.lineWidth = 2;
    ctx.stroke();
    ctx.fillStyle = skin(ctx, 2, 18);
    ctx.fill();
    ctx.beginPath();
    for (const [x, y0] of [[8.5, 6], [11.5, 5], [14.5, 7]]) {
        ctx.moveTo(x, y0);
        ctx.lineTo(x, 11);
    }
    stroke(ctx, handOutline, 1);
}

function drawPanGrab(ctx) {
    // La misma mano agarrando, vista por el dorso: los dedos se doblan hacia dentro y el pulgar
    // queda recogido detrás, asomando por la izquierda.
    ctx.save();
    ctx.translate(6.5, 14.5);
    ctx.rotate((-60 * Math.PI) / 180);
    roundRect(ctx, -3.5, -1.6, 6.5, 3.2, 1.6);
    ctx.fillStyle = rgb(226, 168, 98);
    ctx.fill();
    stroke(ctx, handOutline, 1, "round", "round");
    ctx.restore();
    ctx.beginPath();
    ctx.roundRect(5.5, 7, 3, 5, 1.5);
    ctx.roundRect(8.5, 6, 3, 6, 1.5);
    ctx.roundRect(11.5, 6, 3, 6, 1.5);
    ctx.roundRect(14.5, 7, 3, 5, 1.5);
    ctx.roundRect(5.5, 8.5, 12, 9.5, 3);
    ctx.lineJoin = "round";
    ctx.strokeStyle = handOutline;
    ctx.lineWidth = 2;
    ctx.stroke();
    ctx.fillStyle = skin(ctx, 5, 18);
    ctx.fill();
    ctx.beginPath();
    for (const [x, y0] of [[8.5, 8.5], [11.5, 7.5], [14.5, 8.5]]) {
        ctx.moveTo(x, y0);
        ctx.lineTo(x, 10.5);
    }
    stroke(ctx, handOutline, 1);
}

// --- Barra principal (rejilla de 16 px) -----------------------------------------------------

// Arco con punta de flecha al final (ángulos en grados, positivos en sentido antihorario).
// --- Herramientas con sus dibujos de siempre (PdnIcons.cpp, rejilla de 20 px) -------------

function ants(ctx) {
    ctx.strokeStyle = rgb(65, 75, 88);
    ctx.lineWidth = 1.2;
    ctx.setLineDash([2.5 * 1.2, 1.8 * 1.2]);
}

function ellipseAt(ctx, x, y, w, h) {
    ctx.beginPath();
    ctx.ellipse(x + w / 2, y + h / 2, w / 2, h / 2, 0, 0, Math.PI * 2);
}

function fillStroke(ctx, fill, line, width) {
    if (fill) {
        ctx.fillStyle = fill;
        ctx.fill();
    }
    if (line) {
        ctx.strokeStyle = line;
        ctx.lineWidth = width;
        ctx.stroke();
    }
}

function drawRectSelect(ctx) {
    ctx.fillStyle = rgb(178, 215, 245);
    ctx.fillRect(3, 3, 14, 14);
    ants(ctx);
    ctx.strokeRect(3, 3, 14, 14);
    ctx.setLineDash([]);
}

function drawEllipseSelect(ctx) {
    ellipseAt(ctx, 3, 3, 14, 14);
    ctx.fillStyle = rgb(137, 190, 233);
    ctx.fill();
    ants(ctx);
    ctx.stroke();
    ctx.setLineDash([]);
}

function drawLasso(ctx) {
    ellipseAt(ctx, 3.2, 2, 12.4, 9.8);
    ctx.fillStyle = rgb(178, 215, 245);
    ctx.fill();
    stroke(ctx, rgb(50, 106, 176), 1.7, "round");
    ctx.beginPath();
    ctx.moveTo(9, 11.8);
    ctx.bezierCurveTo(9.4, 15, 13.8, 15.4, 12.2, 18.6);
    stroke(ctx, rgb(50, 106, 176), 1.7, "round");
}

function drawMagicWand(ctx) {
    const line = (x0, y0, x1, y1, color, width) => {
        ctx.beginPath();
        ctx.moveTo(x0, y0);
        ctx.lineTo(x1, y1);
        stroke(ctx, color, width, "round");
    };
    line(3, 17, 11, 9, rgb(74, 85, 101), 3);
    line(11, 9, 13, 7, rgb(196, 208, 222), 3);
    const sparkle = (cx, cy, r, color, w) => {
        line(cx, cy - r, cx, cy + r, color, w);
        line(cx - r, cy, cx + r, cy, color, w);
    };
    sparkle(15.5, 4, 3.2, rgb(255, 196, 36), 1.6);
    sparkle(9.5, 3, 1.9, rgb(255, 226, 120), 1.2);
    sparkle(18, 9.5, 1.7, rgb(255, 226, 120), 1.2);
}

const moveArrow = [[2, 1], [2, 12.5], [5, 9.8], [7, 14.2], [8.9, 13.3], [7, 9.2], [11, 8.8]];

// La cruz con cuatro puntas de las dos herramientas Mover.
function moveCross(ctx) {
    const cx = 14.5, cy = 14.5, r = 5, h = 2, c = rgb(64, 84, 110);
    ctx.beginPath();
    ctx.moveTo(cx, cy - r); ctx.lineTo(cx, cy + r);
    ctx.moveTo(cx - r, cy); ctx.lineTo(cx + r, cy);
    stroke(ctx, c, 1.3, "round");
    ctx.fillStyle = c;
    for (const tri of [[[cx, cy - r - 0.8], [cx - h, cy - r + 1.6], [cx + h, cy - r + 1.6]],
                       [[cx, cy + r + 0.8], [cx - h, cy + r - 1.6], [cx + h, cy + r - 1.6]],
                       [[cx - r - 0.8, cy], [cx - r + 1.6, cy - h], [cx - r + 1.6, cy + h]],
                       [[cx + r + 0.8, cy], [cx + r - 1.6, cy - h], [cx + r - 1.6, cy + h]]]) {
        poly(ctx, tri);
        ctx.fill();
    }
}

function drawMove(ctx) {
    poly(ctx, moveArrow);
    fillStroke(ctx, rgb(96, 146, 205), rgb(32, 56, 92), 1);
    moveCross(ctx);
}

function drawMoveSelection(ctx) {
    poly(ctx, moveArrow);
    stroke(ctx, rgb(104, 154, 212), 1.3, "butt", "round");
    moveCross(ctx);
}

function drawZoom(ctx) {
    ctx.beginPath();
    ctx.moveTo(12.6, 12.6);
    ctx.lineTo(17.6, 17.6);
    stroke(ctx, rgb(40, 80, 140), 3, "round");
    ellipseAt(ctx, 2.2, 2.2, 11.6, 11.6);
    stroke(ctx, rgb(40, 80, 140), 1.8);
    ctx.fillStyle = rgb(50, 60, 76);
    for (let i = 0; i < 4; ++i)
        ctx.fillRect(5 + (i % 2) * 3.3, 5 + Math.floor(i / 2) * 3.3, 2.7, 2.7);
}

function drawEraser(ctx) {
    ctx.save();
    ctx.translate(10, 10);
    ctx.rotate((-35 * Math.PI) / 180);
    roundRect(ctx, -8.4, -3.6, 16.8, 7.2, 1.4);
    ctx.fillStyle = rgb(176, 80, 196);
    ctx.fill();
    roundRect(ctx, -1.2, -3.6, 9.6, 7.2, 1.4);
    ctx.fillStyle = rgb(246, 236, 248);
    ctx.fill();
    roundRect(ctx, -8.4, -3.6, 16.8, 7.2, 1.4);
    stroke(ctx, rgb(90, 40, 104), 0.9);
    ctx.beginPath();
    ctx.moveTo(-1.2, -3.6);
    ctx.lineTo(-1.2, 3.6);
    stroke(ctx, rgb(90, 40, 104), 0.9);
    ctx.restore();
}

function drawPencil(ctx) {
    ctx.save();
    ctx.translate(2, 18);
    ctx.rotate(-Math.PI / 4);
    poly(ctx, [[0, 0], [4.6, -1.8], [4.6, 1.8]]);
    fillStroke(ctx, rgb(236, 210, 160), rgb(90, 70, 40), 0.7);
    poly(ctx, [[0, 0], [1.8, -0.7], [1.8, 0.7]]);
    ctx.fillStyle = rgb(50, 50, 56);
    ctx.fill();
    const edge = rgb(120, 84, 40);
    ctx.beginPath();
    ctx.rect(4.6, -1.8, 13.4, 3.6);
    fillStroke(ctx, linear(ctx, 0, -1.8, 0, 1.8, [[0, rgb(240, 206, 140)], [1, rgb(200, 150, 80)]]), edge, 0.7);
    ctx.beginPath();
    ctx.rect(18, -1.8, 1.6, 3.6);
    fillStroke(ctx, rgb(170, 176, 186), edge, 0.7);
    roundRect(ctx, 19.6, -1.8, 2.6, 3.6, 1.2);
    fillStroke(ctx, rgb(236, 120, 130), edge, 0.7);
    ctx.restore();
}

function drawColorPicker(ctx) {
    ctx.save();
    ctx.translate(2.2, 17.8);
    ctx.rotate(-Math.PI / 4);
    poly(ctx, [[0, 0], [4.4, -1.1], [4.4, 1.1]]);
    fillStroke(ctx, rgb(70, 140, 230), rgb(40, 70, 120), 0.7);
    ctx.beginPath();
    ctx.rect(4.4, -1.6, 7.4, 3.2);
    fillStroke(ctx, rgb(236, 243, 251), rgb(80, 96, 118), 0.8);
    ctx.fillStyle = rgb(70, 140, 230);
    ctx.fillRect(4.8, -1.2, 3, 2.4);
    ctx.beginPath();
    ctx.rect(11.8, -2.4, 2.2, 4.8);
    fillStroke(ctx, rgb(150, 160, 172), rgb(60, 68, 80), 0.8);
    roundRect(ctx, 14, -3.2, 7.6, 6.4, 3.2);
    fillStroke(ctx, linear(ctx, 0, -3.2, 0, 3.2, [[0, rgb(96, 150, 225)], [1, rgb(24, 64, 140)]]), rgb(18, 44, 96), 0.8);
    ctx.restore();
}

function drawCloneStamp(ctx) {
    ellipseAt(ctx, 7, 1, 6, 5.2);
    fillStroke(ctx, rgb(82, 90, 104), rgb(38, 42, 50), 0.9);
    ctx.beginPath();
    ctx.rect(8.6, 4.8, 2.8, 5.4);
    fillStroke(ctx, rgb(112, 120, 134), rgb(38, 42, 50), 0.9);
    poly(ctx, [[5, 10.2], [15, 10.2], [16.6, 14.2], [3.4, 14.2]]);
    fillStroke(ctx, linear(ctx, 4, 10, 16, 15, [[0, rgb(248, 182, 88)], [1, rgb(210, 130, 36)]]), rgb(140, 84, 20), 0.9);
    ctx.beginPath();
    ctx.rect(3.2, 14.6, 13.6, 2.6);
    fillStroke(ctx, rgb(184, 110, 28), rgb(140, 84, 20), 0.9);
}

function drawTextTool(ctx) {
    // "T" con remates, gris, como en Paint.NET.
    ctx.beginPath();
    ctx.moveTo(2.5, 2); ctx.lineTo(17.5, 2); ctx.lineTo(17.5, 6.2); ctx.lineTo(16.2, 6.2);
    ctx.quadraticCurveTo(15.8, 3.8, 13.2, 3.6); ctx.lineTo(11.4, 3.6); ctx.lineTo(11.4, 16.2);
    ctx.quadraticCurveTo(11.6, 17.2, 13.6, 17.2); ctx.lineTo(13.6, 18.2); ctx.lineTo(6.4, 18.2); ctx.lineTo(6.4, 17.2);
    ctx.quadraticCurveTo(8.4, 17.2, 8.6, 16.2); ctx.lineTo(8.6, 3.6); ctx.lineTo(6.8, 3.6);
    ctx.quadraticCurveTo(4.2, 3.8, 3.8, 6.2); ctx.lineTo(2.5, 6.2); ctx.closePath();
    fillStroke(ctx, rgb(128, 136, 150), rgb(70, 76, 86), 0.6);
}

function drawLineTool(ctx) {
    ctx.beginPath();
    ctx.moveTo(3.4, 16.4);
    ctx.bezierCurveTo(7, 6, 13, 18, 16.8, 4);
    stroke(ctx, rgb(158, 172, 192), 1.9, "round");
    for (const [x, y] of [[3.4, 16.4], [16.8, 4]]) {
        ctx.beginPath();
        ctx.arc(x, y, 2.3, 0, Math.PI * 2);
        fillStroke(ctx, rgb(100, 154, 216), rgb(28, 60, 100), 0.9);
    }
}

function drawShapesTool(ctx) {
    ctx.beginPath();
    ctx.rect(1.6, 2.8, 9.4, 9.4);
    fillStroke(ctx, rgb(80, 138, 210), rgb(28, 60, 104), 0.9);
    poly(ctx, [[14.6, 6.2], [19, 16.8], [10.2, 16.8]]);
    fillStroke(ctx, rgb(106, 184, 110), rgb(34, 92, 52), 0.9);
    ellipseAt(ctx, 5.8, 8.2, 8.8, 8.8);
    fillStroke(ctx, rgb(170, 98, 210), rgb(84, 40, 110), 0.9);
}

// --- Opciones de herramienta (Icons.cpp: rejilla de 32 px con un margen de 3) -------------

const options = {
    shape(ctx, type) {
        ctx.beginPath();
        shapePath(ctx, type, 4, 5, 24, 22, 4);
        stroke(ctx, fg(), 1.6);
    },
    sample(ctx, n) {
        ctx.strokeStyle = fg(); ctx.lineWidth = 1; ctx.strokeRect(4.5, 4.5, 23, 23);
        const side = Math.min(19, 2.5 + Math.log2(Math.max(1, Number(n))) * 1.6);
        ctx.fillStyle = accent();
        ctx.fillRect(16 - side / 2, 16 - side / 2, side, side);
    },
};

// Formas de la herramienta Formas (AppState.cpp de Kaint), en el rectángulo x, y, w, h.
export const ShapeTypes = [
    ["rectangle", "Rectángulo"], ["roundedrect", "Rectángulo redondeado"], ["ellipse", "Elipse"], ["diamond", "Rombo"],
    ["trapezoid", "Trapecio"], ["parallelogram", "Paralelogramo"], ["triangle", "Triángulo"],
    ["righttriangle", "Triángulo rectángulo"], ["pentagon", "Pentágono"], ["hexagon", "Hexágono"],
    ["heptagon", "Heptágono"], ["octagon", "Octógono"], ["star3", "Estrella de 3 puntas"], ["star4", "Estrella de 4 puntas"],
    ["star5", "Estrella de 5 puntas"], ["star6", "Estrella de 6 puntas"], ["arrow", "Flecha"],
    ["notchedarrow", "Flecha con muesca"], ["pentagonarrow", "Flecha pentagonal"], ["chevronarrow", "Galón"],
    ["rectcallout", "Bocadillo rectangular"], ["roundedrectcallout", "Bocadillo redondeado"],
    ["ellipsecallout", "Bocadillo elíptico"], ["cloudcallout", "Bocadillo de nube"], ["lightningbolt", "Rayo"],
    ["checkmark", "Marca de verificación"], ["multiply", "Multiplicar"], ["gear", "Engranaje"], ["heart", "Corazón"],
];

export function shapePath(ctx, type, x, y, w, h, cornerRadius = 20) {
    if (w < 0) { x += w; w = -w; }
    if (h < 0) { y += h; h = -h; }
    const at = (fx, fy) => [x + fx * w, y + fy * h];
    const polygon = (pts) => {
        pts.forEach(([fx, fy], i) => (i ? ctx.lineTo(...at(fx, fy)) : ctx.moveTo(...at(fx, fy))));
        ctx.closePath();
    };
    const regular = (n, startDeg, inner) => {
        const count = inner > 0 ? n * 2 : n, pts = [];
        for (let i = 0; i < count; ++i) {
            const a = ((startDeg + (i * 360) / count) * Math.PI) / 180, k = inner > 0 && i % 2 ? inner : 1;
            pts.push([0.5 + 0.5 * k * Math.cos(a), 0.5 + 0.5 * k * Math.sin(a)]);
        }
        polygon(pts);
    };
    const tail = (top) => polygon([[0.25, top - 0.05], [0.45, top - 0.05], [0.2, 1]]);
    const ellipse = (fx0, fy0, fx1, fy1) => {
        const [ax, ay] = at(fx0, fy0), [bx, by] = at(fx1, fy1);
        ctx.moveTo(bx, (ay + by) / 2);
        ctx.ellipse((ax + bx) / 2, (ay + by) / 2, Math.abs(bx - ax) / 2, Math.abs(by - ay) / 2, 0, 0, Math.PI * 2);
    };
    switch (type) {
    case "rectangle": ctx.rect(x, y, w, h); break;
    case "roundedrect": ctx.roundRect(x, y, w, h, Math.min(cornerRadius, Math.min(w, h) / 2)); break;
    case "ellipse": ellipse(0, 0, 1, 1); break;
    case "diamond": polygon([[0.5, 0], [1, 0.5], [0.5, 1], [0, 0.5]]); break;
    case "trapezoid": polygon([[0.2, 0], [0.8, 0], [1, 1], [0, 1]]); break;
    case "parallelogram": polygon([[0.25, 0], [1, 0], [0.75, 1], [0, 1]]); break;
    case "triangle": polygon([[0.5, 0], [1, 1], [0, 1]]); break;
    case "righttriangle": polygon([[0, 0], [1, 1], [0, 1]]); break;
    case "pentagon": regular(5, -90, 0); break;
    case "hexagon": regular(6, 0, 0); break;
    case "heptagon": regular(7, -90, 0); break;
    case "octagon": regular(8, 22.5, 0); break;
    case "star3": regular(3, -90, 0.3); break;
    case "star4": regular(4, -90, 0.4); break;
    case "star5": regular(5, -90, 0.382); break;
    case "star6": regular(6, -90, 0.55); break;
    case "arrow": polygon([[0, 0.3], [0.6, 0.3], [0.6, 0], [1, 0.5], [0.6, 1], [0.6, 0.7], [0, 0.7]]); break;
    case "notchedarrow": polygon([[0, 0.3], [0.6, 0.3], [0.6, 0], [1, 0.5], [0.6, 1], [0.6, 0.7], [0, 0.7], [0.15, 0.5]]); break;
    case "pentagonarrow": polygon([[0, 0], [0.75, 0], [1, 0.5], [0.75, 1], [0, 1]]); break;
    case "chevronarrow": polygon([[0, 0], [0.75, 0], [1, 0.5], [0.75, 1], [0, 1], [0.25, 0.5]]); break;
    case "rectcallout": polygon([[0, 0], [1, 0], [1, 0.75], [0.45, 0.75], [0.2, 1], [0.25, 0.75], [0, 0.75]]); break;
    case "roundedrectcallout": {
        const [bx, by] = at(0, 0), [ex, ey] = at(1, 0.75);
        ctx.roundRect(bx, by, ex - bx, ey - by, Math.min(ex - bx, ey - by) * 0.2);
        tail(0.75);
        break;
    }
    case "ellipsecallout": ellipse(0, 0, 1, 0.8); tail(0.72); break;
    case "cloudcallout":
        for (const [px, py] of [[0.25, 0.32], [0.45, 0.22], [0.68, 0.25], [0.8, 0.45], [0.65, 0.6], [0.4, 0.62], [0.2, 0.52]])
            ellipse(px - 0.2, py - 0.2, px + 0.2, py + 0.2);
        ellipse(0.12, 0.8, 0.2, 0.88);
        ellipse(0.02, 0.92, 0.07, 0.98);
        break;
    case "lightningbolt": polygon([[0.55, 0], [0.12, 0.56], [0.42, 0.56], [0.28, 1], [0.88, 0.38], [0.56, 0.38], [0.76, 0]]); break;
    case "checkmark": polygon([[0, 0.55], [0.15, 0.4], [0.38, 0.62], [0.85, 0.1], [1, 0.25], [0.38, 0.92]]); break;
    case "multiply":
        polygon([[0.15, 0], [0.5, 0.35], [0.85, 0], [1, 0.15], [0.65, 0.5], [1, 0.85], [0.85, 1], [0.5, 0.65], [0.15, 1],
                 [0, 0.85], [0.35, 0.5], [0, 0.15]]);
        break;
    case "gear": {
        const pts = [];
        for (let i = 0; i < 32; ++i) {
            const a = ((i * 360) / 32 - 90) * Math.PI / 180, k = i % 4 < 2 ? 0.5 : 0.38;
            pts.push([0.5 + k * Math.cos(a), 0.5 + k * Math.sin(a)]);
        }
        polygon(pts);
        ellipse(0.35, 0.35, 0.65, 0.65);
        break;
    }
    case "heart":
        ctx.moveTo(...at(0.5, 0.25));
        ctx.bezierCurveTo(...at(0.5, 0), ...at(0, 0), ...at(0, 0.3));
        ctx.bezierCurveTo(...at(0, 0.6), ...at(0.35, 0.75), ...at(0.5, 1));
        ctx.bezierCurveTo(...at(0.65, 0.75), ...at(1, 0.6), ...at(1, 0.3));
        ctx.bezierCurveTo(...at(1, 0), ...at(0.5, 0), ...at(0.5, 0.25));
        ctx.closePath();
        break;
    default: ctx.rect(x, y, w, h);
    }
}

export const shapeFillRule = (type) => (type === "gear" ? "evenodd" : "nonzero");

// --- Creación ------------------------------------------------------------------------------

const drawn = {
    rectangleselect: [20, drawRectSelect], ellipseselect: [20, drawEllipseSelect], lassoselect: [20, drawLasso],
    magicwand: [20, drawMagicWand], moveselectedpixels: [20, drawMove], moveselection: [20, drawMoveSelection],
    zoom: [20, drawZoom], eraser: [20, drawEraser], pencil: [20, drawPencil], colorpicker: [20, drawColorPicker],
    clonestamp: [20, drawCloneStamp], text: [20, drawTextTool], linecurve: [20, drawLineTool], shapes: [20, drawShapesTool],
    paintbrush: [20, drawBrush], paintbucket: [20, drawFill], recolor: [20, drawRecolor], gradient: [20, drawGradientTool],
    pan: [20, drawPan], "pan-grab": [20, drawPanGrab],
};

function painterFor(name) {
    if (drawn[name])
        return { design: drawn[name][0], paint: drawn[name][1] };
    if (name.startsWith("opt-")) {
        const [kind, ...args] = name.slice(4).split(":");
        if (options[kind])
            return { design: 32, paint: (ctx) => options[kind](ctx, ...args) };
    }
    return null;
}

function paintCanvas(canvas) {
    const painter = painterFor(canvas.dataset.icon);
    if (!painter)
        return;
    const size = Number(canvas.dataset.size);
    const dpr = window.devicePixelRatio || 1;
    canvas.width = Math.round(size * dpr);
    canvas.height = Math.round(size * dpr);
    const ctx = canvas.getContext("2d", { willReadFrequently: true });
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    ctx.scale(canvas.width / painter.design, canvas.height / painter.design);
    painter.paint(ctx);
}

// Iconos con su propia imagen (los de los efectos de los complementos).
const customIcons = new Map();
export function setCustomIcon(name, url) {
    if (url)
        customIcons.set(name, url);
    else
        customIcons.delete(name);
}

export function hasIcon(name) {
    return !!(files[name] || painterFor(name) || customIcons.has(name));
}

// El PNG de un paquete de iconos que sustituye al dibujo de una herramienta, si lo trae.
function packedTool(name) {
    return toolFiles[name] ? IconPacks.file("icons", toolFiles[name]) : null;
}

// Un icono de 'size' px: <img> para los PNG y <canvas> para los dibujados.
export function icon(name, size = 16) {
    if (files[name] || packedTool(name) || customIcons.has(name)) {
        const img = document.createElement("img");
        img.className = "kicon";
        img.dataset.icon = name;
        img.dataset.size = size;
        img.src = iconUrl(name);
        img.width = img.height = size;
        img.alt = "";
        img.draggable = false;
        return img;
    }
    if (painterFor(name)) {
        const canvas = document.createElement("canvas");
        canvas.className = "kicon";
        canvas.dataset.icon = name;
        canvas.dataset.size = size;
        canvas.style.width = canvas.style.height = `${size}px`;
        paintCanvas(canvas);
        return canvas;
    }
    const empty = document.createElement("span");
    empty.className = "kicon";
    empty.style.width = empty.style.height = `${size}px`;
    return empty;
}

export function iconUrl(name, size = 24) {
    if (customIcons.has(name))
        return customIcons.get(name);
    const packed = files[name] ? IconPacks.file("icons", files[name]) : packedTool(name);
    if (packed)
        return packed;
    if (files[name])
        return `assets/icons/${files[name]}${files[name].endsWith(".svg") ? "" : ".png"}`;
    const canvas = icon(name, size);
    return canvas.toDataURL ? canvas.toDataURL() : "";
}

// Vuelve a pintar los iconos (otros colores, otro tema u otro paquete de iconos): los dibujados
// se repintan y, si un paquete cambia un dibujo por su PNG (o al revés), se sustituye el elemento.
export function refreshIcons(root = document) {
    for (const element of root.querySelectorAll(".kicon[data-icon]")) {
        const name = element.dataset.icon, wantsImage = !!(files[name] || packedTool(name) || customIcons.has(name));
        if (wantsImage && element.tagName === "IMG")
            element.src = iconUrl(name);
        else if (!wantsImage && element.tagName === "CANVAS")
            paintCanvas(element);
        else
            element.replaceWith(icon(name, Number(element.dataset.size) || 16));
    }
}

window.addEventListener("kaint-iconpack", () => {
    for (const key of Object.keys(cursorCache))
        delete cursorCache[key];
    refreshIcons();
});

// Cursor de la mano (abierta o agarrando), dibujado como el icono de la herramienta.
const cursorCache = {};
export function handCursor(grabbing) {
    const key = grabbing ? "pan-grab" : "pan";
    const packed = IconPacks.file("cursors", grabbing ? "hand_closed_cursor" : "hand_open_cursor");
    if (packed)
        return `url(${packed}) 12 12, ${grabbing ? "grabbing" : "grab"}`;
    if (!cursorCache[key]) {
        const canvas = document.createElement("canvas");
        canvas.width = canvas.height = 24;
        const ctx = canvas.getContext("2d");
        ctx.scale(24 / 20, 24 / 20);
        (grabbing ? drawPanGrab : drawPan)(ctx);
        cursorCache[key] = `url(${canvas.toDataURL()}) 12 12, ${grabbing ? "grabbing" : "grab"}`;
    }
    return cursorCache[key];
}

// Punta de cada icono de herramienta en su rejilla de 20 px (como toolCursor de Kaint).
const cursorTips = {
    rectangleselect: [3, 3], ellipseselect: [3, 3], lassoselect: [12.2, 18.6], magicwand: [15.5, 4],
    moveselectedpixels: [2, 1], moveselection: [2, 1], zoom: [8, 8], paintbucket: [4, 18.6], gradient: [2.4, 2.4],
    paintbrush: [2.4, 18.4], eraser: [3.1, 14.8], pencil: [2, 18], colorpicker: [2.2, 17.8], clonestamp: [10, 17.2],
    recolor: [6, 14], linecurve: [3.4, 16.4], shapes: [1.6, 2.8],
};
const kCursor = 20;

// Imagen del icono a 'px' píxeles (el del paquete de iconos, si lo trae, o el dibujado).
function cursorImage(name, px) {
    const canvas = document.createElement("canvas");
    canvas.width = canvas.height = px;
    const painter = painterFor(name);
    if (painter) {
        const ctx = canvas.getContext("2d");
        ctx.scale(px / painter.design, px / painter.design);
        painter.paint(ctx);
    }
    return canvas.toDataURL();
}

// Cursor con el icono actual de una herramienta, del tamaño del puntero y con el punto activo en
// su punta; nítido también en pantallas de alta densidad.
export function iconCursor(name, hx = null, hy = null, fallback = "crosshair") {
    const tip = cursorTips[name] || [10, 10];
    const x = hx ?? Math.round((tip[0] * kCursor) / 20), y = hy ?? Math.round((tip[1] * kCursor) / 20);
    const packed = packedTool(name);
    if (packed)
        return `url(${packed}) ${x} ${y}, ${fallback}`;
    const key = `${name}/${primary ? css(primary) : ""}/${secondary ? css(secondary) : ""}`;
    if (!cursorCache[key])
        cursorCache[key] = { one: cursorImage(name, kCursor), two: cursorImage(name, kCursor * 2) };
    const c = cursorCache[key];
    return `-webkit-image-set(url(${c.one}) 1x, url(${c.two}) 2x) ${x} ${y}, ${fallback}`;
}

// Cursor de los pinceles con su contorno: el círculo del tamaño del pincel ('r' en píxeles CSS)
// alrededor del punto activo y el icono con la punta en el centro. Lo mueve el sistema, así que
// sigue al ratón sin el retraso de volver a dibujar el lienzo. Si no cabe en un cursor (128 px),
// devuelve null y el contorno se dibuja en el lienzo. Devuelve el cursor y la mitad de su lado.
export function outlineCursor(name, r) {
    const tip = cursorTips[name] || [10, 10];
    r = Math.round(r * 2) / 2;
    const half = Math.ceil(Math.max(r + 1.5, tip[0], kCursor - tip[0], tip[1], kCursor - tip[1]));
    if (2 * half > 128)
        return null;
    const key = `outline/${name}/${r}/${primary ? css(primary) : ""}/${secondary ? css(secondary) : ""}/${packedTool(name) || ""}`;
    if (!cursorCache[key]) {
        const packed = packedTool(name), img = packed && packImage(packed);
        const draw = (scale) => {
            const canvas = document.createElement("canvas");
            canvas.width = canvas.height = 2 * half * scale;
            const ctx = canvas.getContext("2d");
            ctx.scale(scale, scale);
            // Como el contorno que se dibujaba en el lienzo: blanco por fuera y negro por dentro.
            for (const [color, radius] of [["rgba(255,255,255,0.75)", r + 1], ["rgba(0,0,0,0.75)", r]]) {
                ctx.beginPath();
                ctx.arc(half, half, radius, 0, Math.PI * 2);
                ctx.strokeStyle = color;
                ctx.lineWidth = 1;
                ctx.stroke();
            }
            ctx.translate(half - tip[0], half - tip[1]);
            if (img) {
                ctx.drawImage(img, 0, 0, kCursor, kCursor);
            } else {
                const painter = painterFor(name);
                if (painter) {
                    ctx.scale(kCursor / painter.design, kCursor / painter.design);
                    painter.paint(ctx);
                }
            }
            return canvas.toDataURL();
        };
        cursorCache[key] = { css: `-webkit-image-set(url(${draw(1)}) 1x, url(${draw(2)}) 2x) ${half} ${half}, crosshair`, half };
    }
    return cursorCache[key];
}

// Imágenes de los iconos de paquete para dibujarlas en los cursores: hasta que cargan se usa el
// icono dibujado y luego se avisa para rehacer el cursor.
const packImages = new Map();
function packImage(url) {
    let img = packImages.get(url);
    if (!img) {
        img = new Image();
        img.onload = () => {
            for (const key of Object.keys(cursorCache))
                if (key.startsWith("mini/") || key.startsWith("outline/"))
                    delete cursorCache[key];
            window.dispatchEvent(new Event("kaint-cursor"));
        };
        img.src = url;
        packImages.set(url, img);
    }
    return img.complete && img.naturalWidth ? img : null;
}

// Puntero normal (la flecha o, con 'ibeam', la I del texto) con el icono 'name' en miniatura al
// lado, en una rejilla de 28 px; 'scale' da la versión de alta densidad.
function miniCursorImage(name, ibeam, scale) {
    const canvas = document.createElement("canvas");
    canvas.width = canvas.height = 28 * scale;
    const ctx = canvas.getContext("2d");
    ctx.scale(scale, scale);
    ctx.save();
    ctx.translate(0.5, 0.5);
    if (ibeam) {
        const trace = () => {
            ctx.beginPath();
            ctx.moveTo(1, 1); ctx.quadraticCurveTo(4, 1, 4, 3); ctx.quadraticCurveTo(4, 1, 7, 1);
            ctx.moveTo(4, 3); ctx.lineTo(4, 13);
            ctx.moveTo(1, 15); ctx.quadraticCurveTo(4, 15, 4, 13); ctx.quadraticCurveTo(4, 15, 7, 15);
        };
        ctx.lineCap = "round";
        for (const [color, width] of [["#fff", 3], ["#000", 1]]) {
            trace();
            stroke(ctx, color, width, "round", "round");
        }
    } else {
        ctx.beginPath();
        for (const [x, y] of [[0, 0], [0, 16], [4, 12], [7, 18], [9, 17], [6, 11], [11, 11]])
            ctx.lineTo(x, y);
        ctx.closePath();
        ctx.fillStyle = "#fff";
        ctx.fill();
        stroke(ctx, "#000", 1);
    }
    ctx.restore();
    const [ix, iy] = ibeam ? [9, 11] : [12, 13];
    ctx.translate(ix, iy);
    drawMini(ctx, name, kMini);
    return canvas.toDataURL();
}

// Lado de la miniatura junto al puntero.
const kMini = 14;

// La miniatura de 'name' de 'side' px desde (0, 0): el icono de la herramienta o, con Formas, la
// figura elegida.
function drawMini(ctx, name, side) {
    if (name.startsWith("opt-shape:")) {
        // La figura elegida, en negro con borde blanco para verse sobre cualquier imagen.
        for (const [color, width] of [["#fff", 3], ["#000", 1.2]]) {
            ctx.beginPath();
            shapePath(ctx, name.slice(10), 1, 2, side - 2, side - 4, 2.5);
            stroke(ctx, color, width, "round", "round");
        }
        return;
    }
    const packed = packedTool(name), img = packed && packImage(packed);
    if (img) {
        ctx.drawImage(img, 0, 0, side, side);
    } else {
        const painter = painterFor(name);
        if (painter) {
            ctx.scale(side / painter.design, side / painter.design);
            painter.paint(ctx);
        }
    }
}

// La miniatura que acompaña al puntero del sistema (la dibuja el lienzo en 'canvas').
export function paintMini(canvas, name) {
    const dpr = window.devicePixelRatio || 1;
    canvas.width = canvas.height = Math.round(kMini * dpr);
    canvas.style.width = canvas.style.height = `${kMini}px`;
    const ctx = canvas.getContext("2d");
    ctx.scale(dpr, dpr);
    drawMini(ctx, name, kMini);
}

// Como en Paint.NET: el puntero se sigue viendo y la herramienta va a su lado en miniatura (la
// selección, el degradado, la línea, el texto y las figuras). Con el puntero del sistema
// (Configuración), el cursor es la flecha o la I del sistema y la miniatura va aparte, junto a
// él: los navegadores no dejan leer la imagen del cursor del sistema para dibujarla al lado.
export function miniCursor(name, ibeam = false) {
    if (Settings.get("systemPointer") !== false) {
        const system = new String(ibeam ? "text" : "default");
        system.mini = { name, ibeam };
        return system;
    }
    const key = `mini/${ibeam ? "i" : "a"}/${name}/${primary ? css(primary) : ""}/${secondary ? css(secondary) : ""}`;
    if (!cursorCache[key])
        cursorCache[key] = { one: miniCursorImage(name, ibeam, 1), two: miniCursorImage(name, ibeam, 2) };
    const c = cursorCache[key], [x, y] = ibeam ? [4, 8] : [0, 0];
    return `-webkit-image-set(url(${c.one}) 1x, url(${c.two}) 2x) ${x} ${y}, ${ibeam ? "text" : "default"}`;
}

// Flecha curva de girar (como la de paint.js): 'angle' es la dirección, en radianes, desde el
// puntero hacia el centro del giro.
export function rotateCursor(angle) {
    const steps = 72, index = ((Math.round((angle / (2 * Math.PI)) * steps) % steps) + steps) % steps;
    const key = `rotate/${index}`;
    if (!cursorCache[key]) {
        const a = (index * 2 * Math.PI) / steps;
        const draw = (scale) => {
            const side = 40 * scale, canvas = document.createElement("canvas");
            canvas.width = canvas.height = side;
            const ctx = canvas.getContext("2d");
            ctx.translate(side / 2, side / 2);
            ctx.scale(scale, scale);
            const radius = 50, half = (16 * Math.PI) / 180, cx = Math.cos(a) * radius, cy = Math.sin(a) * radius;
            const middle = a + Math.PI, start = middle - half, end = middle + half;
            const on = (t) => [cx + Math.cos(t) * radius, cy + Math.sin(t) * radius];
            const trace = () => {
                ctx.beginPath();
                ctx.arc(cx, cy, radius, start, end);
                for (const [t, d] of [[start, -1], [end, 1]]) {
                    const [tx, ty] = on(t), gx = d * -Math.sin(t), gy = d * Math.cos(t), nx = -gy, ny = gx;
                    const bx = tx - gx * 4, by = ty - gy * 4;
                    ctx.moveTo(bx + nx * 2.25, by + ny * 2.25);
                    ctx.lineTo(tx, ty);
                    ctx.lineTo(bx - nx * 2.25, by - ny * 2.25);
                }
            };
            ctx.lineCap = ctx.lineJoin = "round";
            trace();
            ctx.strokeStyle = "#fff";
            ctx.lineWidth = 3.5;
            ctx.stroke();
            trace();
            ctx.strokeStyle = "#000";
            ctx.lineWidth = 1;
            ctx.stroke();
            return canvas.toDataURL();
        };
        cursorCache[key] = `-webkit-image-set(url(${draw(1)}) 1x, url(${draw(2)}) 2x) 20 20, alias`;
    }
    return cursorCache[key];
}

// Estrella naranja de ocho rayos de las imágenes con cambios sin guardar (como Paint.NET).
export function unsavedStar(size = 12) {
    const dpr = window.devicePixelRatio || 1, canvas = document.createElement("canvas");
    canvas.width = canvas.height = Math.round(size * dpr);
    canvas.style.width = canvas.style.height = `${size}px`;
    const ctx = canvas.getContext("2d");
    const px = size * dpr, scale = px / 32, c = px / 2;
    ctx.lineCap = "butt";
    for (const [color, width] of [["#fff", 8], ["#ffa500", 3]]) {
        ctx.strokeStyle = color;
        ctx.lineWidth = width * scale;
        ctx.beginPath();
        for (let i = 0; i < 8; ++i) {
            const r = (i * 2 * Math.PI) / 8;
            ctx.moveTo(c, c);
            ctx.lineTo(c + c * Math.sin(r), c + c * Math.cos(r));
        }
        ctx.stroke();
    }
    ctx.fillStyle = "#fff";
    ctx.beginPath();
    ctx.arc(c, c, 3 * scale, 0, Math.PI * 2);
    ctx.fill();
    return canvas;
}
