// Kaint: los diálogos de configuración estándar de Paint.NET 3.x (una, dos o tres cantidades y
// el selector de ángulo) con sus tokens. Muchos complementos antiguos (los hechos con las
// primeras versiones de CodeLab, por ejemplo) no traen diálogo propio: crean uno de estos y le
// ponen las etiquetas, los rangos y los valores iniciales.
//
// Además de poder mostrarse como ventanas de WinForms, describen sus controles a Kaint
// (IKaintStandardDialog): así Kaint los muestra con su propio diálogo, con vista previa en la
// imagen, y crea el token con los valores elegidos.
//
// Escrito para Kaint a partir de la API pública de Paint.NET 3.x (no es su código).

using System;
using System.Collections.Generic;
using System.Drawing;
using System.Globalization;
using System.Windows.Forms;

namespace PaintDotNet.Effects
{
    // Un control de un diálogo estándar, para describirlo a Kaint.
    public sealed class KaintStandardParam
    {
        public string Name;
        public string Label;
        public string Type;     // "int" o "double"
        public string Control;  // "" (deslizador) o "AngleChooser"
        public double Minimum;
        public double Maximum;
        public double Default;
    }

    public interface IKaintStandardDialog
    {
        IList<KaintStandardParam> KaintDescribe();
        EffectConfigToken KaintCreateToken(IDictionary<string, object> values);
    }

    internal static class KaintStandard
    {
        public static double Number(IDictionary<string, object> values, string name, double fallback)
        {
            object v;
            if (values == null || !values.TryGetValue(name, out v) || v == null)
                return fallback;
            try
            {
                return Convert.ToDouble(v, CultureInfo.InvariantCulture);
            }
            catch (Exception)
            {
                return fallback;
            }
        }

        public static int Clamp(double v, int min, int max)
        {
            if (min > max)
            {
                int t = min;
                min = max;
                max = t;
            }
            return (int)Math.Max(min, Math.Min(max, Math.Round(v)));
        }

        public static string Spanish(string es, string en)
        {
            return Environment.GetEnvironmentVariable("KAINT_LANG") == "es" ? es : en;
        }
    }

    // --- Tokens ---

    [Serializable]
    public class AmountEffectConfigToken
        : EffectConfigToken
    {
        private int amount;

        public int Amount
        {
            get { return this.amount; }
            set { this.amount = value; }
        }

        public AmountEffectConfigToken(int amount)
        {
            this.amount = amount;
        }

        protected AmountEffectConfigToken(AmountEffectConfigToken copyMe)
            : base(copyMe)
        {
            this.amount = copyMe.amount;
        }

        public override object Clone()
        {
            return new AmountEffectConfigToken(this);
        }
    }

    [Serializable]
    public class TwoAmountsConfigToken
        : EffectConfigToken
    {
        private int amount1;
        private int amount2;

        public int Amount1
        {
            get { return this.amount1; }
            set { this.amount1 = value; }
        }

        public int Amount2
        {
            get { return this.amount2; }
            set { this.amount2 = value; }
        }

        public TwoAmountsConfigToken(int amount1, int amount2)
        {
            this.amount1 = amount1;
            this.amount2 = amount2;
        }

        protected TwoAmountsConfigToken(TwoAmountsConfigToken copyMe)
            : base(copyMe)
        {
            this.amount1 = copyMe.amount1;
            this.amount2 = copyMe.amount2;
        }

        public override object Clone()
        {
            return new TwoAmountsConfigToken(this);
        }
    }

    [Serializable]
    public class ThreeAmountsConfigToken
        : TwoAmountsConfigToken
    {
        private int amount3;

        public int Amount3
        {
            get { return this.amount3; }
            set { this.amount3 = value; }
        }

        public ThreeAmountsConfigToken(int amount1, int amount2, int amount3)
            : base(amount1, amount2)
        {
            this.amount3 = amount3;
        }

        protected ThreeAmountsConfigToken(ThreeAmountsConfigToken copyMe)
            : base(copyMe)
        {
            this.amount3 = copyMe.amount3;
        }

        public override object Clone()
        {
            return new ThreeAmountsConfigToken(this);
        }
    }

    [Serializable]
    public class AngleChooserConfigToken
        : EffectConfigToken
    {
        private double angle;

        public double Angle
        {
            get { return this.angle; }
            set { this.angle = value; }
        }

        public AngleChooserConfigToken(double angle)
        {
            this.angle = angle;
        }

        protected AngleChooserConfigToken(AngleChooserConfigToken copyMe)
            : base(copyMe)
        {
            this.angle = copyMe.angle;
        }

        public override object Clone()
        {
            return new AngleChooserConfigToken(this);
        }
    }

    [Serializable]
    public class EdgeDetectConfigToken
        : AngleChooserConfigToken
    {
        public EdgeDetectConfigToken(double angle)
            : base(angle)
        {
        }

        protected EdgeDetectConfigToken(EdgeDetectConfigToken copyMe)
            : base(copyMe)
        {
        }

        public override object Clone()
        {
            return new EdgeDetectConfigToken(this);
        }
    }

    // --- Base de los diálogos: filas de deslizador + caja numérica y Aceptar / Cancelar ---

    public abstract class KaintSliderDialogBase
        : EffectConfigDialog
    {
        internal sealed class Row
        {
            public string Name;
            public string Label;
            public int Minimum;
            public int Maximum = 100;
            public int Value;
            public TrackBar Slider;
            public NumericUpDown Box;
            public Label Caption;
        }

        internal readonly List<Row> rows = new List<Row>();
        private bool built;
        private bool syncing;

        internal Row AddRow(string name, string label, int min, int max, int value)
        {
            Row r = new Row();
            r.Name = name;
            r.Label = label;
            r.Minimum = min;
            r.Maximum = max;
            r.Value = value;
            this.rows.Add(r);
            return r;
        }

        internal int ValueOf(int index)
        {
            Row r = this.rows[index];
            return KaintStandard.Clamp(r.Value, r.Minimum, r.Maximum);
        }

        internal void SetRow(int index, Action<Row> change)
        {
            Row r = this.rows[index];
            change(r);
            if (r.Maximum < r.Minimum)
                r.Maximum = r.Minimum;
            r.Value = KaintStandard.Clamp(r.Value, r.Minimum, r.Maximum);
            if (this.built)
                SyncRow(r);
            // Paint.NET actualizaba el token al cambiar los valores iniciales.
            InitTokenFromDialog();
        }

        // Los controles se crean al mostrar la ventana (Kaint casi nunca la muestra).
        private void Build()
        {
            if (this.built)
                return;
            this.built = true;
            SuspendLayout();
            int y = 10;
            foreach (Row r in this.rows)
            {
                Row row = r;
                row.Caption = new Label();
                row.Caption.AutoSize = true;
                row.Caption.Location = new Point(10, y);
                Controls.Add(row.Caption);
                y += 20;
                row.Slider = new TrackBar();
                row.Slider.Location = new Point(6, y);
                row.Slider.Size = new Size(200, 40);
                row.Slider.TickStyle = TickStyle.None;
                Controls.Add(row.Slider);
                row.Box = new NumericUpDown();
                row.Box.Location = new Point(212, y + 2);
                row.Box.Size = new Size(64, 20);
                Controls.Add(row.Box);
                row.Slider.ValueChanged += delegate { OnRowChanged(row, row.Slider.Value); };
                row.Box.ValueChanged += delegate { OnRowChanged(row, (int)row.Box.Value); };
                SyncRow(row);
                y += 44;
            }
            Button ok = new Button();
            ok.Text = KaintStandard.Spanish("Aceptar", "OK");
            ok.DialogResult = DialogResult.OK;
            ok.Location = new Point(118, y + 6);
            Button cancel = new Button();
            cancel.Text = KaintStandard.Spanish("Cancelar", "Cancel");
            cancel.DialogResult = DialogResult.Cancel;
            cancel.Location = new Point(200, y + 6);
            Controls.Add(ok);
            Controls.Add(cancel);
            AcceptButton = ok;
            CancelButton = cancel;
            ClientSize = new Size(286, y + 40);
            ResumeLayout(false);
            PerformLayout();
        }

        private void SyncRow(Row r)
        {
            if (r.Slider == null)
                return;
            this.syncing = true;
            r.Caption.Text = r.Label;
            r.Slider.Minimum = r.Minimum;
            r.Slider.Maximum = r.Maximum;
            r.Slider.Value = r.Value;
            r.Box.Minimum = r.Minimum;
            r.Box.Maximum = r.Maximum;
            r.Box.Value = r.Value;
            this.syncing = false;
        }

        private void OnRowChanged(Row r, int value)
        {
            if (this.syncing)
                return;
            r.Value = KaintStandard.Clamp(value, r.Minimum, r.Maximum);
            SyncRow(r);
            FinishTokenUpdate();
        }

        protected override void OnLoad(EventArgs e)
        {
            Build();
            // (Lee el token: después se ponen sus valores en los controles.)
            base.OnLoad(e);
            foreach (Row r in this.rows)
                SyncRow(r);
        }

        public virtual IList<KaintStandardParam> KaintDescribe()
        {
            List<KaintStandardParam> list = new List<KaintStandardParam>();
            foreach (Row r in this.rows)
            {
                KaintStandardParam p = new KaintStandardParam();
                p.Name = r.Name;
                p.Label = string.IsNullOrEmpty(r.Label) ? r.Name : r.Label;
                p.Type = "int";
                p.Control = "";
                p.Minimum = r.Minimum;
                p.Maximum = r.Maximum;
                p.Default = KaintStandard.Clamp(r.Value, r.Minimum, r.Maximum);
                list.Add(p);
            }
            return list;
        }

        internal int Read(IDictionary<string, object> values, int index)
        {
            Row r = this.rows[index];
            return KaintStandard.Clamp(KaintStandard.Number(values, r.Name, r.Value), r.Minimum, r.Maximum);
        }
    }

    // --- Una cantidad ---

    public abstract class AmountEffectConfigDialogBase
        : KaintSliderDialogBase, IKaintStandardDialog
    {
        private string unitsName = "";

        protected AmountEffectConfigDialogBase()
        {
            AddRow("Amount", KaintStandard.Spanish("Cantidad", "Amount"), 0, 100, 50);
            InitTokenFromDialog();
        }

        public string SliderLabel
        {
            get { return this.rows[0].Label; }
            set { SetRow(0, r => r.Label = value); }
        }

        public string SliderUnitsName
        {
            get { return this.unitsName; }
            set { this.unitsName = value ?? ""; }
        }

        public int SliderMinimum
        {
            get { return this.rows[0].Minimum; }
            set { SetRow(0, r => r.Minimum = value); }
        }

        public int SliderMaximum
        {
            get { return this.rows[0].Maximum; }
            set { SetRow(0, r => r.Maximum = value); }
        }

        public int SliderInitialValue
        {
            get { return this.rows[0].Value; }
            set { SetRow(0, r => r.Value = value); }
        }

        protected override void InitialInitToken()
        {
            this.theEffectToken = new AmountEffectConfigToken(50);
        }

        protected override void InitTokenFromDialog()
        {
            if (this.rows.Count == 0)
                return;
            AmountEffectConfigToken token = this.theEffectToken as AmountEffectConfigToken;
            if (token == null)
                this.theEffectToken = new AmountEffectConfigToken(ValueOf(0));
            else
                token.Amount = ValueOf(0);
        }

        protected override void InitDialogFromToken(EffectConfigToken effectToken)
        {
            AmountEffectConfigToken token = effectToken as AmountEffectConfigToken;
            if (token != null && this.rows.Count > 0)
                this.rows[0].Value = token.Amount;
        }

        public EffectConfigToken KaintCreateToken(IDictionary<string, object> values)
        {
            return new AmountEffectConfigToken(Read(values, 0));
        }
    }

    public class AmountEffectConfigDialog
        : AmountEffectConfigDialogBase
    {
        public AmountEffectConfigDialog()
        {
        }
    }

    // --- Dos y tres cantidades ---

    public abstract class TwoAmountsConfigDialogBase
        : KaintSliderDialogBase, IKaintStandardDialog
    {
        protected TwoAmountsConfigDialogBase()
        {
            AddRow("Amount1", KaintStandard.Spanish("Cantidad 1", "Amount 1"), 0, 100, 50);
            AddRow("Amount2", KaintStandard.Spanish("Cantidad 2", "Amount 2"), 0, 100, 50);
            InitTokenFromDialog();
        }

        public string Amount1Label { get { return this.rows[0].Label; } set { SetRow(0, r => r.Label = value); } }
        public int Amount1Minimum { get { return this.rows[0].Minimum; } set { SetRow(0, r => r.Minimum = value); } }
        public int Amount1Maximum { get { return this.rows[0].Maximum; } set { SetRow(0, r => r.Maximum = value); } }
        public int Amount1Default { get { return this.rows[0].Value; } set { SetRow(0, r => r.Value = value); } }
        public string Amount2Label { get { return this.rows[1].Label; } set { SetRow(1, r => r.Label = value); } }
        public int Amount2Minimum { get { return this.rows[1].Minimum; } set { SetRow(1, r => r.Minimum = value); } }
        public int Amount2Maximum { get { return this.rows[1].Maximum; } set { SetRow(1, r => r.Maximum = value); } }
        public int Amount2Default { get { return this.rows[1].Value; } set { SetRow(1, r => r.Value = value); } }

        protected override void InitialInitToken()
        {
            this.theEffectToken = new TwoAmountsConfigToken(50, 50);
        }

        protected override void InitTokenFromDialog()
        {
            if (this.rows.Count < 2)
                return;
            TwoAmountsConfigToken token = this.theEffectToken as TwoAmountsConfigToken;
            if (token == null)
            {
                this.theEffectToken = CreateToken(ValueOf);
                return;
            }
            token.Amount1 = ValueOf(0);
            token.Amount2 = ValueOf(1);
            ThreeAmountsConfigToken three = token as ThreeAmountsConfigToken;
            if (three != null && this.rows.Count > 2)
                three.Amount3 = ValueOf(2);
        }

        protected override void InitDialogFromToken(EffectConfigToken effectToken)
        {
            TwoAmountsConfigToken token = effectToken as TwoAmountsConfigToken;
            if (token == null || this.rows.Count < 2)
                return;
            this.rows[0].Value = token.Amount1;
            this.rows[1].Value = token.Amount2;
            ThreeAmountsConfigToken three = token as ThreeAmountsConfigToken;
            if (three != null && this.rows.Count > 2)
                this.rows[2].Value = three.Amount3;
        }

        internal virtual EffectConfigToken CreateToken(Func<int, int> value)
        {
            return new TwoAmountsConfigToken(value(0), value(1));
        }

        public EffectConfigToken KaintCreateToken(IDictionary<string, object> values)
        {
            return CreateToken(i => Read(values, i));
        }
    }

    public class TwoAmountsConfigDialog
        : TwoAmountsConfigDialogBase
    {
        public TwoAmountsConfigDialog()
        {
        }
    }

    public abstract class ThreeAmountsConfigDialogBase
        : TwoAmountsConfigDialogBase
    {
        protected ThreeAmountsConfigDialogBase()
        {
            AddRow("Amount3", KaintStandard.Spanish("Cantidad 3", "Amount 3"), 0, 100, 50);
            this.theEffectToken = CreateToken(ValueOf);
        }

        public string Amount3Label { get { return this.rows[2].Label; } set { SetRow(2, r => r.Label = value); } }
        public int Amount3Minimum { get { return this.rows[2].Minimum; } set { SetRow(2, r => r.Minimum = value); } }
        public int Amount3Maximum { get { return this.rows[2].Maximum; } set { SetRow(2, r => r.Maximum = value); } }
        public int Amount3Default { get { return this.rows[2].Value; } set { SetRow(2, r => r.Value = value); } }

        protected override void InitialInitToken()
        {
            this.theEffectToken = new ThreeAmountsConfigToken(50, 50, 50);
        }

        internal override EffectConfigToken CreateToken(Func<int, int> value)
        {
            return new ThreeAmountsConfigToken(value(0), value(1), this.rows.Count > 2 ? value(2) : 50);
        }
    }

    public class ThreeAmountsConfigDialog
        : ThreeAmountsConfigDialogBase
    {
        public ThreeAmountsConfigDialog()
        {
        }
    }

    // --- Ángulo ---

    public abstract class AngleChooserConfigDialogBase
        : KaintSliderDialogBase, IKaintStandardDialog
    {
        private double defaultAngle;

        protected AngleChooserConfigDialogBase(double defaultAngle)
        {
            this.defaultAngle = defaultAngle;
            AddRow("Angle", KaintStandard.Spanish("Ángulo", "Angle"), -180, 180, (int)Math.Round(defaultAngle));
            this.theEffectToken = CreateAngleToken(defaultAngle);
        }

        internal virtual EffectConfigToken CreateAngleToken(double angle)
        {
            return new AngleChooserConfigToken(angle);
        }

        public string AngleLabel
        {
            get { return this.rows[0].Label; }
            set { SetRow(0, r => r.Label = value); }
        }

        protected override void InitialInitToken()
        {
            this.theEffectToken = new AngleChooserConfigToken(45);
        }

        protected override void InitTokenFromDialog()
        {
            if (this.rows.Count == 0)
                return;
            AngleChooserConfigToken token = this.theEffectToken as AngleChooserConfigToken;
            if (token == null)
                this.theEffectToken = CreateAngleToken(this.rows[0].Value);
            else
                token.Angle = this.rows[0].Value;
        }

        protected override void InitDialogFromToken(EffectConfigToken effectToken)
        {
            AngleChooserConfigToken token = effectToken as AngleChooserConfigToken;
            if (token != null && this.rows.Count > 0)
                this.rows[0].Value = (int)Math.Round(token.Angle);
        }

        public override IList<KaintStandardParam> KaintDescribe()
        {
            KaintStandardParam p = new KaintStandardParam();
            p.Name = "Angle";
            p.Label = this.rows[0].Label;
            p.Type = "double";
            p.Control = "AngleChooser";
            p.Minimum = -180;
            p.Maximum = 180;
            p.Default = this.defaultAngle;
            return new List<KaintStandardParam> { p };
        }

        public EffectConfigToken KaintCreateToken(IDictionary<string, object> values)
        {
            double angle = KaintStandard.Number(values, "Angle", this.defaultAngle);
            return CreateAngleToken(Math.Max(-180, Math.Min(180, angle)));
        }
    }

    public class AngleChooserConfigDialog
        : AngleChooserConfigDialogBase
    {
        public AngleChooserConfigDialog()
            : base(45)
        {
        }

        protected AngleChooserConfigDialog(double defaultAngle)
            : base(defaultAngle)
        {
        }
    }

    // Detección de bordes de Paint.NET 3: el selector de ángulo con 45° y su propio token.
    public class EdgeDetectConfigDialog
        : AngleChooserConfigDialog
    {
        public EdgeDetectConfigDialog()
            : base(45)
        {
        }

        internal override EffectConfigToken CreateAngleToken(double angle)
        {
            return new EdgeDetectConfigToken(angle);
        }

        protected override void InitialInitToken()
        {
            this.theEffectToken = new EdgeDetectConfigToken(45);
        }
    }

    // Relieve y Realce de Paint.NET 3 usaban el mismo selector.
    public class EmbossConfigDialog
        : AngleChooserConfigDialog
    {
        public EmbossConfigDialog()
            : base(0)
        {
        }
    }
}
