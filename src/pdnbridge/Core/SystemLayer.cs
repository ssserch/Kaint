// Kaint: sustituto de PaintDotNet.SystemLayer (Paint.NET 3.36 usaba Win32)
// para mono en Linux. Solo lo que necesitan las clases de Core copiadas.

using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Runtime.InteropServices;

namespace PaintDotNet.SystemLayer
{
    public static unsafe class Memory
    {
        public static IntPtr Allocate(ulong bytes)
        {
            IntPtr p = Marshal.AllocHGlobal(new IntPtr((long)Math.Max(1UL, bytes)));
            SetToZero(p, bytes);
            return p;
        }

        public static void Free(IntPtr block)
        {
            if (block != IntPtr.Zero)
                Marshal.FreeHGlobal(block);
        }

        public static IntPtr AllocateLarge(ulong bytes)
        {
            return Allocate(bytes);
        }

        public static void FreeLarge(IntPtr block, ulong bytes)
        {
            Free(block);
        }

        public static void ProtectBlockLarge(IntPtr block, ulong bytes, bool readAccess, bool writeAccess)
        {
        }

        // Paint.NET reservaba una sección DIB de Windows; aquí basta con memoria normal.
        public static IntPtr AllocateBitmap(int width, int height, out IntPtr handle)
        {
            handle = IntPtr.Zero;
            return Allocate((ulong)width * (ulong)height * 4UL);
        }

        public static void FreeBitmap(IntPtr bitmapHandle, int width, int height)
        {
        }

        public static void SetToZero(void* dst, ulong length)
        {
            SetToZero(new IntPtr(dst), length);
        }

        public static void SetToZero(IntPtr dst, ulong length)
        {
            byte* p = (byte*)dst;
            for (ulong i = 0; i < length; ++i)
                p[i] = 0;
        }

        public static void Copy(IntPtr dst, IntPtr src, ulong length)
        {
            Buffer.MemoryCopy((void*)src, (void*)dst, length, length);
        }

        public static void Copy(void* dst, void* src, ulong length)
        {
            Buffer.MemoryCopy(src, dst, length, length);
        }
    }

    public static class Processor
    {
        public static int LogicalCpuCount
        {
            get { return Environment.ProcessorCount; }
        }
    }

    public static class Tracing
    {
        public static void Ping(string message) { }
        public static void Enter() { }
        public static void Leave() { }
        public static void LogFeature(string feature) { }
    }

    public static class PdnGraphics
    {
        public static GraphicsPath ClipPath(GraphicsPath subjectPath, CombineMode combineMode, GraphicsPath clipPath)
        {
            // Sin la biblioteca de recorte de Paint.NET: se combina como región.
            using (Region region = new Region(subjectPath))
            {
                switch (combineMode)
                {
                    case CombineMode.Intersect: region.Intersect(clipPath); break;
                    case CombineMode.Union: region.Union(clipPath); break;
                    case CombineMode.Xor: region.Xor(clipPath); break;
                    case CombineMode.Exclude: region.Exclude(clipPath); break;
                    case CombineMode.Complement: region.Complement(clipPath); break;
                    default: break;
                }
                GraphicsPath result = new GraphicsPath();
                foreach (RectangleF r in region.GetRegionScans(new Matrix()))
                    result.AddRectangle(r);
                return result;
            }
        }

        public static void GetRegionScans(Region region, out Rectangle[] scans, out int area)
        {
            RectangleF[] rectsF;
            using (Matrix m = new Matrix())
                rectsF = region.GetRegionScans(m);
            scans = new Rectangle[rectsF.Length];
            area = 0;
            for (int i = 0; i < rectsF.Length; ++i)
            {
                scans[i] = Rectangle.Truncate(rectsF[i]);
                area += scans[i].Width * scans[i].Height;
            }
        }
    }

    // Un Graphics que no dibuja en ningún sitio (para medir regiones).
    public sealed class NullGraphics : IDisposable
    {
        private Bitmap bitmap = new Bitmap(1, 1);
        private Graphics graphics;

        public NullGraphics()
        {
            this.graphics = Graphics.FromImage(this.bitmap);
        }

        public Graphics Graphics
        {
            get { return this.graphics; }
        }

        public void Dispose()
        {
            this.graphics.Dispose();
            this.bitmap.Dispose();
        }
    }

    public static class UI
    {
        public static bool IsOurAppActive
        {
            get { return true; }
        }
    }
}

namespace PaintDotNet
{
    // Textos de Paint.NET: sin recursos traducidos, se devuelve la clave.
    public static class PdnResources
    {
        public static string GetString(string key)
        {
            return key;
        }

        // Los iconos de los efectos integrados no hacen falta en el proceso auxiliar.
        public static ImageResource GetImageResource(string name)
        {
            return new EmptyImageResource();
        }

        private sealed class EmptyImageResource : ImageResource
        {
            protected override System.Drawing.Image Load()
            {
                return null;
            }
        }
    }
}

namespace PaintDotNet.SystemLayer
{
    // Kaint: WaitHandleArray de Paint.NET sin WaitForMultipleObjects de Win32.
    public sealed class WaitHandleArray
    {
        public const int MinimumCount = 1;
        public const int MaximumCount = 64;

        private System.Threading.WaitHandle[] handles;

        public System.Threading.WaitHandle this[int index]
        {
            get { return this.handles[index]; }
            set { this.handles[index] = value; }
        }

        public int Length
        {
            get { return this.handles.Length; }
        }

        public WaitHandleArray(int count)
        {
            if (count < MinimumCount || count > MaximumCount)
                throw new ArgumentOutOfRangeException("count");
            this.handles = new System.Threading.WaitHandle[count];
        }

        public void WaitAll()
        {
            System.Threading.WaitHandle.WaitAll(this.handles);
        }

        public bool AreAllSignaled()
        {
            return AreAllSignaled(0);
        }

        public bool AreAllSignaled(uint msTimeout)
        {
            return System.Threading.WaitHandle.WaitAll(this.handles, (int)Math.Min(msTimeout, (uint)int.MaxValue));
        }

        public int WaitAny()
        {
            return System.Threading.WaitHandle.WaitAny(this.handles);
        }
    }
}
