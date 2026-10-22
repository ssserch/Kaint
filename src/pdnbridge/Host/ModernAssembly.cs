// Carga en mono complementos compilados para .NET moderno (Paint.NET 4.3/4.4).
//
// Sus referencias a System.Drawing.Primitives, System.Drawing.Common y
// System.Runtime apuntan a ensamblados que en mono están repartidos de otra
// forma, y mono resuelve esos nombres con sus fachadas sin pasar por
// AssemblyResolve. Antes de cargar el complemento se cambian esos nombres en
// el montón #Strings de los metadatos: los dos primeros por System.Drawing y
// System.Runtime por Kaint.Runtime (Compat/GenerateRuntimeFacade.cs).

using System;
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

        // Copia del ensamblado con los nombres cambiados, o null si no hace falta
        // (o si no es un ensamblado .NET que se entienda).
        public static byte[] Patch(byte[] image)
        {
            int start, size;
            if (!FindStringsHeap(image, out start, out size))
                return null;
            byte[] copy = null;
            for (int r = 0; r < Renamed.GetLength(0); ++r)
            {
                byte[] needle = Encoding.ASCII.GetBytes(Renamed[r, 0]);
                byte[] target = Encoding.ASCII.GetBytes(Renamed[r, 1]);
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
            return copy;
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

        // Posición y tamaño del montón #Strings (PE -> cabecera CLI -> metadatos).
        private static bool FindStringsHeap(byte[] d, out int start, out int size)
        {
            start = size = 0;
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
                        start = meta + offset;
                        size = length;
                        return start > 0 && start + size <= d.Length;
                    }
                }
            }
            catch (IndexOutOfRangeException)
            {
            }
            return false;
        }
    }
}
