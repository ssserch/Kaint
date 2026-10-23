// Complemento de Paint.NET mínimo para las pruebas del puente: invierte los canales de color
// con un XOR configurable (Amount).

using System.Drawing;
using PaintDotNet;
using PaintDotNet.Effects;
using PaintDotNet.PropertySystem;

namespace KaintTest
{
    public sealed class XorEffect : PropertyBasedEffect
    {
        private int amount;

        public XorEffect()
            : base("Kaint XOR", null, "Kaint Test", EffectFlags.Configurable)
        {
        }

        protected override PropertyCollection OnCreatePropertyCollection()
        {
            return new PropertyCollection(new Property[] { new Int32Property("Amount", 255, 0, 255) });
        }

        protected override void OnSetRenderInfo(PropertyBasedEffectConfigToken newToken, RenderArgs dstArgs, RenderArgs srcArgs)
        {
            this.amount = newToken.GetProperty<Int32Property>("Amount").Value;
            base.OnSetRenderInfo(newToken, dstArgs, srcArgs);
        }

        protected override void OnRender(Rectangle[] rois, int startIndex, int length)
        {
            for (int i = startIndex; i < startIndex + length; ++i) {
                Rectangle r = rois[i];
                for (int y = r.Top; y < r.Bottom; ++y)
                    for (int x = r.Left; x < r.Right; ++x) {
                        ColorBgra c = SrcArgs.Surface[x, y];
                        DstArgs.Surface[x, y] = ColorBgra.FromBgra((byte)(c.B ^ amount), (byte)(c.G ^ amount),
                                                                   (byte)(c.R ^ amount), c.A);
                    }
            }
        }
    }
}
