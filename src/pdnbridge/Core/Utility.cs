/////////////////////////////////////////////////////////////////////////////////
// Paint.NET                                                                   //
// Copyright (C) dotPDN LLC, Rick Brewster, Tom Jackson, and contributors.     //
// Portions Copyright (C) Microsoft Corporation. All Rights Reserved.          //
// See src/Resources/Files/License.txt for full licensing and attribution      //
// details.                                                                    //
// .                                                                           //
/////////////////////////////////////////////////////////////////////////////////

// Kaint: subconjunto de Utility.cs de Paint.NET 3.36 con los métodos que usan
// las clases copiadas (y los complementos).

using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;

namespace PaintDotNet
{
    public static class Utility
    {
        public static readonly Color TransparentKey = Color.FromArgb(192, 192, 192);

        public static string GetStaticName(Type type)
        {
            System.Reflection.PropertyInfo pi = type.GetProperty("StaticName",
                System.Reflection.BindingFlags.Static | System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.GetProperty);
            return (string)pi.GetValue(null, null);
        }

        // Kaint: el icono de la ventana no se usa en el proceso auxiliar.
        public static Icon ImageToIcon(Image image)
        {
            return ImageToIcon(image, TransparentKey);
        }

        public static Icon ImageToIcon(Image image, Color seeThru)
        {
            try
            {
                Bitmap bitmap = new Bitmap(image);
                return Icon.FromHandle(bitmap.GetHicon());
            }
            catch (Exception)
            {
                return null;
            }
        }
        private static bool allowGCFullCollect = true;

        private struct Edge
        {
            public int miny;   // int
            public int maxy;   // int
            public int x;      // fixed point: 24.8
            public int dxdy;   // fixed point: 24.8

            public Edge(int miny, int maxy, int x, int dxdy)
            {
                this.miny = miny;
                this.maxy = maxy;
                this.x = x;
                this.dxdy = dxdy;
            }
        }

        public static bool IsNumber(float x)
        {
            return x >= float.MinValue && x <= float.MaxValue;
        }

        public static bool IsNumber(double x)
        {
            return x >= double.MinValue && x <= double.MaxValue;
        }

        public static bool AllowGCFullCollect
        {
            get
            {
                return allowGCFullCollect;
            }

            set
            {
                allowGCFullCollect = value;
            }
        }

        public static void GCFullCollect()
        {
            if (AllowGCFullCollect)
            {
                GC.Collect();
                GC.WaitForPendingFinalizers();
                GC.Collect();
                GC.WaitForPendingFinalizers();
            }
        }

        /// <summary>
        /// Found on Google Groups when searching for "Region.Union" while looking
        /// for bugs:
        /// ---
        /// Hello,
        /// 
        /// I did not run your code, but I know Region.Union is flawed in both 1.0 and
        /// 1.1, so I assume it is in the gdi+ unmanged code dll.  The best workaround,
        /// in terms of speed, is to use a PdnGraphicsPath, but it must be a path with
        /// FillMode = FillMode.Winding. You add the rectangles to the path, then you do
        /// union onto an empty region with the path. The important point is to do only
        /// one union call on a given empty region. We created a "super region" object
        /// to hide all these bugs and optimize clipping operations. In fact, it is much
        /// faster to use the path than to call Region.Union for each rectangle.
        /// 
        /// Too bad about Region.Union. A lot of people will hit this bug, as it is
        /// essential in high-performance animation.
        /// 
        /// Regards,
        /// Frank Hileman
        /// Prodige Software Corporation
        /// ---
        /// </summary>
        /// <param name="rectsF"></param>
        /// <param name="startIndex"></param>
        /// <param name="length"></param>
        /// <returns></returns>
        public static PdnRegion RectanglesToRegion(RectangleF[] rectsF, int startIndex, int length)
        {
            PdnRegion region;

            if (rectsF == null || rectsF.Length == 0 || length == 0)
            {
                region = PdnRegion.CreateEmpty();
            }
            else
            {
                using (PdnGraphicsPath path = new PdnGraphicsPath())
                {
                    path.FillMode = FillMode.Winding;

                    if (startIndex == 0 && length == rectsF.Length)
                    {
                        path.AddRectangles(rectsF);
                    }
                    else
                    {
                        for (int i = startIndex; i < startIndex + length; ++i)
                        {
                            path.AddRectangle(rectsF[i]);
                        }
                    }

                    region = new PdnRegion(path);
                }
            }

            return region;
        }

        public static PdnRegion RectanglesToRegion(RectangleF[] rectsF)
        {
            return RectanglesToRegion(rectsF, 0, rectsF != null ? rectsF.Length : 0);
        }

        public static PdnRegion RectanglesToRegion(RectangleF[] rectsF1, RectangleF[] rectsF2, params RectangleF[][] rectsFA)
        {
            using (PdnGraphicsPath path = new PdnGraphicsPath())
            {
                path.FillMode = FillMode.Winding;

                if (rectsF1 != null && rectsF1.Length > 0)
                {
                    path.AddRectangles(rectsF1);
                }

                if (rectsF2 != null && rectsF2.Length > 0)
                {
                    path.AddRectangles(rectsF2);
                }
               
                foreach (RectangleF[] rectsF in rectsFA)
                {
                    if (rectsF != null && rectsF.Length > 0)
                    {
                        path.AddRectangles(rectsF);
                    }
                }

                return new PdnRegion(path);
            }
        }

        public static PdnRegion RectanglesToRegion(Rectangle[] rects, int startIndex, int length)
        {
            PdnRegion region;

            if (length == 0)
            {
                region = PdnRegion.CreateEmpty();
            }
            else
            {
                using (PdnGraphicsPath path = new PdnGraphicsPath())
                {
                    path.FillMode = FillMode.Winding;
                    if (startIndex == 0 && length == rects.Length)
                    {
                        path.AddRectangles(rects);
                    }
                    else
                    {
                        for (int i = startIndex; i < startIndex + length; ++i)
                        {
                            path.AddRectangle(rects[i]);
                        }
                    }

                    region = new PdnRegion(path);
                    path.Dispose();
                }
            }

            return region;
        }

        public static PdnRegion RectanglesToRegion(Rectangle[] rects)
        {
            return RectanglesToRegion(rects, 0, rects.Length);
        }

        /// <summary>
        /// Allows you to find the bounding box for a Region object without requiring
        /// the presence of a Graphics object.
        /// (Region.GetBounds takes a Graphics instance as its only parameter.)
        /// </summary>
        /// <param name="region">The region you want to find a bounding box for.</param>
        /// <returns>A RectangleF structure that surrounds the Region.</returns>
        public static Rectangle GetRegionBounds(PdnRegion region)
        {
            Rectangle[] rects = region.GetRegionScansReadOnlyInt();
            return GetRegionBounds(rects, 0, rects.Length);
        }

        /// <summary>
        /// Allows you to find the bounding box for a "region" that is described as an
        /// array of bounding boxes.
        /// </summary>
        /// <param name="rectsF">The "region" you want to find a bounding box for.</param>
        /// <returns>A RectangleF structure that surrounds the Region.</returns>
        public static RectangleF GetRegionBounds(RectangleF[] rectsF, int startIndex, int length)
        {
            if (rectsF.Length == 0)
            {
                return RectangleF.Empty;
            }

            float left = rectsF[startIndex].Left;
            float top = rectsF[startIndex].Top;
            float right = rectsF[startIndex].Right;
            float bottom = rectsF[startIndex].Bottom;

            for (int i = startIndex + 1; i < startIndex + length; ++i)
            {
                RectangleF rectF = rectsF[i];

                if (rectF.Left < left)
                {
                    left = rectF.Left;
                }

                if (rectF.Top < top)
                {
                    top = rectF.Top;
                }

                if (rectF.Right > right)
                {
                    right = rectF.Right;
                }

                if (rectF.Bottom > bottom)
                {
                    bottom = rectF.Bottom;
                }
            }

            return RectangleF.FromLTRB(left, top, right, bottom);
        }

        /// <summary>
        /// Allows you to find the bounding box for a "region" that is described as an
        /// array of bounding boxes.
        /// </summary>
        /// <param name="rectsF">The "region" you want to find a bounding box for.</param>
        /// <returns>A RectangleF structure that surrounds the Region.</returns>
        public static Rectangle GetRegionBounds(Rectangle[] rects, int startIndex, int length)
        {
            if (rects.Length == 0)
            {
                return Rectangle.Empty;
            }

            int left = rects[startIndex].Left;
            int top = rects[startIndex].Top;
            int right = rects[startIndex].Right;
            int bottom = rects[startIndex].Bottom;

            for (int i = startIndex + 1; i < startIndex + length; ++i)
            {
                Rectangle rect = rects[i];

                if (rect.Left < left)
                {
                    left = rect.Left;
                }

                if (rect.Top < top)
                {
                    top = rect.Top;
                }

                if (rect.Right > right)
                {
                    right = rect.Right;
                }

                if (rect.Bottom > bottom)
                {
                    bottom = rect.Bottom;
                }
            }

            return Rectangle.FromLTRB(left, top, right, bottom);
        }

        public static RectangleF GetRegionBounds(RectangleF[] rectsF)
        {
            return GetRegionBounds(rectsF, 0, rectsF.Length);
        }

        public static Rectangle GetRegionBounds(Rectangle[] rects)
        {
            return GetRegionBounds(rects, 0, rects.Length);
        }

        public static void TranslatePointsInPlace(PointF[] ptsF, float dx, float dy)
        {
            for (int i = 0; i < ptsF.Length; ++i)
            {
                ptsF[i].X += dx;
                ptsF[i].Y += dy;
            }
        }

        public static void TranslatePointsInPlace(Point[] pts, int dx, int dy)
        {
            for (int i = 0; i < pts.Length; ++i)
            {
                pts[i].X += dx;
                pts[i].Y += dy;
            }
        }

        public static double Clamp(double x, double min, double max) 
        {
            if (x < min)
            {
                return min;
            }
            else if (x > max)
            {
                return max;
            }
            else
            {
                return x;
            }
        }

        public static float Clamp(float x, float min, float max) 
        {
            if (x < min)
            {
                return min;
            }
            else if (x > max)
            {
                return max;
            }
            else
            {
                return x;
            }
        }

        public static int Clamp(int x, int min, int max)
        {
            if (x < min)
            {
                return min;
            }
            else if (x > max)
            {
                return max;
            }
            else
            {
                return x;
            }
        }

        public static byte ClampToByte(double x) 
        {
            if (x > 255)
            {
                return 255;
            }
            else if (x < 0)
            {
                return 0;
            }
            else
            {
                return (byte)x;
            }
        }

        public static byte ClampToByte(float x) 
        {
            if (x > 255)
            {
                return 255;
            }
            else if (x < 0)
            {
                return 0;
            }
            else
            {
                return (byte)x;
            }
        }

        public static byte ClampToByte(int x) 
        {
            if (x > 255)
            {
                return 255;
            }
            else if (x < 0)
            {
                return 0;
            }
            else
            {
                return (byte)x;
            }
        }

        public static float Lerp(float from, float to, float frac) 
        {
            return (from + frac * (to - from));
        }

        public static double Lerp(double from, double to, double frac) 
        {
            return (from + frac * (to - from));
        }

        public static PointF Lerp(PointF from, PointF to, float frac)
        {
            return new PointF(Lerp(from.X, to.X, frac), Lerp(from.Y, to.Y, frac));
        }

        public static int ReadFromStream(Stream input, byte[] buffer, int offset, int count)
        {
            int totalBytesRead = 0;

            while (totalBytesRead < count)
            {
                int bytesRead = input.Read(buffer, offset + totalBytesRead, count - totalBytesRead);

                if (bytesRead == 0)
                {
                    throw new IOException("ran out of data");
                }

                totalBytesRead += bytesRead;
            }

            return totalBytesRead;
        }

        public static Scanline[] GetScans(Point[] vertices)
        {
            return GetScans(vertices, 0, vertices.Length);
        }

        public static Scanline[] GetScans(Point[] vertices, int startIndex, int length)
        {
            if (length > vertices.Length - startIndex)
            {
                throw new ArgumentException("out of bounds: length > vertices.Length - startIndex");
            }

            int ymax = 0;

            // Build edge table
            Edge[] edgeTable = new Edge[length];
            int edgeCount = 0;

            for (int i = startIndex; i < startIndex + length; ++i)
            {
                Point top = vertices[i];
                Point bottom = vertices[(((i + 1) - startIndex) % length) + startIndex];
                int dy;

                if (top.Y > bottom.Y)
                {
                    Point temp = top;
                    top = bottom;
                    bottom = temp;
                }
                
                dy = bottom.Y - top.Y;

                if (dy != 0)
                {
                    edgeTable[edgeCount] = new Edge(top.Y, bottom.Y, top.X << 8, (((bottom.X - top.X) << 8) / dy));
                    ymax = Math.Max(ymax, bottom.Y);
                    ++edgeCount;
                }
            }

            // Sort edge table by miny
            for (int i = 0; i < edgeCount - 1; ++i)
            {
                int min = i;

                for (int j = i + 1; j < edgeCount; ++j)
                {
                    if (edgeTable[j].miny < edgeTable[min].miny)
                    {
                        min = j;
                    }
                }

                if (min != i)
                {
                    Edge temp = edgeTable[min];
                    edgeTable[min] = edgeTable[i];
                    edgeTable[i] = temp;
                }
            }

            // Compute how many scanlines we will be emitting
            int scanCount = 0;
            int activeLow = 0;
            int activeHigh = 0;
            int yscan1 = edgeTable[0].miny;

            // we assume that edgeTable[0].miny == yscan
            while (activeHigh < edgeCount - 1 && 
                   edgeTable[activeHigh + 1].miny == yscan1)
            {
                ++activeHigh;
            }

            while (yscan1 <= ymax)
            {
                // Find new edges where yscan == miny
                while (activeHigh < edgeCount - 1 &&
                       edgeTable[activeHigh + 1].miny == yscan1)
                {
                    ++activeHigh;
                }

                int count = 0;
                for (int i = activeLow; i <= activeHigh; ++i)
                {
                    if (edgeTable[i].maxy > yscan1)
                    {
                        ++count;
                    }
                }

                scanCount += count / 2;
                ++yscan1;

                // Remove edges where yscan == maxy
                while (activeLow < edgeCount - 1 &&
                       edgeTable[activeLow].maxy <= yscan1)
                {
                    ++activeLow;
                }

                if (activeLow > activeHigh)
                {
                    activeHigh = activeLow;
                }
            }

            // Allocate scanlines that we'll return
            Scanline[] scans = new Scanline[scanCount];

            // Active Edge Table (AET): it is indices into the Edge Table (ET)
            int[] active = new int[edgeCount];
            int activeCount = 0;
            int yscan2 = edgeTable[0].miny;
            int scansIndex = 0;
            
            // Repeat until both the ET and AET are empty
            while (yscan2 <= ymax)
            {
                // Move any edges from the ET to the AET where yscan == miny
                for (int i = 0; i < edgeCount; ++i)
                {
                    if (edgeTable[i].miny == yscan2)
                    {
                        active[activeCount] = i;
                        ++activeCount;
                    }
                }

                // Sort the AET on x
                for (int i = 0; i < activeCount - 1; ++i)
                {
                    int min = i;

                    for (int j = i + 1; j < activeCount; ++j)
                    {
                        if (edgeTable[active[j]].x < edgeTable[active[min]].x)
                        {
                            min = j;
                        }
                    }

                    if (min != i)
                    {
                        int temp = active[min];
                        active[min] = active[i];
                        active[i] = temp;
                    }
                }

                // For each pair of entries in the AET, fill in pixels between their info
                for (int i = 0; i < activeCount; i += 2)
                {
                    Edge el = edgeTable[active[i]];
                    Edge er = edgeTable[active[i + 1]];
                    int startx = (el.x + 0xff) >> 8; // ceil(x)
                    int endx = er.x >> 8;      // floor(x)

                    scans[scansIndex] = new Scanline(startx, yscan2, endx - startx);
                    ++scansIndex;
                }

                ++yscan2;

                // Remove from the AET any edge where yscan == maxy
                int k = 0;
                while (k < activeCount && activeCount > 0)
                {
                    if (edgeTable[active[k]].maxy == yscan2)
                    {
                        // remove by shifting everything down one
                        for (int j =  k + 1; j < activeCount; ++j)
                        {
                            active[j - 1] = active[j];
                        }

                        --activeCount;
                    }
                    else
                    {
                        ++k;
                    }
                }

                // Update x for each entry in AET
                for (int i = 0; i < activeCount; ++i)
                {
                    edgeTable[active[i]].x += edgeTable[active[i]].dxdy;
                }
            }

            return scans;
        }

        public static byte FastScaleByteByByte(byte a, byte b)
        {
            int r1 = a * b + 0x80;
            int r2 = ((r1 >> 8) + r1) >> 8;
            return (byte)r2;
        }
    }
}
