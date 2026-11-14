using System;
using System.Collections.Generic;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Text;
using System.Windows.Forms;

namespace RmgCfg {
    // ---------------- tiny JSON (keeps key order, preserves unknown fields) ----------------
    public class JVal {
        public const int TNull = 0, TBool = 1, TNum = 2, TStr = 3, TArr = 4, TObj = 5;
        public int T = TNull;
        public bool B;
        public double N;
        public string S = "";
        public List<JVal> A = new List<JVal>();
        public List<KeyValuePair<string, JVal>> O = new List<KeyValuePair<string, JVal>>();

        public static JVal NewObj() { JVal v = new JVal(); v.T = TObj; return v; }
        public static JVal NewArr() { JVal v = new JVal(); v.T = TArr; return v; }
        public static JVal NewStr(string s) { JVal v = new JVal(); v.T = TStr; v.S = s == null ? "" : s; return v; }
        public static JVal NewBool(bool b) { JVal v = new JVal(); v.T = TBool; v.B = b; return v; }
        public static JVal NewInt(int n) { JVal v = new JVal(); v.T = TNum; v.N = n; return v; }
        public static JVal NewNum(double d) { JVal v = new JVal(); v.T = TNum; v.N = d; return v; }
    }

    public static class Json {
        public static JVal Parse(string text) {
            int i = 0;
            JVal v = ParseValue(text, ref i, 0);
            SkipWs(text, ref i);
            return v;
        }

        static void SkipWs(string s, ref int i) {
            while (i < s.Length && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) i++;
        }

        static JVal ParseValue(string s, ref int i, int depth) {
            if (depth > 64) throw new Exception("JSON nested too deep");
            SkipWs(s, ref i);
            if (i >= s.Length) throw new Exception("JSON unexpected end");
            char c = s[i];
            if (c == '{') {
                i++;
                JVal v = JVal.NewObj();
                SkipWs(s, ref i);
                if (i < s.Length && s[i] == '}') { i++; return v; }
                while (true) {
                    SkipWs(s, ref i);
                    string key = ParseString(s, ref i);
                    SkipWs(s, ref i);
                    if (i >= s.Length || s[i] != ':') throw new Exception("JSON expected ':'");
                    i++;
                    JVal child = ParseValue(s, ref i, depth + 1);
                    v.O.Add(new KeyValuePair<string, JVal>(key, child));
                    SkipWs(s, ref i);
                    if (i < s.Length && s[i] == ',') { i++; continue; }
                    if (i < s.Length && s[i] == '}') { i++; return v; }
                    throw new Exception("JSON expected ',' or '}'");
                }
            }
            if (c == '[') {
                i++;
                JVal v = JVal.NewArr();
                SkipWs(s, ref i);
                if (i < s.Length && s[i] == ']') { i++; return v; }
                while (true) {
                    JVal child = ParseValue(s, ref i, depth + 1);
                    v.A.Add(child);
                    SkipWs(s, ref i);
                    if (i < s.Length && s[i] == ',') { i++; continue; }
                    if (i < s.Length && s[i] == ']') { i++; return v; }
                    throw new Exception("JSON expected ',' or ']'");
                }
            }
            if (c == '"') {
                JVal v = JVal.NewStr(ParseString(s, ref i));
                return v;
            }
            if (c == 't' && i + 4 <= s.Length && s.Substring(i, 4) == "true") { i += 4; return JVal.NewBool(true); }
            if (c == 'f' && i + 5 <= s.Length && s.Substring(i, 5) == "false") { i += 5; return JVal.NewBool(false); }
            if (c == 'n' && i + 4 <= s.Length && s.Substring(i, 4) == "null") { i += 4; return new JVal(); }
            if (c == '-' || (c >= '0' && c <= '9')) {
                int start = i;
                if (i < s.Length && s[i] == '-') i++;
                while (i < s.Length && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.' || s[i] == 'e' || s[i] == 'E' || s[i] == '+' || s[i] == '-')) i++;
                string num = s.Substring(start, i - start);
                JVal v = new JVal();
                v.T = JVal.TNum;
                v.N = double.Parse(num, CultureInfo.InvariantCulture);
                return v;
            }
            throw new Exception("JSON unexpected character '" + c + "' at " + i.ToString());
        }

        static string ParseString(string s, ref int i) {
            if (i >= s.Length || s[i] != '"') throw new Exception("JSON expected string");
            i++;
            StringBuilder sb = new StringBuilder();
            while (i < s.Length) {
                char c = s[i++];
                if (c == '"') return sb.ToString();
                if (c == '\\') {
                    if (i >= s.Length) break;
                    char e = s[i++];
                    switch (e) {
                        case '"': sb.Append('"'); break;
                        case '\\': sb.Append('\\'); break;
                        case '/': sb.Append('/'); break;
                        case 'b': sb.Append('\b'); break;
                        case 'f': sb.Append('\f'); break;
                        case 'n': sb.Append('\n'); break;
                        case 'r': sb.Append('\r'); break;
                        case 't': sb.Append('\t'); break;
                        case 'u': {
                                if (i + 4 > s.Length) throw new Exception("JSON bad \\u escape");
                                int cp = int.Parse(s.Substring(i, 4), NumberStyles.HexNumber, CultureInfo.InvariantCulture);
                                i += 4;
                                sb.Append((char)cp);
                                break;
                            }
                        default: throw new Exception("JSON bad escape");
                    }
                } else {
                    sb.Append(c);
                }
            }
            throw new Exception("JSON unterminated string");
        }

        public static string Stringify(JVal v, bool pretty) {
            StringBuilder sb = new StringBuilder();
            Write(sb, v, pretty, 0);
            return sb.ToString();
        }

        static void Indent(StringBuilder sb, bool pretty, int level) {
            if (!pretty) return;
            sb.Append('\n');
            sb.Append(' ', level * 2);
        }

        static void Write(StringBuilder sb, JVal v, bool pretty, int level) {
            switch (v.T) {
                case JVal.TNull: sb.Append("null"); break;
                case JVal.TBool: sb.Append(v.B ? "true" : "false"); break;
                case JVal.TNum: {
                        if (Math.Abs(v.N - Math.Round(v.N)) < 0.000001) sb.Append(((long)Math.Round(v.N)).ToString(CultureInfo.InvariantCulture));
                        else sb.Append(v.N.ToString("0.####", CultureInfo.InvariantCulture));
                        break;
                    }
                case JVal.TStr: WriteEscaped(sb, v.S); break;
                case JVal.TArr: {
                        if (v.A.Count == 0) { sb.Append("[]"); break; }
                        sb.Append('[');
                        for (int i = 0; i < v.A.Count; i++) {
                            if (i > 0) sb.Append(',');
                            Indent(sb, pretty, level + 1);
                            Write(sb, v.A[i], pretty, level + 1);
                        }
                        Indent(sb, pretty, level);
                        sb.Append(']');
                        break;
                    }
                case JVal.TObj: {
                        if (v.O.Count == 0) { sb.Append("{}"); break; }
                        sb.Append('{');
                        for (int i = 0; i < v.O.Count; i++) {
                            if (i > 0) sb.Append(',');
                            Indent(sb, pretty, level + 1);
                            WriteEscaped(sb, v.O[i].Key);
                            sb.Append(pretty ? ": " : ":");
                            Write(sb, v.O[i].Value, pretty, level + 1);
                        }
                        Indent(sb, pretty, level);
                        sb.Append('}');
                        break;
                    }
            }
        }

        static void WriteEscaped(StringBuilder sb, string s) {
            sb.Append('"');
            for (int i = 0; i < s.Length; i++) {
                char c = s[i];
                switch (c) {
                    case '"': sb.Append("\\\""); break;
                    case '\\': sb.Append("\\\\"); break;
                    case '\n': sb.Append("\\n"); break;
                    case '\r': sb.Append("\\r"); break;
                    case '\t': sb.Append("\\t"); break;
                    case '\b': sb.Append("\\b"); break;
                    case '\f': sb.Append("\\f"); break;
                    default:
                        if (c < 0x20) sb.Append("\\u").Append(((int)c).ToString("x4"));
                        else sb.Append(c);
                        break;
                }
            }
            sb.Append('"');
        }

        // ---------- accessors ----------
        public static JVal Get(JVal obj, string key) {
            if (obj == null || obj.T != JVal.TObj) return null;
            for (int i = 0; i < obj.O.Count; i++) if (obj.O[i].Key == key) return obj.O[i].Value;
            return null;
        }

        public static void Set(JVal obj, string key, JVal val) {
            if (obj == null || obj.T != JVal.TObj) return;
            for (int i = 0; i < obj.O.Count; i++) {
                if (obj.O[i].Key == key) { obj.O[i] = new KeyValuePair<string, JVal>(key, val); return; }
            }
            obj.O.Add(new KeyValuePair<string, JVal>(key, val));
        }

        public static JVal Obj(JVal parent, string key) {
            JVal v = Get(parent, key);
            if (v == null || v.T != JVal.TObj) { v = JVal.NewObj(); Set(parent, key, v); }
            return v;
        }

        public static bool GetBool(JVal obj, string key, bool def) {
            JVal v = Get(obj, key);
            return (v != null && v.T == JVal.TBool) ? v.B : def;
        }

        public static int GetInt(JVal obj, string key, int def) {
            JVal v = Get(obj, key);
            return (v != null && v.T == JVal.TNum) ? (int)Math.Round(v.N) : def;
        }

        public static string GetStr(JVal obj, string key, string def) {
            JVal v = Get(obj, key);
            return (v != null && v.T == JVal.TStr) ? v.S : def;
        }

        public static List<string> GetStrList(JVal obj, string key) {
            List<string> list = new List<string>();
            JVal v = Get(obj, key);
            if (v != null && v.T == JVal.TArr) {
                for (int i = 0; i < v.A.Count; i++) if (v.A[i].T == JVal.TStr) list.Add(v.A[i].S);
            }
            return list;
        }

        public static JVal StrList(List<string> items) {
            JVal arr = JVal.NewArr();
            for (int i = 0; i < items.Count; i++) arr.A.Add(JVal.NewStr(items[i]));
            return arr;
        }
    }

    // ---------------- color swatch control ----------------
    public class ColorSwatch : Control {
        public string Hex = "#000000";
        public event EventHandler HexChanged;

        public ColorSwatch() {
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.UserPaint | ControlStyles.ResizeRedraw, true);
            Size = new Size(52, 24);
            Cursor = Cursors.Hand;
        }

        public static Color FromHex(string hex, Color def) {
            if (string.IsNullOrEmpty(hex)) return def;
            string s = hex.Trim();
            if (s.StartsWith("#")) s = s.Substring(1);
            if (s.Length != 6) return def;
            try {
                int r = int.Parse(s.Substring(0, 2), NumberStyles.HexNumber);
                int g = int.Parse(s.Substring(2, 2), NumberStyles.HexNumber);
                int b = int.Parse(s.Substring(4, 2), NumberStyles.HexNumber);
                return Color.FromArgb(r, g, b);
            } catch { return def; }
        }

        public static string ToHex(Color c) {
            return "#" + c.R.ToString("X2") + c.G.ToString("X2") + c.B.ToString("X2");
        }

        protected override void OnPaint(PaintEventArgs e) {
            Graphics g = e.Graphics;
            g.Clear(Parent != null ? Parent.BackColor : SystemColors.Control);
            Rectangle r = new Rectangle(0, 0, Width - 1, Height - 1);
            Color c = FromHex(Hex, Color.Black);
            using (SolidBrush b = new SolidBrush(c)) g.FillRectangle(b, r);
            using (Pen p = new Pen(Color.FromArgb(120, 120, 120))) g.DrawRectangle(p, r);
        }

        protected override void OnClick(EventArgs e) {
            base.OnClick(e);
            using (ColorDialog dlg = new ColorDialog()) {
                dlg.FullOpen = true;
                dlg.Color = FromHex(Hex, Color.Black);
                if (dlg.ShowDialog(this) == DialogResult.OK) {
                    Hex = ToHex(dlg.Color);
                    Invalidate();
                    if (HexChanged != null) HexChanged(this, EventArgs.Empty);
                }
            }
        }
    }
}
