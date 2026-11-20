// Kaint: proceso auxiliar de .NET (CoreCLR) que carga complementos de efectos de Paint.NET (3.x, 4.x
// y 5.x, también los de GPU con OpenD2D) y los ejecuta para Kaint.
//
// Mismo protocolo que kaint-pdn-host.exe (mono): una petición JSON por línea en stdin y una
// respuesta JSON por línea en stdout; antes de la respuesta de "render" pueden llegar líneas
// {"progress":n,"total":m}. Los píxeles viajan por memoria compartida con el origen y luego el
// destino en BGRA de alfa directo. Lo de dentro (cargar, describir, calcular) es el puente de
// paint.rs (third_party/pdn5/bridge).

using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Threading;
using PaintDotNet;
using PaintDotNet.Bridge;
using PaintDotNet.Effects;
using PaintDotNet.Effects.Gpu;
using PaintDotNet.IndirectUI;
using PaintDotNet.PropertySystem;

namespace Kaint.PdnHost5
{
    public static class Program
    {
        private const int ProtocolVersion = 2;

        private static readonly object outLock = new();
        private static TextWriter output = null!;
        private static readonly Dictionary<string, PluginContext> plugins = new(StringComparer.Ordinal);
        private static Thread? renderThread;
        private static volatile bool cancelRequested;

        private static readonly bool Spanish = Environment.GetEnvironmentVariable("KAINT_LANG") == "es";
        private static string L(string es, string en) => Spanish ? es : en;

        public static int Main(string[] args)
        {
            output = new StreamWriter(Console.OpenStandardOutput(), new UTF8Encoding(false));
            // Lo que escriban los complementos en la consola no debe romper el protocolo.
            Console.SetOut(Console.Error);
            Gate.AllowAll = Environment.GetEnvironmentVariable("KAINT_PDN_GATE") != "1";
            // Los complementos compilados para .NET 11 (Paint.NET 5.2) se cargan en este .NET 10.
            Func<byte[], byte[]?>? previous = PluginHost.Rewrite;
            PluginHost.Rewrite = image =>
            {
                byte[]? changed = previous?.Invoke(image);
                byte[]? retargeted = Retarget.Apply(changed ?? image);
                return retargeted ?? changed;
            };

            if (args.Length >= 3 && args[0] == "stamp-version")
            {
                // publish.sh: System.Drawing.Common 6.0 (el de Unix) con la versión que piden los
                // ensamblados (10.0.0.0); .NET no comprueba la firma de nombre seguro.
                byte[] image = File.ReadAllBytes(args[1]);
                using (var module = Mono.Cecil.ModuleDefinition.ReadModule(new MemoryStream(image)))
                {
                    module.Assembly.Name.Version = Version.Parse(args[2]);
                    module.Write(args[1]);
                }
                return 0;
            }
            if (args.Length >= 2 && args[0] == "inspect")
            {
                // Prueba a mano: kaint-pdn5-host inspect complemento.dll
                Console.Error.WriteLine(Inspect(args[1]).ToJsonString(new JsonSerializerOptions { WriteIndented = true }));
                return 0;
            }

            TextReader input = new StreamReader(Console.OpenStandardInput(), new UTF8Encoding(false));
            string? line;
            while ((line = input.ReadLine()) is not null)
            {
                if (line.Trim().Length == 0)
                    continue;
                JsonDocument request;
                try
                {
                    request = JsonDocument.Parse(line);
                }
                catch (Exception ex)
                {
                    Send(Error(L("Petición no válida: ", "Invalid request: ") + ex.Message));
                    continue;
                }
                string cmd = Str(request.RootElement, "cmd");
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
            renderThread?.Join(2000);
            return 0;
        }

        private static void Handle(string cmd, JsonDocument request)
        {
            JsonElement r = request.RootElement;
            switch (cmd)
            {
                case "hello":
                {
                    JsonObject hello = Ok();
                    hello["protocol"] = ProtocolVersion;
                    hello["runtime"] = ".NET " + Environment.Version;
                    hello["system"] = Environment.GetEnvironmentVariable("KAINT_PDN_RUNTIME") ?? ".NET";
                    hello["direct2d"] = Direct2DStatus();
                    string? openD2D = Environment.GetEnvironmentVariable("KAINT_OPEND2D_VERSION");
                    if (!string.IsNullOrEmpty(openD2D))
                        hello["opend2d"] = openD2D;
                    hello["gpu"] = true;
                    hello["headlessDialogs"] = true;
                    hello["fileTypes"] = true;
                    Send(hello);
                    break;
                }
                case "inspect":
                    Send(Inspect(Str(r, "path")));
                    break;
                case "render":
                    renderThread?.Join();
                    cancelRequested = false;
                    renderThread = new Thread(() =>
                    {
                        try
                        {
                            Send(Render(r));
                        }
                        catch (Exception ex)
                        {
                            Send(Error(Describe(ex)));
                        }
                        finally
                        {
                            request.Dispose();
                        }
                    });
                    renderThread.IsBackground = true;
                    renderThread.Priority = ThreadPriority.BelowNormal;
                    renderThread.Start();
                    return; // el hilo libera la petición
                case "cancel":
                    cancelRequested = true;
                    break;
                case "dialog":
                    // Las ventanas propias se leen sin mostrarlas (inspect): las que Kaint no puede
                    // mostrar en su diálogo las abre mono o Wine.
                    Send(Error(L("Este sistema no muestra ventanas de complementos; Kaint usa mono o Wine para ellas.",
                                 "This system does not show plugin windows; Kaint uses mono or Wine for them.")));
                    break;
                default:
                    Send(Error(L("Orden desconocida: ", "Unknown command: ") + cmd));
                    break;
            }
            request.Dispose();
        }

        // --- Respuestas ---

        private static JsonObject Ok() => new() { ["ok"] = true };

        private static JsonObject Error(string message) => new() { ["ok"] = false, ["error"] = message };

        internal static void Send(JsonNode message)
        {
            string text = message.ToJsonString();
            lock (outLock)
            {
                output.Write(text);
                output.Write('\n');
                output.Flush();
            }
        }

        private static string Describe(Exception ex)
        {
            while ((ex is TargetInvocationException || ex is TypeInitializationException) && ex.InnerException is not null)
                ex = ex.InnerException;
            if (!string.IsNullOrEmpty(Environment.GetEnvironmentVariable("KAINT_PDN_DEBUG")))
                Console.Error.WriteLine(ex.ToString());
            if (ex is DllNotFoundException && ex.Message.Contains("gdiplus"))
                return L("Falta libgdiplus (System.Drawing). Instala el paquete libgdiplus.",
                         "libgdiplus (System.Drawing) is missing. Install the libgdiplus package.");
            return PluginHost.Explain(ex);
        }

        private static string Str(JsonElement e, string name)
            => e.TryGetProperty(name, out JsonElement v) && v.ValueKind == JsonValueKind.String ? v.GetString() ?? "" : "";

        private static double Num(JsonElement e, string name, double def = 0)
            => e.TryGetProperty(name, out JsonElement v) && v.ValueKind == JsonValueKind.Number ? v.GetDouble() : def;

        // "ok" si las bibliotecas de Direct2D (OpenD2D) cargan; si no, por qué.
        private static string Direct2DStatus()
        {
            if (OperatingSystem.IsWindows())
                return "ok";
            string name = OperatingSystem.IsMacOS() ? "libd2d1.dylib" : "libd2d1.so";
            foreach (string? dir in new[] { Environment.GetEnvironmentVariable("KAINT_OPEND2D"), AppContext.BaseDirectory })
            {
                if (string.IsNullOrEmpty(dir))
                    continue;
                string path = Path.Combine(dir, name);
                if (File.Exists(path))
                    return NativeLibrary.TryLoad(path, out IntPtr h) && NativeLibrary.TryGetExport(h, "D2D1CreateFactory", out _) ? "ok" : "error";
            }
            return "missing";
        }

        // --- Inspección ---

        private static PluginContext Plugin(string path)
        {
            path = Path.GetFullPath(path);
            lock (plugins)
            {
                if (!plugins.TryGetValue(path, out PluginContext? ctx))
                    plugins[path] = ctx = PluginHost.Load(path);
                return ctx;
            }
        }

        private static JsonObject Inspect(string path)
        {
            JsonObject result = Ok();
            var effects = new JsonArray();
            var errors = new JsonArray();
            result["effects"] = effects;
            result["errors"] = errors;
            result["name"] = Path.GetFileNameWithoutExtension(path);
            result["runtime"] = "dotnet";

            PluginContext ctx;
            try
            {
                ctx = Plugin(path);
            }
            catch (BadImageFormatException)
            {
                result["library"] = true;
                errors.Add(L("No es un ensamblado .NET.", "It is not a .NET assembly."));
                return result;
            }
            ReadAssemblyInfo(ctx.Assembly, result);

            using JsonDocument listed = JsonDocument.Parse(PluginHost.In(ctx, () => PluginHost.Enumerate(ctx)));
            JsonElement root = listed.RootElement;
            foreach (JsonElement e in root.GetProperty("effects").EnumerateArray())
            {
                string id = Str(e, "id");
                try
                {
                    effects.Add(PluginHost.In(ctx, () => DescribeEffect(ctx, e)));
                }
                catch (Exception ex)
                {
                    errors.Add(Str(e, "name") + ": " + Describe(ex));
                }
            }
            foreach (JsonElement err in root.GetProperty("errors").EnumerateArray())
                errors.Add(err.GetString());
            var fileTypes = JsonNode.Parse(root.GetProperty("fileTypes").GetRawText());
            if (fileTypes is JsonArray ft && ft.Count > 0)
                result["fileTypes"] = ft;
            result["library"] = effects.Count == 0 && errors.Count == 0 && (fileTypes as JsonArray)?.Count is null or 0;
            return result;
        }

        private static void ReadAssemblyInfo(Assembly asm, JsonObject result)
        {
            AssemblyName an = asm.GetName();
            result["version"] = an.Version?.ToString() ?? "";
            try
            {
                foreach (CustomAttributeData a in asm.GetCustomAttributesData())
                {
                    if (a.ConstructorArguments.Count != 1 || a.ConstructorArguments[0].Value is not string value || value.Length == 0)
                        continue;
                    switch (a.AttributeType.Name)
                    {
                        case nameof(AssemblyTitleAttribute): result["name"] = value; break;
                        case nameof(AssemblyProductAttribute): result["product"] = value; break;
                        case nameof(AssemblyCompanyAttribute): result["author"] = value; break;
                        case nameof(AssemblyCopyrightAttribute): result["copyright"] = value; break;
                        case nameof(AssemblyDescriptionAttribute): result["description"] = value; break;
                    }
                }
            }
            catch (Exception)
            {
            }
        }

        private static Type FindType(PluginContext ctx, string id)
            => ctx.Effects.Concat(ctx.GpuEffects).FirstOrDefault(t => t.FullName == id)
               ?? throw new InvalidOperationException($"effect '{id}' not found");

        // Lo que un efecto dice de sí mismo, en el formato de Kaint.
        private static JsonObject DescribeEffect(PluginContext ctx, JsonElement listed)
        {
            string id = Str(listed, "id");
            Type t = FindType(ctx, id);
            var d = new JsonObject
            {
                ["id"] = id,
                ["name"] = Str(listed, "name"),
                ["subMenu"] = listed.TryGetProperty("submenu", out JsonElement sub) && sub.ValueKind == JsonValueKind.String ? sub.GetString() : null,
                ["gpu"] = Str(listed, "kind") == "gpu",
                ["clipboard"] = listed.TryGetProperty("clipboard", out JsonElement cb) && cb.ValueKind == JsonValueKind.True,
            };
            EffectCategory category = Attribute.GetCustomAttribute(t, typeof(EffectCategoryAttribute)) is EffectCategoryAttribute a
                ? a.Category : EffectCategory.Effect;
            d["adjustment"] = category == EffectCategory.Adjustment;
            d["hidden"] = category == EffectCategory.DoNotDisplay;

            object instance = Activator.CreateInstance(t)!;
            try
            {
                Image? icon = instance switch
                {
                    ClassicEffectBase c => c.Image,
                    EffectBase g => g.Image,
                    _ => null,
                };
                d["icon"] = IconPng(icon);

                switch (instance)
                {
                    case PropertyBasedEffect pbe:
                    {
                        pbe.EnvironmentParameters = DefaultEnvironment();
                        PropertyCollection props = pbe.CreatePropertyCollection() ?? PropertyCollection.CreateEmpty();
                        ControlInfo? ui = null;
                        try { ui = pbe.CreateConfigUI(props); } catch (Exception) { }
                        d["kind"] = props.Count > 0 ? "properties" : "none";
                        KaintSchema.Describe(props, ui, d);
                        break;
                    }
                    case EffectBase gpu when !PluginHost.HasOwnForm(t):
                    {
                        PropertyCollection props = GpuDriver.Properties(gpu);
                        ControlInfo? ui = null;
                        try { ui = GpuDriver.ConfigUI(gpu, props); } catch (Exception) { }
                        d["kind"] = props.Count > 0 ? "properties" : "none";
                        KaintSchema.Describe(props, ui, d);
                        break;
                    }
                    case Effect classic when !classic.CheckForEffectFlags(EffectFlags.Configurable):
                        d["kind"] = "none";
                        d["params"] = new JsonArray();
                        d["rules"] = new JsonArray();
                        break;
                    default:
                    {
                        // Ventana propia (EffectConfigDialog o EffectConfigForm): sus ajustes, leídos sin mostrarla.
                        using JsonDocument schema = JsonDocument.Parse(PluginHost.EffectSchema(ctx, id));
                        bool complete = KaintSchema.DescribeWindow(schema.RootElement, d);
                        if (complete && ((JsonArray)d["params"]!).Count > 0)
                        {
                            d["kind"] = "properties";
                            d["window"] = true;
                        }
                        else
                        {
                            // Ajustes que el diálogo de Kaint no sabe mostrar: la ventana real, con mono o Wine.
                            d["kind"] = "dialog";
                            d["needsWindow"] = true;
                        }
                        break;
                    }
                }
            }
            finally
            {
                (instance as IDisposable)?.Dispose();
            }
            return d;
        }

        private static EffectEnvironmentParameters DefaultEnvironment()
            => new() { SourceSurface = new Surface(1, 1), SelectionBounds = new Rectangle(0, 0, 1, 1) };

        private static string? IconPng(Image? icon)
        {
            if (icon is null)
                return null;
            try
            {
                using var ms = new MemoryStream();
                icon.Save(ms, ImageFormat.Png);
                return Convert.ToBase64String(ms.ToArray());
            }
            catch (Exception)
            {
                return null;
            }
        }

        // --- Renderizado ---

        // Memoria compartida con el origen y el destino: un archivo de /dev/shm o una memoria con
        // nombre ("Local\\...") en Windows.
        private sealed unsafe class SharedPixels : IDisposable
        {
            private readonly MemoryMappedFile file;
            private readonly MemoryMappedViewAccessor view;
            private byte* pointer;
            public readonly long Bytes;

            public SharedPixels(string path, int width, int height)
            {
                Bytes = (long)width * height * 4;
                file = path.StartsWith("Local\\", StringComparison.Ordinal) && OperatingSystem.IsWindows()
                    ? MemoryMappedFile.OpenExisting(path, MemoryMappedFileRights.ReadWrite)
                    : MemoryMappedFile.CreateFromFile(path, FileMode.Open, null, 0, MemoryMappedFileAccess.ReadWrite);
                view = file.CreateViewAccessor(0, Bytes * 2, MemoryMappedFileAccess.ReadWrite);
                view.SafeMemoryMappedViewHandle.AcquirePointer(ref pointer);
            }

            public Span<byte> Source => new(pointer + view.PointerOffset, checked((int)Bytes));
            public Span<byte> Destination => new(pointer + view.PointerOffset + Bytes, checked((int)Bytes));

            public void Dispose()
            {
                if (pointer is not null)
                    view.SafeMemoryMappedViewHandle.ReleasePointer();
                pointer = null;
                view.Dispose();
                file.Dispose();
            }
        }

        private static JsonObject Render(JsonElement request)
        {
            PluginContext ctx = Plugin(Str(request, "path"));
            string id = Str(request, "effect");
            int width = (int)Num(request, "width");
            int height = (int)Num(request, "height");
            var bounds = new Rectangle(0, 0, width, height);
            Rectangle roi = bounds;
            if (request.TryGetProperty("roi", out JsonElement r) && r.ValueKind == JsonValueKind.Array && r.GetArrayLength() == 4)
                roi = Rectangle.Intersect(bounds, new Rectangle(r[0].GetInt32(), r[1].GetInt32(), r[2].GetInt32(), r[3].GetInt32()));
            if (roi.Width <= 0 || roi.Height <= 0)
                return Ok();
            string props = KaintSchema.Props(request);

            using var pixels = new SharedPixels(Str(request, "shm"), width, height);
            byte[] buffer = pixels.Source.ToArray();
            byte[] result = PluginHost.In(ctx, () => RenderInto(ctx, id, props, buffer, width, height, roi));
            if (cancelRequested)
                return new JsonObject { ["ok"] = false, ["error"] = "Cancelado", ["cancelled"] = true };
            result.AsSpan().CopyTo(pixels.Destination);
            return Ok();
        }

        private static byte[] RenderInto(PluginContext ctx, string id, string props, byte[] buffer, int width, int height, Rectangle roi)
        {
            PluginHost.RenderJob job = PluginHost.RenderBegin(ctx, id, props, buffer, width, height, roi.X, roi.Y, roi.Width, roi.Height);
            if (job.IsWhole)
                return job.Finish(); // Paint.NET 5: todo el grafo de una vez.

            var (effect, token, dst, src, rects) = job.Parts;
            try
            {
                if (rects.Length > 0)
                {
                    // Una sola llamada: el reparto "None" de Paint.NET 5, o SingleRenderCall de 4.x (el bit 4).
                    ulong flags = (ulong)effect.Options.Flags;
                    if (effect.Options.RenderingSchedule == EffectRenderingSchedule.None || (flags & 4) != 0)
                        effect.Render(token, dst, src, rects, 0, rects.Length);
                    else
                    {
                        // Un solo hilo: SingleThreaded de Paint.NET 5 o el de 4.x (el bit 2).
                        bool single = effect.CheckForEffectFlags(EffectFlags.SingleThreaded) || (flags & 2) != 0;
                        int threads = single ? 1 : Math.Max(1, Environment.ProcessorCount);
                        RunStrips(ctx, effect, token, dst, src, rects, Math.Min(threads, rects.Length));
                    }
                }
            }
            finally
            {
                RenderArgs.FlushPending();
            }
            var output = new byte[(long)width * height * 4];
            PluginHost.StoreSurface(dst.Surface, output, width, height);
            (effect as IDisposable)?.Dispose();
            return output;
        }

        // Franjas en varios hilos, como el reparto de Paint.NET; la primera, sola (muchos complementos
        // preparan cosas en su primera llamada a Render sin protegerlas).
        private static void RunStrips(PluginContext ctx, Effect effect, EffectConfigToken? token, RenderArgs dst, RenderArgs src,
                                      Rectangle[] strips, int threadCount)
        {
            int next = -1;
            int done = 0;
            Exception? failure = null;
            long lastReport = 0;
            var clock = System.Diagnostics.Stopwatch.StartNew();
            void Work()
            {
                using (ctx.Alc.EnterContextualReflection())
                    while (!cancelRequested && failure is null)
                    {
                        int i = Interlocked.Increment(ref next);
                        if (i >= strips.Length)
                            break;
                        try
                        {
                            effect.Render(token, dst, src, strips, i, 1);
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
                            Send(new JsonObject { ["progress"] = n, ["total"] = strips.Length });
                    }
            }
            if (strips.Length > 1 && threadCount > 1 && !cancelRequested)
            {
                next = 0;
                effect.Render(token, dst, src, strips, 0, 1);
                done = 1;
            }
            var workers = new Thread[threadCount - 1];
            for (int i = 0; i < workers.Length; ++i)
            {
                workers[i] = new Thread(Work) { IsBackground = true, Priority = ThreadPriority.BelowNormal };
                workers[i].Start();
            }
            Work();
            foreach (Thread t in workers)
                t.Join();
            if (failure is not null)
                throw failure;
        }
    }
}
