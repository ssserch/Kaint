/////////////////////////////////////////////////////////////////////////////////
// Paint.NET                                                                   //
// Copyright (C) Rick Brewster, Tom Jackson, and past contributors.            //
// Portions Copyright (C) Microsoft Corporation. All Rights Reserved.          //
// See src/Resources/Files/License.txt for full licensing and attribution      //
// details.                                                                    //
// .                                                                           //
/////////////////////////////////////////////////////////////////////////////////

// Kaint: reescrito sin los controles WinForms de Paint.NET 3.36. La tabla de
// tipos de control y propiedades por defecto sale de los atributos
// [PropertyControlInfo] y [PropertyControlProperty] de aquellos controles.

using PaintDotNet.PropertySystem;
using System;
using System.Collections.Generic;

namespace PaintDotNet.IndirectUI
{
    public sealed class PropertyControlInfo
        : ControlInfo,
          IPropertyRef
    {
        private struct ControlProp
        {
            public string Name;
            public Type Type;
            public object Default;
            public ControlProp(string name, Type type, object def) { Name = name; Type = type; Default = def; }
        }

        // Tipos de control posibles para cada tipo de propiedad (el primero es el predeterminado).
        private static readonly Dictionary<Type, PropertyControlType[]> controlTypes = new Dictionary<Type, PropertyControlType[]>()
        {
            { typeof(BooleanProperty), new[] { PropertyControlType.CheckBox } },
            { typeof(DoubleProperty), new[] { PropertyControlType.Slider, PropertyControlType.AngleChooser } },
            { typeof(DoubleVectorProperty), new[] { PropertyControlType.PanAndSlider, PropertyControlType.Slider } },
            { typeof(Int32Property), new[] { PropertyControlType.Slider, PropertyControlType.IncrementButton, PropertyControlType.ColorWheel } },
            { typeof(StaticListChoiceProperty), new[] { PropertyControlType.DropDown, PropertyControlType.RadioButton } },
            { typeof(StringProperty), new[] { PropertyControlType.TextBox } },
            { typeof(ImageProperty), new[] { PropertyControlType.TextBox } },
        };

        private static ControlProp[] PropsFor(Type propertyType, PropertyControlType controlType)
        {
            List<ControlProp> list = new List<ControlProp>();
            list.Add(new ControlProp("DisplayName", typeof(string), ""));
            list.Add(new ControlProp("Description", typeof(string), ""));
            list.Add(new ControlProp("ShowHeaderLine", typeof(bool), true));
            bool isInt = propertyType == typeof(Int32Property);
            switch (controlType)
            {
                case PropertyControlType.Slider:
                    if (propertyType == typeof(DoubleVectorProperty))
                        goto case PropertyControlType.PanAndSlider;
                    list.Add(new ControlProp("ShowResetButton", typeof(bool), true));
                    list.Add(new ControlProp("SliderShowTickMarks", typeof(bool), false));
                    list.Add(new ControlProp("SliderSmallChange", isInt ? typeof(int) : typeof(double), isInt ? (object)1 : (object)1.0));
                    list.Add(new ControlProp("SliderLargeChange", isInt ? typeof(int) : typeof(double), isInt ? (object)5 : (object)5.0));
                    list.Add(new ControlProp("UpDownIncrement", isInt ? typeof(int) : typeof(double), isInt ? (object)1 : (object)1.0));
                    if (!isInt)
                    {
                        list.Add(new ControlProp("DecimalPlaces", typeof(int), 2));
                        list.Add(new ControlProp("UseExponentialScale", typeof(bool), false));
                    }
                    break;
                case PropertyControlType.PanAndSlider:
                case PropertyControlType.RollBallAndSliders:
                    list.Add(new ControlProp("ShowResetButton", typeof(bool), true));
                    list.Add(new ControlProp("DecimalPlaces", typeof(int), 2));
                    list.Add(new ControlProp("UseExponentialScale", typeof(bool), false));
                    foreach (string axis in new[] { "X", "Y", "Z" })
                    {
                        list.Add(new ControlProp("SliderSmallChange" + axis, typeof(double), 1.0));
                        list.Add(new ControlProp("SliderLargeChange" + axis, typeof(double), 5.0));
                        list.Add(new ControlProp("UpDownIncrement" + axis, typeof(double), 1.0));
                        list.Add(new ControlProp("SliderShowTickMarks" + axis, typeof(bool), false));
                    }
                    list.Add(new ControlProp("StaticImageUnderlay", typeof(object), null));
                    break;
                case PropertyControlType.AngleChooser:
                    list.Add(new ControlProp("ShowResetButton", typeof(bool), true));
                    list.Add(new ControlProp("DecimalPlaces", typeof(int), 2));
                    break;
                case PropertyControlType.IncrementButton:
                    list.Add(new ControlProp("ButtonText", typeof(string), "+"));
                    break;
                case PropertyControlType.ColorWheel:
                    list.Add(new ControlProp("ShowResetButton", typeof(bool), true));
                    break;
                case PropertyControlType.TextBox:
                    list.Add(new ControlProp("Multiline", typeof(bool), false));
                    break;
                default:
                    break;
            }
            return list.ToArray();
        }

        private Property property;
        private StaticListChoiceProperty controlType;
        private Dictionary<object, string> valueDisplayNames = new Dictionary<object, string>();
        // Valores de las propiedades del control (DisplayName, SliderLargeChange...).
        private Dictionary<string, object> values = new Dictionary<string, object>();

        public Property Property
        {
            get { return this.property; }
        }

        public StaticListChoiceProperty ControlType
        {
            get { return this.controlType; }
        }

        public void SetValueDisplayName(object value, string displayName)
        {
            this.valueDisplayNames[value] = displayName;
        }

        public string GetValueDisplayName(object value)
        {
            string valueDisplayName;
            this.valueDisplayNames.TryGetValue(value, out valueDisplayName);
            return valueDisplayName ?? (value == null ? "" : value.ToString());
        }

        // Kaint: valor de una propiedad del control (o el predeterminado).
        public object GetControlValue(string name)
        {
            object v;
            if (this.values.TryGetValue(name, out v))
                return v;
            foreach (ControlProp cp in PropsFor(this.property.GetType(), (PropertyControlType)this.controlType.Value))
                if (cp.Name == name)
                    return cp.Default;
            return null;
        }

        internal bool SetControlValue(object controlPropertyName, object value)
        {
            string name = controlPropertyName == null ? "" : controlPropertyName.ToString();
            foreach (ControlProp cp in PropsFor(this.property.GetType(), (PropertyControlType)this.controlType.Value))
            {
                if (cp.Name != name)
                    continue;
                try
                {
                    if (value != null && cp.Type != typeof(object) && !cp.Type.IsInstanceOfType(value) && value is IConvertible)
                        value = Convert.ChangeType(value, cp.Type, System.Globalization.CultureInfo.InvariantCulture);
                }
                catch (Exception)
                {
                }
                break;
            }
            this.values[name] = value;
            RebuildControlProperties();
            return true;
        }

        private void RebuildControlProperties()
        {
            List<Property> props = new List<Property>();
            foreach (ControlProp cp in PropsFor(this.property.GetType(), (PropertyControlType)this.controlType.Value))
            {
                object v;
                if (!this.values.TryGetValue(cp.Name, out v))
                    v = cp.Default;
                if (cp.Type == typeof(object) || (v != null && !cp.Type.IsInstanceOfType(v)))
                    continue;
                try
                {
                    props.Add(Property.Create(cp.Type, cp.Name, v));
                }
                catch (Exception)
                {
                }
            }
            this.ControlProperties = new PropertyCollection(props);
        }

        private PropertyControlInfo(Property property)
            : base()
        {
            this.property = property;
            PropertyControlType[] choices;
            if (!controlTypes.TryGetValue(property.GetType(), out choices))
                choices = new[] { PropertyControlType.TextBox };
            object[] boxed = new object[choices.Length];
            for (int i = 0; i < choices.Length; ++i)
                boxed[i] = choices[i];
            this.controlType = new StaticListChoiceProperty(ControlInfoPropertyNames.ControlType, boxed, 0, false);
            this.controlType.ValueChanged += (s, e) => RebuildControlProperties();
            RebuildControlProperties();
        }

        private PropertyControlInfo(PropertyControlInfo cloneMe)
            : base(cloneMe)
        {
            this.property = cloneMe.property;
            this.controlType = (StaticListChoiceProperty)cloneMe.controlType.Clone();
            this.controlType.ValueChanged += (s, e) => RebuildControlProperties();
            this.valueDisplayNames = new Dictionary<object, string>(cloneMe.valueDisplayNames);
            this.values = new Dictionary<string, object>(cloneMe.values);
        }

        public static PropertyControlInfo CreateFor(Property property)
        {
            return new PropertyControlInfo(property);
        }

        public override ControlInfo Clone()
        {
            return new PropertyControlInfo(this);
        }
    }
}
