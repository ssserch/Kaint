// Kaint: formato de archivo (FileType clásico de Paint.NET) de prueba para el sistema .NET:
// "KRAW", ancho y alto (int32) y los píxeles BGRA. tests/core_tests.cpp lo guarda y lo vuelve a abrir.
using System;
using System.IO;
using PaintDotNet;

namespace KaintTest
{
    public sealed class KrawFileType : FileType
    {
        public KrawFileType()
            : base("Kaint Raw", FileTypeFlags.SupportsLoading | FileTypeFlags.SupportsSaving, new[] { ".kraw" })
        {
        }

        protected override Document OnLoad(Stream input)
        {
            var reader = new BinaryReader(input);
            if (new string(reader.ReadChars(4)) != "KRAW")
                throw new FormatException("not a KRAW file");
            int w = reader.ReadInt32(), h = reader.ReadInt32();
            var surface = new Surface(w, h);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    byte[] p = reader.ReadBytes(4);
                    surface[x, y] = ColorBgra.FromBgra(p[0], p[1], p[2], p[3]);
                }
            var doc = new Document(w, h);
            doc.Layers.Add(new BitmapLayer(surface, true));
            return doc;
        }

        protected override void OnSave(Document input, Stream output, SaveConfigToken token, Surface scratchSurface,
                                       ProgressEventHandler progressCallback)
        {
            Surface s = ((BitmapLayer)input.Layers[0]!).Surface;
            var writer = new BinaryWriter(output);
            writer.Write("KRAW".ToCharArray());
            writer.Write(s.Width);
            writer.Write(s.Height);
            for (int y = 0; y < s.Height; ++y)
                for (int x = 0; x < s.Width; ++x)
                {
                    ColorBgra c = s[x, y];
                    writer.Write(new[] { c.B, c.G, c.R, c.A });
                }
            writer.Flush();
        }
    }

    public sealed class Factory : IFileTypeFactory
    {
        public FileType[] GetFileTypeInstances() => new FileType[] { new KrawFileType() };
    }
}
