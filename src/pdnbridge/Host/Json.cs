// Kaint: JSON mínimo para el protocolo entre Kaint y el proceso auxiliar.
// Objetos -> Dictionary<string, object>, listas -> List<object>, números ->
// double, más string, bool y null.

using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace Kaint.PdnHost
{
    public static class Json
    {
        public static object Parse(string text)
        {
            int pos = 0;
            object value = ParseValue(text, ref pos);
            SkipSpaces(text, ref pos);
            if (pos != text.Length)
                throw new FormatException("JSON: texto sobrante en " + pos);
            return value;
        }

        private static void SkipSpaces(string s, ref int pos)
        {
            while (pos < s.Length && char.IsWhiteSpace(s[pos]))
                ++pos;
        }

        private static object ParseValue(string s, ref int pos)
        {
            SkipSpaces(s, ref pos);
            if (pos >= s.Length)
                throw new FormatException("JSON: fin inesperado");
            char c = s[pos];
            if (c == '{')
            {
                ++pos;
                Dictionary<string, object> obj = new Dictionary<string, object>();
                SkipSpaces(s, ref pos);
                if (pos < s.Length && s[pos] == '}')
                {
                    ++pos;
                    return obj;
                }
                while (true)
                {
                    SkipSpaces(s, ref pos);
                    string key = ParseString(s, ref pos);
                    SkipSpaces(s, ref pos);
                    Expect(s, ref pos, ':');
                    obj[key] = ParseValue(s, ref pos);
                    SkipSpaces(s, ref pos);
                    if (pos < s.Length && s[pos] == ',')
                    {
                        ++pos;
                        continue;
                    }
                    Expect(s, ref pos, '}');
                    return obj;
                }
            }
            if (c == '[')
            {
                ++pos;
                List<object> list = new List<object>();
                SkipSpaces(s, ref pos);
                if (pos < s.Length && s[pos] == ']')
                {
                    ++pos;
                    return list;
                }
                while (true)
                {
                    list.Add(ParseValue(s, ref pos));
                    SkipSpaces(s, ref pos);
                    if (pos < s.Length && s[pos] == ',')
                    {
                        ++pos;
                        continue;
                    }
                    Expect(s, ref pos, ']');
                    return list;
                }
            }
            if (c == '"')
                return ParseString(s, ref pos);
            if (string.CompareOrdinal(s, pos, "true", 0, 4) == 0)
            {
                pos += 4;
                return true;
            }
            if (string.CompareOrdinal(s, pos, "false", 0, 5) == 0)
            {
                pos += 5;
                return false;
            }
            if (string.CompareOrdinal(s, pos, "null", 0, 4) == 0)
            {
                pos += 4;
                return null;
            }
            int start = pos;
            while (pos < s.Length && "+-0123456789.eE".IndexOf(s[pos]) >= 0)
                ++pos;
            if (start == pos)
                throw new FormatException("JSON: carácter inesperado '" + c + "' en " + pos);
            return double.Parse(s.Substring(start, pos - start), NumberStyles.Float, CultureInfo.InvariantCulture);
        }

        private static void Expect(string s, ref int pos, char c)
        {
            if (pos >= s.Length || s[pos] != c)
                throw new FormatException("JSON: se esperaba '" + c + "' en " + pos);
            ++pos;
        }

        private static string ParseString(string s, ref int pos)
        {
            Expect(s, ref pos, '"');
            StringBuilder sb = new StringBuilder();
            while (pos < s.Length)
            {
                char c = s[pos++];
                if (c == '"')
                    return sb.ToString();
                if (c != '\\')
                {
                    sb.Append(c);
                    continue;
                }
                char e = s[pos++];
                switch (e)
                {
                    case 'n': sb.Append('\n'); break;
                    case 'r': sb.Append('\r'); break;
                    case 't': sb.Append('\t'); break;
                    case 'b': sb.Append('\b'); break;
                    case 'f': sb.Append('\f'); break;
                    case 'u':
                        sb.Append((char)int.Parse(s.Substring(pos, 4), NumberStyles.HexNumber));
                        pos += 4;
                        break;
                    default: sb.Append(e); break;
                }
            }
            throw new FormatException("JSON: cadena sin cerrar");
        }

        public static string Serialize(object value)
        {
            StringBuilder sb = new StringBuilder();
            Write(sb, value);
            return sb.ToString();
        }

        private static void Write(StringBuilder sb, object value)
        {
            if (value == null)
            {
                sb.Append("null");
            }
            else if (value is string)
            {
                WriteString(sb, (string)value);
            }
            else if (value is bool)
            {
                sb.Append((bool)value ? "true" : "false");
            }
            else if (value is double || value is float || value is decimal)
            {
                double d = Convert.ToDouble(value, CultureInfo.InvariantCulture);
                if (double.IsNaN(d) || double.IsInfinity(d))
                    sb.Append("0");
                else
                    sb.Append(d.ToString("R", CultureInfo.InvariantCulture));
            }
            else if (value is int || value is long || value is uint || value is short || value is byte || value is ulong || value is sbyte || value is ushort)
            {
                sb.Append(Convert.ToString(value, CultureInfo.InvariantCulture));
            }
            else if (value is IDictionary)
            {
                sb.Append('{');
                bool first = true;
                foreach (DictionaryEntry entry in (IDictionary)value)
                {
                    if (!first)
                        sb.Append(',');
                    first = false;
                    WriteString(sb, entry.Key.ToString());
                    sb.Append(':');
                    Write(sb, entry.Value);
                }
                sb.Append('}');
            }
            else if (value is IEnumerable)
            {
                sb.Append('[');
                bool first = true;
                foreach (object item in (IEnumerable)value)
                {
                    if (!first)
                        sb.Append(',');
                    first = false;
                    Write(sb, item);
                }
                sb.Append(']');
            }
            else
            {
                WriteString(sb, value.ToString());
            }
        }

        private static void WriteString(StringBuilder sb, string s)
        {
            sb.Append('"');
            foreach (char c in s)
            {
                switch (c)
                {
                    case '"': sb.Append("\\\""); break;
                    case '\\': sb.Append("\\\\"); break;
                    case '\n': sb.Append("\\n"); break;
                    case '\r': sb.Append("\\r"); break;
                    case '\t': sb.Append("\\t"); break;
                    default:
                        if (c < 0x20)
                            sb.Append("\\u").Append(((int)c).ToString("x4"));
                        else
                            sb.Append(c);
                        break;
                }
            }
            sb.Append('"');
        }

        // Ayudas para leer peticiones.
        public static string Str(Dictionary<string, object> obj, string key, string def = null)
        {
            object v;
            return obj.TryGetValue(key, out v) && v != null ? v.ToString() : def;
        }

        public static double Num(Dictionary<string, object> obj, string key, double def = 0)
        {
            object v;
            return obj.TryGetValue(key, out v) && v != null ? Convert.ToDouble(v, CultureInfo.InvariantCulture) : def;
        }

        public static List<object> List(Dictionary<string, object> obj, string key)
        {
            object v;
            return obj.TryGetValue(key, out v) ? v as List<object> : null;
        }
    }
}
