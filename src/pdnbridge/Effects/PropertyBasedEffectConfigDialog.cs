// Kaint: el diálogo de Paint.NET de los efectos con propiedades (PropertyBasedEffect), sencillo: un
// control por propiedad (número, casilla o lista). Kaint muestra estos efectos con su propia
// ventana; este solo lo abren los complementos que usan el diálogo de otro efecto, como Random
// Effect de pyrochild (antes CreateConfigDialog devolvía null y fallaba).

using PaintDotNet.PropertySystem;
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Windows.Forms;

namespace PaintDotNet.Effects
{
    public sealed class PropertyBasedEffectConfigDialog
        : EffectConfigDialog<PropertyBasedEffect, PropertyBasedEffectConfigToken>
    {
        private PropertyCollection initial;
        private TableLayoutPanel table;
        // Lo que lee cada control y lo escribe en el token.
        private readonly List<Action<PropertyCollection>> readers = new List<Action<PropertyCollection>>();
        private bool loading;

        internal PropertyBasedEffectConfigDialog(PropertyCollection props, string title)
            : base(props)
        {
            this.Text = title;
            this.FormBorderStyle = FormBorderStyle.FixedDialog;
            this.MaximizeBox = false;
            this.MinimizeBox = false;
            this.AutoSize = true;
            this.AutoSizeMode = AutoSizeMode.GrowAndShrink;
            this.table = new TableLayoutPanel();
            this.table.ColumnCount = 2;
            this.table.AutoSize = true;
            this.table.Dock = DockStyle.Fill;
            this.table.Padding = new Padding(8);
            this.Controls.Add(this.table);
            BuildControls();
        }

        internal override void OnBeforeConstructor(object context)
        {
            this.initial = (PropertyCollection)context;
        }

        protected override PropertyBasedEffectConfigToken CreateInitialToken()
        {
            return new PropertyBasedEffectConfigToken(this.initial.Clone());
        }

        private void BuildControls()
        {
            PropertyBasedEffectConfigToken token = this.EffectToken;
            if (token == null)
                return;
            foreach (Property p in token.Properties)
            {
                string name = p.Name.ToString();
                Control control = null;
                Int32Property i32 = p as Int32Property;
                DoubleProperty dbl = p as DoubleProperty;
                BooleanProperty boolean = p as BooleanProperty;
                StaticListChoiceProperty choice = p as StaticListChoiceProperty;
                if (i32 != null)
                {
                    NumericUpDown n = new NumericUpDown { Minimum = i32.MinValue, Maximum = i32.MaxValue, Width = 120 };
                    control = n;
                    this.readers.Add(props => Set(props, name, (int)n.Value));
                    n.ValueChanged += Changed;
                }
                else if (dbl != null)
                {
                    NumericUpDown n = new NumericUpDown { DecimalPlaces = 2, Width = 120,
                        Minimum = (decimal)Math.Max(dbl.MinValue, (double)decimal.MinValue / 2),
                        Maximum = (decimal)Math.Min(dbl.MaxValue, (double)decimal.MaxValue / 2) };
                    control = n;
                    this.readers.Add(props => Set(props, name, (double)n.Value));
                    n.ValueChanged += Changed;
                }
                else if (boolean != null)
                {
                    CheckBox c = new CheckBox { Text = string.Empty, AutoSize = true };
                    control = c;
                    this.readers.Add(props => Set(props, name, c.Checked));
                    c.CheckedChanged += Changed;
                }
                else if (choice != null)
                {
                    ComboBox c = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 160 };
                    foreach (object o in choice.ValueChoices)
                        c.Items.Add(o);
                    control = c;
                    this.readers.Add(props => { if (c.SelectedItem != null) Set(props, name, c.SelectedItem); });
                    c.SelectedIndexChanged += Changed;
                }
                if (control == null)
                    continue;
                control.Tag = name;
                control.Enabled = !p.ReadOnly;
                this.table.Controls.Add(new Label { Text = name, AutoSize = true, Anchor = AnchorStyles.Left });
                this.table.Controls.Add(control);
            }
            Button ok = new Button { Text = "OK", DialogResult = DialogResult.OK };
            Button cancel = new Button { Text = "Cancel", DialogResult = DialogResult.Cancel };
            FlowLayoutPanel buttons = new FlowLayoutPanel { AutoSize = true, FlowDirection = FlowDirection.RightToLeft, Dock = DockStyle.Fill };
            buttons.Controls.Add(cancel);
            buttons.Controls.Add(ok);
            this.table.Controls.Add(buttons);
            this.table.SetColumnSpan(buttons, 2);
            this.AcceptButton = ok;
            this.CancelButton = cancel;
        }

        private static void Set(PropertyCollection props, string name, object value)
        {
            Property p = props[name];
            if (p == null || p.ReadOnly)
                return;
            try
            {
                p.Value = value;
            }
            catch (Exception)
            {
                // Un valor que la propiedad no admite: se queda el que tenía.
            }
        }

        private void Changed(object sender, EventArgs e)
        {
            if (!this.loading)
                FinishTokenUpdate();
        }

        protected override void InitDialogFromToken(PropertyBasedEffectConfigToken effectTokenCopy)
        {
            this.loading = true;
            try
            {
                foreach (Control control in this.table.Controls)
                {
                    string name = control.Tag as string;
                    if (name == null)
                        continue;
                    Property p = effectTokenCopy.Properties[name];
                    if (p == null)
                        continue;
                    NumericUpDown n = control as NumericUpDown;
                    CheckBox c = control as CheckBox;
                    ComboBox combo = control as ComboBox;
                    if (n != null)
                        n.Value = Math.Max(n.Minimum, Math.Min(n.Maximum, Convert.ToDecimal(p.Value)));
                    else if (c != null)
                        c.Checked = (bool)p.Value;
                    else if (combo != null)
                        combo.SelectedItem = p.Value;
                }
            }
            finally
            {
                this.loading = false;
            }
        }

        protected override void LoadIntoTokenFromDialog(PropertyBasedEffectConfigToken writeValuesHere)
        {
            foreach (Action<PropertyCollection> read in this.readers)
                read(writeValuesHere.Properties);
        }
    }
}
