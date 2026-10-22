// Kaint: versión simplificada de PdnBaseForm de Paint.NET 3.36. Los diálogos
// propios de algunos complementos (EffectConfigDialog) derivan de ella y se
// muestran con el WinForms de mono.

using System;
using System.Drawing;
using System.Windows.Forms;

namespace PaintDotNet
{
    public class PdnBaseForm
        : Form
    {
        private bool forceActiveTitleBar;
        private bool enableOpacity = true;

        public PdnBaseForm()
        {
            this.KeyPreview = true;
            this.ShowInTaskbar = false;
            this.StartPosition = FormStartPosition.CenterScreen;
        }

        public bool ForceActiveTitleBar
        {
            get { return this.forceActiveTitleBar; }
            set { this.forceActiveTitleBar = value; }
        }

        public bool EnableOpacity
        {
            get { return this.enableOpacity; }
            set { this.enableOpacity = value; }
        }

        public bool EnableInstanceOpacity
        {
            get { return this.enableOpacity; }
            set { this.enableOpacity = value; }
        }

        public static bool EnableAllOpacity
        {
            get { return true; }
            set { }
        }

        public bool IsShown
        {
            get { return this.Visible; }
        }

        public bool UseAppThemeColors { get; set; }

        protected override void Dispose(bool disposing)
        {
            base.Dispose(disposing);
        }
    }
}
