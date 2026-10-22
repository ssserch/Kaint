// Kaint: tipos de PaintDotNet.Effects que añadió Paint.NET 4.x y que usan
// los complementos de esa época.

using System;
using System.Drawing;

namespace PaintDotNet.Effects
{
    public enum EffectRenderingSchedule
    {
        DefaultTilesForCpuRendering = 0,
        SmallHorizontalStrips = 1,
        None = 2
    }

    public sealed class EffectOptions
    {
        public EffectFlags Flags { get; set; }
        public EffectRenderingSchedule RenderingSchedule { get; set; }
    }

    // Nombres de los submenús integrados, con los textos de Paint.NET en
    // español: así los efectos de los complementos se juntan con los de Kaint.
    public static class SubmenuNames
    {
        public static string Artistic { get { return "Artístico"; } }
        public static string Blurs { get { return "Desenfoques"; } }
        public static string Distort { get { return "Distorsionar"; } }
        public static string Noise { get { return "Ruido"; } }
        public static string Photo { get { return "Fotografía"; } }
        public static string Render { get { return "Representación"; } }
        public static string Stylize { get { return "Estilizado"; } }
        public static string Object { get { return "Objeto"; } }
        public static string Color { get { return "Color"; } }
    }

    // Giro y zum integrado: algunos complementos solo lo nombran.
    public sealed class RotateZoomEffect
        : Effect
    {
        public RotateZoomEffect()
            : base("Giro y zum", null, null, EffectFlags.None)
        {
        }

        public override void Render(EffectConfigToken parameters, RenderArgs dstArgs, RenderArgs srcArgs, Rectangle[] rois, int startIndex, int length)
        {
            dstArgs.Surface.CopySurface(srcArgs.Surface, rois, startIndex, length);
        }
    }
}
