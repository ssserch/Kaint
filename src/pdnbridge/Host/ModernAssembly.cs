// Carga complementos compilados para .NET moderno (Paint.NET 4.2, 4.3 y 4.4, y los de la API
// clásica de Paint.NET 5) en mono y en el .NET Framework de Windows.
//
// Sus referencias a System.Drawing.Primitives, System.Drawing.Common y
// System.Runtime apuntan a ensamblados que en mono están repartidos de otra
// forma, y mono resuelve esos nombres con sus fachadas sin pasar por
// AssemblyResolve. Antes de cargar el complemento se cambian esos nombres en
// el montón #Strings de los metadatos: los dos primeros por System.Drawing y
// System.Runtime por Kaint.Runtime (Compat/GenerateRuntimeFacade.cs).
//
// El .NET Framework, además, exige que el ensamblado que se da para una referencia con nombre
// seguro (System.Drawing, Version=6.0.0.0, PublicKeyToken=cc7b13ffcd2ddd51...) sea ese mismo:
// darle el System.Drawing 4.0 de Windows falla con "la definición del manifiesto no coincide", y
// Kaint.Runtime (sin firmar) con "se necesita un ensamblado con nombre seguro". Allí se quita
// también la clave pública de las referencias de .NET moderno (versión 5 o más, o los nombres que
// no existen en el .NET Framework), que pasan a resolverse por su nombre en
// Program.ResolveAssembly.

using System;
using System.Collections.Generic;
using System.Text;

namespace Kaint.PdnHost
{
    internal static class ModernAssembly
    {
        // Nombre de ensamblado de .NET moderno -> el que se usa en su lugar.
        private static readonly string[,] Renamed = {
            { "System.Drawing.Primitives", "System.Drawing" },
            { "System.Drawing.Common", "System.Drawing" },
            // Casi toda la biblioteca base: Kaint.Runtime.dll lo reenvía a
            // mscorlib, System y System.Core (Compat/GenerateRuntimeFacade.cs).
            { "System.Runtime", "Kaint.Runtime" },
        };

        // Nombres que solo existen en .NET moderno (sus referencias nunca valen tal cual en el .NET
        // Framework, sea cual sea su versión).
        private static readonly HashSet<string> ModernOnly = new HashSet<string> {
            "System.Private.CoreLib", "System.Private.Uri", "System.Private.Xml", "System.Drawing.Common",
            "System.Drawing.Primitives", "System.Windows.Forms.Primitives", "Kaint.Runtime",
        };

        private static readonly bool OnMono = Type.GetType("Mono.Runtime") != null;

        // Copia del ensamblado con los nombres cambiados, o null si no hace falta
        // (o si no es un ensamblado .NET que se entienda).
        public static byte[] Patch(byte[] image)
        {
            Metadata md;
            if (!Metadata.Read(image, out md))
                return null;
            byte[] copy = null;
            for (int r = 0; r < Renamed.GetLength(0); ++r)
            {
                byte[] needle = Encoding.ASCII.GetBytes(Renamed[r, 0]);
                byte[] target = Encoding.ASCII.GetBytes(Renamed[r, 1]);
                int start = md.StringsStart, size = md.StringsSize;
                for (int i = start; i + needle.Length < start + size; ++i)
                {
                    // Una entrada entera del montón: precedida y seguida de un cero.
                    if ((i > start && image[i - 1] != 0) || image[i + needle.Length] != 0 || !Matches(image, i, needle))
                        continue;
                    if (copy == null)
                        copy = (byte[])image.Clone();
                    // El nombre nuevo (más corto) + cero: el resto de la entrada queda
                    // como una cola sin usar (si otra compartía el final, sigue igual).
                    Array.Copy(target, 0, copy, i, target.Length);
                    copy[i + target.Length] = 0;
                }
            }
            if (!OnMono)
                copy = StripModernKeys(copy ?? image, md) ?? copy;
            return copy;
        }

        // Quita la clave pública (y la marca de que la lleva) de las referencias de .NET moderno.
        // Devuelve la imagen cambiada o null si no había ninguna.
        private static byte[] StripModernKeys(byte[] image, Metadata md)
        {
            if (md.AssemblyRefRows <= 0)
                return null;
            byte[] copy = null;
            for (int row = 0; row < md.AssemblyRefRows; ++row)
            {
                int at = md.AssemblyRefStart + row * md.AssemblyRefRowSize;
                int major = U16(image, at);
                // MajorVersion, MinorVersion, BuildNumber, RevisionNumber, Flags, PublicKeyOrToken...
                int keyAt = at + 12;
                int nameAt = keyAt + md.BlobIndexSize;
                int key = md.BlobIndexSize == 2 ? U16(image, keyAt) : I32(image, keyAt);
                int nameIndex = md.StringIndexSize == 2 ? U16(image, nameAt) : I32(image, nameAt);
                if (key == 0)
                    continue;
                string name = ReadString(copy ?? image, md.StringsStart + nameIndex);
                bool modern = major >= 5 || ModernOnly.Contains(name);
                // System.Windows.Forms 5.0/6.0 llevan la misma clave que la 4.0 de Windows; aun así
                // la versión no coincide, así que también se quita.
                if (!modern || name.StartsWith("PaintDotNet", StringComparison.Ordinal))
                    continue;
                if (copy == null)
                    copy = (byte[])image.Clone();
                for (int k = 0; k < md.BlobIndexSize; ++k)
                    copy[keyAt + k] = 0;
                // Flags: sin afPublicKey (0x0001).
                copy[at + 8] = (byte)(copy[at + 8] & ~1);
            }
            return copy;
        }

        private static string ReadString(byte[] d, int at)
        {
            int end = at;
            while (end < d.Length && d[end] != 0)
                ++end;
            return Encoding.UTF8.GetString(d, at, end - at);
        }

        private static bool Matches(byte[] data, int at, byte[] needle)
        {
            for (int k = 0; k < needle.Length; ++k)
                if (data[at + k] != needle[k])
                    return false;
            return true;
        }

        private static int U16(byte[] d, int at) { return d[at] | (d[at + 1] << 8); }
        private static int I32(byte[] d, int at) { return d[at] | (d[at + 1] << 8) | (d[at + 2] << 16) | (d[at + 3] << 24); }

        // Lo que hace falta de los metadatos (ECMA-335, II.24): el montón #Strings y la tabla
        // AssemblyRef del flujo #~.
        private struct Metadata
        {
            public int StringsStart, StringsSize;
            public int StringIndexSize, BlobIndexSize;
            public int AssemblyRefStart, AssemblyRefRows, AssemblyRefRowSize;

            // Índices codificados: bits de la etiqueta y las tablas a las que pueden apuntar.
            private static readonly int[] TypeDefOrRef = { 0x02, 0x01, 0x1B };
            private static readonly int[] HasConstant = { 0x04, 0x08, 0x17 };
            private static readonly int[] HasCustomAttribute = { 0x06, 0x04, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x00, 0x0E, 0x17, 0x14,
                                                                 0x11, 0x1A, 0x1B, 0x20, 0x23, 0x26, 0x27, 0x28, 0x2A, 0x2C, 0x2B };
            private static readonly int[] HasFieldMarshal = { 0x04, 0x08 };
            private static readonly int[] HasDeclSecurity = { 0x02, 0x06, 0x20 };
            private static readonly int[] MemberRefParent = { 0x02, 0x01, 0x1A, 0x06, 0x1B };
            private static readonly int[] HasSemantics = { 0x14, 0x17 };
            private static readonly int[] MethodDefOrRef = { 0x06, 0x0A };
            private static readonly int[] MemberForwarded = { 0x04, 0x06 };
            private static readonly int[] CustomAttributeType = { 0x06, 0x0A, 0x06, 0x0A, 0x06 };
            private static readonly int[] ResolutionScope = { 0x00, 0x1A, 0x23, 0x01 };

            public static bool Read(byte[] d, out Metadata md)
            {
                md = new Metadata();
                try
                {
                    if (d.Length < 0x40 || d[0] != 'M' || d[1] != 'Z')
                        return false;
                    int pe = I32(d, 0x3C);
                    if (I32(d, pe) != 0x00004550) // "PE\0\0"
                        return false;
                    int coff = pe + 4;
                    int sections = U16(d, coff + 2);
                    int optSize = U16(d, coff + 16);
                    int opt = coff + 20;
                    int magic = U16(d, opt);
                    int dirs = opt + (magic == 0x20B ? 112 : 96);
                    int cliRva = I32(d, dirs + 14 * 8);
                    int sectionTable = opt + optSize;
                    Func<int, int> toOffset = rva =>
                    {
                        for (int s = 0; s < sections; ++s)
                        {
                            int h = sectionTable + s * 40;
                            int va = I32(d, h + 12), rawSize = I32(d, h + 16), raw = I32(d, h + 20);
                            int vsize = Math.Max(I32(d, h + 8), rawSize);
                            if (rva >= va && rva < va + vsize)
                                return rva - va + raw;
                        }
                        return -1;
                    };
                    int cli = toOffset(cliRva);
                    if (cli < 0)
                        return false;
                    int meta = toOffset(I32(d, cli + 8));
                    if (meta < 0 || I32(d, meta) != 0x424A5342) // "BSJB"
                        return false;
                    int versionLength = I32(d, meta + 12);
                    int p = meta + 16 + versionLength;
                    int streams = U16(d, p + 2);
                    p += 4;
                    int tables = -1;
                    for (int s = 0; s < streams; ++s)
                    {
                        int offset = I32(d, p), length = I32(d, p + 4);
                        int nameStart = p + 8, nameEnd = nameStart;
                        while (d[nameEnd] != 0)
                            ++nameEnd;
                        string name = Encoding.ASCII.GetString(d, nameStart, nameEnd - nameStart);
                        p = nameStart + ((nameEnd - nameStart + 4) & ~3);
                        if (name == "#Strings")
                        {
                            md.StringsStart = meta + offset;
                            md.StringsSize = length;
                        }
                        else if (name == "#~" || name == "#-")
                        {
                            tables = meta + offset;
                        }
                    }
                    if (md.StringsStart <= 0 || md.StringsStart + md.StringsSize > d.Length)
                        return false;
                    if (tables > 0)
                        ReadTables(d, tables, ref md);
                    return true;
                }
                catch (IndexOutOfRangeException)
                {
                    return false;
                }
            }

            private static void ReadTables(byte[] d, int at, ref Metadata md)
            {
                int heapSizes = d[at + 6];
                long valid = (long)((uint)I32(d, at + 8)) | ((long)I32(d, at + 12) << 32);
                int[] rows = new int[64];
                int p = at + 24;
                for (int t = 0; t < 64; ++t)
                {
                    if ((valid & (1L << t)) == 0)
                        continue;
                    rows[t] = I32(d, p);
                    p += 4;
                }
                // Con datos extra (#- de algunos ofuscadores) viene un entero más.
                if ((heapSizes & 0x40) != 0)
                    p += 4;
                int str = (heapSizes & 1) != 0 ? 4 : 2;
                int guid = (heapSizes & 2) != 0 ? 4 : 2;
                int blob = (heapSizes & 4) != 0 ? 4 : 2;
                md.StringIndexSize = str;
                md.BlobIndexSize = blob;
                Func<int, int> idx = t => rows[t] < 65536 ? 2 : 4;
                Func<int[], int, int> coded = (list, bits) =>
                {
                    int max = 0;
                    foreach (int t in list)
                        max = Math.Max(max, rows[t]);
                    return max < (1 << (16 - bits)) ? 2 : 4;
                };
                int typeDefOrRef = coded(TypeDefOrRef, 2), hasConstant = coded(HasConstant, 2);
                int hasCustomAttribute = coded(HasCustomAttribute, 5), hasFieldMarshal = coded(HasFieldMarshal, 1);
                int hasDeclSecurity = coded(HasDeclSecurity, 2), memberRefParent = coded(MemberRefParent, 3);
                int hasSemantics = coded(HasSemantics, 1), methodDefOrRef = coded(MethodDefOrRef, 1);
                int memberForwarded = coded(MemberForwarded, 1), customAttributeType = coded(CustomAttributeType, 3);
                int resolutionScope = coded(ResolutionScope, 2);
                int[] size = new int[0x24];
                size[0x00] = 2 + str + guid * 3;                                  // Module
                size[0x01] = resolutionScope + str * 2;                           // TypeRef
                size[0x02] = 4 + str * 2 + typeDefOrRef + idx(0x04) + idx(0x06);  // TypeDef
                size[0x03] = idx(0x04);                                           // FieldPtr
                size[0x04] = 2 + str + blob;                                      // Field
                size[0x05] = idx(0x06);                                           // MethodPtr
                size[0x06] = 4 + 2 + 2 + str + blob + idx(0x08);                  // MethodDef
                size[0x07] = idx(0x08);                                           // ParamPtr
                size[0x08] = 2 + 2 + str;                                         // Param
                size[0x09] = idx(0x02) + typeDefOrRef;                            // InterfaceImpl
                size[0x0A] = memberRefParent + str + blob;                        // MemberRef
                size[0x0B] = 2 + hasConstant + blob;                              // Constant
                size[0x0C] = hasCustomAttribute + customAttributeType + blob;     // CustomAttribute
                size[0x0D] = hasFieldMarshal + blob;                              // FieldMarshal
                size[0x0E] = 2 + hasDeclSecurity + blob;                          // DeclSecurity
                size[0x0F] = 2 + 4 + idx(0x02);                                   // ClassLayout
                size[0x10] = 4 + idx(0x04);                                       // FieldLayout
                size[0x11] = blob;                                                // StandAloneSig
                size[0x12] = idx(0x02) + idx(0x14);                               // EventMap
                size[0x13] = idx(0x14);                                           // EventPtr
                size[0x14] = 2 + str + typeDefOrRef;                              // Event
                size[0x15] = idx(0x02) + idx(0x17);                               // PropertyMap
                size[0x16] = idx(0x17);                                           // PropertyPtr
                size[0x17] = 2 + str + blob;                                      // Property
                size[0x18] = 2 + idx(0x06) + hasSemantics;                        // MethodSemantics
                size[0x19] = idx(0x02) + methodDefOrRef * 2;                      // MethodImpl
                size[0x1A] = str;                                                 // ModuleRef
                size[0x1B] = blob;                                                // TypeSpec
                size[0x1C] = 2 + memberForwarded + str + idx(0x1A);               // ImplMap
                size[0x1D] = 4 + idx(0x04);                                       // FieldRVA
                size[0x1E] = 8;                                                   // EncLog
                size[0x1F] = 4;                                                   // EncMap
                size[0x20] = 4 + 8 + 4 + blob + str * 2;                          // Assembly
                size[0x21] = 4;                                                   // AssemblyProcessor
                size[0x22] = 12;                                                  // AssemblyOS
                size[0x23] = 8 + 4 + blob + str * 2 + blob;                       // AssemblyRef
                for (int t = 0; t < 0x23; ++t)
                    p += rows[t] * size[t];
                md.AssemblyRefStart = p;
                md.AssemblyRefRows = rows[0x23];
                md.AssemblyRefRowSize = size[0x23];
                if (p + rows[0x23] * size[0x23] > d.Length)
                    md.AssemblyRefRows = 0;
            }
        }
    }
}
