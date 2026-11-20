// Kaint: describe las propiedades de un efecto (PropertyCollection + IndirectUI) con el formato que
// entiende Kaint (el mismo que el proceso de mono: "params" y "rules"), y traduce los valores que
// llegan de Kaint al formato del puente de paint.rs.

using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Reflection;
using System.Text.Json;
using System.Text.Json.Nodes;
using PaintDotNet;
using PaintDotNet.IndirectUI;
using PaintDotNet.PropertySystem;

namespace Kaint.PdnHost5
{
    internal static class KaintSchema
    {
        // --- Efectos con propiedades (PropertyBasedEffect y los de Paint.NET 5) ---

        public static void Describe(PropertyCollection props, ControlInfo? ui, JsonObject d)
        {
            var list = new JsonArray();
            var shown = new HashSet<string>(StringComparer.Ordinal);
            if (ui is not null)
                foreach (PropertyControlInfo pci in Controls(ui))
                {
                    if (pci.Property is null || pci.Property is TabContainerStateProperty || !shown.Add(pci.Property.Name))
                        continue;
                    foreach (JsonObject p in DescribeProperty(pci.Property, pci, false))
                        list.Add(p);
                }
            // Las que no tienen control conservan su valor por defecto.
            foreach (Property p in props)
                if (p is not TabContainerStateProperty && !shown.Contains(p.Name))
                    foreach (JsonObject o in DescribeProperty(p, null, true))
                        list.Add(o);
            d["params"] = list;
            d["rules"] = DescribeRules(props);
        }

        // Los controles de propiedades en el orden del diálogo (también dentro de paneles y pestañas).
        private static IEnumerable<PropertyControlInfo> Controls(ControlInfo ui)
        {
            if (ui is PropertyControlInfo pci)
                yield return pci;
            foreach (ControlInfo child in ui.ChildControls)
                foreach (PropertyControlInfo c in Controls(child))
                    yield return c;
        }

        private static string ControlString(PropertyControlInfo? pci, ControlInfoPropertyNames name)
            => pci?.GetControlValue(name)?.ToString() ?? "";

        private static IEnumerable<JsonObject> DescribeProperty(Property p, PropertyControlInfo? pci, bool hidden)
        {
            var d = new JsonObject();
            d["name"] = p.Name;
            string label = ControlString(pci, ControlInfoPropertyNames.DisplayName);
            d["label"] = label.Length > 0 ? label : p.Name;
            d["description"] = ControlString(pci, ControlInfoPropertyNames.Description);
            d["control"] = pci is null ? "" : ((PropertyControlType)pci.ControlType.Value!).ToString();
            d["readOnly"] = p.ReadOnly;
            d["hidden"] = hidden;
            if (pci?.GetControlValue(ControlInfoPropertyNames.ControlStyle) is SliderControlStyle style && style != SliderControlStyle.Default)
                d["sliderStyle"] = style.ToString();
            int decimals = pci?.GetControlValue(ControlInfoPropertyNames.DecimalPlaces) is int places ? places : 2;

            switch (p)
            {
                case BooleanProperty bp:
                    d["type"] = "bool";
                    d["default"] = (bool)bp.DefaultValue!;
                    break;
                case Int32Property ip:
                    d["type"] = "int";
                    d["min"] = ip.MinValue;
                    d["max"] = ip.MaxValue;
                    d["default"] = (int)ip.DefaultValue!;
                    break;
                case DoubleProperty dp:
                    d["type"] = "double";
                    d["min"] = dp.MinValue;
                    d["max"] = dp.MaxValue;
                    d["default"] = (double)dp.DefaultValue!;
                    d["decimals"] = decimals;
                    break;
                case VectorProperty<double> vp:
                    d["type"] = "vector";
                    d["min"] = new JsonArray(vp.MinValues.First, vp.MinValues.Second);
                    d["max"] = new JsonArray(vp.MaxValues.First, vp.MaxValues.Second);
                    var dv = (Pair<double, double>)vp.DefaultValue!;
                    d["default"] = new JsonArray(dv.First, dv.Second);
                    d["decimals"] = decimals;
                    break;
                case Vector3Property<double> v3:
                {
                    // Kaint no tiene un control de tres componentes: una por eje.
                    var dv3 = (Tuple<double, double, double>)v3.DefaultValue!;
                    (string axis, double def, double min, double max)[] parts =
                    [
                        ("X", dv3.Item1, v3.MinValues.Item1, v3.MaxValues.Item1),
                        ("Y", dv3.Item2, v3.MinValues.Item2, v3.MaxValues.Item2),
                        ("Z", dv3.Item3, v3.MinValues.Item3, v3.MaxValues.Item3),
                    ];
                    foreach (var (axis, def, min, max) in parts)
                    {
                        var c = (JsonObject)d.DeepClone();
                        c["name"] = p.Name + "." + axis;
                        c["label"] = (label.Length > 0 ? label : p.Name) + " " + axis;
                        c["control"] = "";
                        c["type"] = "double";
                        c["min"] = min;
                        c["max"] = max;
                        c["default"] = def;
                        c["decimals"] = decimals;
                        yield return c;
                    }
                    yield break;
                }
                case StaticListChoiceProperty sp:
                {
                    d["type"] = "choice";
                    var names = new JsonArray();
                    object[] choices = sp.ValueChoices;
                    int def = 0;
                    for (int i = 0; i < choices.Length; ++i)
                    {
                        object choice = choices[i];
                        names.Add(pci is not null && choice is not null ? pci.GetValueDisplayName(choice) : choice?.ToString() ?? "");
                        if (Equals(choice, sp.DefaultValue))
                            def = i;
                    }
                    d["choices"] = names;
                    d["default"] = def;
                    break;
                }
                case StringProperty sp:
                    d["type"] = "string";
                    d["default"] = sp.DefaultValue?.ToString() ?? "";
                    d["maxLength"] = sp.MaxLength;
                    d["multiline"] = pci?.GetControlValue(ControlInfoPropertyNames.Multiline) is true;
                    break;
                default:
                    d["type"] = "unsupported";
                    break;
            }
            yield return d;
        }

        private static object? Field(object target, string name)
        {
            for (Type? t = target.GetType(); t is not null; t = t.BaseType)
            {
                FieldInfo? f = t.GetField(name, BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.Public);
                if (f is not null)
                    return f.GetValue(target);
            }
            return null;
        }

        // Valor de una regla en el formato del protocolo (índice para las listas).
        private static JsonNode? RuleValue(PropertyCollection props, string source, object? value)
        {
            if (props[source] is StaticListChoiceProperty list)
                return Array.FindIndex(list.ValueChoices, c => Equals(c, value));
            return value switch
            {
                null => null,
                bool b => b,
                string s => s,
                IConvertible c => TryDouble(c),
                _ => value.ToString(),
            };
        }

        private static JsonNode? TryDouble(IConvertible c)
        {
            try { return c.ToDouble(CultureInfo.InvariantCulture); }
            catch (Exception) { return c.ToString(CultureInfo.InvariantCulture); }
        }

        private static JsonObject Condition(string source, JsonArray values)
            => new() { ["source"] = source, ["values"] = values };

        private static JsonArray DescribeRules(PropertyCollection props)
        {
            var rules = new JsonArray();
            IEnumerable<PropertyCollectionRule> all = Field(props, "rules") as IEnumerable<PropertyCollectionRule> ?? [];
            foreach (PropertyCollectionRule rule in all)
            {
                try
                {
                    Type type = rule.GetType();
                    string name = type.IsGenericType ? type.GetGenericTypeDefinition().Name : type.Name;
                    var r = new JsonObject();
                    var conditions = new JsonArray();
                    switch (name)
                    {
                        case "ReadOnlyBoundToBooleanRule":
                            r["type"] = "readOnly";
                            r["target"] = Field(rule, "target")?.ToString();
                            conditions.Add(Condition(Field(rule, "source")!.ToString()!, new JsonArray(true)));
                            r["inverse"] = Field(rule, "inverse") is true;
                            break;
                        case "ReadOnlyBoundToValueRule`2":
                        {
                            r["type"] = "readOnly";
                            r["target"] = Field(rule, "target")?.ToString();
                            string source = Field(rule, "source")!.ToString()!;
                            var values = new JsonArray();
                            if (Field(rule, "requiredValues") is System.Collections.IEnumerable many)
                                foreach (object? v in many)
                                    values.Add(RuleValue(props, source, v));
                            else
                                values.Add(RuleValue(props, source, Field(rule, "requiredValue")));
                            conditions.Add(Condition(source, values));
                            r["inverse"] = Field(rule, "inverse") is true;
                            break;
                        }
                        case "ReadOnlyBoundToNameValuesRule":
                            r["type"] = "readOnly";
                            r["target"] = Field(rule, "target")?.ToString();
                            if (Field(rule, "nameValues") is (object, object)[] pairs)
                                foreach ((object source, object value) in pairs)
                                    conditions.Add(Condition(source.ToString()!, new JsonArray(RuleValue(props, source.ToString()!, value))));
                            r["inverse"] = Field(rule, "inverse") is true;
                            break;
                        case "LinkValuesBasedOnBooleanRule`2":
                            r["type"] = "link";
                            var targets = new JsonArray();
                            if (Field(rule, "targets") is object[] names)
                                foreach (object n in names)
                                    targets.Add(n.ToString());
                            r["targets"] = targets;
                            r["source"] = Field(rule, "source")?.ToString();
                            r["inverse"] = Field(rule, "inverse") is true;
                            break;
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

        // --- Efectos con ventana propia (leída sin mostrarla: DialogSettings de paint.rs) ---

        // Tipos de ajuste que Kaint aún no puede mostrar en su diálogo: esos efectos abren su ventana
        // real (con mono o Wine).
        private static readonly HashSet<string> NeedsRealWindow = new(StringComparer.Ordinal) { "canvas", "code", "curve", "grid" };

        // Traduce el esquema de una ventana propia ("properties" de paint.rs) a "params" de Kaint.
        // Devuelve false si tiene ajustes que Kaint no sabe mostrar.
        public static bool DescribeWindow(JsonElement schema, JsonObject d)
        {
            var list = new JsonArray();
            bool complete = true;
            if (schema.TryGetProperty("properties", out JsonElement props))
                foreach (JsonElement p in props.EnumerateArray())
                {
                    string type = Str(p, "type");
                    if (NeedsRealWindow.Contains(type))
                    {
                        complete = false;
                        continue;
                    }
                    var o = new JsonObject();
                    o["name"] = Str(p, "name");
                    string label = Str(p, "label");
                    string description = Str(p, "description");
                    o["label"] = label.Length > 0 ? label : description.Length > 0 ? description : Str(p, "name");
                    o["description"] = type == "bool" ? "" : description;
                    o["control"] = "";
                    o["readOnly"] = false;
                    o["hidden"] = false;
                    switch (type)
                    {
                        case "int":
                            o["type"] = "int";
                            o["min"] = Num(p, "min");
                            o["max"] = Num(p, "max");
                            o["default"] = (int)Math.Round(Num(p, "default"));
                            break;
                        case "double":
                        {
                            o["type"] = "double";
                            o["min"] = Num(p, "min");
                            o["max"] = Num(p, "max");
                            o["default"] = Num(p, "default");
                            double step = p.TryGetProperty("step", out JsonElement s) ? s.GetDouble() : 0.01;
                            o["decimals"] = step <= 0 ? 2 : Math.Clamp((int)Math.Ceiling(-Math.Log10(step) - 1e-9), 0, 4);
                            break;
                        }
                        case "bool":
                            o["type"] = "bool";
                            o["default"] = p.TryGetProperty("default", out JsonElement b) && b.ValueKind == JsonValueKind.True;
                            break;
                        case "string":
                            o["type"] = "string";
                            o["default"] = Str(p, "default");
                            o["maxLength"] = Num(p, "maxLength");
                            break;
                        case "color":
                            o["type"] = "color";
                            o["default"] = (long)Num(p, "default");
                            break;
                        case "choice":
                        {
                            o["type"] = "choice";
                            var names = new JsonArray();
                            if (p.TryGetProperty("choices", out JsonElement cs))
                                foreach (JsonElement c in cs.EnumerateArray())
                                    names.Add(c.ValueKind == JsonValueKind.Object ? Str(c, "label") : c.ToString());
                            o["choices"] = names;
                            o["default"] = (int)Num(p, "default");
                            break;
                        }
                        case "list":
                        {
                            // Una lista: como una lista desplegable con sus elementos.
                            var names = new JsonArray();
                            if (p.TryGetProperty("items", out JsonElement items))
                                foreach (JsonElement c in items.EnumerateArray())
                                    names.Add(c.ValueKind == JsonValueKind.String ? c.GetString() : c.ToString());
                            if (names.Count == 0)
                                continue;
                            o["type"] = "choice";
                            o["choices"] = names;
                            o["default"] = Math.Max(0, (int)Num(p, "default"));
                            break;
                        }
                        default:
                            // Botones y selectores de archivo: no son valores (la ventana usa lo que tenga).
                            continue;
                    }
                    list.Add(o);
                }
            d["params"] = list;
            d["rules"] = new JsonArray();
            return complete;
        }

        private static string Str(JsonElement e, string name)
            => e.TryGetProperty(name, out JsonElement v) && v.ValueKind == JsonValueKind.String ? v.GetString() ?? "" : "";

        private static double Num(JsonElement e, string name)
            => e.TryGetProperty(name, out JsonElement v) && v.ValueKind == JsonValueKind.Number ? v.GetDouble() : 0;

        // --- Valores ---

        // Los valores de Kaint ({"nombre": valor}) como las propiedades del puente: un vector [x, y] va
        // como "nombre.X" y "nombre.Y"; "$environment" lleva los colores, la paleta y la selección.
        public static string Props(JsonElement request)
        {
            var o = new JsonObject();
            if (request.TryGetProperty("values", out JsonElement values) && values.ValueKind == JsonValueKind.Object)
                foreach (JsonProperty v in values.EnumerateObject())
                {
                    if (v.Value.ValueKind == JsonValueKind.Array)
                    {
                        string[] axes = ["X", "Y", "Z"];
                        int i = 0;
                        foreach (JsonElement c in v.Value.EnumerateArray())
                            if (i < axes.Length)
                                o[v.Name + "." + axes[i++]] = JsonNode.Parse(c.GetRawText());
                        continue;
                    }
                    o[v.Name] = JsonNode.Parse(v.Value.GetRawText());
                }
            var env = new JsonObject();
            if (request.TryGetProperty("primary", out JsonElement p) && p.ValueKind == JsonValueKind.Number)
                env["primary"] = (uint)(long)p.GetDouble();
            if (request.TryGetProperty("secondary", out JsonElement s) && s.ValueKind == JsonValueKind.Number)
                env["secondary"] = (uint)(long)s.GetDouble();
            if (request.TryGetProperty("palette", out JsonElement pal) && pal.ValueKind == JsonValueKind.Array)
                env["palette"] = new JsonArray(pal.EnumerateArray().Select(c => (JsonNode?)(uint)(long)c.GetDouble()).ToArray());
            if (request.TryGetProperty("selection", out JsonElement sel) && sel.ValueKind == JsonValueKind.Array && sel.GetArrayLength() > 0)
                env["selection"] = JsonNode.Parse(sel.GetRawText());
            o["$environment"] = env;
            return o.ToJsonString();
        }
    }
}
