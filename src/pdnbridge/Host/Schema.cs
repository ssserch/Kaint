// Kaint: describe a Kaint las propiedades de un PropertyBasedEffect (tipo,
// rango, control de IndirectUI, reglas) y aplica los valores que llegan.

using System;
using System.Collections.Generic;
using System.Globalization;
using System.Reflection;
using PaintDotNet;
using PaintDotNet.Effects;
using PaintDotNet.IndirectUI;
using PaintDotNet.PropertySystem;

namespace Kaint.PdnHost
{
    internal static class Schema
    {
        public static void Describe(PropertyBasedEffect effect, Dictionary<string, object> d)
        {
            PropertyCollection props = effect.CreatePropertyCollection();
            ControlInfo ui = effect.KaintCreateConfigUI(props);
            List<object> list = new List<object>();
            Dictionary<string, bool> shown = new Dictionary<string, bool>();
            if (ui != null)
            {
                foreach (ControlInfo child in ui.ChildControls)
                {
                    PropertyControlInfo pci = child as PropertyControlInfo;
                    if (pci == null || pci.Property == null)
                        continue;
                    shown[pci.Property.Name] = true;
                    list.Add(DescribeProperty(pci.Property, pci, false));
                }
            }
            // Las que no tienen control conservan su valor por defecto.
            foreach (Property p in props)
                if (!shown.ContainsKey(p.Name))
                    list.Add(DescribeProperty(p, null, true));
            d["params"] = list;
            d["rules"] = DescribeRules(props);

            try
            {
                PropertyCollection window = effect.KaintCreateWindowProperties();
                if (window != null)
                {
                    Property title = window[ControlInfoPropertyNames.WindowTitle];
                    if (title != null && title.Value != null && title.Value.ToString().Length > 0)
                        d["windowTitle"] = title.Value.ToString();
                    Property help = window[ControlInfoPropertyNames.WindowHelpContent];
                    Property helpType = window[ControlInfoPropertyNames.WindowHelpContentType];
                    if (help != null && help.Value != null && help.Value.ToString().Length > 0)
                    {
                        d["help"] = help.Value.ToString();
                        d["helpRich"] = helpType != null && helpType.Value != null && helpType.Value.ToString() == "RichText";
                    }
                }
            }
            catch (Exception)
            {
            }
        }

        private static string ControlString(PropertyControlInfo pci, string name)
        {
            object v = pci == null ? null : pci.GetControlValue(name);
            return v == null ? "" : v.ToString();
        }

        private static object ControlValue(PropertyControlInfo pci, string name)
        {
            return pci == null ? null : pci.GetControlValue(name);
        }

        private static Dictionary<string, object> DescribeProperty(Property p, PropertyControlInfo pci, bool hidden)
        {
            Dictionary<string, object> d = new Dictionary<string, object>();
            d["name"] = p.Name;
            string label = ControlString(pci, "DisplayName");
            d["label"] = label.Length > 0 ? label : p.Name;
            d["description"] = ControlString(pci, "Description");
            d["control"] = pci == null ? "" : ((PropertyControlType)pci.ControlType.Value).ToString();
            d["readOnly"] = p.ReadOnly;
            d["hidden"] = hidden;
            // Estilo del deslizador (Paint.NET 4.x): Hue, HueCentered, SaturationHue.
            object style = ControlValue(pci, "ControlStyle");
            if (style != null && Enum.IsDefined(typeof(SliderControlStyle), style) && (SliderControlStyle)style != SliderControlStyle.Default)
                d["sliderStyle"] = ((SliderControlStyle)style).ToString();

            if (p is BooleanProperty)
            {
                d["type"] = "bool";
                d["default"] = (bool)p.DefaultValue;
            }
            else if (p is Int32Property)
            {
                Int32Property ip = (Int32Property)p;
                d["type"] = "int";
                d["min"] = ip.MinValue;
                d["max"] = ip.MaxValue;
                d["default"] = ip.DefaultValue;
            }
            else if (p is DoubleProperty)
            {
                DoubleProperty dp = (DoubleProperty)p;
                d["type"] = "double";
                d["min"] = dp.MinValue;
                d["max"] = dp.MaxValue;
                d["default"] = dp.DefaultValue;
                object places = ControlValue(pci, "DecimalPlaces");
                d["decimals"] = places is int ? (int)places : 2;
            }
            else if (p is DoubleVectorProperty)
            {
                DoubleVectorProperty vp = (DoubleVectorProperty)p;
                d["type"] = "vector";
                d["min"] = new object[] { vp.MinValueX, vp.MinValueY };
                d["max"] = new object[] { vp.MaxValueX, vp.MaxValueY };
                d["default"] = new object[] { vp.DefaultValueX, vp.DefaultValueY };
                object places = ControlValue(pci, "DecimalPlaces");
                d["decimals"] = places is int ? (int)places : 2;
            }
            else if (p is StaticListChoiceProperty)
            {
                StaticListChoiceProperty sp = (StaticListChoiceProperty)p;
                d["type"] = "choice";
                List<object> names = new List<object>();
                int def = 0;
                for (int i = 0; i < sp.ValueChoices.Length; ++i)
                {
                    object choice = sp.ValueChoices[i];
                    names.Add(pci != null ? pci.GetValueDisplayName(choice) : (choice == null ? "" : choice.ToString()));
                    if (object.Equals(choice, sp.DefaultValue))
                        def = i;
                }
                d["choices"] = names;
                d["default"] = def;
            }
            else if (p is StringProperty)
            {
                StringProperty sp = (StringProperty)p;
                d["type"] = "string";
                d["default"] = sp.DefaultValue == null ? "" : sp.DefaultValue.ToString();
                d["maxLength"] = sp.MaxLength;
                object multiline = ControlValue(pci, "Multiline");
                d["multiline"] = multiline is bool && (bool)multiline;
            }
            else
            {
                d["type"] = "unsupported";
            }
            return d;
        }

        private static object Field(object target, string name)
        {
            for (Type t = target.GetType(); t != null; t = t.BaseType)
            {
                FieldInfo f = t.GetField(name, BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.Public);
                if (f != null)
                    return f.GetValue(target);
            }
            return null;
        }

        // Valor de una regla en el formato del protocolo (índice para las listas).
        private static object RuleValue(PropertyCollection props, string sourceName, object value)
        {
            StaticListChoiceProperty list = props[sourceName] as StaticListChoiceProperty;
            if (list != null)
            {
                for (int i = 0; i < list.ValueChoices.Length; ++i)
                    if (object.Equals(list.ValueChoices[i], value))
                        return i;
                return -1;
            }
            if (value is bool || value is string)
                return value;
            if (value is IConvertible)
            {
                try
                {
                    return System.Convert.ToDouble(value, CultureInfo.InvariantCulture);
                }
                catch (Exception)
                {
                }
            }
            return value == null ? null : value.ToString();
        }

        private static Dictionary<string, object> Condition(string source, List<object> values)
        {
            Dictionary<string, object> c = new Dictionary<string, object>();
            c["source"] = source;
            c["values"] = values;
            return c;
        }

        private static List<object> DescribeRules(PropertyCollection props)
        {
            List<object> rules = new List<object>();
            foreach (PropertyCollectionRule rule in props.Rules)
            {
                try
                {
                    Type type = rule.GetType();
                    string typeName = type.IsGenericType ? type.GetGenericTypeDefinition().Name : type.Name;
                    Dictionary<string, object> r = new Dictionary<string, object>();
                    List<object> conditions = new List<object>();
                    switch (typeName)
                    {
                        case "ReadOnlyBoundToBooleanRule":
                        {
                            r["type"] = "readOnly";
                            r["target"] = Field(rule, "targetPropertyName");
                            conditions.Add(Condition((string)Field(rule, "sourceBooleanPropertyName"), new List<object> { true }));
                            r["inverse"] = Field(rule, "inverse");
                            break;
                        }
                        case "ReadOnlyBoundToValueRule`2":
                        {
                            r["type"] = "readOnly";
                            r["target"] = Field(rule, "targetPropertyName");
                            string source = (string)Field(rule, "sourcePropertyName");
                            List<object> values = new List<object>();
                            System.Collections.IEnumerable raw = Field(rule, "valuesForReadOnly") as System.Collections.IEnumerable;
                            if (raw != null)
                                foreach (object v in raw)
                                    values.Add(RuleValue(props, source, v));
                            conditions.Add(Condition(source, values));
                            r["inverse"] = Field(rule, "inverse");
                            break;
                        }
                        case "ReadOnlyBoundToNameValuesRule":
                        {
                            ReadOnlyBoundToNameValuesRule nv = (ReadOnlyBoundToNameValuesRule)rule;
                            r["type"] = "readOnly";
                            r["target"] = nv.TargetPropertyName;
                            foreach (Pair<object, object> pair in nv.SourceNameValues)
                            {
                                string source = pair.First.ToString();
                                conditions.Add(Condition(source, new List<object> { RuleValue(props, source, pair.Second) }));
                            }
                            r["inverse"] = nv.Inverse;
                            break;
                        }
                        case "LinkValuesBasedOnBooleanRule`2":
                        {
                            r["type"] = "link";
                            List<object> targets = new List<object>();
                            string[] names = Field(rule, "targetPropertyNames") as string[];
                            if (names != null)
                                targets.AddRange(names);
                            r["targets"] = targets;
                            r["source"] = Field(rule, "sourcePropertyName");
                            r["inverse"] = Field(rule, "inverse");
                            break;
                        }
                        default:
                            continue;
                    }
                    if (conditions.Count > 0)
                        r["conditions"] = conditions;
                    rules.Add(r);
                }
                catch (Exception)
                {
                }
            }
            return rules;
        }

        // Aplica los valores recibidos (por nombre). Dos pasadas porque las
        // reglas de solo lectura dependen de otras propiedades.
        public static void Apply(PropertyCollection props, Dictionary<string, object> values)
        {
            for (int pass = 0; pass < 2; ++pass)
            {
                foreach (Property p in props)
                {
                    object v;
                    if (!values.TryGetValue(p.Name, out v) || v == null || p.ReadOnly)
                        continue;
                    try
                    {
                        object converted = ConvertValue(p, v);
                        if (converted != null && !object.Equals(converted, p.Value))
                            p.Value = converted;
                    }
                    catch (Exception)
                    {
                    }
                }
            }
        }

        private static double Clamp(double v, double min, double max)
        {
            return v < min ? min : (v > max ? max : v);
        }

        private static object ConvertValue(Property p, object v)
        {
            CultureInfo inv = CultureInfo.InvariantCulture;
            if (p is BooleanProperty)
                return v is bool ? v : (object)(System.Convert.ToDouble(v, inv) != 0);
            if (p is Int32Property)
            {
                Int32Property ip = (Int32Property)p;
                return (int)Math.Round(Clamp(System.Convert.ToDouble(v, inv), ip.MinValue, ip.MaxValue));
            }
            if (p is DoubleProperty)
            {
                DoubleProperty dp = (DoubleProperty)p;
                return Clamp(System.Convert.ToDouble(v, inv), dp.MinValue, dp.MaxValue);
            }
            if (p is DoubleVectorProperty)
            {
                DoubleVectorProperty vp = (DoubleVectorProperty)p;
                List<object> xy = v as List<object>;
                if (xy == null || xy.Count != 2)
                    return null;
                return Pair.Create(Clamp(System.Convert.ToDouble(xy[0], inv), vp.MinValueX, vp.MaxValueX),
                                   Clamp(System.Convert.ToDouble(xy[1], inv), vp.MinValueY, vp.MaxValueY));
            }
            if (p is StaticListChoiceProperty)
            {
                StaticListChoiceProperty sp = (StaticListChoiceProperty)p;
                int i = (int)System.Convert.ToDouble(v, inv);
                return i >= 0 && i < sp.ValueChoices.Length ? sp.ValueChoices[i] : null;
            }
            if (p is StringProperty)
            {
                StringProperty sp = (StringProperty)p;
                string s = v.ToString();
                return sp.MaxLength > 0 && s.Length > sp.MaxLength ? s.Substring(0, sp.MaxLength) : s;
            }
            return null;
        }
    }
}
