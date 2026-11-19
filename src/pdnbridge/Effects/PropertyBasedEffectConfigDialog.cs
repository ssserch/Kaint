// Kaint: el diálogo de Paint.NET de los efectos con propiedades (PropertyBasedEffect), sencillo:
// por cada propiedad, su nombre y un control (deslizador con número, casilla, lista o los dos
// números de un punto), con los nombres que da la descripción de IndirectUI del efecto. Kaint
// muestra estos efectos con su propia ventana; este solo lo abren los complementos que usan el
// diálogo de otro efecto, como Random Effect de pyrochild (antes CreateConfigDialog devolvía null
// y fallaba).

using PaintDotNet.IndirectUI;
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
        // Ancho mínimo del contenido, como el de los diálogos de Paint.NET: también cabe la barra de
        // dos botones (282 píxeles) que Random Effect pone encima con el mismo ancho.
        private const int MinContentWidth = 320;
        private const int SliderSteps = 1000;

        private PropertyCollection initial;
        private readonly ControlInfo configUI;
        private TableLayoutPanel table;
        // Lo que lee cada control y lo escribe en el token, y lo que pone en cada control el valor
        // de un token.
        private readonly List<Action<PropertyCollection>> readers = new List<Action<PropertyCollection>>();
        private readonly List<Action<PropertyCollection>> writers = new List<Action<PropertyCollection>>();
        private bool loading;

        internal PropertyBasedEffectConfigDialog(PropertyCollection props, ControlInfo configUI, string title)
            : base(props)
        {
            this.configUI = configUI;
            this.Text = title;
            this.FormBorderStyle = FormBorderStyle.FixedDialog;
            this.MaximizeBox = false;
            this.MinimizeBox = false;
            this.ShowInTaskbar = false;
            this.AutoSize = true;
            this.AutoSizeMode = AutoSizeMode.GrowAndShrink;
            this.table = new TableLayoutPanel();
            this.table.ColumnCount = 1;
            this.table.AutoSize = true;
            this.table.AutoSizeMode = AutoSizeMode.GrowAndShrink;
            this.table.Dock = DockStyle.Fill;
            this.table.Padding = new Padding(10, 8, 10, 8);
            this.table.MinimumSize = new Size(MinContentWidth, 0);
            this.Controls.Add(this.table);
            BuildControls();
        }

        internal PropertyBasedEffectConfigDialog(PropertyCollection props, string title)
            : this(props, null, title)
        {
        }

        internal override void OnBeforeConstructor(object context)
        {
            this.initial = (PropertyCollection)context;
        }

        protected override PropertyBasedEffectConfigToken CreateInitialToken()
        {
            return new PropertyBasedEffectConfigToken(this.initial.Clone());
        }

        private PropertyControlInfo ControlFor(Property p)
        {
            if (this.configUI == null)
                return null;
            try
            {
                return this.configUI.FindControlForPropertyName(p.Name);
            }
            catch (Exception)
            {
                return null;
            }
        }

        private static string ControlText(PropertyControlInfo pci, string name)
        {
            object v = pci == null ? null : pci.GetControlValue(name);
            return v == null ? string.Empty : v.ToString();
        }

        private static string ChoiceText(PropertyControlInfo pci, object value)
        {
            string text = pci != null ? pci.GetValueDisplayName(value) : (value == null ? string.Empty : value.ToString());
            Type type = value as Type;
            if (type != null && text == type.ToString())
            {
                // Los modos de fusión (y otros tipos) con su nombre, no el del tipo.
                try
                {
                    string name = Utility.GetStaticName(type);
                    if (!string.IsNullOrEmpty(name))
                        return name;
                }
                catch (Exception)
                {
                }
                return type.Name;
            }
            return text;
        }

        private sealed class ChoiceItem
        {
            public readonly object Value;
            private readonly string text;

            public ChoiceItem(object value, string text)
            {
                this.Value = value;
                this.text = text;
            }

            public override string ToString()
            {
                return this.text;
            }
        }

        private void AddRow(Control control)
        {
            control.Margin = new Padding(0, 0, 0, 6);
            this.table.Controls.Add(control);
        }

        private Label Header(string text)
        {
            return new Label { Text = text, AutoSize = true, Margin = new Padding(0, 4, 0, 2) };
        }

        // Deslizador y número enlazados (enteros o con decimales).
        private Control NumberRow(double min, double max, int decimals, Func<PropertyCollection, double> get, Action<PropertyCollection, double> set)
        {
            TableLayoutPanel row = new TableLayoutPanel { ColumnCount = 2, AutoSize = true, Dock = DockStyle.Fill, Margin = Padding.Empty };
            row.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            row.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            decimal dmin = (decimal)Math.Max(min, -1e9);
            decimal dmax = (decimal)Math.Min(max, 1e9);
            NumericUpDown number = new NumericUpDown
            {
                Minimum = dmin,
                Maximum = dmax,
                DecimalPlaces = decimals,
                Increment = decimals > 0 ? (decimal)Math.Pow(10, -Math.Min(decimals, 2)) : 1,
                Width = 80,
                Anchor = AnchorStyles.Right,
            };
            TrackBar slider = new TrackBar
            {
                Minimum = 0,
                Maximum = SliderSteps,
                TickStyle = TickStyle.None,
                AutoSize = false,
                Height = 24,
                Dock = DockStyle.Fill,
                Margin = new Padding(0, 0, 6, 0),
            };
            bool syncing = false;
            Func<decimal, int> toSlider = v => dmax > dmin ? (int)Math.Round((v - dmin) / (dmax - dmin) * SliderSteps) : 0;
            slider.ValueChanged += (s, e) =>
            {
                if (syncing)
                    return;
                syncing = true;
                decimal v = dmin + (dmax - dmin) * slider.Value / SliderSteps;
                number.Value = Math.Max(dmin, Math.Min(dmax, Math.Round(v, decimals)));
                syncing = false;
                Changed(s, e);
            };
            number.ValueChanged += (s, e) =>
            {
                if (syncing)
                    return;
                syncing = true;
                slider.Value = Math.Max(0, Math.Min(SliderSteps, toSlider(number.Value)));
                syncing = false;
                Changed(s, e);
            };
            row.Controls.Add(slider, 0, 0);
            row.Controls.Add(number, 1, 0);
            this.readers.Add(props => set(props, (double)number.Value));
            this.writers.Add(props =>
            {
                decimal v = Math.Max(dmin, Math.Min(dmax, (decimal)get(props)));
                syncing = true;
                number.Value = v;
                slider.Value = Math.Max(0, Math.Min(SliderSteps, toSlider(v)));
                syncing = false;
            });
            return row;
        }

        private void BuildControls()
        {
            PropertyBasedEffectConfigToken token = this.EffectToken;
            if (token == null)
                return;
            foreach (Property p in token.Properties)
            {
                object name = p.Name;
                PropertyControlInfo pci = ControlFor(p);
                string label = ControlText(pci, ControlInfoPropertyNames.DisplayName.ToString());
                if (label.Length == 0)
                    label = p.Name.ToString();
                string description = ControlText(pci, ControlInfoPropertyNames.Description.ToString());
                Control control = null;
                bool header = true;

                Int32Property i32 = p as Int32Property;
                DoubleProperty dbl = p as DoubleProperty;
                BooleanProperty boolean = p as BooleanProperty;
                StaticListChoiceProperty choice = p as StaticListChoiceProperty;
                DoubleVectorProperty vector = p as DoubleVectorProperty;
                PropertyControlType type = pci == null ? PropertyControlType.Slider : (PropertyControlType)pci.ControlType.Value;
                if (i32 != null && type == PropertyControlType.ColorWheel)
                {
                    // Un color (0xRRGGBB): una muestra que abre el selector de colores.
                    Button swatch = new Button { Width = 80, Height = 24, FlatStyle = FlatStyle.Flat, Text = string.Empty };
                    Action<int> show = rgb => swatch.BackColor = Color.FromArgb(255, (rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
                    swatch.Click += (s, e) =>
                    {
                        using (ColorDialog pick = new ColorDialog { Color = swatch.BackColor, FullOpen = true })
                        {
                            if (pick.ShowDialog(this) != DialogResult.OK)
                                return;
                            show(pick.Color.ToArgb() & 0xffffff);
                            Changed(s, e);
                        }
                    };
                    control = swatch;
                    this.readers.Add(props => Set(props, name, swatch.BackColor.ToArgb() & 0xffffff));
                    this.writers.Add(props => show((int)props[name].Value));
                }
                else if (i32 != null && type == PropertyControlType.IncrementButton)
                {
                    // Como el botón «Reseed» de Paint.NET: cada clic suma uno.
                    Button button = new Button { Text = ControlText(pci, "ButtonText").Length > 0 ? ControlText(pci, "ButtonText") : "Reseed", AutoSize = true };
                    int value = 0;
                    button.Click += (s, e) =>
                    {
                        value = value >= i32.MaxValue ? i32.MinValue : value + 1;
                        Changed(s, e);
                    };
                    control = button;
                    this.readers.Add(props => Set(props, name, value));
                    this.writers.Add(props => value = (int)props[name].Value);
                }
                else if (i32 != null)
                {
                    control = NumberRow(i32.MinValue, i32.MaxValue, 0,
                        props => (double)(int)props[name].Value, (props, v) => Set(props, name, (int)Math.Round(v)));
                }
                else if (dbl != null)
                {
                    object places = pci == null ? null : pci.GetControlValue(ControlInfoPropertyNames.DecimalPlaces.ToString());
                    control = NumberRow(dbl.MinValue, dbl.MaxValue, places is int ? Math.Max(0, Math.Min(6, (int)places)) : 2,
                        props => (double)props[name].Value, (props, v) => Set(props, name, v));
                }
                else if (boolean != null)
                {
                    // Como en Paint.NET: el nombre arriba y la descripción en la casilla (si la hay).
                    header = description.Length > 0;
                    CheckBox c = new CheckBox { Text = header ? description : label, AutoSize = true };
                    control = c;
                    this.readers.Add(props => Set(props, name, c.Checked));
                    this.writers.Add(props => c.Checked = (bool)props[name].Value);
                    c.CheckedChanged += Changed;
                }
                else if (choice != null)
                {
                    ComboBox c = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Dock = DockStyle.Fill };
                    foreach (object o in choice.ValueChoices)
                        c.Items.Add(new ChoiceItem(o, ChoiceText(pci, o)));
                    control = c;
                    this.readers.Add(props =>
                    {
                        ChoiceItem item = c.SelectedItem as ChoiceItem;
                        if (item != null)
                            Set(props, name, item.Value);
                    });
                    this.writers.Add(props =>
                    {
                        object value = props[name].Value;
                        for (int i = 0; i < c.Items.Count; ++i)
                            if (object.Equals(((ChoiceItem)c.Items[i]).Value, value))
                                c.SelectedIndex = i;
                    });
                    c.SelectedIndexChanged += Changed;
                }
                else if (vector != null)
                {
                    FlowLayoutPanel row = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Fill, Margin = Padding.Empty, WrapContents = false };
                    NumericUpDown x = new NumericUpDown { DecimalPlaces = 2, Increment = 0.01m, Width = 90,
                        Minimum = (decimal)Math.Max(vector.MinValueX, -1e9), Maximum = (decimal)Math.Min(vector.MaxValueX, 1e9) };
                    NumericUpDown y = new NumericUpDown { DecimalPlaces = 2, Increment = 0.01m, Width = 90,
                        Minimum = (decimal)Math.Max(vector.MinValueY, -1e9), Maximum = (decimal)Math.Min(vector.MaxValueY, 1e9) };
                    row.Controls.Add(new Label { Text = "X", AutoSize = true, Anchor = AnchorStyles.Left, Margin = new Padding(0, 6, 4, 0) });
                    row.Controls.Add(x);
                    row.Controls.Add(new Label { Text = "Y", AutoSize = true, Anchor = AnchorStyles.Left, Margin = new Padding(10, 6, 4, 0) });
                    row.Controls.Add(y);
                    control = row;
                    this.readers.Add(props => Set(props, name, Pair.Create((double)x.Value, (double)y.Value)));
                    this.writers.Add(props =>
                    {
                        Pair<double, double> v = (Pair<double, double>)props[name].Value;
                        x.Value = Math.Max(x.Minimum, Math.Min(x.Maximum, (decimal)v.First));
                        y.Value = Math.Max(y.Minimum, Math.Min(y.Maximum, (decimal)v.Second));
                    });
                    x.ValueChanged += Changed;
                    y.ValueChanged += Changed;
                }
                if (control == null)
                    continue;
                control.Enabled = !p.ReadOnly;
                if (header)
                    AddRow(Header(label));
                AddRow(control);
            }
            Button ok = new Button { Text = "OK", DialogResult = DialogResult.OK, Width = 80 };
            Button cancel = new Button { Text = "Cancel", DialogResult = DialogResult.Cancel, Width = 80 };
            FlowLayoutPanel buttons = new FlowLayoutPanel
            {
                AutoSize = true,
                FlowDirection = FlowDirection.RightToLeft,
                Dock = DockStyle.Fill,
                Margin = new Padding(0, 8, 0, 0),
            };
            buttons.Controls.Add(cancel);
            buttons.Controls.Add(ok);
            this.table.Controls.Add(buttons);
            this.AcceptButton = ok;
            this.CancelButton = cancel;
        }

        private static void Set(PropertyCollection props, object name, object value)
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
                foreach (Action<PropertyCollection> write in this.writers)
                {
                    try
                    {
                        write(effectTokenCopy.Properties);
                    }
                    catch (Exception)
                    {
                        // Una propiedad que no está en este token (o con otro tipo): se deja.
                    }
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
