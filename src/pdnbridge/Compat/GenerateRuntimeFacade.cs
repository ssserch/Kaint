// Kaint: genera el código de Kaint.Runtime.dll, una fachada que reenvía todos
// los tipos públicos de mscorlib, System y System.Core de mono.
//
// En .NET moderno casi toda la biblioteca base se referencia como
// "System.Runtime"; la fachada System.Runtime de mono solo reenvía una parte
// (System.Math, por ejemplo, está en su fachada System.Runtime.Extensions).
// El proceso auxiliar cambia esa referencia de los complementos de .NET
// moderno por "Kaint.Runtime" (ver Host/ModernAssembly.cs) y les da esta.
//
// Uso: mono GenerateRuntimeFacade.exe salida.cs
// (se compila con -r:System -r:System.Core -r:System.Configuration)

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;

internal static class GenerateRuntimeFacade
{
    private static string TypeOfName(Type t)
    {
        string name = t.FullName;
        int tick = name.IndexOf('`');
        if (tick < 0)
            return "global::" + name;
        // Genéricos sin argumentos: typeof(global::N.Nombre<,>).
        int arity = t.GetGenericArguments().Length;
        return "global::" + name.Substring(0, tick) + "<" + new string(',', arity - 1) + ">";
    }

    public static int Main(string[] args)
    {
        Assembly[] sources = {
            typeof(object).Assembly,                         // mscorlib
            typeof(Uri).Assembly,                            // System
            typeof(Enumerable).Assembly,                     // System.Core
        };
        HashSet<string> seen = new HashSet<string>();
        StringBuilder sb = new StringBuilder();
        sb.AppendLine("// Generado por GenerateRuntimeFacade.cs: no editar.");
        sb.AppendLine("using System.Reflection;");
        sb.AppendLine("using System.Runtime.CompilerServices;");
        sb.AppendLine("[assembly: AssemblyVersion(\"6.0.0.0\")]");
        foreach (Assembly asm in sources)
        {
            foreach (Type t in asm.GetExportedTypes().OrderBy(x => x.FullName, StringComparer.Ordinal))
            {
                // Los anidados viajan con su tipo; System.Void no se puede nombrar en C#.
                if (t.IsNested || t == typeof(void) || t.FullName.Contains("<") || !seen.Add(t.FullName))
                    continue;
                // Sin espacio de nombres u obsoletos con error: C# no deja nombrarlos.
                if (t.Namespace == null)
                    continue;
                ObsoleteAttribute obsolete = (ObsoleteAttribute)Attribute.GetCustomAttribute(t, typeof(ObsoleteAttribute));
                if (obsolete != null && obsolete.IsError)
                    continue;
                sb.Append("[assembly: TypeForwardedTo(typeof(").Append(TypeOfName(t)).AppendLine("))]");
            }
        }
        File.WriteAllText(args[0], sb.ToString());
        return 0;
    }
}
