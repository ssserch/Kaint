// Kaint: proceso auxiliar que carga complementos de efectos de Paint.NET
// (.dll) y los ejecuta para Kaint (con mono en Linux, con .NET Framework en
// Windows).
//
// Protocolo: una petición JSON por línea en stdin y una respuesta JSON por
// línea en stdout. Antes de la respuesta de "render" pueden llegar líneas
// {"progress":n,"total":m}. Los píxeles viajan por memoria compartida
// (/dev/shm en Linux, con nombre en Windows) con el origen y luego el destino
// en BGRA de alfa directo: el mismo formato que las capas de Kaint y que
// ColorBgra, así que no hay conversión.

using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.Globalization;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Reflection;
using System.Text;
using System.Threading;
using System.Windows.Forms;
using PaintDotNet;
using PaintDotNet.Effects;
using PaintDotNet.IndirectUI;
using PaintDotNet.PropertySystem;

namespace Kaint.PdnHost
{
    internal sealed class LoadedEffect
    {
        public string AssemblyPath;
        public Type Type;
        public Effect Instance;
        // Último token aceptado en el diálogo propio del complemento.
        public EffectConfigToken DialogToken;
    }

    public static class Program
    {
        private const int ProtocolVersion = 1;

        private static readonly object outLock = new object();
        private static TextWriter output;
        private static readonly Dictionary<string, Assembly> assemblies = new Dictionary<string, Assembly>();
        private static readonly Dictionary<string, LoadedEffect> effects = new Dictionary<string, LoadedEffect>();
        private static readonly List<string> probeDirs = new List<string>();

        private static Thread renderThread;
        private static volatile Effect rendering;
        private static volatile bool cancelRequested;

        public static int Main(string[] args)
        {
            output = new StreamWriter(Console.OpenStandardOutput(), new UTF8Encoding(false));
            // Lo que escriban los complementos en la consola no debe romper el protocolo.
            Console.SetOut(Console.Error);
            AppDomain.CurrentDomain.AssemblyResolve += ResolveAssembly;

            TextReader input = new StreamReader(Console.OpenStandardInput(), new UTF8Encoding(false));
            string line;
            while ((line = input.ReadLine()) != null)
            {
                if (line.Trim().Length == 0)
                    continue;
                Dictionary<string, object> request;
                try
                {
                    request = (Dictionary<string, object>)Json.Parse(line);
                }
                catch (Exception ex)
                {
                    Send(Error(L("Petición no válida: ", "Invalid request: ") + ex.Message));
                    continue;
                }
                string cmd = Json.Str(request, "cmd", "");
                if (cmd == "quit")
                    break;
                try
                {
                    Handle(cmd, request);
                }
                catch (Exception ex)
                {
                    Send(Error(Describe(ex)));
                }
            }
            if (renderThread != null)
                renderThread.Join(2000);
            return 0;
        }

        private static void Handle(string cmd, Dictionary<string, object> request)
        {
            switch (cmd)
            {
                case "hello":
                    Dictionary<string, object> hello = Ok();
                    hello["protocol"] = ProtocolVersion;
                    hello["runtime"] = RuntimeVersion();
                    Send(hello);
                    break;
                case "inspect":
                    Send(Inspect(Json.Str(request, "path")));
                    break;
                case "render":
                    // Kaint no pide otro renderizado sin haber recibido la respuesta
                    // del anterior, pero el hilo puede seguir vivo un instante
                    // después de enviarla: se espera a que termine.
                    if (renderThread != null)
                        renderThread.Join();
                    cancelRequested = false;
                    renderThread = new Thread(() => Send(SafeRender(request)));
                    renderThread.IsBackground = true;
                    renderThread.Start();
                    break;
                case "cancel":
                    // Sin respuesta: la da el renderizado al terminar.
                    cancelRequested = true;
                    Effect effect = rendering;
                    if (effect != null)
                        effect.SignalCancelRequest();
                    break;
                case "dialog":
                    Send(ShowDialog(request));
                    break;
                default:
                    Send(Error(L("Orden desconocida: ", "Unknown command: ") + cmd));
                    break;
            }
        }

        // --- Respuestas ---

        private static Dictionary<string, object> Ok()
        {
            Dictionary<string, object> r = new Dictionary<string, object>();
            r["ok"] = true;
            return r;
        }

        // Idioma de los mensajes: el de Kaint, que lo pasa en KAINT_LANG.
        private static readonly bool Spanish = Environment.GetEnvironmentVariable("KAINT_LANG") == "es";

        private static string L(string es, string en)
        {
            return Spanish ? es : en;
        }

        private static Dictionary<string, object> Error(string message)
        {
            Dictionary<string, object> r = new Dictionary<string, object>();
            r["ok"] = false;
            r["error"] = message;
            return r;
        }

        internal static void Send(object message)
        {
            string text = Json.Serialize(message);
            lock (outLock)
            {
                output.Write(text);
                output.Write('\n');
                output.Flush();
            }
        }

        private static string Describe(Exception ex)
        {
            while ((ex is TargetInvocationException || ex is TypeInitializationException) && ex.InnerException != null)
                ex = ex.InnerException;
            // Con KAINT_PDN_DEBUG, la traza completa en stderr (Kaint la deja pasar).
            if (!string.IsNullOrEmpty(Environment.GetEnvironmentVariable("KAINT_PDN_DEBUG")))
                Console.Error.WriteLine(ex.ToString());
            if (ex is DllNotFoundException && ex.Message.Contains("gdiplus"))
                return L("Falta libgdiplus (System.Drawing). Instala el paquete libgdiplus.",
                         "libgdiplus (System.Drawing) is missing. Install the libgdiplus package.");
            if (ex is InvalidProgramException || ex is MissingMethodException)
                return L("Usa partes de Paint.NET que la capa de compatibilidad no tiene (",
                         "It uses parts of Paint.NET that the compatibility layer does not have (")
                       + ex.Message.Split('\n')[0].Trim() + ").";
            return ex.GetType().Name + ": " + ex.Message;
        }

        private static string RuntimeVersion()
        {
            Type mono = Type.GetType("Mono.Runtime");
            if (mono != null)
            {
                MethodInfo name = mono.GetMethod("GetDisplayName", BindingFlags.NonPublic | BindingFlags.Static);
                if (name != null)
                    return "Mono " + name.Invoke(null, null);
            }
            return Environment.Version.ToString();
        }

        // --- Carga de ensamblados ---

        // Ensamblados de .NET moderno (Paint.NET 4.3+) cuyo contenido está en
        // otro ensamblado de mono: System.Drawing.Common y System.Drawing.Primitives
        // en System.Drawing, y System.Windows.Forms de cualquier versión.
        private static Assembly ModernEquivalent(string name)
        {
            switch (name)
            {
                case "System.Drawing.Common":
                case "System.Drawing.Primitives":
                case "System.Drawing":
                    return typeof(System.Drawing.Color).Assembly;
                case "System.Windows.Forms":
                case "System.Windows.Forms.Primitives":
                    return typeof(System.Windows.Forms.Form).Assembly;
                case "System.Private.CoreLib":
                    return typeof(object).Assembly;
            }
            return null;
        }

        private static readonly HashSet<string> resolving = new HashSet<string>();

        private static Assembly ResolveAssembly(object sender, ResolveEventArgs e)
        {
            string name = new AssemblyName(e.Name).Name;
            Assembly modern = ModernEquivalent(name);
            if (modern != null)
                return modern;
            List<string> dirs = new List<string>();
            dirs.Add(AppDomain.CurrentDomain.BaseDirectory);
            lock (probeDirs)
                dirs.AddRange(probeDirs);
            foreach (string dir in dirs)
            {
                string path = Path.Combine(dir, name + ".dll");
                if (File.Exists(path))
                    return Assembly.LoadFrom(path);
            }
            // Fachadas de .NET moderno (System.Collections 6.0...): la de mono
            // con el mismo nombre, sin pedir versión.
            if (name.StartsWith("System.") || name.StartsWith("Microsoft."))
            {
                lock (resolving)
                {
                    if (!resolving.Add(name))
                        return null;
                }
                try
                {
                    return Assembly.Load(name);
                }
                catch (Exception)
                {
                    return null;
                }
                finally
                {
                    lock (resolving)
                        resolving.Remove(name);
                }
            }
            return null;
        }

        private static void AddProbeDir(string dir)
        {
            lock (probeDirs)
            {
                if (!probeDirs.Contains(dir))
                    probeDirs.Add(dir);
                // Algunas colecciones guardan sus bibliotecas en subcarpetas (DspToolbox...).
                try
                {
                    foreach (string sub in Directory.GetDirectories(dir))
                        if (!probeDirs.Contains(sub))
                            probeDirs.Add(sub);
                }
                catch (Exception)
                {
                }
            }
        }

        private static Assembly LoadAssembly(string path)
        {
            path = Path.GetFullPath(path);
            Assembly asm;
            if (assemblies.TryGetValue(path, out asm))
                return asm;
            AddProbeDir(Path.GetDirectoryName(path));
            // Los de .NET moderno se cargan con los nombres de System.Drawing
            // corregidos (ver ModernAssembly); el resto, tal cual.
            byte[] patched = ModernAssembly.Patch(File.ReadAllBytes(path));
            asm = patched != null ? Assembly.Load(patched) : Assembly.LoadFrom(path);
            assemblies[path] = asm;
            return asm;
        }

        // Complementos compilados para .NET moderno (para explicar mejor sus errores).
        private static readonly HashSet<string> modernPlugins = new HashSet<string>();

        // Motivo por el que un ensamblado no puede funcionar en esta capa (o null).
        private static string Incompatibility(Assembly asm, out bool isPlugin)
        {
            bool usesModernRuntime = false;
            bool usesPdn5 = false;
            isPlugin = false;
            foreach (AssemblyName r in asm.GetReferencedAssemblies())
            {
                switch (r.Name)
                {
                    case "System.Runtime":
                    case "System.Private.CoreLib":
                    case "System.Drawing.Common":
                    case "netstandard":
                        usesModernRuntime = true;
                        break;
                    case "PaintDotNet.Effects":
                    case "PaintDotNet.Core":
                        isPlugin = true;
                        break;
                    case "PaintDotNet.Effects.Core":
                    case "PaintDotNet.Effects.Gpu":
                    case "PaintDotNet.Primitives":
                    case "PaintDotNet.Fundamentals":
                    case "PaintDotNet.ComponentModel":
                    case "PaintDotNet.Windows":
                    case "PaintDotNet.Windows.Core":
                    case "PaintDotNet.Framework":
                        isPlugin = true;
                        usesPdn5 = true;
                        break;
                }
            }
            if (usesPdn5)
                return L("Usa la API de efectos de Paint.NET 5 (Direct2D/GPU), que no existe fuera de Windows.",
                         "It uses the Paint.NET 5 effects API (Direct2D/GPU), which does not exist outside Windows.");
            // Los de .NET moderno con la API clásica (Paint.NET 4.3 y 4.4) se
            // intentan: mono resuelve casi todo con ModernEquivalent y sus fachadas.
            if (usesModernRuntime && isPlugin)
                modernPlugins.Add(asm.FullName);
            return null;
        }

        // --- Inspección ---

        private static Dictionary<string, object> Inspect(string path)
        {
            Dictionary<string, object> result = Ok();
            List<object> list = new List<object>();
            List<object> errors = new List<object>();
            result["effects"] = list;
            result["errors"] = errors;
            result["name"] = Path.GetFileNameWithoutExtension(path);

            Assembly asm;
            try
            {
                asm = LoadAssembly(path);
            }
            catch (BadImageFormatException)
            {
                result["library"] = true;
                errors.Add(L("No es un ensamblado .NET.", "It is not a .NET assembly."));
                return result;
            }

            bool isPlugin;
            string incompatible = Incompatibility(asm, out isPlugin);
            result["library"] = !isPlugin;
            ReadAssemblyInfo(asm, result);
            if (incompatible != null)
            {
                errors.Add(incompatible);
                return result;
            }
            if (!isPlugin)
                return result;

            Type[] types;
            try
            {
                types = asm.GetTypes();
            }
            catch (ReflectionTypeLoadException ex)
            {
                types = ex.Types;
                Dictionary<string, bool> seen = new Dictionary<string, bool>();
                foreach (Exception le in ex.LoaderExceptions)
                {
                    string msg = le == null ? "" : Describe(le);
                    if (le != null && !seen.ContainsKey(msg))
                    {
                        seen[msg] = true;
                        errors.Add(msg);
                    }
                }
            }

            // La información del ensamblado manda; la de los tipos solo completa.
            bool supportInfoRead = ReadSupportInfo(PluginSupportInfo.GetPluginSupportInfo(asm), result, true);
            foreach (Type type in types)
            {
                // Como Paint.NET, también los tipos no públicos (pyrochild...).
                if (type == null || type.IsAbstract || type.ContainsGenericParameters || !typeof(Effect).IsAssignableFrom(type))
                    continue;
                if (type.GetConstructor(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic, null, Type.EmptyTypes, null) == null)
                    continue;
                try
                {
                    LoadedEffect le = new LoadedEffect();
                    le.AssemblyPath = Path.GetFullPath(path);
                    le.Type = type;
                    le.Instance = CreateEffect(type);
                    effects[EffectKey(le.AssemblyPath, type.FullName)] = le;
                    list.Add(DescribeEffect(le));
                    if (!supportInfoRead)
                        supportInfoRead = ReadSupportInfo(PluginSupportInfo.GetPluginSupportInfo(type), result, false);
                }
                catch (Exception ex)
                {
                    errors.Add(type.Name + ": " + Describe(ex));
                }
            }
            return result;
        }

        private static T Attr<T>(Assembly asm) where T : Attribute
        {
            try
            {
                object[] list = asm.GetCustomAttributes(typeof(T), false);
                return list.Length > 0 ? (T)list[0] : null;
            }
            catch (Exception)
            {
                return null;
            }
        }

        private static void ReadAssemblyInfo(Assembly asm, Dictionary<string, object> result)
        {
            AssemblyName an = asm.GetName();
            result["version"] = an.Version == null ? "" : an.Version.ToString();
            // Uno a uno: pedirlos todos crearía también los de .NET moderno
            // (SupportedOSPlatformAttribute...), que mono no tiene.
            AssemblyTitleAttribute title = Attr<AssemblyTitleAttribute>(asm);
            if (title != null && title.Title.Length > 0)
                result["name"] = title.Title;
            AssemblyProductAttribute product = Attr<AssemblyProductAttribute>(asm);
            if (product != null && product.Product.Length > 0)
                result["product"] = product.Product;
            AssemblyCompanyAttribute company = Attr<AssemblyCompanyAttribute>(asm);
            if (company != null)
                result["author"] = company.Company;
            AssemblyCopyrightAttribute copyright = Attr<AssemblyCopyrightAttribute>(asm);
            if (copyright != null)
                result["copyright"] = copyright.Copyright;
            AssemblyDescriptionAttribute description = Attr<AssemblyDescriptionAttribute>(asm);
            if (description != null)
                result["description"] = description.Description;
        }

        private static bool ReadSupportInfo(IPluginSupportInfo info, Dictionary<string, object> result, bool includeName)
        {
            if (info == null)
                return false;
            try
            {
                if (includeName && !string.IsNullOrEmpty(info.DisplayName))
                    result["name"] = info.DisplayName;
                if (!string.IsNullOrEmpty(info.Author))
                    result["author"] = info.Author;
                if (!string.IsNullOrEmpty(info.Copyright))
                    result["copyright"] = info.Copyright;
                if (info.Version != null)
                    result["version"] = info.Version.ToString();
                if (info.WebsiteUri != null)
                    result["website"] = info.WebsiteUri.ToString();
            }
            catch (Exception)
            {
            }
            return true;
        }

        private static string EffectKey(string assemblyPath, string typeName)
        {
            return assemblyPath + "|" + typeName;
        }

        // Imagen de origen provisional mientras no hay una de verdad: algunos
        // complementos la usan al crear su interfaz (miniatura del centro).
        private static Surface placeholder;

        private static Effect CreateEffect(Type type)
        {
            Effect effect = (Effect)Activator.CreateInstance(type, true);
            effect.Services = Services.Instance;
            if (placeholder == null)
            {
                placeholder = new Surface(160, 120);
                placeholder.Clear(ColorBgra.FromBgra(200, 200, 200, 255));
            }
            effect.EnvironmentParameters = new EffectEnvironmentParameters(ColorBgra.Black, ColorBgra.White, 2.0f,
                new PdnRegion(placeholder.Bounds), placeholder);
            return effect;
        }

        private static string EffectKind(Effect effect)
        {
            if (effect is PropertyBasedEffect)
                return "properties";
            return effect.CheckForEffectFlags(EffectFlags.Configurable) ? "dialog" : "none";
        }

        private static Dictionary<string, object> DescribeEffect(LoadedEffect le)
        {
            Effect effect = le.Instance;
            Dictionary<string, object> d = new Dictionary<string, object>();
            d["id"] = le.Type.FullName;
            d["name"] = effect.Name;
            d["subMenu"] = effect.SubMenuName;
            d["adjustment"] = effect.Category == EffectCategory.Adjustment;
            d["hidden"] = effect.Category == EffectCategory.DoNotDisplay;
            d["kind"] = EffectKind(effect);
            d["singleThreaded"] = effect.CheckForEffectFlags(EffectFlags.SingleThreaded);
            d["singleRenderCall"] = effect.CheckForEffectFlags(EffectFlags.SingleRenderCall);
            try
            {
                if (effect.Image != null)
                {
                    using (MemoryStream ms = new MemoryStream())
                    {
                        effect.Image.Save(ms, ImageFormat.Png);
                        d["icon"] = Convert.ToBase64String(ms.ToArray());
                    }
                }
            }
            catch (Exception)
            {
            }

            PropertyBasedEffect pbe = effect as PropertyBasedEffect;
            if (pbe != null)
                Schema.Describe(pbe, d);
            return d;
        }

        // --- Renderizado ---

        private static LoadedEffect FindEffect(Dictionary<string, object> request)
        {
            string path = Path.GetFullPath(Json.Str(request, "path"));
            string id = Json.Str(request, "effect");
            LoadedEffect le;
            if (effects.TryGetValue(EffectKey(path, id), out le))
                return le;
            Assembly asm = LoadAssembly(path);
            Type type = asm.GetType(id, true);
            le = new LoadedEffect();
            le.AssemblyPath = path;
            le.Type = type;
            le.Instance = CreateEffect(type);
            effects[EffectKey(path, id)] = le;
            return le;
        }

        private static ColorBgra ColorFrom(Dictionary<string, object> request, string key, ColorBgra def)
        {
            object v;
            if (!request.TryGetValue(key, out v) || v == null)
                return def;
            return ColorBgra.FromUInt32((uint)Convert.ToDouble(v, CultureInfo.InvariantCulture));
        }

        private static List<ColorBgra> PaletteFrom(Dictionary<string, object> request)
        {
            List<ColorBgra> colors = new List<ColorBgra>();
            List<object> list = Json.List(request, "palette");
            if (list != null)
                foreach (object v in list)
                    colors.Add(ColorBgra.FromUInt32((uint)Convert.ToDouble(v, CultureInfo.InvariantCulture)));
            return colors;
        }

        private static Rectangle RectFrom(Dictionary<string, object> request, string key, Rectangle def)
        {
            List<object> r = Json.List(request, key);
            if (r == null || r.Count != 4)
                return def;
            return new Rectangle(Convert.ToInt32(r[0]), Convert.ToInt32(r[1]), Convert.ToInt32(r[2]), Convert.ToInt32(r[3]));
        }

        // Memoria compartida con el origen y el destino: un archivo de /dev/shm en Linux o una
        // memoria con nombre ("Local\\...") en Windows.
        private sealed unsafe class SharedPixels
            : IDisposable
        {
            private MemoryMappedFile file;
            private MemoryMappedViewAccessor view;
            private byte* pointer;
            public Surface Source;
            public Surface Destination;

            public SharedPixels(string path, int width, int height)
            {
                long bytes = (long)width * height * 4;
                this.file = path.StartsWith("Local\\", StringComparison.Ordinal)
                    ? MemoryMappedFile.OpenExisting(path, MemoryMappedFileRights.ReadWrite)
                    : MemoryMappedFile.CreateFromFile(path, FileMode.Open, null, 0, MemoryMappedFileAccess.ReadWrite);
                this.view = this.file.CreateViewAccessor(0, bytes * 2, MemoryMappedFileAccess.ReadWrite);
                this.view.SafeMemoryMappedViewHandle.AcquirePointer(ref this.pointer);
                byte* start = this.pointer + this.view.PointerOffset;
                this.Source = Surface.KaintWrap(width, height, width * 4, new IntPtr(start));
                this.Destination = Surface.KaintWrap(width, height, width * 4, new IntPtr(start + bytes));
            }

            public void Dispose()
            {
                if (this.Source != null)
                    this.Source.Dispose();
                if (this.Destination != null)
                    this.Destination.Dispose();
                if (this.pointer != null)
                    this.view.SafeMemoryMappedViewHandle.ReleasePointer();
                this.pointer = null;
                this.view.Dispose();
                this.file.Dispose();
            }
        }

        private static Dictionary<string, object> SafeRender(Dictionary<string, object> request)
        {
            try
            {
                return Render(request);
            }
            catch (Exception ex)
            {
                return Error(Describe(ex));
            }
            finally
            {
                rendering = null;
            }
        }

        private static EffectConfigToken CreateToken(LoadedEffect le, Dictionary<string, object> request, SharedPixels pixels, Rectangle roi)
        {
            Effect effect = le.Instance;
            PropertyBasedEffect pbe = effect as PropertyBasedEffect;
            if (pbe != null)
            {
                PropertyCollection props = pbe.CreatePropertyCollection();
                object values;
                if (request.TryGetValue("values", out values) && values is Dictionary<string, object>)
                    Schema.Apply(props, (Dictionary<string, object>)values);
                return new PropertyBasedEffectConfigToken(props);
            }
            if (!effect.CheckForEffectFlags(EffectFlags.Configurable))
                return null;
            if (le.DialogToken != null)
                return (EffectConfigToken)le.DialogToken.Clone();
            // Sin diálogo previo: el token inicial que crea el propio diálogo.
            using (EffectConfigDialog dialog = effect.CreateConfigDialog())
            {
                dialog.Effect = effect;
                dialog.EffectSourceSurface = pixels.Source;
                dialog.Selection = new PdnRegion(roi);
                return dialog.EffectToken == null ? null : (EffectConfigToken)dialog.EffectToken.Clone();
            }
        }

        private static Dictionary<string, object> Render(Dictionary<string, object> request)
        {
            LoadedEffect le = FindEffect(request);
            Effect effect = le.Instance;
            int width = (int)Json.Num(request, "width");
            int height = (int)Json.Num(request, "height");
            Rectangle bounds = new Rectangle(0, 0, width, height);
            Rectangle roi = Rectangle.Intersect(bounds, RectFrom(request, "roi", bounds));
            if (roi.Width <= 0 || roi.Height <= 0)
                return Ok();

            using (SharedPixels pixels = new SharedPixels(Json.Str(request, "shm"), width, height))
            {
                // El destino empieza como copia del origen: lo que el efecto no toque queda igual.
                pixels.Destination.CopySurface(pixels.Source, roi.Location, roi);

                Services.Instance.SetCurrentPalette(PaletteFrom(request));
                effect.EnvironmentParameters = new EffectEnvironmentParameters(
                    ColorFrom(request, "primary", ColorBgra.Black),
                    ColorFrom(request, "secondary", ColorBgra.White),
                    (float)Json.Num(request, "brushWidth", 2),
                    new PdnRegion(roi),
                    pixels.Source);

                EffectConfigToken token = CreateToken(le, request, pixels, roi);
                effect.KaintResetCancel();
                rendering = effect;
                if (cancelRequested)
                    effect.SignalCancelRequest();

                using (RenderArgs dstArgs = new RenderArgs(pixels.Destination))
                using (RenderArgs srcArgs = new RenderArgs(pixels.Source))
                {
                    effect.SetRenderInfo(token, dstArgs, srcArgs);
                    if (effect.CheckForEffectFlags(EffectFlags.SingleRenderCall))
                    {
                        // Como Paint.NET: toda la selección de una vez (algunos
                        // procesan columnas enteras y fallan con franjas).
                        effect.Render(token, dstArgs, srcArgs, new Rectangle[] { roi }, 0, 1);
                    }
                    else
                    {
                        Rectangle[] strips = Strips(roi, effect);
                        int threads = effect.CheckForEffectFlags(EffectFlags.SingleThreaded) ? 1 : Math.Max(1, Environment.ProcessorCount);
                        RunStrips(effect, token, dstArgs, srcArgs, strips, Math.Min(threads, strips.Length));
                    }
                }
            }
            if (cancelRequested)
            {
                Dictionary<string, object> r = Error("Cancelado");
                r["cancelled"] = true;
                return r;
            }
            return Ok();
        }

        // Franjas horizontales como el reparto en mosaicos de Paint.NET.
        private static Rectangle[] Strips(Rectangle roi, Effect effect)
        {
            int count = effect.CheckForEffectFlags(EffectFlags.SingleThreaded) ? Math.Min(roi.Height, 16) : Math.Min(roi.Height, Math.Max(1, Environment.ProcessorCount) * 8);
            count = Math.Max(1, count);
            Rectangle[] strips = new Rectangle[count];
            for (int i = 0; i < count; ++i)
            {
                int y0 = roi.Top + (int)((long)roi.Height * i / count);
                int y1 = roi.Top + (int)((long)roi.Height * (i + 1) / count);
                strips[i] = new Rectangle(roi.Left, y0, roi.Width, y1 - y0);
            }
            return strips;
        }

        private static void RunStrips(Effect effect, EffectConfigToken token, RenderArgs dstArgs, RenderArgs srcArgs, Rectangle[] strips, int threadCount)
        {
            int next = -1;
            int done = 0;
            Exception failure = null;
            long lastReport = 0;
            System.Diagnostics.Stopwatch clock = System.Diagnostics.Stopwatch.StartNew();
            ThreadStart work = () =>
            {
                while (!cancelRequested && failure == null)
                {
                    int i = Interlocked.Increment(ref next);
                    if (i >= strips.Length)
                        break;
                    try
                    {
                        effect.Render(token, dstArgs, srcArgs, strips, i, 1);
                    }
                    catch (Exception ex)
                    {
                        Interlocked.CompareExchange(ref failure, ex, null);
                        break;
                    }
                    int n = Interlocked.Increment(ref done);
                    long now = clock.ElapsedMilliseconds;
                    long last = Interlocked.Read(ref lastReport);
                    if (now - last >= 100 && Interlocked.CompareExchange(ref lastReport, now, last) == last)
                    {
                        Dictionary<string, object> progress = new Dictionary<string, object>();
                        progress["progress"] = n;
                        progress["total"] = strips.Length;
                        Send(progress);
                    }
                }
            };
            Thread[] workers = new Thread[threadCount - 1];
            for (int i = 0; i < workers.Length; ++i)
            {
                workers[i] = new Thread(work);
                workers[i].IsBackground = true;
                workers[i].Start();
            }
            work();
            foreach (Thread t in workers)
                t.Join();
            if (failure != null)
                throw failure;
        }

        // --- Diálogos propios de los complementos (WinForms de mono) ---

        private static Dictionary<string, object> ShowDialog(Dictionary<string, object> request)
        {
            LoadedEffect le = FindEffect(request);
            Effect effect = le.Instance;
            int width = (int)Json.Num(request, "width");
            int height = (int)Json.Num(request, "height");
            Rectangle bounds = new Rectangle(0, 0, width, height);
            Rectangle roi = Rectangle.Intersect(bounds, RectFrom(request, "roi", bounds));
            if (string.IsNullOrEmpty(Environment.GetEnvironmentVariable("DISPLAY")))
                return Error(L("El diálogo de este complemento usa WinForms y necesita X11 (XWayland).",
                               "This plugin's dialog uses WinForms and needs X11 (XWayland)."));

            using (SharedPixels pixels = new SharedPixels(Json.Str(request, "shm"), width, height))
            {
                Services.Instance.SetCurrentPalette(PaletteFrom(request));
                effect.EnvironmentParameters = new EffectEnvironmentParameters(
                    ColorFrom(request, "primary", ColorBgra.Black),
                    ColorFrom(request, "secondary", ColorBgra.White),
                    (float)Json.Num(request, "brushWidth", 2),
                    new PdnRegion(roi),
                    pixels.Source);
                using (EffectConfigDialog dialog = effect.CreateConfigDialog())
                {
                    if (dialog == null)
                        return Error(L("El complemento no tiene diálogo.", "The plugin has no dialog."));
                    dialog.Effect = effect;
                    dialog.EffectSourceSurface = pixels.Source;
                    dialog.Selection = new PdnRegion(roi);
                    if (le.DialogToken != null)
                        dialog.EffectToken = (EffectConfigToken)le.DialogToken.Clone();
                    string title = Json.Str(request, "title");
                    if (!string.IsNullOrEmpty(title))
                        dialog.Text = title;
                    dialog.StartPosition = FormStartPosition.CenterScreen;
                    dialog.TopMost = true;
                    DialogResult result = dialog.ShowDialog();
                    Dictionary<string, object> r = Ok();
                    r["accepted"] = result == DialogResult.OK;
                    if (result == DialogResult.OK && dialog.EffectToken != null)
                        le.DialogToken = (EffectConfigToken)dialog.EffectToken.Clone();
                    return r;
                }
            }
        }
    }
}
