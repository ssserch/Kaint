# Interface icons

The PNG icons in this folder (menus, the main toolbar, the tool options, the Layers and History
windows, adjustments, effects and settings pages) come from SSSerch's fork of
[paint.js](https://github.com/LabyStudio/paintdotjs) and are used in Kaint with their author's
permission. Kaint scales them to the size it needs and turns them gray when an option is disabled.

The icons of the Tools window are still drawn in code (`src/app/PdnIcons.cpp`): Paintbrush,
Paint Bucket, Recolor and Gradient show the active colors. The tool PNGs here are used for the
Zoom and Color Picker cursors.

An icon pack (Settings → Icon packs) replaces any of these files, and also the drawn tool icons,
with its own `icons/<file>.png`.
