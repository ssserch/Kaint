// Kaint: arreglos de los diálogos propios de los complementos (WinForms) cuando se ejecutan con
// mono, para que se vean como en Windows.

using System.Windows.Forms;

namespace Kaint.PdnHost
{
    internal static class MonoFormFixes
    {
        public static void Apply(Control root)
        {
            foreach (Control child in root.Controls)
                Fix(child);
        }

        private static void Fix(Control control)
        {
            // En .NET, los colores de un NumericUpDown (o DomainUpDown) pasan a su cuadro de texto;
            // en mono no, y los de tema oscuro (QAM Fault de PolyGlitch: fondo gris oscuro y texto
            // blanco) quedaban con el texto blanco sobre fondo claro, ilegible.
            UpDownBase upDown = control as UpDownBase;
            if (upDown != null)
            {
                foreach (Control part in upDown.Controls)
                {
                    if (part is TextBoxBase)
                    {
                        part.BackColor = upDown.BackColor;
                        part.ForeColor = upDown.ForeColor;
                    }
                }
                return;
            }
            foreach (Control child in control.Controls)
                Fix(child);
        }
    }
}
