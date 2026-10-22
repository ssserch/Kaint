// Kaint: tipos de PaintDotNet.Base que añadió Paint.NET 4.x y que usan los
// complementos de esa época (lista sacada de sus referencias de metadatos).

using System;
using System.Collections;
using System.Collections.Generic;
using System.Drawing;
using PaintDotNet.PropertySystem;

namespace PaintDotNet
{
    public static class DoubleExtensions
    {
        public static double Clamp(this double x, double min, double max)
        {
            return x < min ? min : (x > max ? max : x);
        }
    }

    public static class FloatExtensions
    {
        public static float Clamp(this float x, float min, float max)
        {
            return x < min ? min : (x > max ? max : x);
        }
    }

    public static class Int32Extensions
    {
        public static int Clamp(this int x, int min, int max)
        {
            return x < min ? min : (x > max ? max : x);
        }
    }

    public static class DoubleUtil
    {
        public static double Clamp(double x, double min, double max)
        {
            return x < min ? min : (x > max ? max : x);
        }

        public static double Lerp(double from, double to, double frac)
        {
            return from + frac * (to - from);
        }
    }

    public static class FloatUtil
    {
        public static float Clamp(float x, float min, float max)
        {
            return x < min ? min : (x > max ? max : x);
        }

        public static float Lerp(float from, float to, float frac)
        {
            return from + frac * (to - from);
        }
    }

    public static class Int32Util
    {
        public static int Clamp(int x, int min, int max)
        {
            return x < min ? min : (x > max ? max : x);
        }

        public static byte ClampToByte(int x)
        {
            return (byte)(x < 0 ? 0 : (x > 255 ? 255 : x));
        }
    }

    public static class StringExtensions
    {
        public static bool IsNullOrEmpty(this string s)
        {
            return string.IsNullOrEmpty(s);
        }
    }

    public static class TypeExtensions
    {
        public static bool IsObsolete(this Type type, bool inherit)
        {
            return type.GetCustomAttributes(typeof(ObsoleteAttribute), inherit).Length > 0;
        }
    }

    public static class ServiceProviderExtensions
    {
        public static T GetService<T>(this IServiceProvider serviceProvider)
        {
            if (serviceProvider == null)
                return default(T);
            return (T)serviceProvider.GetService(typeof(T));
        }
    }

    public static class CloneableExtensions
    {
        public static T CloneT<T>(this T source)
            where T : class, ICloneable
        {
            return source == null ? null : (T)source.Clone();
        }
    }

    public static class EventHandlerExtensions
    {
        public static void Raise(this EventHandler handler, object sender)
        {
            if (handler != null)
                handler(sender, EventArgs.Empty);
        }
    }
}

namespace PaintDotNet.Collections
{
    public static class EnumerableExtensions
    {
        public static T[] ToArrayEx<T>(this IEnumerable<T> source)
        {
            return new List<T>(source).ToArray();
        }

        public static int IndexOf<T>(this IEnumerable<T> source, T item)
        {
            int i = 0;
            EqualityComparer<T> comparer = EqualityComparer<T>.Default;
            foreach (T x in source)
            {
                if (comparer.Equals(x, item))
                    return i;
                ++i;
            }
            return -1;
        }
    }

    public static class ListExtensions
    {
        public static IList<TResult> Select<T, TResult>(this IList<T> source, Func<T, TResult> selector)
        {
            List<TResult> result = new List<TResult>(source.Count);
            foreach (T x in source)
                result.Add(selector(x));
            return result;
        }
    }

    // En Paint.NET es una lista por segmentos para colecciones enormes; aquí basta List<T>.
    public class SegmentedList<T>
        : IList<T>
    {
        private List<T> items = new List<T>();

        public SegmentedList() { }
        public SegmentedList(IEnumerable<T> items) { this.items.AddRange(items); }

        public T this[int index] { get { return this.items[index]; } set { this.items[index] = value; } }
        public int Count { get { return this.items.Count; } }
        public bool IsReadOnly { get { return false; } }
        public void Add(T item) { this.items.Add(item); }
        public void Clear() { this.items.Clear(); }
        public bool Contains(T item) { return this.items.Contains(item); }
        public void CopyTo(T[] array, int arrayIndex) { this.items.CopyTo(array, arrayIndex); }
        public IEnumerator<T> GetEnumerator() { return this.items.GetEnumerator(); }
        public int IndexOf(T item) { return this.items.IndexOf(item); }
        public void Insert(int index, T item) { this.items.Insert(index, item); }
        public bool Remove(T item) { return this.items.Remove(item); }
        public void RemoveAt(int index) { this.items.RemoveAt(index); }
        IEnumerator IEnumerable.GetEnumerator() { return this.items.GetEnumerator(); }
    }

    public class SynchronizedList<T, TList>
        : IList<T>
          where TList : IList<T>
    {
        private readonly TList list;
        private readonly object sync = new object();

        public SynchronizedList(TList list) { this.list = list; }

        public T this[int index]
        {
            get { lock (this.sync) return this.list[index]; }
            set { lock (this.sync) this.list[index] = value; }
        }

        public int Count { get { lock (this.sync) return this.list.Count; } }
        public bool IsReadOnly { get { return false; } }
        public void Add(T item) { lock (this.sync) this.list.Add(item); }
        public void Clear() { lock (this.sync) this.list.Clear(); }
        public bool Contains(T item) { lock (this.sync) return this.list.Contains(item); }
        public void CopyTo(T[] array, int arrayIndex) { lock (this.sync) this.list.CopyTo(array, arrayIndex); }
        public IEnumerator<T> GetEnumerator() { lock (this.sync) return new List<T>(this.list).GetEnumerator(); }
        public int IndexOf(T item) { lock (this.sync) return this.list.IndexOf(item); }
        public void Insert(int index, T item) { lock (this.sync) this.list.Insert(index, item); }
        public bool Remove(T item) { lock (this.sync) return this.list.Remove(item); }
        public void RemoveAt(int index) { lock (this.sync) this.list.RemoveAt(index); }
        IEnumerator IEnumerable.GetEnumerator() { return GetEnumerator(); }
    }

    public class SynchronizedList<T>
        : SynchronizedList<T, IList<T>>
    {
        public SynchronizedList(IList<T> list) : base(list) { }
    }
}

namespace PaintDotNet.ComponentModel
{
    public abstract class RefTrackedObject
        : IDisposable
    {
        private bool isDisposed;

        public bool IsDisposed
        {
            get { return this.isDisposed; }
        }

        public void Dispose()
        {
            if (!this.isDisposed)
            {
                this.isDisposed = true;
                Dispose(true);
                GC.SuppressFinalize(this);
            }
        }

        protected virtual void Dispose(bool disposing)
        {
        }
    }
}

namespace PaintDotNet.Rendering
{
    public struct RectInt32
    {
        private int x;
        private int y;
        private int width;
        private int height;

        public RectInt32(int x, int y, int width, int height)
        {
            this.x = x;
            this.y = y;
            this.width = width;
            this.height = height;
        }

        // Propiedades (no campos): los complementos llaman a get_Width, etc.
        public int X { get { return this.x; } set { this.x = value; } }
        public int Y { get { return this.y; } set { this.y = value; } }
        public int Width { get { return this.width; } set { this.width = value; } }
        public int Height { get { return this.height; } set { this.height = value; } }
        public int Left { get { return this.X; } }
        public int Top { get { return this.Y; } }
        public int Right { get { return this.X + this.Width; } }
        public int Bottom { get { return this.Y + this.Height; } }
        public bool HasPositiveArea { get { return this.Width > 0 && this.Height > 0; } }
        public bool IsEmpty { get { return this.Width == 0 && this.Height == 0; } }

        public Rectangle ToGdipRectangle()
        {
            return new Rectangle(this.X, this.Y, this.Width, this.Height);
        }
    }

    public struct PointInt32
    {
        public int X;
        public int Y;
        public PointInt32(int x, int y) { this.X = x; this.Y = y; }
    }

    public struct SizeInt32
    {
        public int Width;
        public int Height;
        public SizeInt32(int width, int height) { this.Width = width; this.Height = height; }
    }
}

namespace PaintDotNet.Drawing
{
    public static class RectangleExtensions
    {
        public static PaintDotNet.Rendering.RectInt32 ToRectInt32(this Rectangle r)
        {
            return new PaintDotNet.Rendering.RectInt32(r.X, r.Y, r.Width, r.Height);
        }
    }

    public static class RectangleUtil
    {
        public static Rectangle Bounds(Rectangle[] rects)
        {
            if (rects == null || rects.Length == 0)
                return Rectangle.Empty;
            Rectangle b = rects[0];
            for (int i = 1; i < rects.Length; ++i)
                b = Rectangle.Union(b, rects[i]);
            return b;
        }
    }
}

namespace PaintDotNet.Imaging
{
    // Píxel de 8 bits con solo alfa (máscaras de selección en Paint.NET 4).
    public struct ColorAlpha8
    {
        public byte A;

        public ColorAlpha8(byte a)
        {
            this.A = a;
        }

        public static ColorAlpha8 Transparent { get { return new ColorAlpha8(0); } }
        public static ColorAlpha8 Opaque { get { return new ColorAlpha8(255); } }
    }
}

namespace PaintDotNet.PropertySystem
{
    // Solo lectura cuando alguna de las propiedades origen tiene el valor indicado.
    public sealed class ReadOnlyBoundToNameValuesRule
        : PropertyCollectionRule
    {
        private string targetPropertyName;
        private bool inverse;
        private Pair<object, object>[] sourceNameValues;

        public ReadOnlyBoundToNameValuesRule(object targetPropertyName, bool inverse, params Pair<object, object>[] sourcePropertyNameValuePairs)
        {
            this.targetPropertyName = targetPropertyName.ToString();
            this.inverse = inverse;
            this.sourceNameValues = (Pair<object, object>[])sourcePropertyNameValuePairs.Clone();
        }

        // Kaint: el puente describe la regla a Kaint para que deshabilite el control.
        public string TargetPropertyName { get { return this.targetPropertyName; } }
        public bool Inverse { get { return this.inverse; } }
        public Pair<object, object>[] SourceNameValues { get { return this.sourceNameValues; } }

        protected override void OnInitialized()
        {
            foreach (Pair<object, object> pair in this.sourceNameValues)
            {
                Property source = Owner[pair.First];
                if (source != null)
                    source.ValueChanged += (s, e) => Sync();
            }
            Sync();
        }

        private void Sync()
        {
            bool readOnly = false;
            foreach (Pair<object, object> pair in this.sourceNameValues)
            {
                Property source = Owner[pair.First];
                if (source != null && object.Equals(source.Value, pair.Second))
                    readOnly = true;
            }
            Property target = Owner[this.targetPropertyName];
            if (target != null)
                target.ReadOnly = readOnly ^ this.inverse;
        }

        public override PropertyCollectionRule Clone()
        {
            return new ReadOnlyBoundToNameValuesRule(this.targetPropertyName, this.inverse, this.sourceNameValues);
        }
    }
}

namespace PaintDotNet.Clipboard
{
    // Kaint: Paint.NET 4.x (el servicio de portapapeles lo implementa).
    public interface IClipboard
    {
    }
}
