// Iconos de Kaint: los PNG de la interfaz (assets/icons, la misma tabla que src/app/PngIcons.cpp)
// y los dibujados por código (src/app/PdnIcons.cpp e Icons.cpp): la barra de herramientas, las
// herramientas que llevan los colores activos y la mano. Los dibujados se vuelven a pintar al
// cambiar de tema o de colores.

const files = {
    rectangleselect: "rectangle_select_tool_icon", ellipseselect: "ellipse_select_tool_icon",
    lassoselect: "lasso_select_tool_icon", magicwand: "magic_wand_tool_icon", moveselectedpixels: "move_tool_icon",
    moveselection: "move_selection_tool_icon", zoom: "zoom_tool_icon", eraser: "eraser_tool_icon",
    pencil: "pencil_tool_icon", colorpicker: "color_picker_tool_icon", clonestamp: "clone_stamp_tool_icon",
    text: "text_tool_icon", linecurve: "line_tool_icon", shapes: "shapes_tool_icon_16",
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
    "settings-diagnostics": "settings_diagnostics_24",
    "status-selection": "selection_icon", "status-cursor": "cursor_x_y_icon", palette: "swatch_icon", "color-swap": "swap_icon",
    "dialog-warning": "warning_icon", "dialog-cancel": "cancel_icon",
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
const isDark = () => document.documentElement.classList.contains("dark");

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
function arrowArc(ctx, cx, cy, r, startDeg, spanDeg, width, fill, line) {
    const a0 = (-startDeg * Math.PI) / 180, a1 = (-(startDeg + spanDeg) * Math.PI) / 180;
    const end = ((startDeg + spanDeg) * Math.PI) / 180, dir = spanDeg >= 0 ? 1 : -1;
    const tip = [cx + Math.cos(end) * r, cy - Math.sin(end) * r];
    const tan = [-Math.sin(end) * dir, -Math.cos(end) * dir], rad = [Math.cos(end), -Math.sin(end)];
    const head = [[tip[0] + tan[0] * width * 1.35, tip[1] + tan[1] * width * 1.35],
                  [tip[0] + rad[0] * width * 1.2, tip[1] + rad[1] * width * 1.2],
                  [tip[0] - rad[0] * width * 1.2, tip[1] - rad[1] * width * 1.2]];
    for (const [color, extra] of [[line, 1.8], [fill, 0]]) {
        ctx.beginPath();
        ctx.arc(cx, cy, r, a0, a1, spanDeg > 0);
        stroke(ctx, color, width + extra, "butt", "round");
        poly(ctx, head);
        ctx.fillStyle = color;
        ctx.fill();
        if (extra) {
            ctx.lineWidth = extra;
            ctx.lineJoin = "round";
            ctx.strokeStyle = color;
            ctx.stroke();
        }
    }
}

function page(ctx, pts, fill, line = rgb(100, 100, 100)) {
    poly(ctx, pts);
    ctx.fillStyle = fill;
    ctx.fill();
    stroke(ctx, line, 1);
}

const toolbar = {
    "document-new"(ctx) {
        page(ctx, [[2.5, 0.5], [8.5, 0.5], [11.5, 3.5], [11.5, 9], [9, 14.5], [2.5, 14.5]], "#fff");
        page(ctx, [[8.5, 0.5], [8.5, 3.5], [11.5, 3.5]], rgb(210, 214, 222));
        ctx.beginPath();
        ctx.moveTo(12, 8.5); ctx.lineTo(12, 15.5); ctx.moveTo(8.5, 12); ctx.lineTo(15.5, 12);
        stroke(ctx, rgb(46, 160, 90), 2);
    },
    "document-open"(ctx) {
        page(ctx, [[0.5, 5.5], [0.5, 3.5], [5, 3.5], [6.5, 5], [13.5, 5], [13.5, 14.5], [0.5, 14.5]], rgb(214, 170, 96), rgb(150, 110, 50));
        page(ctx, [[0.5, 14.5], [2.5, 7.5], [15.5, 7.5], [13.5, 14.5]], rgb(240, 196, 120), rgb(150, 110, 50));
        arrowArc(ctx, 13.6, 6.6, 4.6, 180, -80, 1.6, rgb(46, 170, 120), rgb(20, 100, 70));
    },
    "document-save"(ctx) {
        page(ctx, [[0.5, 0.5], [12.5, 0.5], [15.5, 3.5], [15.5, 15.5], [0.5, 15.5]], rgb(150, 80, 190), rgb(80, 40, 110));
        ctx.fillStyle = rgb(236, 230, 244);
        ctx.fillRect(3.5, 8.5, 9, 7);
        ctx.strokeStyle = rgb(80, 40, 110);
        ctx.lineWidth = 1;
        ctx.strokeRect(3.5, 8.5, 9, 7);
        ctx.beginPath();
        ctx.moveTo(5, 11); ctx.lineTo(11, 11); ctx.moveTo(5, 13); ctx.lineTo(11, 13);
        stroke(ctx, rgb(150, 130, 170), 0.8);
        ctx.fillStyle = rgb(200, 170, 225);
        ctx.fillRect(4, 1, 7, 4);
    },
    "document-print"(ctx) {
        ctx.lineWidth = 1;
        ctx.strokeStyle = rgb(80, 80, 90);
        ctx.fillStyle = rgb(235, 238, 242);
        ctx.fillRect(4, 2, 8, 4); ctx.strokeRect(4, 2, 8, 4);
        roundRect(ctx, 2, 6, 12, 5, 1);
        ctx.fillStyle = rgb(120, 130, 145); ctx.fill(); ctx.stroke();
        ctx.fillStyle = "#fff";
        ctx.fillRect(4, 10, 8, 4); ctx.strokeRect(4, 10, 8, 4);
    },
    "edit-cut"(ctx) {
        ctx.fillStyle = rgb(112, 112, 112);
        poly(ctx, [[1.9, 0.2], [3.6, 0.2], [9.2, 8.4], [7.4, 9.6]]); ctx.fill();
        poly(ctx, [[14.1, 0.2], [12.4, 0.2], [6.8, 8.4], [8.6, 9.6]]); ctx.fill();
        const handle = rgb(60, 118, 178);
        for (const x of [3.5, 12.5]) {
            ctx.beginPath();
            ctx.ellipse(x, 12.8, 2.4, 2.2, 0, 0, Math.PI * 2);
            stroke(ctx, handle, 1.4, "round", "round");
        }
        ctx.beginPath();
        ctx.moveTo(7.4, 9.6); ctx.lineTo(5.7, 12.6); ctx.moveTo(8.6, 9.6); ctx.lineTo(10.3, 12.6);
        stroke(ctx, handle, 1.7, "round", "round");
        roundRect(ctx, 6.3, 9, 3.4, 2.4, 0.8);
        ctx.fillStyle = handle;
        ctx.fill();
    },
    "edit-copy"(ctx) {
        for (const [x, y, fill] of [[1.5, 0.5, rgb(226, 229, 235)], [5.5, 4.5, "#fff"]]) {
            page(ctx, [[x, y], [x + 6, y], [x + 9, y + 3], [x + 9, y + 11], [x, y + 11]], fill);
            page(ctx, [[x + 6, y], [x + 6, y + 3], [x + 9, y + 3]], rgb(200, 204, 212));
        }
    },
    "edit-paste"(ctx) {
        roundRect(ctx, 0.5, 1.5, 11, 13.5, 1);
        ctx.fillStyle = rgb(224, 178, 104); ctx.fill(); stroke(ctx, rgb(150, 110, 50), 1);
        roundRect(ctx, 3.5, 0.5, 5, 3, 1);
        ctx.fillStyle = rgb(170, 130, 70); ctx.fill(); stroke(ctx, rgb(150, 110, 50), 1);
        page(ctx, [[6.5, 5.5], [12, 5.5], [15.5, 9], [15.5, 15.5], [6.5, 15.5]], "#fff");
        page(ctx, [[12, 5.5], [12, 9], [15.5, 9]], rgb(200, 204, 212));
    },
    "transform-crop"(ctx) {
        ctx.fillStyle = rgb(150, 156, 166);
        ctx.fillRect(5, 5, 6, 6);
        ctx.beginPath();
        ctx.moveTo(4, 1); ctx.lineTo(4, 12); ctx.lineTo(15, 12); ctx.moveTo(12, 15); ctx.lineTo(12, 4); ctx.lineTo(1, 4);
        stroke(ctx, rgb(60, 60, 70), 1.4);
    },
    "edit-select-none"(ctx) {
        ctx.setLineDash([2, 2]);
        ctx.strokeStyle = rgb(110, 110, 120);
        ctx.lineWidth = 1;
        ctx.strokeRect(2.5, 2.5, 11, 11);
        ctx.setLineDash([]);
        ctx.beginPath();
        ctx.moveTo(5, 5); ctx.lineTo(11, 11); ctx.moveTo(11, 5); ctx.lineTo(5, 11);
        stroke(ctx, rgb(200, 60, 60), 1.6);
    },
    "edit-undo": (ctx) => arrowArc(ctx, 8.8, 9.2, 4.6, -40, 215, 2.6, rgb(52, 128, 232), rgb(18, 60, 140)),
    "edit-redo": (ctx) => arrowArc(ctx, 7.2, 9.2, 4.6, 220, -215, 2.6, rgb(52, 128, 232), rgb(18, 60, 140)),
    "view-grid"(ctx) {
        ctx.beginPath();
        for (let i = 1; i <= 13; i += 4) {
            ctx.moveTo(i + 0.5, 1); ctx.lineTo(i + 0.5, 14); ctx.moveTo(1, i + 0.5); ctx.lineTo(14, i + 0.5);
        }
        stroke(ctx, rgb(110, 120, 135), 1);
    },
    measure(ctx) {
        page(ctx, [[0.5, 0.5], [15.5, 0.5], [15.5, 5.5], [5.5, 5.5], [5.5, 15.5], [0.5, 15.5]], rgb(236, 196, 120), rgb(120, 90, 40));
        ctx.beginPath();
        for (let i = 7; i < 15; i += 3) {
            ctx.moveTo(i, 0.5); ctx.lineTo(i, 3); ctx.moveTo(0.5, i); ctx.lineTo(3, i);
        }
        stroke(ctx, rgb(120, 90, 40), 0.9);
    },
};
// En el tema oscuro, Kaint aclara los trazos oscuros de estos iconos (excepto deshacer/rehacer).
const lightenedInDark = new Set(Object.keys(toolbar).filter((k) => k !== "edit-undo" && k !== "edit-redo"));

// --- Opciones de herramienta (Icons.cpp: rejilla de 32 px con un margen de 3) -------------

const options = {
    gradient(ctx, type) {
        const a = fg();
        const r = [3, 3, 26, 26];
        const clear = "rgba(0,0,0,0)";
        let fill = null;
        if (type === "linear" || type === "reflected") {
            fill = ctx.createLinearGradient(r[0], 0, r[0] + r[2], 0);
            if (type === "linear") { fill.addColorStop(0, a); fill.addColorStop(1, clear); }
            else { fill.addColorStop(0, clear); fill.addColorStop(0.5, a); fill.addColorStop(1, clear); }
        } else if (type === "radial" || type === "diamond") {
            fill = ctx.createRadialGradient(16, 16, 0, 16, 16, 13);
            fill.addColorStop(0, a); fill.addColorStop(1, clear);
        } else if (type === "conical") {
            fill = ctx.createConicGradient(-Math.PI / 2, 16, 16);
            fill.addColorStop(0, a); fill.addColorStop(1, clear);
        }
        if (type === "diamond") {
            poly(ctx, [[16, 3], [29, 16], [16, 29], [3, 16]]);
            ctx.fillStyle = fill; ctx.fill();
        } else if (type === "spiral" || type === "spiralccw") {
            ctx.beginPath();
            for (let i = 0; i <= 60; ++i) {
                const t = i / 60, ang = (type === "spiral" ? 1 : -1) * t * 4 * Math.PI;
                const x = 16 + Math.cos(ang) * t * 13, y = 16 + Math.sin(ang) * t * 13;
                i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
            }
            stroke(ctx, a, 2);
        } else {
            ctx.fillStyle = fill;
            ctx.fillRect(...r);
        }
        ctx.strokeStyle = a; ctx.lineWidth = 1; ctx.strokeRect(...r);
    },
    shape(ctx, type) {
        ctx.beginPath();
        shapePath(ctx, type, 4, 5, 24, 22, 4);
        stroke(ctx, fg(), 1.6);
    },
    style(ctx, style) {
        const box = [5, 8, 22, 16];
        if (style !== "outline") { ctx.fillStyle = style === "fill" ? fg() : accent(); ctx.fillRect(...box); }
        if (style !== "fill") { ctx.strokeStyle = fg(); ctx.lineWidth = 2.5; ctx.strokeRect(...box); }
    },
    curve(ctx, type) {
        const a = [3, 25], b = [12, 5], c = [20, 27], d = [29, 7];
        ctx.beginPath();
        ctx.moveTo(...a);
        if (type === "straight") ctx.lineTo(...d);
        else ctx.bezierCurveTo(...b, ...c, ...d);
        stroke(ctx, fg(), 2);
        if (type === "bezier") {
            ctx.setLineDash([1, 2]);
            ctx.beginPath(); ctx.moveTo(...a); ctx.lineTo(...b); ctx.moveTo(...c); ctx.lineTo(...d);
            stroke(ctx, accent(), 1);
            ctx.setLineDash([]);
        }
    },
    cap(ctx, cap, end) {
        const y = 16, start = end !== "end", tip = start ? 5 : 27, dir = start ? 1 : -1;
        ctx.beginPath();
        ctx.moveTo(tip + dir * (cap === "flat" || cap === "round" ? 0 : 6), y);
        ctx.lineTo(start ? 29 : 3, y);
        stroke(ctx, fg(), 4, cap === "round" ? "round" : "butt");
        if (cap === "arrow" || cap === "filled") {
            const head = [[tip, y], [tip + dir * 10, y - 7], [tip + dir * 10, y + 7]];
            if (cap === "filled") { poly(ctx, head); ctx.fillStyle = fg(); ctx.fill(); }
            else { poly(ctx, [head[1], head[0], head[2]], false); stroke(ctx, fg(), 2.5); }
        }
    },
    aa(ctx, on) {
        ctx.save();
        if (on === "1") {
            ctx.beginPath();
            ctx.moveTo(5, 6);
            ctx.bezierCurveTo(16, 5, 14, 27, 27, 26);
            stroke(ctx, fg(), 2.5, "round", "round");
        } else {
            ctx.beginPath();
            for (let i = 0; i <= 6; ++i) {
                const x = 5 + (22 * i) / 6, yy = 6 + (20 * i) / 6;
                i ? ctx.lineTo(x, yy) : ctx.moveTo(x, yy);
                ctx.lineTo(x + 22 / 6, yy);
            }
            stroke(ctx, fg(), 2.5, "round", "round");
        }
        ctx.fillStyle = accent();
        for (const [x, y] of [[5, 6], [27, 26]]) { ctx.beginPath(); ctx.arc(x, y, 2.6, 0, Math.PI * 2); ctx.fill(); }
        ctx.restore();
    },
    selquality(ctx, on) {
        const ac = accent();
        ctx.beginPath();
        ctx.ellipse(16, 16, 11, 11, 0, 0, Math.PI * 2);
        ctx.globalAlpha = 0.5; ctx.fillStyle = ac; ctx.fill(); ctx.globalAlpha = 1;
        ctx.setLineDash([2, 2]);
        stroke(ctx, fg(), 1);
        ctx.setLineDash([]);
        if (on !== "1") {
            ctx.fillStyle = fg();
            for (const [x, y] of [[8, 6], [24, 6], [8, 24], [24, 24]]) ctx.fillRect(x, y, 2, 2);
        }
    },
    selmode(ctx, mode) {
        const a = [3, 3, 16, 16], b = [13, 13, 16, 16];
        ctx.save();
        ctx.fillStyle = accent();
        const rect = (r) => { ctx.beginPath(); ctx.rect(...r); };
        if (mode === "replace") { rect(b); ctx.fill(); }
        else if (mode === "union") { rect(a); ctx.fill(); rect(b); ctx.fill(); }
        else if (mode === "exclude") { ctx.beginPath(); ctx.rect(...a); ctx.rect(13, 13, 6, 6); ctx.fill("evenodd"); }
        else if (mode === "intersect") ctx.fillRect(13, 13, 6, 6);
        else if (mode === "xor") { ctx.beginPath(); ctx.rect(...a); ctx.rect(...b); ctx.rect(13, 13, 6, 6); ctx.fill("evenodd");
                                   ctx.fillStyle = accent(); }
        ctx.setLineDash([2, 2]);
        ctx.strokeStyle = fg(); ctx.lineWidth = 1;
        ctx.strokeRect(a[0] + 0.5, a[1] + 0.5, a[2], a[3]);
        ctx.strokeRect(b[0] + 0.5, b[1] + 0.5, b[2], b[3]);
        ctx.restore();
    },
    dash(ctx, style) {
        const pattern = { solid: [], dash: [7, 4], dot: [2.5, 3], dashdot: [7, 3, 2.5, 3], dashdotdot: [7, 3, 2.5, 3, 2.5, 3] }[style] || [];
        ctx.setLineDash(pattern);
        ctx.beginPath(); ctx.moveTo(3, 16); ctx.lineTo(29, 16);
        stroke(ctx, fg(), 2.5);
        ctx.setLineDash([]);
    },
    flood(ctx, global) {
        const c = 26 / 4;
        for (let j = 0; j < 4; ++j)
            for (let i = 0; i < 4; ++i) {
                const same = (i + j) % 3 === 0 || (i < 2 && j < 2);
                const filled = global === "1" ? same : (i < 2 && j < 2) || (i === 2 && j === 0);
                ctx.fillStyle = filled ? accent() : same ? fg() : "rgba(128,128,128,0.3)";
                ctx.fillRect(3 + i * c, 3 + j * c, c - 1, c - 1);
            }
    },
    rectmode(ctx, mode) {
        ctx.save();
        ctx.setLineDash([3, 3]);
        ctx.fillStyle = accent(); ctx.globalAlpha = 0.35;
        ctx.fillRect(4, 7, 21, 18);
        ctx.globalAlpha = 1;
        ctx.strokeStyle = fg(); ctx.lineWidth = 1.5; ctx.strokeRect(4, 7, 21, 18);
        ctx.restore();
        if (mode === "normal") return;
        ctx.fillStyle = accent();
        if (mode === "ratio") { ctx.fillRect(18, 23, 6, 6); ctx.fillRect(23, 18, 6, 6); }
        else { ctx.fillRect(18, 22, 11, 7); ctx.beginPath(); ctx.arc(23.5, 21, 3.5, Math.PI, 0); stroke(ctx, accent(), 1.6); }
    },
    sample(ctx, n) {
        ctx.strokeStyle = fg(); ctx.lineWidth = 1; ctx.strokeRect(4.5, 4.5, 23, 23);
        const side = Math.min(19, 2.5 + Math.log2(Math.max(1, Number(n))) * 1.6);
        ctx.fillStyle = accent();
        ctx.fillRect(16 - side / 2, 16 - side / 2, side, side);
    },
    stepper(ctx, sign) {
        ctx.strokeStyle = fg(); ctx.lineWidth = 1; ctx.strokeRect(4.5, 4.5, 23, 23);
        ctx.beginPath(); ctx.moveTo(10, 16); ctx.lineTo(22, 16);
        if (sign === "plus") { ctx.moveTo(16, 10); ctx.lineTo(16, 22); }
        stroke(ctx, fg(), 1.5);
    },
    blend(ctx) {
        // Matraz, como el de Paint.NET.
        const top = 3, bottom = 29, neck = top + 26 * 0.38;
        poly(ctx, [[13, top], [13, neck], [4, bottom], [28, bottom], [19, neck], [19, top]]);
        ctx.save();
        ctx.clip();
        ctx.fillStyle = accent();
        ctx.fillRect(0, top + 26 * 0.62, 32, 32);
        ctx.restore();
        poly(ctx, [[13, top], [13, neck], [4, bottom], [28, bottom], [19, neck], [19, top]]);
        stroke(ctx, fg(), 1.6, "round", "round");
    },
    recolor(ctx, mode) {
        if (mode === "once") {
            ctx.beginPath(); ctx.arc(16, 16, 8.3, 0, Math.PI * 2); stroke(ctx, fg(), 2);
            ctx.beginPath();
            ctx.moveTo(16, 3); ctx.lineTo(16, 9); ctx.moveTo(16, 29); ctx.lineTo(16, 23);
            ctx.moveTo(3, 16); ctx.lineTo(9, 16); ctx.moveTo(29, 16); ctx.lineTo(23, 16);
            stroke(ctx, fg(), 2);
            ctx.fillStyle = accent(); ctx.beginPath(); ctx.arc(16, 16, 2.5, 0, Math.PI * 2); ctx.fill();
        } else {
            ctx.lineWidth = 1.2; ctx.strokeStyle = fg();
            ctx.fillStyle = "#000"; ctx.fillRect(3, 3, 16, 16); ctx.strokeRect(3, 3, 16, 16);
            ctx.fillStyle = "#fff"; ctx.fillRect(13, 13, 16, 16); ctx.strokeRect(13, 13, 16, 16);
        }
    },
    align(ctx, align) {
        ctx.beginPath();
        for (let i = 0; i < 5; ++i) {
            const w = i % 2 ? 14 : 22, y = 6.5 + i * 5;
            const x = align === "left" ? 5 : align === "right" ? 27 - w : 16 - w / 2;
            ctx.moveTo(x, y); ctx.lineTo(x + w, y);
        }
        stroke(ctx, fg(), 2);
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
    paintbrush: [20, drawBrush], paintbucket: [20, drawFill], recolor: [20, drawRecolor], gradient: [20, drawGradientTool],
    pan: [20, drawPan], "pan-grab": [20, drawPanGrab],
};

function painterFor(name) {
    if (drawn[name])
        return { design: drawn[name][0], paint: drawn[name][1] };
    if (name.startsWith("toolbar-")) {
        const key = name.slice(8);
        if (toolbar[key])
            return { design: 16, paint: toolbar[key], lighten: lightenedInDark.has(key) };
    }
    if (name.startsWith("opt-")) {
        const [kind, ...args] = name.slice(4).split(":");
        if (options[kind])
            return { design: 32, paint: (ctx) => options[kind](ctx, ...args) };
    }
    return null;
}

// Aclara los trazos oscuros conservando el tono (como "lightened" de PdnIcons.cpp).
function lightenDark(ctx, w, h) {
    const img = ctx.getImageData(0, 0, w, h), d = img.data;
    for (let i = 0; i < d.length; i += 4) {
        if (!d[i + 3])
            continue;
        const r = d[i] / 255, g = d[i + 1] / 255, b = d[i + 2] / 255;
        const max = Math.max(r, g, b), min = Math.min(r, g, b), l = (max + min) / 2;
        if (l * 255 >= 150)
            continue;
        let h = 0, s = 0;
        if (max !== min) {
            const dd = max - min;
            s = l > 0.5 ? dd / (2 - max - min) : dd / (max + min);
            h = max === r ? (g - b) / dd + (g < b ? 6 : 0) : max === g ? (b - r) / dd + 2 : (r - g) / dd + 4;
            h *= 60;
        }
        const nl = (235 - l * 255) / 255;
        const f = (n) => {
            const k = (n + h / 30) % 12, a = s * Math.min(nl, 1 - nl);
            return Math.round((nl - a * Math.max(-1, Math.min(k - 3, 9 - k, 1))) * 255);
        };
        d[i] = f(0); d[i + 1] = f(8); d[i + 2] = f(4);
    }
    ctx.putImageData(img, 0, 0);
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
    if (painter.lighten && isDark())
        lightenDark(ctx, canvas.width, canvas.height);
}

export function hasIcon(name) {
    return !!(files[name] || painterFor(name));
}

// Un icono de 'size' px: <img> para los PNG y <canvas> para los dibujados.
export function icon(name, size = 16) {
    if (files[name]) {
        const img = document.createElement("img");
        img.className = "kicon";
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
    if (files[name])
        return `assets/icons/${files[name]}${files[name].endsWith(".svg") ? "" : ".png"}`;
    const canvas = icon(name, size);
    return canvas.toDataURL ? canvas.toDataURL() : "";
}

export function refreshIcons(root = document) {
    for (const canvas of root.querySelectorAll("canvas.kicon[data-icon]"))
        paintCanvas(canvas);
}

// Cursor de la mano (abierta o agarrando), dibujado como el icono de la herramienta.
const cursorCache = {};
export function handCursor(grabbing) {
    const key = grabbing ? "pan-grab" : "pan";
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

// Cursor con el icono de una herramienta y su punto activo.
export function iconCursor(name, hx, hy, fallback = "crosshair") {
    return `url(${iconUrl(name, 24)}) ${hx} ${hy}, ${fallback}`;
}
