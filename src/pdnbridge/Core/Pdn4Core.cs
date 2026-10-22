// Kaint: tipos de PaintDotNet.Core que añadió Paint.NET 4.x y que usan los
// complementos de esa época.

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace PaintDotNet.AppModel
{
    // Paleta actual y la predeterminada (Paint.NET 4.x).
    public interface IPalettesService
    {
        IReadOnlyList<ColorBgra> CurrentPalette { get; }
        IReadOnlyList<ColorBgra> DefaultPalette { get; }
    }

    public interface IClipboardService
        : PaintDotNet.Clipboard.IClipboard
    {
    }

    public interface IShellService
    {
        bool LaunchFolder(IWin32Window owner, string folderPath);
        bool LaunchUrl(IWin32Window owner, string url);
    }

    public interface IUserFilesService
    {
        string UserFilesPath { get; }
    }

    public interface ILocalizedEnumValue
    {
        object EnumValue { get; }
        string LocalizedName { get; }
    }

    public interface IEnumLocalizer
    {
        IList<ILocalizedEnumValue> GetLocalizedEnumValues();
        ILocalizedEnumValue GetLocalizedEnumValue(object enumValue);
    }

    public interface IEnumLocalizerFactory
    {
        IEnumLocalizer Create(Type enumType);
    }
}

namespace PaintDotNet
{
    public sealed class UIScaleFactor
    {
        private readonly double scale;

        public UIScaleFactor(double scale)
        {
            this.scale = scale;
        }

        public static UIScaleFactor Current
        {
            get { return new UIScaleFactor(1.0); }
        }

        public double Scale
        {
            get { return this.scale; }
        }

        public int ScaleWidth(int width) { return (int)Math.Round(width * this.scale); }
        public int ScaleHeight(int height) { return (int)Math.Round(height * this.scale); }
    }

    // Cursor de espera mientras dura el objeto.
    public sealed class WaitCursorChanger
        : IDisposable
    {
        private Control control;
        private Cursor oldCursor;

        public WaitCursorChanger(Control control)
        {
            this.control = control;
            if (control != null)
            {
                this.oldCursor = control.Cursor;
                control.Cursor = Cursors.WaitCursor;
            }
        }

        public void Dispose()
        {
            if (this.control != null)
            {
                this.control.Cursor = this.oldCursor;
                this.control = null;
            }
        }
    }
}

namespace PaintDotNet.Rendering
{
    public interface IRenderer<TPixel>
        where TPixel : struct
    {
        int Width { get; }
        int Height { get; }
    }

    public interface ISurface<TPixel>
        : IRenderer<TPixel>
          where TPixel : struct
    {
        int Stride { get; }
        IntPtr Scan0 { get; }
    }

    // Operación de composición de capas (Paint.NET 4): una operación binaria de píxeles.
    public abstract class CompositionOp
        : BinaryPixelOp
    {
    }

    public static class RendererExtensions
    {
        public static ISurface<TPixel> UseTileOrToSurface<TPixel>(this IRenderer<TPixel> renderer, RectInt32 rect)
            where TPixel : struct
        {
            Surface surface = renderer as Surface;
            if (surface != null)
                return (ISurface<TPixel>)(object)surface.CreateWindow(rect.ToGdipRectangle());
            throw new NotSupportedException("Kaint: este tipo de origen de píxeles no está disponible");
        }
    }

    public static class SurfaceExtensions
    {
        public static IntPtr GetPointPointer<TPixel>(this ISurface<TPixel> surface, int x, int y)
            where TPixel : struct
        {
            return new IntPtr(surface.Scan0.ToInt64() + (long)y * surface.Stride + (long)x * Marshal.SizeOf(typeof(TPixel)));
        }
    }

    public static class SurfaceBgraExtensions
    {
        public static ColorBgra GetBilinearSample(this ISurface<ColorBgra> surface, float x, float y)
        {
            Surface s = surface as Surface;
            if (s != null)
                return s.GetBilinearSample(x, y);
            using (Surface wrapped = Surface.KaintWrap(surface.Width, surface.Height, surface.Stride, surface.Scan0))
                return wrapped.GetBilinearSample(x, y);
        }

        public static unsafe void SetPointUnchecked(this ISurface<ColorBgra> surface, int x, int y, ColorBgra color)
        {
            *(ColorBgra*)SurfaceExtensions.GetPointPointer(surface, x, y) = color;
        }

        public static unsafe ColorBgra GetPointUnchecked(this ISurface<ColorBgra> surface, int x, int y)
        {
            return *(ColorBgra*)SurfaceExtensions.GetPointPointer(surface, x, y);
        }
    }
}

namespace PaintDotNet.Clipboard
{
    public static class ClipboardExtensions
    {
        // Lo pone el proceso auxiliar: lee la imagen del portapapeles del sistema.
        public static Func<Surface> KaintReadSurface;

        public static Surface TryGetSurface(this IClipboard clipboard)
        {
            Func<Surface> read = KaintReadSurface;
            return read == null ? null : read();
        }
    }
}
