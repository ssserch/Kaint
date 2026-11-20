// Kaint: los complementos de Paint.NET 5.2 se compilan para .NET 11 y piden System.Runtime
// 11.0.0.0 y compañía; este proceso usa .NET 10, que no los da. Al cargarlos, sus referencias a la
// biblioteca de .NET se bajan a la versión de este runtime (como ModernAssembly.Patch del proceso de
// mono). Si un complemento usa algo que solo existe en .NET 11, falla ese efecto (MissingMethod...),
// y Kaint lo intenta con mono o Wine.

using System;
using System.IO;
using Mono.Cecil;

namespace Kaint.PdnHost5
{
    internal static class Retarget
    {
        private static readonly Version Runtime = new(Environment.Version.Major, 0, 0, 0);

        private static bool IsFramework(string name)
            => name is "netstandard" or "mscorlib" or "WindowsBase"
               || name.StartsWith("System.", StringComparison.Ordinal) || name == "System"
               || name.StartsWith("Microsoft.", StringComparison.Ordinal);

        // La imagen con las referencias cambiadas, o null si no hacía falta.
        public static byte[]? Apply(byte[] image)
        {
            try
            {
                using var module = ModuleDefinition.ReadModule(new MemoryStream(image), new ReaderParameters { ReadSymbols = false });
                bool changed = false;
                foreach (AssemblyNameReference reference in module.AssemblyReferences)
                {
                    if (!IsFramework(reference.Name) || reference.Version is null || reference.Version.Major <= Runtime.Major)
                        continue;
                    // Solo las versiones de .NET (11.0.0.0...), no las de paquetes con otra numeración.
                    if (reference.Version.Minor != 0 || reference.Version.Build != 0)
                        continue;
                    reference.Version = Runtime;
                    changed = true;
                }
                if (!changed)
                    return null;
                using var outStream = new MemoryStream();
                module.Write(outStream);
                return outStream.ToArray();
            }
            catch (Exception)
            {
                return null;
            }
        }
    }
}
