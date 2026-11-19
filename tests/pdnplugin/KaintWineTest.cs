// Complemento de Paint.NET para probar el respaldo de Wine: sus dos efectos llaman a kernel32.dll,
// que solo existe en Windows (y en Wine). Con mono en Linux, "Kaint Wine load" no se puede crear
// (falla al cargar el complemento) y "Kaint Wine render" falla al aplicarse; con Wine funcionan.
// Los dos ponen el rojo a 255.

using System.Drawing;
using System.Runtime.InteropServices;
using PaintDotNet;
using PaintDotNet.Effects;

namespace KaintWineTest
{
    internal static class Windows
    {
        [DllImport("kernel32.dll")]
        public static extern uint GetCurrentProcessId();
    }

    public sealed class WineLoadEffect : Effect
    {
        public WineLoadEffect()
            : base("Kaint Wine load", null, "Kaint Test", EffectFlags.None)
        {
            Windows.GetCurrentProcessId();
        }

        public override void Render(EffectConfigToken parameters, RenderArgs dstArgs, RenderArgs srcArgs, Rectangle[] rois,
                                    int startIndex, int length)
        {
            RedRender.Render(dstArgs, srcArgs, rois, startIndex, length);
        }
    }

    public sealed class WineRenderEffect : Effect
    {
        public WineRenderEffect()
            : base("Kaint Wine render", null, "Kaint Test", EffectFlags.None)
        {
        }

        public override void Render(EffectConfigToken parameters, RenderArgs dstArgs, RenderArgs srcArgs, Rectangle[] rois,
                                    int startIndex, int length)
        {
            Windows.GetCurrentProcessId();
            RedRender.Render(dstArgs, srcArgs, rois, startIndex, length);
        }
    }

    internal static class RedRender
    {
        public static void Render(RenderArgs dstArgs, RenderArgs srcArgs, Rectangle[] rois, int startIndex, int length)
        {
            for (int i = startIndex; i < startIndex + length; ++i) {
                Rectangle r = rois[i];
                for (int y = r.Top; y < r.Bottom; ++y)
                    for (int x = r.Left; x < r.Right; ++x) {
                        ColorBgra c = srcArgs.Surface[x, y];
                        dstArgs.Surface[x, y] = ColorBgra.FromBgra(c.B, c.G, 255, c.A);
                    }
            }
        }
    }
}
