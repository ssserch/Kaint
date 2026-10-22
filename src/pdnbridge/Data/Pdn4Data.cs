// Kaint: tipos de PaintDotNet.Data que añadió Paint.NET 4.x.

using System;
using PaintDotNet.Rendering;

namespace PaintDotNet
{
    // Modos de fusión de capa de Paint.NET 4 (mismo orden que UserBlendOps).
    public enum LayerBlendMode
    {
        Normal,
        Multiply,
        Additive,
        ColorBurn,
        ColorDodge,
        Reflect,
        Glow,
        Overlay,
        Difference,
        Negation,
        Lighten,
        Darken,
        Screen,
        Xor
    }

    public static class LayerBlendModeUtil
    {
        private sealed class UserBlendCompositionOp
            : CompositionOp
        {
            private readonly UserBlendOp op;
            private readonly byte opacity;

            public UserBlendCompositionOp(UserBlendOp op, byte opacity)
            {
                this.op = op;
                this.opacity = opacity;
            }

            public override ColorBgra Apply(ColorBgra lhs, ColorBgra rhs)
            {
                if (this.opacity != 255)
                    rhs.A = (byte)((rhs.A * this.opacity + 127) / 255);
                return this.op.Apply(lhs, rhs);
            }
        }

        public static UserBlendOp CreateBlendOp(LayerBlendMode mode)
        {
            switch (mode)
            {
                case LayerBlendMode.Multiply: return new UserBlendOps.MultiplyBlendOp();
                case LayerBlendMode.Additive: return new UserBlendOps.AdditiveBlendOp();
                case LayerBlendMode.ColorBurn: return new UserBlendOps.ColorBurnBlendOp();
                case LayerBlendMode.ColorDodge: return new UserBlendOps.ColorDodgeBlendOp();
                case LayerBlendMode.Reflect: return new UserBlendOps.ReflectBlendOp();
                case LayerBlendMode.Glow: return new UserBlendOps.GlowBlendOp();
                case LayerBlendMode.Overlay: return new UserBlendOps.OverlayBlendOp();
                case LayerBlendMode.Difference: return new UserBlendOps.DifferenceBlendOp();
                case LayerBlendMode.Negation: return new UserBlendOps.NegationBlendOp();
                case LayerBlendMode.Lighten: return new UserBlendOps.LightenBlendOp();
                case LayerBlendMode.Darken: return new UserBlendOps.DarkenBlendOp();
                case LayerBlendMode.Screen: return new UserBlendOps.ScreenBlendOp();
                case LayerBlendMode.Xor: return new UserBlendOps.XorBlendOp();
                default: return new UserBlendOps.NormalBlendOp();
            }
        }

        public static CompositionOp CreateCompositionOp(LayerBlendMode blendMode, byte opacity)
        {
            return new UserBlendCompositionOp(CreateBlendOp(blendMode), opacity);
        }
    }
}
