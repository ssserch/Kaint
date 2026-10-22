// Kaint: métodos de PdnRegion que añadió Paint.NET 4.x.

using System.Drawing;

namespace PaintDotNet
{
    public sealed partial class PdnRegion
    {
        public static PdnRegion FromRectangles(Rectangle[] rects)
        {
            return Utility.RectanglesToRegion(rects);
        }
    }
}
