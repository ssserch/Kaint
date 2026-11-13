// Kaint: efectos integrados de Paint.NET 3.x que algunos complementos crean y ejecutan por dentro
// (Burninate de BoltBait pinta su fuego sobre las Nubes; Combined Adjustments de PhotoAdjustments
// usa Reducir ruido). Mismos nombres, propiedades y valores que los de Paint.NET.
//
// Escritos para Kaint a partir de la API pública de Paint.NET 3.x (no es su código).

using PaintDotNet.IndirectUI;
using PaintDotNet.PropertySystem;
using System;
using System.Collections.Generic;
using System.Drawing;

namespace PaintDotNet.Effects
{
    // Elección del modo de fusión que acepta el tipo (como Paint.NET) o una instancia del modo
    // (Burninate le pasa la suya).
    internal sealed class KaintBlendOpChoiceProperty
        : StaticListChoiceProperty
    {
        public KaintBlendOpChoiceProperty(object name, object[] choices, int defaultChoiceIndex)
            : base(name, choices, defaultChoiceIndex, false)
        {
        }

        private KaintBlendOpChoiceProperty(KaintBlendOpChoiceProperty cloneMe, KaintBlendOpChoiceProperty sentinelNotUsed)
            : base(cloneMe, sentinelNotUsed)
        {
        }

        public override Property Clone()
        {
            return new KaintBlendOpChoiceProperty(this, this);
        }

        protected override bool ValidateNewValueT(object newValue)
        {
            return newValue is UserBlendOp || base.ValidateNewValueT(newValue);
        }

        protected override object OnClampNewValueT(object newValue)
        {
            return newValue is UserBlendOp ? newValue : base.OnClampNewValueT(newValue);
        }

        public static UserBlendOp Create(object value)
        {
            UserBlendOp op = value as UserBlendOp;
            if (op != null)
                return op;
            Type type = value as Type;
            return type != null ? UserBlendOps.CreateBlendOp(type) : UserBlendOps.CreateDefaultBlendOp();
        }
    }

    // Nubes (Efectos > Representación): ruido de Perlin entre el color principal y el secundario,
    // fusionado con la imagen.
    public sealed class CloudsEffect
        : InternalPropertyBasedEffect
    {
        public static string StaticName
        {
            get { return PdnResources.GetString("CloudsEffect.Name"); }
        }

        public static ImageResource StaticImage
        {
            get { return PdnResources.GetImageResource("Icons.CloudsEffect.png"); }
        }

        public enum PropertyNames
        {
            Scale = 0,
            Power = 1,
            BlendOp = 2,
            Seed = 3
        }

        private static readonly int[] permutation = {
            151, 160, 137, 91, 90, 15, 131, 13, 201, 95, 96, 53, 194, 233, 7, 225, 140, 36, 103, 30, 69, 142,
            8, 99, 37, 240, 21, 10, 23, 190, 6, 148, 247, 120, 234, 75, 0, 26, 197, 62, 94, 252, 219, 203,
            117, 35, 11, 32, 57, 177, 33, 88, 237, 149, 56, 87, 174, 20, 125, 136, 171, 168, 68, 175, 74, 165,
            71, 134, 139, 48, 27, 166, 77, 146, 158, 231, 83, 111, 229, 122, 60, 211, 133, 230, 220, 105, 92, 41,
            55, 46, 245, 40, 244, 102, 143, 54, 65, 25, 63, 161, 1, 216, 80, 73, 209, 76, 132, 187, 208, 89,
            18, 169, 200, 196, 135, 130, 116, 188, 159, 86, 164, 100, 109, 198, 173, 186, 3, 64, 52, 217, 226, 250,
            124, 123, 5, 202, 38, 147, 118, 126, 255, 82, 85, 212, 207, 206, 59, 227, 47, 16, 58, 17, 182, 189,
            28, 42, 223, 183, 170, 213, 119, 248, 152, 2, 44, 154, 163, 70, 221, 153, 101, 155, 167, 43, 172, 9,
            129, 22, 39, 253, 19, 98, 108, 110, 79, 113, 224, 232, 178, 185, 112, 104, 218, 246, 97, 228, 251, 34,
            242, 193, 238, 210, 144, 12, 191, 179, 162, 241, 81, 51, 145, 235, 249, 14, 239, 107, 49, 192, 214, 31,
            181, 199, 106, 157, 184, 84, 204, 176, 115, 121, 50, 45, 127, 4, 150, 254, 138, 236, 205, 93, 222, 114,
            67, 29, 24, 72, 243, 141, 128, 195, 78, 66, 215, 61, 156, 180,
        };

        private static readonly int[] permuteLookup;

        static CloudsEffect()
        {
            permuteLookup = new int[512];
            for (int i = 0; i < 256; ++i)
            {
                permuteLookup[i] = permutation[i];
                permuteLookup[256 + i] = permutation[i];
            }
        }

        private int scale;
        private double power;
        private int seed;
        private UserBlendOp blendOp;

        public CloudsEffect()
            : base(StaticName, StaticImage.Reference, SubmenuNames.Render, EffectFlags.Configurable)
        {
        }

        protected override PropertyCollection OnCreatePropertyCollection()
        {
            List<Property> props = new List<Property>();
            props.Add(new Int32Property(PropertyNames.Scale, 250, 2, 1000));
            props.Add(new DoubleProperty(PropertyNames.Power, 0.5, 0.0, 1.0));
            Type[] blendOps = UserBlendOps.GetBlendOps();
            int normal = Math.Max(0, Array.IndexOf(blendOps, UserBlendOps.GetDefaultBlendOp()));
            props.Add(new KaintBlendOpChoiceProperty(PropertyNames.BlendOp, blendOps, normal));
            props.Add(new Int32Property(PropertyNames.Seed, 0, 0, 255));
            return new PropertyCollection(props);
        }

        protected override ControlInfo OnCreateConfigUI(PropertyCollection props)
        {
            return CreateDefaultConfigUI(props);
        }

        protected override void OnSetRenderInfo(PropertyBasedEffectConfigToken newToken, RenderArgs dstArgs, RenderArgs srcArgs)
        {
            this.scale = newToken.GetProperty<Int32Property>(PropertyNames.Scale).Value;
            this.power = newToken.GetProperty<DoubleProperty>(PropertyNames.Power).Value;
            this.seed = newToken.GetProperty<Int32Property>(PropertyNames.Seed).Value;
            this.blendOp = KaintBlendOpChoiceProperty.Create(newToken.GetProperty<StaticListChoiceProperty>(PropertyNames.BlendOp).Value);
            base.OnSetRenderInfo(newToken, dstArgs, srcArgs);
        }

        private static double Fade(double t)
        {
            return t * t * t * (t * (t * 6 - 15) + 10);
        }

        private static double Grad(int hash, double x, double y)
        {
            int h = hash & 15;
            double u = h < 8 ? x : y;
            double v = h < 4 ? y : (h == 12 || h == 14 ? x : 0);
            return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
        }

        private static double Lerp(double t, double a, double b)
        {
            return a + t * (b - a);
        }

        private static double Noise(byte ix, byte iy, double x, double y, byte seed)
        {
            double u = Fade(x);
            double v = Fade(y);
            int a = permuteLookup[ix + seed] + iy;
            int aa = permuteLookup[a];
            int ab = permuteLookup[a + 1];
            int b = permuteLookup[ix + 1 + seed] + iy;
            int ba = permuteLookup[b];
            int bb = permuteLookup[b + 1];
            double gradAA = Grad(permuteLookup[aa], x, y);
            double gradBA = Grad(permuteLookup[ba], x - 1, y);
            double gradAB = Grad(permuteLookup[ab], x, y - 1);
            double gradBB = Grad(permuteLookup[bb], x - 1, y - 1);
            return Lerp(v, Lerp(u, gradAA, gradBA), Lerp(u, gradAB, gradBB));
        }

        private static unsafe void RenderClouds(Surface surface, Rectangle rect, int scale, byte seed, double power,
                                                ColorBgra colorFrom, ColorBgra colorTo)
        {
            int w = surface.Width;
            int h = surface.Height;
            for (int y = rect.Top; y < rect.Bottom; ++y)
            {
                ColorBgra* ptr = surface.GetPointAddressUnchecked(rect.Left, y);
                int dy = 2 * y - h;
                for (int x = rect.Left; x < rect.Right; ++x)
                {
                    int dx = 2 * x - w;
                    double val = 0;
                    double mult = 1;
                    int div = scale;
                    for (int i = 0; i < 12 && mult > 0.03 && div > 0; ++i)
                    {
                        double dxr = 65536 + (double)dx / div;
                        double dyr = 65536 + (double)dy / div;
                        int dxd = (int)dxr;
                        int dyd = (int)dyr;
                        dxr -= dxd;
                        dyr -= dyd;
                        val += Noise(unchecked((byte)dxd), unchecked((byte)dyd), dxr, dyr, unchecked((byte)(seed + i))) * mult;
                        div /= 2;
                        mult *= power;
                    }
                    *ptr = ColorBgra.Lerp(colorFrom, colorTo, Math.Max(0.0, Math.Min(1.0, (val + 1) / 2)));
                    ++ptr;
                }
            }
        }

        protected override void OnRender(Rectangle[] rois, int startIndex, int length)
        {
            Surface dst = DstArgs.Surface;
            Surface src = SrcArgs.Surface;
            ColorBgra from = EnvironmentParameters.PrimaryColor;
            ColorBgra to = EnvironmentParameters.SecondaryColor;
            UserBlendOp op = this.blendOp ?? UserBlendOps.CreateDefaultBlendOp();
            for (int i = startIndex; i < startIndex + length; ++i)
            {
                Rectangle rect = Rectangle.Intersect(rois[i], dst.Bounds);
                if (rect.Width <= 0 || rect.Height <= 0)
                    continue;
                RenderClouds(dst, rect, this.scale, (byte)this.seed, this.power, from, to);
                // Las nubes encima de la imagen con el modo de fusión elegido.
                op.Apply(dst, rect.Location, src, rect.Location, dst, rect.Location, rect.Size);
            }
        }
    }

    // Reducir ruido (Efectos > Ruido): acerca cada píxel al percentil de su color en el vecindario.
    public sealed class ReduceNoiseEffect
        : LocalHistogramEffect
    {
        public static string StaticName
        {
            get { return PdnResources.GetString("ReduceNoiseEffect.Name"); }
        }

        public static ImageResource StaticImage
        {
            get { return PdnResources.GetImageResource("Icons.ReduceNoiseEffect.png"); }
        }

        public enum PropertyNames
        {
            Radius = 0,
            Strength = 1
        }

        private int radius;
        private double strength;

        public ReduceNoiseEffect()
            : base(StaticName, StaticImage.Reference, SubmenuNames.Noise, EffectFlags.Configurable)
        {
        }

        protected override PropertyCollection OnCreatePropertyCollection()
        {
            List<Property> props = new List<Property>();
            props.Add(new Int32Property(PropertyNames.Radius, 6, 0, 200));
            props.Add(new DoubleProperty(PropertyNames.Strength, 0.4, 0, 1));
            return new PropertyCollection(props);
        }

        protected override ControlInfo OnCreateConfigUI(PropertyCollection props)
        {
            return CreateDefaultConfigUI(props);
        }

        protected override void OnSetRenderInfo(PropertyBasedEffectConfigToken newToken, RenderArgs dstArgs, RenderArgs srcArgs)
        {
            this.radius = newToken.GetProperty<Int32Property>(PropertyNames.Radius).Value;
            this.strength = -0.2 * newToken.GetProperty<DoubleProperty>(PropertyNames.Strength).Value;
            base.OnSetRenderInfo(newToken, dstArgs, srcArgs);
        }

        public override unsafe ColorBgra Apply(ColorBgra color, int area, int* hb, int* hg, int* hr, int* ha)
        {
            ColorBgra normalized = GetPercentileOfColor(color, area, hb, hg, hr, ha);
            double lerp = this.strength * (1 - 0.75 * color.GetIntensity());
            return ColorBgra.Lerp(color, normalized, lerp);
        }

        private static unsafe ColorBgra GetPercentileOfColor(ColorBgra color, int area, int* hb, int* hg, int* hr, int* ha)
        {
            int rc = 0;
            int gc = 0;
            int bc = 0;
            for (int i = 0; i < color.R; ++i)
                rc += hr[i];
            for (int i = 0; i < color.G; ++i)
                gc += hg[i];
            for (int i = 0; i < color.B; ++i)
                bc += hb[i];
            area = Math.Max(1, area);
            rc = (rc * 255) / area;
            gc = (gc * 255) / area;
            bc = (bc * 255) / area;
            return ColorBgra.FromBgra((byte)bc, (byte)gc, (byte)rc, color.A);
        }

        protected override void OnRender(Rectangle[] rois, int startIndex, int length)
        {
            for (int i = startIndex; i < startIndex + length; ++i)
                RenderRect(this.radius, SrcArgs.Surface, DstArgs.Surface, rois[i]);
        }
    }
}
