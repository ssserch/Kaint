// Kaint: servicios de Paint.NET 4.x que los complementos piden con
// Effect.Services.GetService (abrir enlaces, carpeta de archivos de usuario,
// nombres de enumeraciones, paletas y portapapeles).

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Windows.Forms;
using PaintDotNet;
using PaintDotNet.AppModel;
using PaintDotNet.Clipboard;

namespace Kaint.PdnHost
{
    internal sealed class Services
        : IServiceProvider,
          IShellService,
          IUserFilesService,
          IEnumLocalizerFactory,
          IPalettesService,
          IClipboardService
    {
        private Services()
        {
            ClipboardExtensions.KaintReadSurface = ReadClipboardSurface;
        }

        public object GetService(Type serviceType)
        {
            if (serviceType == typeof(IShellService) || serviceType == typeof(IUserFilesService)
                || serviceType == typeof(IEnumLocalizerFactory) || serviceType == typeof(IPalettesService)
                || serviceType == typeof(IClipboardService))
                return this;
            return null;
        }

        // --- Paletas ---

        // La de Paint.NET: 64 colores opacos y los 32 primeros al 50 %.
        private static readonly uint[] DefaultColors = {
            0xff000000, 0xff404040, 0xffff0000, 0xffff6a00, 0xffffd800, 0xffb6ff00, 0xff4cff00, 0xff00ff21,
            0xff00ff90, 0xff00ffff, 0xff0094ff, 0xff0026ff, 0xff4800ff, 0xffb200ff, 0xffff00dc, 0xffff006e,
            0xffffffff, 0xff808080, 0xff7f0000, 0xff7f3300, 0xff7f6a00, 0xff5b7f00, 0xff267f00, 0xff007f0e,
            0xff007f46, 0xff007f7f, 0xff004a7f, 0xff00137f, 0xff21007f, 0xff57007f, 0xff7f006e, 0xff7f0037,
            0xffa0a0a0, 0xff303030, 0xffff7f7f, 0xffffb27f, 0xffffe97f, 0xffdaff7f, 0xffa5ff7f, 0xff7fff8e,
            0xff7fffc5, 0xff7fffff, 0xff7fc9ff, 0xff7f92ff, 0xffa17fff, 0xffd67fff, 0xffff7fed, 0xffff7fb6,
            0xffc0c0c0, 0xff606060, 0xff7f3f3f, 0xff7f593f, 0xff7f743f, 0xff6d7f3f, 0xff527f3f, 0xff3f7f47,
            0xff3f7f62, 0xff3f7f7f, 0xff3f647f, 0xff3f497f, 0xff503f7f, 0xff6b3f7f, 0xff7f3f76, 0xff7f3f5b,
        };

        // Después de DefaultColors: los campos estáticos se inicializan en orden.
        public static readonly Services Instance = new Services();

        private static IReadOnlyList<ColorBgra> CreateDefaultPalette()
        {
            List<ColorBgra> list = new List<ColorBgra>();
            foreach (uint c in DefaultColors)
                list.Add(ColorBgra.FromUInt32(c));
            for (int i = 0; i < 32; ++i)
                list.Add(ColorBgra.FromUInt32((DefaultColors[i] & 0x00ffffff) | 0x80000000));
            return list.AsReadOnly();
        }

        private readonly IReadOnlyList<ColorBgra> defaultPalette = CreateDefaultPalette();
        private IReadOnlyList<ColorBgra> currentPalette;

        // La paleta de Kaint, que llega con cada petición (vacía: la de Paint.NET).
        public void SetCurrentPalette(List<ColorBgra> colors)
        {
            this.currentPalette = colors == null || colors.Count == 0 ? null : colors.AsReadOnly();
        }

        public IReadOnlyList<ColorBgra> CurrentPalette
        {
            get { return this.currentPalette ?? this.defaultPalette; }
        }

        public IReadOnlyList<ColorBgra> DefaultPalette
        {
            get { return this.defaultPalette; }
        }

        // --- Portapapeles ---

        // La imagen del portapapeles del sistema en PNG: wl-paste (Wayland) o
        // xclip (X11). Sin ninguno de los dos, o sin imagen, null.
        private static Surface ReadClipboardSurface()
        {
            foreach (string[] cmd in new[] {
                         new[] { "wl-paste", "--no-newline --type image/png" },
                         new[] { "xclip", "-selection clipboard -target image/png -out" } })
            {
                try
                {
                    ProcessStartInfo psi = new ProcessStartInfo(cmd[0], cmd[1]);
                    psi.UseShellExecute = false;
                    psi.RedirectStandardOutput = true;
                    psi.RedirectStandardError = true;
                    using (Process p = Process.Start(psi))
                    using (MemoryStream png = new MemoryStream())
                    {
                        p.StandardOutput.BaseStream.CopyTo(png);
                        p.WaitForExit(5000);
                        if (p.ExitCode != 0 || png.Length == 0)
                            continue;
                        png.Position = 0;
                        using (Bitmap bitmap = new Bitmap(png))
                            return Surface.CopyFromBitmap(bitmap);
                    }
                }
                catch (Exception)
                {
                }
            }
            return null;
        }

        private static bool Open(string target)
        {
            try
            {
                Process.Start(new ProcessStartInfo("xdg-open", "\"" + target.Replace("\"", "") + "\"") { UseShellExecute = false });
                return true;
            }
            catch (Exception)
            {
                return false;
            }
        }

        public bool LaunchFolder(IWin32Window owner, string folderPath)
        {
            return Open(folderPath);
        }

        public bool LaunchUrl(IWin32Window owner, string url)
        {
            return Open(url);
        }

        public string UserFilesPath
        {
            get
            {
                string path = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Personal), "Kaint");
                return path;
            }
        }

        public IEnumLocalizer Create(Type enumType)
        {
            return new EnumLocalizer(enumType);
        }

        private sealed class LocalizedValue
            : ILocalizedEnumValue
        {
            private readonly object value;

            public LocalizedValue(object value)
            {
                this.value = value;
            }

            public object EnumValue
            {
                get { return this.value; }
            }

            public string LocalizedName
            {
                get { return this.value.ToString(); }
            }
        }

        private sealed class EnumLocalizer
            : IEnumLocalizer
        {
            private readonly Type type;

            public EnumLocalizer(Type type)
            {
                this.type = type;
            }

            public IList<ILocalizedEnumValue> GetLocalizedEnumValues()
            {
                List<ILocalizedEnumValue> list = new List<ILocalizedEnumValue>();
                foreach (object v in Enum.GetValues(this.type))
                    list.Add(new LocalizedValue(v));
                return list;
            }

            public ILocalizedEnumValue GetLocalizedEnumValue(object enumValue)
            {
                return new LocalizedValue(enumValue);
            }
        }
    }
}
