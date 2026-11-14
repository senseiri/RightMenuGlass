using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Drawing.Text;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows.Forms;
using Microsoft.Win32;

namespace RmgCfg {
    public class MainForm : Form {
        [DllImport("user32.dll")]
        static extern bool SetProcessDPIAware();

        readonly string cfgPath;
        readonly string cfgDir;
        readonly string appDir;
        JVal root = JVal.NewObj();
        // 设置界面沿用右键菜单的字体：若配置了字体文件，用私有字体集合加载，
        // 字段必须留住引用，否则集合被回收后字体失效。
        PrivateFontCollection uiFonts;

        TabControl tabs;
        Label statusLabel;

        // 常规
        CheckBox chkEnabled, chkLogVerbose;
        CheckBox chkAutostart, chkAutostartWatch;

        // 亚克力
        CheckBox chkAcrylic, chkAutoTint;
        NumericUpDown numBlur, numOpacity, numNoise, numCorner, numShadow, numMargin;
        ColorSwatch swTint, swLightTint;

        // 文字
        CheckBox chkColors, chkShowShortcut, chkShowCheckmarks;
        ComboBox cboColorUnit;
        ColorSwatch swDark, swLight, swExtraDark, swExtraLight;
        NumericUpDown numThreshold, numSoftness, numHaloW, numHaloA, numFontSize, numItemHeight, numIconSize, numHeaderH, numMinW, numMaxW, numMaxH, numHoverDelay;
        ComboBox cboFont;
        ComboBox cboOpenDir;
        TextBox txtFontFile;
        Panel previewPanel;

        // 动画
        CheckBox chkAnim;
        ComboBox cboAnimMode;
        NumericUpDown numAnimDur, numAnimOpacity, numAnimScale, numAnimOffset;
        NumericUpDown numBz1, numBz2, numBz3, numBz4;
        Panel animPreview;
        Timer animTimer;
        double animProgress = 1.0;

        // 顶栏
        DataGridView grid;
        ComboBox cboPreset;

        // 第三方
        CheckBox chkThird, chkSubmenu, chkSubmenuIcon, chkMerge;
        TextBox txtThirdTitle;
        TextBox txtHide, txtMergeTitle;
        TreeView treeProviders;
        bool treeGuard;

        public MainForm() {
            appDir = Path.GetDirectoryName(Application.ExecutablePath);
            cfgDir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "RightMenuGlass");
            cfgPath = Path.Combine(cfgDir, "config.json");

            Text = "RightMenuGlass 设置";
            Font = new Font("Microsoft YaHei UI", 9f);
            ClientSize = new Size(820, 660);
            MinimumSize = new Size(840, 700);
            StartPosition = FormStartPosition.CenterScreen;
            MaximizeBox = false;

            LoadConfig();
            ApplyMenuFontToUi();
            BuildUi();
            LoadIntoUi();

            // 支持 rmgcfg.exe --tab 2 直接打开指定页（方便截图与文档演示）。
            string[] args = Environment.GetCommandLineArgs();
            for (int i = 1; i < args.Length - 1; ++i) {
                if (args[i] == "--tab") {
                    int idx;
                    if (int.TryParse(args[i + 1], out idx) && idx >= 0 && idx < tabs.TabPages.Count) {
                        tabs.SelectedIndex = idx;
                    }
                }
            }
        }

        // ---------------- config io ----------------
        void LoadConfig() {
            try {
                if (File.Exists(cfgPath)) {
                    string text = File.ReadAllText(cfgPath, Encoding.UTF8);
                    JVal v = Json.Parse(text);
                    if (v != null && v.T == JVal.TObj) root = v;
                }
            } catch (Exception ex) {
                MessageBox.Show("读取配置失败，将使用默认值：\n" + ex.Message, "RightMenuGlass", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            }
        }

        // 让设置界面的字体跟右键菜单保持一致：优先用 style.fontFile 指定的字体文件，
        // 否则用 style.fontFamily。这样在界面里调字号/换字体时，看的就是菜单里的观感。
        void ApplyMenuFontToUi() {
            string family = "Microsoft YaHei UI";
            float size = 9f;
            JVal st = Json.Get(root, "style");
            if (st != null) {
                family = Json.GetStr(st, "fontFamily", family);
                size = (float)Json.GetInt(st, "fontSize", 9);
                string file = Json.GetStr(st, "fontFile", "");
                if (file != null && file.Length > 0 && File.Exists(file)) {
                    try {
                        uiFonts = new PrivateFontCollection();
                        uiFonts.AddFontFile(file);
                        if (uiFonts.Families.Length > 0) family = uiFonts.Families[0].Name;
                    } catch (Exception) {
                        uiFonts = null;
                    }
                }
            }
            if (size < 6f) size = 6f;
            if (size > 40f) size = 40f;
            try {
                Font = new Font(family, size, FontStyle.Regular, GraphicsUnit.Point);
            } catch (Exception) {
                Font = new Font("Microsoft YaHei UI", 9f);
            }
        }

        // Never print the real user profile path: screenshots of this window end up in
        // the repository and it would leak the account name.
        string ShownPath(string full) {
            string localApp = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
            if (localApp.Length > 0 && full.StartsWith(localApp, StringComparison.OrdinalIgnoreCase)) {
                return "%LOCALAPPDATA%" + full.Substring(localApp.Length);
            }
            return full;
        }

        void ExportSettings() {
            CollectFromUi();
            SaveFileDialog dlg = new SaveFileDialog();
            dlg.Title = "导出 RightMenuGlass 设置";
            dlg.Filter = "RightMenuGlass 配置 (*.json)|*.json|所有文件 (*.*)|*.*";
            dlg.FileName = "RightMenuGlass-config.json";
            dlg.OverwritePrompt = true;
            if (dlg.ShowDialog(this) != DialogResult.OK) return;
            try {
                File.WriteAllText(dlg.FileName, Json.Stringify(root, true) + "\r\n", new UTF8Encoding(false));
                statusLabel.Text = "已导出：" + dlg.FileName;
            } catch (Exception ex) {
                MessageBox.Show("导出失败：\r\n" + ex.Message, "RightMenuGlass",
                                MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        void ImportSettings() {
            OpenFileDialog dlg = new OpenFileDialog();
            dlg.Title = "导入 RightMenuGlass 设置";
            dlg.Filter = "RightMenuGlass 配置 (*.json)|*.json|所有文件 (*.*)|*.*";
            dlg.CheckFileExists = true;
            if (dlg.ShowDialog(this) != DialogResult.OK) return;
            JVal parsed = null;
            try {
                parsed = Json.Parse(File.ReadAllText(dlg.FileName, Encoding.UTF8));
            } catch (Exception ex) {
                MessageBox.Show("这个文件不是有效的 JSON：\r\n" + ex.Message, "RightMenuGlass",
                                MessageBoxButtons.OK, MessageBoxIcon.Error);
                return;
            }
            if (parsed == null || parsed.T != JVal.TObj) {
                MessageBox.Show("这不是一份 RightMenuGlass 配置文件（顶层必须是一个 JSON 对象）。",
                                "RightMenuGlass", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }
            string[] known = new string[] { "version", "enabled", "logVerbose", "colors", "acrylic",
                                            "animation", "style", "thirdParty", "buttons" };
            bool any = false;
            for (int i = 0; i < known.Length && !any; i++) {
                if (Json.Get(parsed, known[i]) != null) any = true;
            }
            if (!any) {
                if (MessageBox.Show("这个文件里没有任何 RightMenuGlass 认识的字段，仍然导入吗？",
                                    "RightMenuGlass", MessageBoxButtons.YesNo, MessageBoxIcon.Warning) != DialogResult.Yes) return;
            }
            root = parsed;
            LoadIntoUi();
            ApplyMenuFontToUi();
            statusLabel.Text = "已载入 " + Path.GetFileName(dlg.FileName) + "，点「保存并应用」后生效。";
        }

        void SaveConfig() {
            CollectFromUi();
            try {
                if (!Directory.Exists(cfgDir)) Directory.CreateDirectory(cfgDir);
                File.WriteAllText(cfgPath, Json.Stringify(root, true) + "\n", new UTF8Encoding(false));
                statusLabel.Text = "已保存：" + ShownPath(cfgPath);
                ApplyAutostartFromUi();
            } catch (Exception ex) {
                MessageBox.Show("保存失败：\n" + ex.Message, "RightMenuGlass", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        // ---------------- ui helpers ----------------
        Label MkLabel(string text, int x, int y, int w) {
            Label l = new Label();
            l.Text = text;
            l.AutoSize = false;
            l.TextAlign = ContentAlignment.MiddleLeft;
            l.Bounds = new Rectangle(x, y, w, 24);
            return l;
        }

        Label MkHint(string text, int x, int y, int w, int h) {
            Label l = new Label();
            l.Text = text;
            l.AutoSize = false;
            l.ForeColor = Color.FromArgb(110, 110, 110);
            l.Bounds = new Rectangle(x, y, w, h);
            return l;
        }

        CheckBox MkCheck(string text, int x, int y, int w) {
            CheckBox c = new CheckBox();
            c.Text = text;
            c.AutoSize = false;
            c.Bounds = new Rectangle(x, y, w, 24);
            return c;
        }

        NumericUpDown MkNum(int x, int y, int min, int max, int val, int w) {
            NumericUpDown n = new NumericUpDown();
            n.Minimum = min;
            n.Maximum = max;
            n.Value = Math.Max(min, Math.Min(max, val));
            n.Bounds = new Rectangle(x, y, w, 24);
            return n;
        }

        TextBox MkText(int x, int y, int w, string val) {
            TextBox t = new TextBox();
            t.Bounds = new Rectangle(x, y, w, 24);
            t.Text = val;
            return t;
        }

        Button MkBtn(string text, int x, int y, int w, EventHandler h) {
            Button b = new Button();
            b.Text = text;
            b.Bounds = new Rectangle(x, y, w, 28);
            if (h != null) b.Click += h;
            return b;
        }

        GroupBox MkGroup(string text, int x, int y, int w, int h) {
            GroupBox g = new GroupBox();
            g.Text = text;
            g.Bounds = new Rectangle(x, y, w, h);
            return g;
        }

        // ---------------- layout ----------------
        void BuildUi() {
            tabs = new TabControl();
            tabs.Bounds = new Rectangle(12, 12, ClientSize.Width - 24, ClientSize.Height - 74);
            tabs.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right | AnchorStyles.Bottom;
            Controls.Add(tabs);

            tabs.TabPages.Add(BuildGeneralTab());
            tabs.TabPages.Add(BuildAppearanceTab());
            tabs.TabPages.Add(BuildAnimationTab());
            tabs.TabPages.Add(BuildTextTab());
            tabs.TabPages.Add(BuildTopBarTab());
            tabs.TabPages.Add(BuildThirdPartyTab());

            Button btnSave = MkBtn("保存并应用", ClientSize.Width - 260, ClientSize.Height - 48, 120, delegate (object s, EventArgs e) { SaveConfig(); });
            btnSave.Anchor = AnchorStyles.Bottom | AnchorStyles.Right;
            Button btnClose = MkBtn("关闭", ClientSize.Width - 128, ClientSize.Height - 48, 100, delegate (object s, EventArgs e) { Close(); });
            btnClose.Anchor = AnchorStyles.Bottom | AnchorStyles.Right;

            statusLabel = new Label();
            statusLabel.AutoSize = false;
            statusLabel.Bounds = new Rectangle(16, ClientSize.Height - 44, ClientSize.Width - 300, 24);
            statusLabel.Anchor = AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
            // Never show the real user profile path (it leaks the account name in screenshots).
            // Display the environment variable form instead.
            statusLabel.Text = "配置：" + ShownPath(cfgPath);
            statusLabel.ForeColor = Color.FromArgb(110, 110, 110);

            Controls.Add(statusLabel);
            Controls.Add(btnSave);
            Controls.Add(btnClose);
        }

        TabPage NewTab(string title) {
            TabPage p = new TabPage(title);
            p.BackColor = SystemColors.Control;
            return p;
        }

        TabPage BuildGeneralTab() {
            TabPage p = NewTab("常规");

            chkEnabled = MkCheck("启用右键菜单美化（关闭后恢复系统原生菜单）", 20, 18, 460);
            chkLogVerbose = MkCheck("写入详细日志（排查问题时才需要）", 20, 50, 460);
            p.Controls.Add(chkEnabled);
            p.Controls.Add(chkLogVerbose);
            p.Controls.Add(MkHint("注意：配置保存在 %LOCALAPPDATA%\\RightMenuGlass\\config.json，修改后下一次弹出菜单立即生效，无需重启。", 20, 78, 760, 22));

            GroupBox g1 = MkGroup("文件", 20, 112, 770, 108);
            g1.Controls.Add(MkBtn("打开配置目录", 16, 26, 130, delegate (object s, EventArgs e) { OpenDir(cfgDir); }));
            g1.Controls.Add(MkBtn("打开日志文件", 156, 26, 130, delegate (object s, EventArgs e) {
                string log = Path.Combine(cfgDir, "dll.log");
                if (File.Exists(log)) Process.Start("notepad.exe", "\"" + log + "\"");
                else MessageBox.Show("还没有日志文件，先用右键菜单弹出一次。", "RightMenuGlass");
            }));
            g1.Controls.Add(MkBtn("打开程序目录", 296, 26, 130, delegate (object s, EventArgs e) { OpenDir(appDir); }));
            g1.Controls.Add(MkBtn("导出设置…", 440, 26, 120, delegate (object s, EventArgs e) { ExportSettings(); }));
            g1.Controls.Add(MkBtn("导入设置…", 572, 26, 120, delegate (object s, EventArgs e) { ImportSettings(); }));
            g1.Controls.Add(MkHint("导出/导入只处理 config.json 这一份设置，插件和菜单记录不包含在内。", 16, 64, 730, 22));
            p.Controls.Add(g1);

            GroupBox g2 = MkGroup("插件", 20, 232, 770, 96);
            g2.Controls.Add(MkBtn("打开插件目录", 16, 26, 130, delegate (object s, EventArgs e) {
                string dir = Path.Combine(cfgDir, "plugins");
                if (!Directory.Exists(dir)) Directory.CreateDirectory(dir);
                OpenDir(dir);
            }));
            g2.Controls.Add(MkBtn("安装示例插件", 156, 26, 130, delegate (object s, EventArgs e) { InstallSamplePlugins(); }));
            g2.Controls.Add(MkBtn("打开插件文档", 296, 26, 130, delegate (object s, EventArgs e) {
                string doc = Path.Combine(appDir, "docs", "PLUGINS.md");
                if (File.Exists(doc)) Process.Start("notepad.exe", "\"" + doc + "\"");
                else MessageBox.Show("找不到 docs\\PLUGINS.md", "RightMenuGlass");
            }));
            g2.Controls.Add(MkHint("插件是 UTF-8 编码的 .json 文件，放在插件目录里即可，格式见文档。", 16, 62, 730, 22));
            p.Controls.Add(g2);

            GroupBox g3 = MkGroup("应用到资源管理器", 20, 334, 770, 212);
            g3.Controls.Add(MkBtn("仅重新注入 DLL", 16, 26, 140, delegate (object s, EventArgs e) { RunCtl("inject", true); }));
            g3.Controls.Add(MkBtn("重启资源管理器并注入", 166, 26, 170, delegate (object s, EventArgs e) {
                if (MessageBox.Show("会关闭所有文件夹窗口（不会丢失已保存的文件），桌面会闪烁一下。继续？", "RightMenuGlass", MessageBoxButtons.OKCancel, MessageBoxIcon.Question) == DialogResult.OK)
                    RunCtl("restart", true);
            }));
            g3.Controls.Add(MkBtn("查看注入状态", 346, 26, 120, delegate (object s, EventArgs e) { RunCtl("status", false); }));
            g3.Controls.Add(MkHint("第一次使用、或升级了 DLL 之后，需要点「重启资源管理器并注入」。日常修改配置不需要。", 16, 66, 730, 40));

            chkAutostart = MkCheck("开机自动注入（登录后注入一次就退出，不常驻）", 16, 110, 520);
            chkAutostartWatch = MkCheck("常驻监视（explorer 重启后自动补注入，任务管理器里会多一个 rmgctl.exe）", 34, 136, 700);
            g3.Controls.Add(chkAutostart);
            g3.Controls.Add(chkAutostartWatch);
            g3.Controls.Add(MkHint("取消勾选则开机不自动注入；之后右键菜单失效时，点上面的按钮手动注入一次即可。", 16, 164, 730, 40));
            chkAutostart.CheckedChanged += delegate (object s, EventArgs e) { UpdateAutostartEnabled(); };
            p.Controls.Add(g3);

            return p;
        }

        TabPage BuildAppearanceTab() {
            TabPage p = NewTab("外观");

            GroupBox g = MkGroup("亚克力 / 毛玻璃", 20, 16, 770, 448);

            chkAcrylic = MkCheck("启用亚克力模糊背景", 16, 28, 300);

            g.Controls.Add(MkLabel("模糊半径", 16, 66, 90));
            numBlur = MkNum(110, 66, 1, 40, 18, 70);
            g.Controls.Add(numBlur);
            g.Controls.Add(MkHint("像素，越大越柔和（内部按 1/4 降采样计算，开销很小）", 190, 68, 540, 22));

            g.Controls.Add(MkLabel("不透明度", 16, 102, 90));
            numOpacity = MkNum(110, 102, 0, 100, 58, 70);
            g.Controls.Add(numOpacity);
            g.Controls.Add(MkHint("0 = 完全透明（只看到模糊背景），100 = 完全不透明", 190, 104, 540, 22));

            g.Controls.Add(MkLabel("深色基调", 16, 138, 90));
            swTint = new ColorSwatch();
            swTint.Bounds = new Rectangle(110, 138, 52, 24);
            g.Controls.Add(swTint);
            g.Controls.Add(MkHint("背景偏暗时叠加的颜色", 180, 140, 300, 22));

            g.Controls.Add(MkLabel("浅色基调", 16, 174, 90));
            swLightTint = new ColorSwatch();
            swLightTint.Bounds = new Rectangle(110, 174, 52, 24);
            g.Controls.Add(swLightTint);
            chkAutoTint = MkCheck("根据背景亮度自动在深浅基调之间过渡", 180, 174, 380);
            g.Controls.Add(chkAutoTint);

            g.Controls.Add(MkLabel("噪点", 16, 210, 90));
            numNoise = MkNum(110, 210, 0, 40, 5, 70);
            g.Controls.Add(numNoise);
            g.Controls.Add(MkHint("模拟磨砂玻璃的颗粒感，0 = 关闭", 190, 212, 340, 22));

            g.Controls.Add(MkLabel("圆角", 16, 246, 90));
            numCorner = MkNum(110, 246, 0, 28, 10, 70);
            g.Controls.Add(numCorner);

            g.Controls.Add(MkLabel("阴影", 16, 282, 90));
            numShadow = MkNum(110, 282, 0, 48, 14, 70);
            g.Controls.Add(numShadow);

            g.Controls.Add(MkLabel("内边距", 16, 318, 90));
            numMargin = MkNum(110, 318, 0, 40, 10, 70);
            g.Controls.Add(numMargin);

            g.Controls.Add(MkLabel("展开方向", 16, 356, 90));
            cboOpenDir = new ComboBox();
            cboOpenDir.DropDownStyle = ComboBoxStyle.DropDownList;
            cboOpenDir.Bounds = new Rectangle(110, 354, 152, 24);
            cboOpenDir.Items.AddRange(new object[] {
                "右下（默认）", "右上", "左下", "左上", "自动（朝屏幕中央）"
            });
            g.Controls.Add(cboOpenDir);
            g.Controls.Add(MkHint("菜单从指针的哪一侧展开；放不下时自动翻到另一侧。", 276, 358, 470, 22));
            g.Controls.Add(MkHint("选「自动」时按指针所在的半屏朝屏幕中央展开。", 110, 382, 660, 22));

            g.Controls.Add(MkHint("所有尺寸都会按当前显示器的 DPI 自动缩放。", 16, 410, 730, 22));

            p.Controls.Add(g);
            return p;
        }

        // ---------------- animation tab ----------------
        bool loadGuard;

        NumericUpDown MkNumDec(int x, int y, decimal min, decimal max, decimal val, decimal step, int w) {
            NumericUpDown n = new NumericUpDown();
            n.DecimalPlaces = 2;
            n.Increment = step;
            n.Minimum = min;
            n.Maximum = max;
            n.Value = Math.Max(min, Math.Min(max, val));
            n.Bounds = new Rectangle(x, y, w, 24);
            n.ValueChanged += delegate (object s, EventArgs e) { if (!loadGuard) { animProgress = 1.0; animPreview.Invalidate(); } };
            return n;
        }

        static double BzAxis(double t, double p1, double p2) {
            double mt = 1 - t;
            return 3 * mt * mt * t * p1 + 3 * mt * t * t * p2 + t * t * t;
        }

        // y for a given x on cubic-bezier(x1,y1,x2,y2) with endpoints (0,0)/(1,1).
        static double BzEase(double x, double x1, double y1, double x2, double y2) {
            if (x <= 0) return 0;
            if (x >= 1) return 1;
            double lo = 0, hi = 1, t = x;
            for (int i = 0; i < 32; i++) {
                double cx = BzAxis(t, x1, x2);
                if (cx < x) lo = t; else hi = t;
                t = (lo + hi) / 2;
            }
            return BzAxis(t, y1, y2);
        }

        // 取色粒度：0 逐像素 / 1 按字 / 2 整项
        static int UnitToIndex(string u) {
            if (u == "glyph") return 1;
            if (u == "item") return 2;
            return 0;
        }

        static string IndexToUnit(int i) {
            if (i == 1) return "glyph";
            if (i == 2) return "item";
            return "pixel";
        }

        static int ModeToIndex(string mode) {
            if (mode == null) return 2;
            string m = mode.Trim().ToLower();
            if (m == "none") return 0;
            if (m == "fast") return 1;
            if (m == "elegant") return 3;
            if (m == "custom") return 4;
            return 2;
        }

        static string IndexToMode(int idx) {
            if (idx == 0) return "none";
            if (idx == 1) return "fast";
            if (idx == 3) return "elegant";
            if (idx == 4) return "custom";
            return "smooth";
        }

        void ApplyAnimPreset(int idx) {
            if (idx == 0) {
                numAnimDur.Value = 0;
            } else if (idx == 1) {
                numAnimDur.Value = 120;
                numAnimOpacity.Value = 0;
                numAnimScale.Value = 98;
                numAnimOffset.Value = 4;
                numBz1.Value = 0.20m; numBz2.Value = 0.90m;
                numBz3.Value = 0.30m; numBz4.Value = 1.00m;
            } else if (idx == 2) {
                numAnimDur.Value = 220;
                numAnimOpacity.Value = 0;
                numAnimScale.Value = 96;
                numAnimOffset.Value = 8;
                numBz1.Value = 0.16m; numBz2.Value = 0.84m;
                numBz3.Value = 0.44m; numBz4.Value = 1.00m;
            } else if (idx == 3) {
                numAnimDur.Value = 420;
                numAnimOpacity.Value = 0;
                numAnimScale.Value = 93;
                numAnimOffset.Value = 12;
                numBz1.Value = 0.22m; numBz2.Value = 1.00m;
                numBz3.Value = 0.36m; numBz4.Value = 1.00m;
            }
        }

        void UpdateAnimEnabledState() {
            int idx = cboAnimMode.SelectedIndex;
            bool custom = idx == 4;
            bool on = chkAnim.Checked && idx != 0;
            numAnimDur.Enabled = custom && on;
            numAnimOpacity.Enabled = custom && on;
            numAnimScale.Enabled = custom && on;
            numAnimOffset.Enabled = custom && on;
            numBz1.Enabled = custom && on;
            numBz2.Enabled = custom && on;
            numBz3.Enabled = custom && on;
            numBz4.Enabled = custom && on;
            animPreview.Enabled = on;
        }

        void OnAnimModeChanged() {
            if (loadGuard) return;
            ApplyAnimPreset(cboAnimMode.SelectedIndex);
            UpdateAnimEnabledState();
            PlayAnimPreview();
        }

        void PlayAnimPreview() {
            if (animTimer != null) { animTimer.Stop(); animTimer.Dispose(); animTimer = null; }
            animProgress = 0.0;
            animPreview.Invalidate();
            if (cboAnimMode.SelectedIndex == 0 && !chkAnim.Checked) { animProgress = 1.0; animPreview.Invalidate(); return; }
            animTimer = new Timer();
            animTimer.Interval = 15;
            long started = Environment.TickCount;
            int dur = (int)numAnimDur.Value;
            if (dur <= 0) { animProgress = 1.0; animPreview.Invalidate(); animTimer.Dispose(); animTimer = null; return; }
            animTimer.Tick += delegate (object s, EventArgs e) {
                double t = (double)(Environment.TickCount - started) / (double)dur;
                if (t >= 1.0) {
                    animProgress = 1.0;
                    animPreview.Invalidate();
                    animTimer.Stop();
                } else {
                    animProgress = t;
                    animPreview.Invalidate();
                }
            };
            animTimer.Start();
        }

        void AnimPreviewPaint(object sender, PaintEventArgs e) {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            Rectangle area = animPreview.ClientRectangle;
            g.Clear(Color.FromArgb(24, 24, 28));

            double x1 = (double)numBz1.Value, y1 = (double)numBz2.Value;
            double x2 = (double)numBz3.Value, y2 = (double)numBz4.Value;

            // 左：曲线图
            Rectangle plot = new Rectangle(14, 14, 180, 152);
            using (Pen grid = new Pen(Color.FromArgb(60, 60, 68))) {
                for (int i = 0; i <= 4; i++) {
                    int gx = plot.Left + plot.Width * i / 4;
                    int gy = plot.Top + plot.Height * i / 4;
                    g.DrawLine(grid, gx, plot.Top, gx, plot.Bottom);
                    g.DrawLine(grid, plot.Left, gy, plot.Right, gy);
                }
            }
            PointF[] pts = new PointF[61];
            for (int i = 0; i <= 60; i++) {
                double tx = i / 60.0;
                double ty = BzEase(tx, x1, y1, x2, y2);
                pts[i] = new PointF((float)(plot.Left + tx * plot.Width),
                                    (float)(plot.Bottom - ty * plot.Height));
            }
            using (Pen curve = new Pen(Color.FromArgb(120, 190, 255), 2f)) g.DrawLines(curve, pts);
            using (Brush cap = new SolidBrush(Color.FromArgb(120, 190, 255))) {
                double cy = BzEase(animProgress, x1, y1, x2, y2);
                g.FillEllipse(cap, (float)(plot.Left + animProgress * plot.Width) - 3,
                                   (float)(plot.Bottom - cy * plot.Height) - 3, 6, 6);
            }

            // 右：按参数画一张菜单卡片
            double ease = BzEase(animProgress, x1, y1, x2, y2);
            int a0 = (int)numAnimOpacity.Value;
            int s0 = (int)numAnimScale.Value;
            int dy0 = (int)numAnimOffset.Value;
            int alpha = (int)Math.Round(a0 + (100 - a0) * ease);
            double scale = (s0 + (100 - s0) * ease) / 100.0;
            int offY = (int)Math.Round(dy0 * (1 - ease));

            Rectangle card = new Rectangle(area.Width - 250, 20, 210, 140);
            int cw = (int)(card.Width * scale);
            int ch = (int)(card.Height * scale);
            Rectangle shown = new Rectangle(card.Left + (card.Width - cw) / 2,
                                            card.Top + (card.Height - ch) / 2 + offY, cw, ch);
            int shade = 230;
            using (SolidBrush b = new SolidBrush(Color.FromArgb(alpha * shade / 100, 246, 246, 248)))
                g.FillRectangle(b, shown);
            using (Pen edge = new Pen(Color.FromArgb(alpha * 60 / 100, 255, 255, 255)))
                g.DrawRectangle(edge, shown);
            int textAlpha = alpha;
            using (SolidBrush tb = new SolidBrush(Color.FromArgb(textAlpha, 20, 20, 24))) {
                int rows = 4;
                for (int i = 0; i < rows; i++) {
                    int ty = shown.Top + 16 + i * ((shown.Height - 24) / rows);
                    if (ty + 8 > shown.Bottom) break;
                    g.FillRectangle(tb, shown.Left + 14, ty, Math.Max(10, shown.Width - 60), 8);
                }
            }
        }

        TabPage BuildAnimationTab() {
            TabPage p = NewTab("动画");

            chkAnim = MkCheck("启用呼出动画（淡入 + 轻微放大）", 20, 18, 460);
            chkAnim.CheckedChanged += delegate (object s, EventArgs e) { if (loadGuard) return; UpdateAnimEnabledState(); PlayAnimPreview(); };
            p.Controls.Add(chkAnim);

            p.Controls.Add(MkLabel("预设", 20, 52, 40));
            cboAnimMode = new ComboBox();
            cboAnimMode.DropDownStyle = ComboBoxStyle.DropDownList;
            cboAnimMode.Items.AddRange(new object[] { "无", "快速", "平滑", "优雅", "自定义" });
            cboAnimMode.Bounds = new Rectangle(66, 50, 120, 24);
            cboAnimMode.SelectedIndexChanged += delegate (object s, EventArgs e) { OnAnimModeChanged(); };
            p.Controls.Add(cboAnimMode);
            p.Controls.Add(MkHint("快速 / 平滑 / 优雅 由快到慢，都会套用一组推荐参数并把下面的数值锁定；想自己调曲线就选「自定义」。", 200, 52, 580, 22));

            GroupBox g1 = MkGroup("时长与幅度", 20, 84, 380, 176);
            g1.Controls.Add(MkLabel("时长（毫秒）", 16, 26, 120));
            numAnimDur = MkNum(140, 26, 0, 1500, 220, 70);
            numAnimDur.ValueChanged += delegate (object s, EventArgs e) { if (!loadGuard) { animProgress = 1.0; animPreview.Invalidate(); } };
            g1.Controls.Add(numAnimDur);
            g1.Controls.Add(MkLabel("起始不透明度", 16, 60, 120));
            numAnimOpacity = MkNum(140, 60, 0, 100, 0, 70);
            numAnimOpacity.ValueChanged += delegate (object s, EventArgs e) { if (!loadGuard) { animProgress = 1.0; animPreview.Invalidate(); } };
            g1.Controls.Add(numAnimOpacity);
            g1.Controls.Add(MkLabel("起始缩放（%）", 16, 94, 120));
            numAnimScale = MkNum(140, 94, 60, 100, 96, 70);
            numAnimScale.ValueChanged += delegate (object s, EventArgs e) { if (!loadGuard) { animProgress = 1.0; animPreview.Invalidate(); } };
            g1.Controls.Add(numAnimScale);
            g1.Controls.Add(MkLabel("起始下移（像素）", 16, 128, 120));
            numAnimOffset = MkNum(140, 128, 0, 64, 8, 70);
            numAnimOffset.ValueChanged += delegate (object s, EventArgs e) { if (!loadGuard) { animProgress = 1.0; animPreview.Invalidate(); } };
            g1.Controls.Add(numAnimOffset);
            p.Controls.Add(g1);

            GroupBox g2 = MkGroup("曲线（三次贝塞尔：x 是时间，y 是进度）", 412, 84, 378, 176);
            g2.Controls.Add(MkLabel("x1", 16, 26, 30));
            numBz1 = MkNumDec(50, 26, 0, 1, 0.16m, 0.01m, 60);
            g2.Controls.Add(numBz1);
            g2.Controls.Add(MkLabel("y1", 126, 26, 30));
            numBz2 = MkNumDec(160, 26, 0, 1, 0.84m, 0.01m, 60);
            g2.Controls.Add(numBz2);
            g2.Controls.Add(MkLabel("x2", 16, 66, 30));
            numBz3 = MkNumDec(50, 66, 0, 1, 0.44m, 0.01m, 60);
            g2.Controls.Add(numBz3);
            g2.Controls.Add(MkLabel("y2", 126, 66, 30));
            numBz4 = MkNumDec(160, 66, 0, 1, 1.00m, 0.01m, 60);
            g2.Controls.Add(numBz4);
            g2.Controls.Add(MkHint("x 越大前段越慢，y 决定冲进度的快慢。平滑曲线常用 0.16/0.84/0.44/1。", 16, 108, 344, 44));
            p.Controls.Add(g2);

            GroupBox g3 = MkGroup("预览", 20, 268, 770, 254);
            animPreview = new Panel();
            animPreview.Bounds = new Rectangle(14, 24, 740, 180);
            animPreview.BackColor = Color.FromArgb(24, 24, 28);
            animPreview.BorderStyle = BorderStyle.FixedSingle;
            animPreview.Paint += AnimPreviewPaint;
            g3.Controls.Add(animPreview);
            g3.Controls.Add(MkBtn("试播", 14, 212, 90, delegate (object s, EventArgs e) { PlayAnimPreview(); }));
            g3.Controls.Add(MkHint("左边是曲线形状（圆点表示当前时刻），右边是按上面的参数画出来的菜单卡片。", 116, 216, 640, 22));
            p.Controls.Add(g3);

            return p;
        }

        TabPage BuildTextTab() {
            TabPage p = NewTab("文字");

            GroupBox g1 = MkGroup("文字颜色（按背景亮度逐像素取色）", 20, 12, 770, 232);
            chkColors = MkCheck("启用智能文字颜色", 16, 26, 300);
            // 取色粒度：逐像素 / 每个字 / 整个菜单项
            g1.Controls.Add(MkLabel("取色粒度", 16, 56, 76));
            cboColorUnit = new ComboBox();
            cboColorUnit.DropDownStyle = ComboBoxStyle.DropDownList;
            cboColorUnit.Bounds = new Rectangle(100, 54, 210, 24);
            cboColorUnit.Items.AddRange(new object[] {
                "逐像素（同一个字可以多种颜色）", "按字取色（每个字一种颜色）", "整项同色（一行只用一个颜色）"
            });
            g1.Controls.Add(cboColorUnit);
            g1.Controls.Add(MkHint("按字取色时一行里不同字可以是不同颜色；整项同色最统一。", 320, 56, 440, 22));

            g1.Controls.Add(MkLabel("浅背景用", 16, 86, 80));
            swDark = new ColorSwatch();
            swDark.Bounds = new Rectangle(100, 86, 52, 24);
            g1.Controls.Add(swDark);
            g1.Controls.Add(MkLabel("深背景用", 180, 86, 80));
            swLight = new ColorSwatch();
            swLight.Bounds = new Rectangle(264, 86, 52, 24);
            g1.Controls.Add(swLight);

            g1.Controls.Add(MkLabel("亮度分界", 360, 86, 76));
            numThreshold = MkNum(440, 86, 0, 255, 140, 70);
            g1.Controls.Add(numThreshold);

            g1.Controls.Add(MkLabel("过渡宽度", 520, 86, 76));
            numSoftness = MkNum(600, 86, 0, 120, 6, 60);
            g1.Controls.Add(numSoftness);

            g1.Controls.Add(MkLabel("备用浅色", 16, 122, 80));
            swExtraLight = new ColorSwatch();
            swExtraLight.Bounds = new Rectangle(100, 122, 52, 24);
            g1.Controls.Add(swExtraLight);
            g1.Controls.Add(MkLabel("备用深色", 180, 122, 80));
            swExtraDark = new ColorSwatch();
            swExtraDark.Bounds = new Rectangle(264, 122, 52, 24);
            g1.Controls.Add(swExtraDark);

            g1.Controls.Add(MkLabel("描边宽度", 360, 122, 76));
            numHaloW = MkNum(440, 122, 0, 3, 1, 60);
            g1.Controls.Add(numHaloW);
            g1.Controls.Add(MkLabel("描边强度", 510, 122, 76));
            numHaloA = MkNum(590, 122, 0, 100, 40, 60);
            g1.Controls.Add(numHaloA);

            g1.Controls.Add(MkHint("亮度分界 0-255：背景亮于此值用「深背景用」色，备用色留空则不用。", 16, 154, 730, 20));
            g1.Controls.Add(MkHint("过渡宽度默认 2（0 = 硬切换），颜色实际横跨约两倍像素，填得太小会自动按最小值处理；描边给文字加一圈反色光晕。", 16, 176, 730, 20));
            g1.Controls.Add(MkHint("取消勾选「智能文字颜色」后所有文字固定用「浅背景用」那一格的颜色，不再参考背景。", 16, 198, 730, 20));

            g1.Controls.Add(chkColors);

            GroupBox g2 = MkGroup("字体与尺寸", 20, 252, 770, 300);
            g2.Controls.Add(MkLabel("字体", 16, 28, 60));
            cboFont = new ComboBox();
            cboFont.DropDownStyle = ComboBoxStyle.DropDownList;
            cboFont.Bounds = new Rectangle(80, 28, 240, 24);
            g2.Controls.Add(cboFont);

            g2.Controls.Add(MkLabel("字体文件", 16, 62, 60));
            txtFontFile = MkText(80, 62, 560, "");
            g2.Controls.Add(txtFontFile);
            g2.Controls.Add(MkBtn("浏览", 650, 60, 70, delegate (object s, EventArgs e) {
                using (OpenFileDialog d = new OpenFileDialog()) {
                    d.Filter = "字体文件|*.ttf;*.otf;*.ttc|所有文件|*.*";
                    if (d.ShowDialog(this) == DialogResult.OK) txtFontFile.Text = d.FileName;
                }
            }));
            g2.Controls.Add(MkHint("可选。指定后优先使用该字体文件，留空则使用上面的系统字体。", 80, 88, 640, 22));

            g2.Controls.Add(MkLabel("字号", 16, 118, 60));
            numFontSize = MkNum(80, 118, 6, 40, 9, 70);
            g2.Controls.Add(numFontSize);
            g2.Controls.Add(MkLabel("行高", 180, 118, 60));
            numItemHeight = MkNum(240, 118, 18, 80, 32, 70);
            g2.Controls.Add(numItemHeight);
            g2.Controls.Add(MkLabel("图标", 340, 118, 60));
            numIconSize = MkNum(400, 118, 8, 48, 16, 70);
            g2.Controls.Add(numIconSize);

            g2.Controls.Add(MkLabel("最小宽", 16, 154, 60));
            numMinW = MkNum(80, 154, 120, 800, 180, 70);
            g2.Controls.Add(numMinW);
            g2.Controls.Add(MkLabel("最大宽", 180, 154, 60));
            numMaxW = MkNum(240, 154, 160, 1600, 520, 70);
            g2.Controls.Add(numMaxW);
            g2.Controls.Add(MkLabel("最大高", 340, 154, 60));
            numMaxH = MkNum(400, 154, 200, 2000, 720, 70);
            g2.Controls.Add(numMaxH);
            g2.Controls.Add(MkLabel("顶栏高", 520, 154, 70));
            numHeaderH = MkNum(596, 154, 18, 120, 32, 70);
            numHeaderH.Enabled = true;
            g2.Controls.Add(numHeaderH);

            chkShowShortcut = MkCheck("显示快捷键提示", 16, 190, 200);
            chkShowCheckmarks = MkCheck("显示勾选标记", 220, 190, 180);
            g2.Controls.Add(chkShowShortcut);
            g2.Controls.Add(chkShowCheckmarks);

            g2.Controls.Add(MkLabel("子菜单延迟", 400, 190, 80));
            numHoverDelay = MkNum(480, 190, 0, 2000, 320, 70);
            g2.Controls.Add(numHoverDelay);
            g2.Controls.Add(MkHint("毫秒，鼠标悬停多久后展开子菜单", 560, 192, 200, 22));

            previewPanel = new Panel();
            previewPanel.Bounds = new Rectangle(16, 226, 738, 58);
            previewPanel.BorderStyle = BorderStyle.FixedSingle;
            previewPanel.Paint += new PaintEventHandler(PreviewPaint);
            g2.Controls.Add(previewPanel);

            EventHandler refresh = delegate { previewPanel.Invalidate(); };
            chkColors.CheckedChanged += refresh;
            cboColorUnit.SelectedIndexChanged += refresh;
            numThreshold.ValueChanged += refresh;
            numSoftness.ValueChanged += refresh;
            numHaloW.ValueChanged += refresh;
            numHaloA.ValueChanged += refresh;
            numFontSize.ValueChanged += refresh;
            cboFont.SelectedIndexChanged += refresh;
            swDark.HexChanged += refresh;
            swLight.HexChanged += refresh;

            p.Controls.Add(g1);
            p.Controls.Add(g2);
            return p;
        }

        // 预览用真实的规则重画：背景左浅右深，分界处按“过渡宽度”铺一条渐变带，
        // 再把文字蒙版逐像素按背景亮度取黑/白（并在过渡带里做插值），
        // 这样调数字时能直接看到实际效果，而不是一张示意图。
        void PreviewPaint(object sender, PaintEventArgs e) {
            Graphics g = e.Graphics;
            Rectangle r = previewPanel.ClientRectangle;
            int w = r.Width;
            int h = r.Height;
            if (w < 8 || h < 8) return;

            int split = w / 2;
            bool smart = chkColors.Checked;         // 关闭后整串一个固定色，完全不看背景
            int unit = cboColorUnit.SelectedIndex < 0 ? 0 : cboColorUnit.SelectedIndex; // 0 像素 1 字 2 项
            int soft = (int)numSoftness.Value;
            // DLL 里局部平均窗口的半径取 soft，颜色实际横跨约 2*soft 像素，这里保持一致。
            int band = (smart && unit == 0 && soft > 0) ? Math.Max(4, soft * 2) : 0;
            int threshold = (int)numThreshold.Value;
            const int lumMax = 245, lumMin = 28;   // 与下面画出来的背景两端一致
            Color darkC = ColorSwatch.FromHex(swDark.Hex, Color.Black);
            Color lightC = ColorSwatch.FromHex(swLight.Hex, Color.White);

            using (Bitmap canvas = new Bitmap(w, h, PixelFormat.Format32bppArgb)) {
                using (Graphics cg = Graphics.FromImage(canvas)) {
                    using (SolidBrush light = new SolidBrush(Color.FromArgb(245, 245, 247)))
                        cg.FillRectangle(light, 0, 0, split, h);
                    using (SolidBrush dark = new SolidBrush(Color.FromArgb(28, 28, 30)))
                        cg.FillRectangle(dark, split, 0, w - split, h);
                    if (band > 0) {
                        int bx = Math.Max(0, split - band / 2);
                        int bw = Math.Min(w, split + band / 2) - bx;
                        if (bw > 0) {
                            using (LinearGradientBrush lg = new LinearGradientBrush(
                                       new Rectangle(bx, 0, bw, h),
                                       Color.FromArgb(245, 245, 247), Color.FromArgb(28, 28, 30),
                                       LinearGradientMode.Horizontal))
                                cg.FillRectangle(lg, bx, 0, bw, h);
                        }
                    }
                }

                string sample = "复制  剪切  粘贴  删除";
                Rectangle rr = new Rectangle(0, 0, w, h);
                int[] chEdge = new int[sample.Length + 1];   // 每个字符的右边界，供“按字取色”预览用
                int[] chLum = new int[sample.Length];
                using (Bitmap mask = new Bitmap(w, h, PixelFormat.Format32bppArgb)) {
                    using (Graphics mg = Graphics.FromImage(mask)) {
                        mg.Clear(Color.Transparent);
                        mg.TextRenderingHint = TextRenderingHint.AntiAliasGridFit;
                        using (Font f = new Font(cboFont.SelectedItem as string != null ? (string)cboFont.SelectedItem : "Microsoft YaHei UI", (float)numFontSize.Value))
                        using (StringFormat sf = new StringFormat()) {
                            sf.LineAlignment = StringAlignment.Center;
                            using (SolidBrush b = new SolidBrush(Color.White))
                                mg.DrawString(sample, f, b, new RectangleF(6, 0, w - 12, h), sf);
                            chEdge[0] = 6;
                            for (int ci = 0; ci < sample.Length; ++ci)
                                chEdge[ci + 1] = 6 + (int)Math.Round(mg.MeasureString(sample.Substring(0, ci + 1), f).Width);
                        }
                    }

                    BitmapData cd = canvas.LockBits(rr, ImageLockMode.ReadWrite, PixelFormat.Format32bppArgb);
                    BitmapData md = mask.LockBits(rr, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
                    try {
                        int[] cb = new int[w * h];
                        int[] mb = new int[w * h];
                        Marshal.Copy(cd.Scan0, cb, 0, cb.Length);
                        Marshal.Copy(md.Scan0, mb, 0, mb.Length);
                        if (unit == 1) {
                            // 每个字符所在列块的平均背景亮度：同一像素属于哪个字，就按那个字的亮度取色
                            for (int ci = 0; ci < sample.Length; ++ci) {
                                int x0 = Math.Max(0, chEdge[ci]), x1 = Math.Min(w, chEdge[ci + 1]);
                                long sum = 0;
                                int n = 0;
                                for (int y2 = 0; y2 < h; y2 += 2)
                                    for (int x2 = x0; x2 < x1; x2 += 2) {
                                        int c0 = cb[y2 * w + x2];
                                        int r0 = c0 & 0xFF, g0 = (c0 >> 8) & 0xFF, b0 = (c0 >> 16) & 0xFF;
                                        sum += (r0 * 299 + g0 * 587 + b0 * 114) / 1000;
                                        ++n;
                                    }
                                chLum[ci] = n > 0 ? (int)(sum / n) : -1;
                            }
                        }
                        // 先铺描边光晕：只在文字蒙版为 0 的像素上，取邻域最大覆盖度，
                        // 用与文字相反的一侧颜色（暗底加亮边、亮底加暗边）混一层，提升可读性。
                        int hw = (int)numHaloW.Value;
                        int ha = (int)numHaloA.Value;
                        if (!smart) ha = 0;   // 固定色模式下没有“反色”可言，描边只会加粗
                        if (hw > 0 && ha > 0) {
                            for (int y = 0; y < h; ++y) {
                                for (int x = 0; x < w; ++x) {
                                    int i = y * w + x;
                                    if (((mb[i] >> 24) & 0xFF) != 0) continue;
                                    int best = 0;
                                    int y0 = Math.Max(0, y - hw), y1 = Math.Min(h - 1, y + hw);
                                    int x0 = Math.Max(0, x - hw), x1 = Math.Min(w - 1, x + hw);
                                    for (int yy = y0; yy <= y1 && best < 255; ++yy) {
                                        int row = yy * w;
                                        for (int xx = x0; xx <= x1; ++xx) {
                                            int a2 = (mb[row + xx] >> 24) & 0xFF;
                                            if (a2 > best) best = a2;
                                        }
                                    }
                                    if (best <= 0) continue;
                                    int c = cb[i];
                                    int br = c & 0xFF, bg2 = (c >> 8) & 0xFF, bb = (c >> 16) & 0xFF;
                                    int lum = (br * 299 + bg2 * 587 + bb * 114) / 1000;
                                    bool useDark = lum >= threshold;
                                    int hr = useDark ? darkC.R : lightC.R;
                                    int hg = useDark ? darkC.G : lightC.G;
                                    int hb = useDark ? darkC.B : lightC.B;
                                    int aa2 = best * ha / 100;
                                    int nr = (br * (255 - aa2) + hr * aa2) / 255;
                                    int ng = (bg2 * (255 - aa2) + hg * aa2) / 255;
                                    int nb = (bb * (255 - aa2) + hb * aa2) / 255;
                                    cb[i] = (c & unchecked((int)0xFF000000)) | (nb << 16) | (ng << 8) | nr;
                                }
                            }
                        }

                        for (int i = 0; i < cb.Length; ++i) {
                            int a = (mb[i] >> 24) & 0xFF;
                            if (a == 0) continue;
                            int c = cb[i];
                            int br = c & 0xFF, bg2 = (c >> 8) & 0xFF, bb = (c >> 16) & 0xFF;
                            int lum = (br * 299 + bg2 * 587 + bb * 114) / 1000;
                            int nr, ng, nb;
                            if (!smart) {
                                // 关闭智能文字颜色：整串固定用“浅背景用”那一格的颜色
                                nr = darkC.R; ng = darkC.G; nb = darkC.B;
                            } else {
                                double t;
                                if (unit != 0) {
                                    // 按字 / 整项：先算这个单元的平均亮度，再整体做一次硬切换
                                    int eff = lum;
                                    if (unit == 2) {
                                        eff = (lumMax + lumMin) / 2;
                                    } else {
                                        int px0 = i % w;
                                        int ci = sample.Length - 1;
                                        for (int k = 0; k < sample.Length; ++k)
                                            if (px0 >= chEdge[k] && px0 < chEdge[k + 1]) { ci = k; break; }
                                        if (chLum[ci] >= 0) eff = chLum[ci];
                                    }
                                    t = eff > threshold ? 1.0 : 0.0;
                                } else if (soft <= 0) {
                                    t = lum > threshold ? 1.0 : 0.0;
                                } else {
                                    int spanUp = lumMax - threshold, spanDown = threshold - lumMin;
                                    if (spanUp <= 0 || spanDown <= 0) {
                                        t = lum > threshold ? 1.0 : 0.0;
                                    } else {
                                        // 两侧分别按实测亮度端点归一化：最亮处纯深色、最暗处纯浅色，
                                        // 中间按 smoothstep 平滑过渡（与 DLL 中的规则一致）。
                                        if (lum >= threshold) t = 0.5 + 0.5 * (lum - threshold) / (double)spanUp;
                                        else t = 0.5 - 0.5 * (threshold - lum) / (double)spanDown;
                                        if (t < 0) t = 0; else if (t > 1) t = 1;
                                        t = t * t * (3 - 2 * t);
                                    }
                                }
                                nr = (int)(lightC.R + (darkC.R - lightC.R) * t);
                                ng = (int)(lightC.G + (darkC.G - lightC.G) * t);
                                nb = (int)(lightC.B + (darkC.B - lightC.B) * t);
                            }
                            int or1 = (br * (255 - a) + nr * a) / 255;
                            int og1 = (bg2 * (255 - a) + ng * a) / 255;
                            int ob1 = (bb * (255 - a) + nb * a) / 255;
                            cb[i] = (a << 24) | (ob1 << 16) | (og1 << 8) | or1;
                        }
                        Marshal.Copy(cb, 0, cd.Scan0, cb.Length);
                    } finally {
                        canvas.UnlockBits(cd);
                        mask.UnlockBits(md);
                    }
                }
                g.DrawImage(canvas, 0, 0);
            }
        }

        TabPage BuildTopBarTab() {
            TabPage p = NewTab("顶栏");

            p.Controls.Add(MkHint("顶栏按钮在菜单最上方，横向排列。关键字用于匹配菜单项（例如 copy 匹配“复制”），图标填 4 位十六进制字符编码（如 E8C8）或直接填一个字符。", 20, 12, 760, 44));

            grid = new DataGridView();
            grid.Bounds = new Rectangle(20, 60, 620, 300);
            grid.AllowUserToAddRows = true;
            grid.AllowUserToDeleteRows = true;
            grid.RowHeadersWidth = 26;
            grid.BackgroundColor = SystemColors.Window;
            grid.AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.None;

            DataGridViewCheckBoxColumn colEnabled = new DataGridViewCheckBoxColumn();
            colEnabled.HeaderText = "启用";
            colEnabled.Width = 50;
            grid.Columns.Add(colEnabled);

            DataGridViewTextBoxColumn colLabel = new DataGridViewTextBoxColumn();
            colLabel.HeaderText = "名称(提示)";
            colLabel.Width = 120;
            grid.Columns.Add(colLabel);

            DataGridViewTextBoxColumn colCmd = new DataGridViewTextBoxColumn();
            colCmd.HeaderText = "关键字";
            colCmd.Width = 140;
            grid.Columns.Add(colCmd);

            DataGridViewTextBoxColumn colIcon = new DataGridViewTextBoxColumn();
            colIcon.HeaderText = "图标编码";
            colIcon.Width = 120;
            grid.Columns.Add(colIcon);
            p.Controls.Add(grid);

            int bx = 656;
            p.Controls.Add(MkBtn("上移", bx, 60, 100, delegate (object s, EventArgs e) { MoveRow(-1); }));
            p.Controls.Add(MkBtn("下移", bx, 96, 100, delegate (object s, EventArgs e) { MoveRow(1); }));
            p.Controls.Add(MkBtn("删除选中", bx, 132, 100, delegate (object s, EventArgs e) { DeleteRow(); }));

            cboPreset = new ComboBox();
            cboPreset.DropDownStyle = ComboBoxStyle.DropDownList;
            cboPreset.Bounds = new Rectangle(bx, 180, 100, 24);
            string[] presets = new string[] { "复制", "剪切", "粘贴", "重命名", "删除", "属性", "刷新", "新建文件夹", "打开", "设置" };
            cboPreset.Items.AddRange(presets);
            cboPreset.SelectedIndex = 0;
            p.Controls.Add(cboPreset);
            p.Controls.Add(MkBtn("添加到顶栏", bx, 210, 100, delegate (object s, EventArgs e) { AddPreset(); }));

            p.Controls.Add(MkHint("支持的关键字：copy 剪切 cut 粘贴 paste 重命名 rename 删除 delete 属性 props 刷新 refresh 新建文件夹 new 打开 open 设置 rmgcfg 更多选项 more", 20, 372, 760, 44));
            return p;
        }

        static readonly string[,] PresetTable = new string[,] {
            { "复制", "copy", "E8C8" },
            { "剪切", "cut", "E8C6" },
            { "粘贴", "paste", "E77F" },
            { "重命名", "rename", "E8AC" },
            { "删除", "delete", "E74D" },
            { "属性", "props", "E713" },
            { "刷新", "refresh", "E72C" },
            { "新建文件夹", "new", "E8F4" },
            { "打开", "open", "E8E5" },
            { "设置", "rmgcfg", "E713" }
        };

        void AddPreset() {
            int i = cboPreset.SelectedIndex;
            if (i < 0) return;
            grid.Rows.Add(true, PresetTable[i, 0], PresetTable[i, 1], PresetTable[i, 2]);
        }

        void MoveRow(int delta) {
            if (grid.CurrentRow == null) return;
            int idx = grid.CurrentRow.Index;
            int target = idx + delta;
            if (target < 0 || target >= grid.Rows.Count) return;
            if (grid.Rows[target].IsNewRow) return;
            object[] a = RowValues(grid.Rows[idx]);
            object[] b = RowValues(grid.Rows[target]);
            SetRowValues(grid.Rows[idx], b);
            SetRowValues(grid.Rows[target], a);
            grid.CurrentCell = grid.Rows[target].Cells[1];
        }

        object[] RowValues(DataGridViewRow r) {
            object[] v = new object[4];
            for (int i = 0; i < 4; i++) v[i] = r.Cells[i].Value;
            return v;
        }

        void SetRowValues(DataGridViewRow r, object[] v) {
            for (int i = 0; i < 4; i++) r.Cells[i].Value = v[i];
        }

        void DeleteRow() {
            if (grid.CurrentRow == null) return;
            if (!grid.CurrentRow.IsNewRow) grid.Rows.Remove(grid.CurrentRow);
        }

        TabPage BuildThirdPartyTab() {
            TabPage p = NewTab("第三方菜单");

            chkThird = MkCheck("把第三方软件添加的菜单项收进二级菜单", 20, 18, 480);
            p.Controls.Add(chkThird);

            p.Controls.Add(MkLabel("二级菜单标题", 20, 54, 110));
            txtThirdTitle = MkText(136, 54, 200, "");
            p.Controls.Add(txtThirdTitle);
            chkSubmenu = MkCheck("启用收纳（关闭后第三方项保持在一级菜单）", 360, 54, 420);
            p.Controls.Add(chkSubmenu);
            chkSubmenuIcon = MkCheck("标题右侧显示箭头图标", 360, 84, 300);
            p.Controls.Add(chkSubmenuIcon);

            GroupBox g1 = MkGroup("提升到一级菜单（按软件分类）", 16, 104, 764, 334);
            p.Controls.Add(g1);
            g1.Controls.Add(MkHint("在资源管理器里右键几次，让软件记录下各软件实际添加的菜单项，再点「刷新列表」。勾选软件名＝该软件的全部菜单项提到一级；只勾子项＝只提这一项。", 14, 20, 520, 56));

            treeProviders = new TreeView();
            treeProviders.CheckBoxes = true;
            treeProviders.HideSelection = false;
            treeProviders.Bounds = new Rectangle(14, 80, 520, 190);
            treeProviders.AfterCheck += TreeAfterCheck;
            g1.Controls.Add(treeProviders);

            g1.Controls.Add(MkBtn("刷新列表", 548, 80, 190, delegate (object s, EventArgs e) { LoadObservedIntoTree(); }));
            g1.Controls.Add(MkBtn("打开记录文件", 548, 116, 190, delegate (object s, EventArgs e) {
                string obs = Path.Combine(cfgDir, "observed.json");
                if (File.Exists(obs)) Process.Start("notepad.exe", "\"" + obs + "\"");
                else MessageBox.Show("还没有记录文件，先在资源管理器里右键几次。", "RightMenuGlass");
            }));
            g1.Controls.Add(MkBtn("清空记录", 548, 152, 190, delegate (object s, EventArgs e) { ClearObserved(); }));

            chkMerge = MkCheck("勾选多个时合并为一个一级菜单", 14, 278, 300);
            g1.Controls.Add(chkMerge);
            g1.Controls.Add(MkLabel("名称", 330, 278, 40));
            txtMergeTitle = MkText(376, 278, 160, "常用工具");
            g1.Controls.Add(txtMergeTitle);
            g1.Controls.Add(MkHint("不合并时，每个被提升的菜单项各自占一级菜单的一行。", 14, 302, 740, 20));

            GroupBox g2 = MkGroup("完全隐藏", 16, 446, 764, 100);
            p.Controls.Add(g2);
            g2.Controls.Add(MkLabel("菜单项文字包含这些关键词时直接隐藏（每行一个）", 14, 20, 500));
            txtHide = new TextBox();
            txtHide.Multiline = true;
            txtHide.ScrollBars = ScrollBars.Vertical;
            txtHide.Bounds = new Rectangle(14, 42, 736, 36);
            g2.Controls.Add(txtHide);
            g2.Controls.Add(MkHint("例如填入“上传到”“发送到”可隐藏对应项。", 14, 82, 736, 18));

            return p;
        }

        static bool IsSubset(HashSet<string> sub, HashSet<string> super) {
            foreach (string s in sub) if (!super.Contains(s)) return false;
            return true;
        }

        // 父节点勾选 = 整组提升；子节点勾选 = 单项提升
        void TreeAfterCheck(object sender, TreeViewEventArgs e) {
            if (treeGuard) return;
            treeGuard = true;
            try {
                TreeNode n = e.Node;
                if (n == null) return;
                string tag = n.Tag as string;
                if (tag != null && tag.StartsWith("G:")) {
                    foreach (TreeNode c in n.Nodes) c.Checked = n.Checked;
                }
                else if (n.Parent != null) {
                    bool all = n.Parent.Nodes.Count > 0;
                    foreach (TreeNode c in n.Parent.Nodes) { if (!c.Checked) { all = false; break; } }
                    n.Parent.Checked = all;
                }
            }
            finally {
                treeGuard = false;
            }
        }

        // 从 observed.json 读实际出现过的第三方菜单项，套上配置里已勾选的状态
        void LoadObservedIntoTree() {
            treeGuard = true;
            try {
                treeProviders.BeginUpdate();
                treeProviders.Nodes.Clear();

                HashSet<string> allGroups = new HashSet<string>();
                Dictionary<string, HashSet<string>> picked = new Dictionary<string, HashSet<string>>();
                JVal tp = Json.Obj(root, "thirdParty");
                JVal pg = Json.Get(tp, "promoteGroups");
                if (pg != null && pg.T == JVal.TArr) {
                    foreach (JVal g in pg.A) {
                        string id = Json.GetStr(g, "id", "");
                        if (id.Length > 0) allGroups.Add(id);
                    }
                }
                JVal pi = Json.Get(tp, "promoteItems");
                if (pi != null && pi.T == JVal.TArr) {
                    foreach (JVal it in pi.A) {
                        string id = Json.GetStr(it, "id", "");
                        string norm = Json.GetStr(it, "item", "");
                        if (id.Length == 0 || norm.Length == 0) continue;
                        HashSet<string> set;
                        if (!picked.TryGetValue(id, out set)) { set = new HashSet<string>(); picked[id] = set; }
                        set.Add(norm);
                    }
                }

                JVal list = null;
                string obs = Path.Combine(cfgDir, "observed.json");
                if (File.Exists(obs)) {
                    try { list = Json.Get(Json.Parse(File.ReadAllText(obs)), "providers"); }
                    catch { list = null; }
                }

                int shown = 0;
                if (list != null && list.T == JVal.TArr) {
                    foreach (JVal pv in list.A) {
                        string id = Json.GetStr(pv, "id", "");
                        if (id.Length == 0) continue;
                        string name = Json.GetStr(pv, "name", "");
                        if (name.Length == 0) name = "未命名软件";

                        TreeNode node = new TreeNode();
                        node.Tag = "G:" + id;
                        HashSet<string> norms = new HashSet<string>();
                        JVal its = Json.Get(pv, "items");
                        if (its != null && its.T == JVal.TArr) {
                            foreach (JVal iv in its.A) {
                                string text = Json.GetStr(iv, "text", "");
                                string norm = Json.GetStr(iv, "norm", "");
                                if (norm.Length == 0) continue;
                                TreeNode c = new TreeNode(text.Length > 0 ? text : norm);
                                c.Tag = "I:" + id + "\u001f" + norm;
                                node.Nodes.Add(c);
                                norms.Add(norm);
                            }
                        }
                        node.Text = name + "（" + node.Nodes.Count + " 项）";

                        bool hasPick = picked.ContainsKey(id);
                        bool selAll = allGroups.Contains(id)
                            || (hasPick && norms.Count > 0 && IsSubset(picked[id], norms));
                        node.Checked = selAll;
                        foreach (TreeNode c in node.Nodes) {
                            string t = (string)c.Tag;
                            int k = t.IndexOf('\u001f');
                            string nm = k >= 0 ? t.Substring(k + 1) : "";
                            c.Checked = selAll || (hasPick && picked[id].Contains(nm));
                        }
                        treeProviders.Nodes.Add(node);
                        shown++;
                    }
                }
                if (shown == 0) {
                    TreeNode hint = new TreeNode("（还没有记录：先在资源管理器里对各类文件、空白处各右键一次，再点「刷新列表」）");
                    hint.ForeColor = Color.Gray;
                    treeProviders.Nodes.Add(hint);
                }
                treeProviders.ExpandAll();
            }
            finally {
                treeProviders.EndUpdate();
                treeGuard = false;
            }
        }

        void ClearObserved() {
            string obs = Path.Combine(cfgDir, "observed.json");
            if (!File.Exists(obs)) { MessageBox.Show("还没有记录文件。", "RightMenuGlass"); return; }
            if (MessageBox.Show("清空已记录的软件列表？已勾选的提升设置会保留在配置里。", "RightMenuGlass",
                    MessageBoxButtons.OKCancel) != DialogResult.OK) return;
            try { File.Delete(obs); }
            catch { }
            LoadObservedIntoTree();
        }

        // ---------------- load / collect ----------------
        void LoadIntoUi() {
            chkEnabled.Checked = Json.GetBool(root, "enabled", true);
            chkLogVerbose.Checked = Json.GetBool(root, "logVerbose", false);

            JVal ac = Json.Obj(root, "acrylic");
            chkAcrylic.Checked = Json.GetBool(ac, "enabled", true);
            numBlur.Value = Clamp(Json.GetInt(ac, "blurRadius", 18), numBlur.Minimum, numBlur.Maximum);
            numOpacity.Value = Clamp(Json.GetInt(ac, "opacity", 58), numOpacity.Minimum, numOpacity.Maximum);
            swTint.Hex = Json.GetStr(ac, "tint", "#202020");
            swLightTint.Hex = Json.GetStr(ac, "lightTint", "#F3F3F5");
            chkAutoTint.Checked = Json.GetBool(ac, "autoTint", true);
            numNoise.Value = Clamp(Json.GetInt(ac, "noise", 5), numNoise.Minimum, numNoise.Maximum);
            numCorner.Value = Clamp(Json.GetInt(ac, "corner", 10), numCorner.Minimum, numCorner.Maximum);
            numShadow.Value = Clamp(Json.GetInt(ac, "shadow", 14), numShadow.Minimum, numShadow.Maximum);
            numMargin.Value = Clamp(Json.GetInt(ac, "margin", 10), numMargin.Minimum, numMargin.Maximum);

            JVal co = Json.Obj(root, "colors");
            chkColors.Checked = Json.GetBool(co, "enabled", true);
            string selUnit = Json.GetStr(co, "colorUnit", Json.GetBool(co, "perPixel", true) ? "pixel" : "item");
            cboColorUnit.SelectedIndex = UnitToIndex(selUnit);
            swDark.Hex = Json.GetStr(co, "darkText", "#000000");
            swLight.Hex = Json.GetStr(co, "lightText", "#FFFFFF");
            numThreshold.Value = Clamp(Json.GetInt(co, "threshold", 140), numThreshold.Minimum, numThreshold.Maximum);
            numSoftness.Value = Clamp(Json.GetInt(co, "softness", 2), numSoftness.Minimum, numSoftness.Maximum);
            numHaloW.Value = Clamp(Json.GetInt(co, "haloWidth", 1), numHaloW.Minimum, numHaloW.Maximum);
            numHaloA.Value = Clamp(Json.GetInt(co, "haloAlpha", 40), numHaloA.Minimum, numHaloA.Maximum);
            swExtraDark.Hex = Json.GetStr(co, "extraDark", "#000000");
            swExtraLight.Hex = Json.GetStr(co, "extraLight", "#FFFFFF");

            JVal an = Json.Obj(root, "animation");
            loadGuard = true;
            chkAnim.Checked = Json.GetBool(an, "enabled", true);
            cboAnimMode.SelectedIndex = ModeToIndex(Json.GetStr(an, "mode", "elegant"));
            numAnimDur.Value = Clamp(Json.GetInt(an, "durationMs", 420), numAnimDur.Minimum, numAnimDur.Maximum);
            numAnimOpacity.Value = Clamp(Json.GetInt(an, "fromOpacity", 0), numAnimOpacity.Minimum, numAnimOpacity.Maximum);
            numAnimScale.Value = Clamp(Json.GetInt(an, "fromScale", 93), numAnimScale.Minimum, numAnimScale.Maximum);
            numAnimOffset.Value = Clamp(Json.GetInt(an, "fromOffsetY", 12), numAnimOffset.Minimum, numAnimOffset.Maximum);
            double[] cvs = new double[] { 0.22, 1.0, 0.36, 1.0 };
            JVal cu = Json.Get(an, "curve");
            if (cu != null && cu.T == JVal.TArr && cu.A.Count >= 4) {
                for (int i = 0; i < 4; i++) cvs[i] = cu.A[i].N;
            }
            numBz1.Value = ClampDouble(cvs[0], numBz1.Minimum, numBz1.Maximum);
            numBz2.Value = ClampDouble(cvs[1], numBz2.Minimum, numBz2.Maximum);
            numBz3.Value = ClampDouble(cvs[2], numBz3.Minimum, numBz3.Maximum);
            numBz4.Value = ClampDouble(cvs[3], numBz4.Minimum, numBz4.Maximum);
            loadGuard = false;
            UpdateAnimEnabledState();

            JVal st = Json.Obj(root, "style");
            string family = Json.GetStr(st, "fontFamily", "Microsoft YaHei UI");
            cboFont.Items.Clear();
            try {
                using (InstalledFontCollection col = new InstalledFontCollection()) {
                    for (int i = 0; i < col.Families.Length; i++) cboFont.Items.Add(col.Families[i].Name);
                }
            } catch { }
            if (!cboFont.Items.Contains(family)) cboFont.Items.Add(family);
            cboFont.SelectedItem = family;
            txtFontFile.Text = Json.GetStr(st, "fontFile", "");
            numFontSize.Value = Clamp(Json.GetInt(st, "fontSize", 9), numFontSize.Minimum, numFontSize.Maximum);
            numItemHeight.Value = Clamp(Json.GetInt(st, "itemHeight", 32), numItemHeight.Minimum, numItemHeight.Maximum);
            numIconSize.Value = Clamp(Json.GetInt(st, "iconSize", 16), numIconSize.Minimum, numIconSize.Maximum);
            numHeaderH.Value = Clamp(Json.GetInt(st, "headerHeight", 32), numHeaderH.Minimum, numHeaderH.Maximum);
            numMinW.Value = Clamp(Json.GetInt(st, "minWidth", 180), numMinW.Minimum, numMinW.Maximum);
            numMaxW.Value = Clamp(Json.GetInt(st, "maxWidth", 520), numMaxW.Minimum, numMaxW.Maximum);
            numMaxH.Value = Clamp(Json.GetInt(st, "maxHeight", 720), numMaxH.Minimum, numMaxH.Maximum);
            chkShowShortcut.Checked = Json.GetBool(st, "showShortcut", true);
            chkShowCheckmarks.Checked = Json.GetBool(st, "showCheckmarks", true);
            numHoverDelay.Value = Clamp(Json.GetInt(st, "hoverDelayMs", 320), numHoverDelay.Minimum, numHoverDelay.Maximum);
            cboOpenDir.SelectedIndex = DirToIndex(Json.GetStr(st, "openDirection", "right-down"));

            JVal tp = Json.Obj(root, "thirdParty");
            chkThird.Checked = Json.GetBool(tp, "enabled", true);
            txtThirdTitle.Text = Json.GetStr(tp, "title", "更多选项");
            chkSubmenu.Checked = Json.GetBool(tp, "submenu", true);
            chkSubmenuIcon.Checked = Json.GetBool(tp, "submenuTitleIcon", true);
            chkMerge.Checked = Json.GetBool(tp, "mergePromoted", false);
            txtMergeTitle.Text = Json.GetStr(tp, "mergedTitle", "常用工具");
            txtHide.Text = string.Join("\r\n", Json.GetStrList(tp, "hide").ToArray());
            LoadObservedIntoTree();

            grid.Rows.Clear();
            JVal btns = Json.Get(root, "buttons");
            if (btns != null && btns.T == JVal.TArr) {
                for (int i = 0; i < btns.A.Count; i++) {
                    JVal b = btns.A[i];
                    if (b.T != JVal.TObj) continue;
                    grid.Rows.Add(Json.GetBool(b, "enabled", true), Json.GetStr(b, "label", ""), Json.GetStr(b, "cmd", ""), IconToDisplay(Json.GetStr(b, "icon", "")));
                }
            }
            LoadAutostartIntoUi();
        }

        // Private-use glyphs are unreadable in a grid, so show their hex code instead.
        static string IconToDisplay(string icon) {
            if (icon != null && icon.Length == 1 && icon[0] >= 0xE000 && icon[0] <= 0xF8FF) {
                return ((int)icon[0]).ToString("X4");
            }
            return icon == null ? "" : icon;
        }

        static string IconFromDisplay(string s) {
            if (s == null) return "";
            s = s.Trim();
            if (s.Length == 4) {
                int code;
                if (int.TryParse(s, System.Globalization.NumberStyles.HexNumber, System.Globalization.CultureInfo.InvariantCulture, out code)) {
                    if (code >= 0xE000 && code <= 0xF8FF) return ((char)code).ToString();
                }
            }
            return s;
        }

        static decimal Clamp(int v, decimal min, decimal max) {
            if (v < min) return min;
            if (v > max) return max;
            return v;
        }

        static decimal ClampDouble(double v, decimal min, decimal max) {
            decimal d = (decimal)v;
            if (d < min) return min;
            if (d > max) return max;
            return d;
        }

        static int DirToIndex(string d) {
            if (d == "right-up") return 1;
            if (d == "left-down") return 2;
            if (d == "left-up") return 3;
            if (d == "auto") return 4;
            return 0;
        }

        static string IndexToDir(int i) {
            switch (i) {
                case 1: return "right-up";
                case 2: return "left-down";
                case 3: return "left-up";
                case 4: return "auto";
            }
            return "right-down";
        }

        void CollectFromUi() {
            Json.Set(root, "enabled", JVal.NewBool(chkEnabled.Checked));
            Json.Set(root, "logVerbose", JVal.NewBool(chkLogVerbose.Checked));

            JVal ac = Json.Obj(root, "acrylic");
            Json.Set(ac, "enabled", JVal.NewBool(chkAcrylic.Checked));
            Json.Set(ac, "blurRadius", JVal.NewInt((int)numBlur.Value));
            Json.Set(ac, "opacity", JVal.NewInt((int)numOpacity.Value));
            Json.Set(ac, "tint", JVal.NewStr(swTint.Hex));
            Json.Set(ac, "lightTint", JVal.NewStr(swLightTint.Hex));
            Json.Set(ac, "autoTint", JVal.NewBool(chkAutoTint.Checked));
            Json.Set(ac, "noise", JVal.NewInt((int)numNoise.Value));
            Json.Set(ac, "corner", JVal.NewInt((int)numCorner.Value));
            Json.Set(ac, "shadow", JVal.NewInt((int)numShadow.Value));
            Json.Set(ac, "margin", JVal.NewInt((int)numMargin.Value));

            JVal co = Json.Obj(root, "colors");
            Json.Set(co, "enabled", JVal.NewBool(chkColors.Checked));
            Json.Set(co, "colorUnit", JVal.NewStr(IndexToUnit(cboColorUnit.SelectedIndex < 0 ? 0 : cboColorUnit.SelectedIndex)));
            // 旧版本只认 perPixel：只在“整项同色”时为 false，保证旧版本读到接近的语义
            Json.Set(co, "perPixel", JVal.NewBool(cboColorUnit.SelectedIndex <= 0));
            Json.Set(co, "darkText", JVal.NewStr(swDark.Hex));
            Json.Set(co, "lightText", JVal.NewStr(swLight.Hex));
            Json.Set(co, "threshold", JVal.NewInt((int)numThreshold.Value));
            Json.Set(co, "softness", JVal.NewInt((int)numSoftness.Value));
            Json.Set(co, "haloWidth", JVal.NewInt((int)numHaloW.Value));
            Json.Set(co, "haloAlpha", JVal.NewInt((int)numHaloA.Value));
            Json.Set(co, "extraDark", JVal.NewStr(swExtraDark.Hex == "#000000" ? "" : swExtraDark.Hex));
            Json.Set(co, "extraLight", JVal.NewStr(swExtraLight.Hex == "#FFFFFF" ? "" : swExtraLight.Hex));

            JVal an = Json.Obj(root, "animation");
            Json.Set(an, "enabled", JVal.NewBool(chkAnim.Checked));
            Json.Set(an, "mode", JVal.NewStr(IndexToMode(cboAnimMode.SelectedIndex)));
            Json.Set(an, "durationMs", JVal.NewInt((int)numAnimDur.Value));
            Json.Set(an, "fromOpacity", JVal.NewInt((int)numAnimOpacity.Value));
            Json.Set(an, "fromScale", JVal.NewInt((int)numAnimScale.Value));
            Json.Set(an, "fromOffsetY", JVal.NewInt((int)numAnimOffset.Value));
            JVal curve = JVal.NewArr();
            curve.A.Add(JVal.NewNum((double)numBz1.Value));
            curve.A.Add(JVal.NewNum((double)numBz2.Value));
            curve.A.Add(JVal.NewNum((double)numBz3.Value));
            curve.A.Add(JVal.NewNum((double)numBz4.Value));
            Json.Set(an, "curve", curve);

            JVal st = Json.Obj(root, "style");
            string fam = cboFont.SelectedItem as string;
            Json.Set(st, "fontFamily", JVal.NewStr(fam == null ? "Microsoft YaHei UI" : fam));
            Json.Set(st, "fontFile", JVal.NewStr(txtFontFile.Text.Trim()));
            Json.Set(st, "fontSize", JVal.NewInt((int)numFontSize.Value));
            Json.Set(st, "itemHeight", JVal.NewInt((int)numItemHeight.Value));
            Json.Set(st, "iconSize", JVal.NewInt((int)numIconSize.Value));
            Json.Set(st, "headerHeight", JVal.NewInt((int)numHeaderH.Value));
            Json.Set(st, "minWidth", JVal.NewInt((int)numMinW.Value));
            Json.Set(st, "maxWidth", JVal.NewInt((int)numMaxW.Value));
            Json.Set(st, "maxHeight", JVal.NewInt((int)numMaxH.Value));
            Json.Set(st, "showShortcut", JVal.NewBool(chkShowShortcut.Checked));
            Json.Set(st, "showCheckmarks", JVal.NewBool(chkShowCheckmarks.Checked));
            Json.Set(st, "hoverDelayMs", JVal.NewInt((int)numHoverDelay.Value));
            Json.Set(st, "openDirection", JVal.NewStr(IndexToDir(cboOpenDir.SelectedIndex)));

            JVal tp = Json.Obj(root, "thirdParty");
            Json.Set(tp, "enabled", JVal.NewBool(chkThird.Checked));
            Json.Set(tp, "title", JVal.NewStr(txtThirdTitle.Text));
            Json.Set(tp, "submenu", JVal.NewBool(chkSubmenu.Checked));
            Json.Set(tp, "submenuTitleIcon", JVal.NewBool(chkSubmenuIcon.Checked));
            Json.Set(tp, "hide", Json.StrList(SplitLines(txtHide.Text)));

            JVal groups = JVal.NewArr();
            JVal items = JVal.NewArr();
            foreach (TreeNode node in treeProviders.Nodes) {
                if (!(node.Tag is string)) continue;
                string tag = (string)node.Tag;
                if (!tag.StartsWith("G:")) continue;
                string id = tag.Substring(2);
                string name = node.Text;
                int cut = name.LastIndexOf("（");
                if (cut > 0) name = name.Substring(0, cut);

                if (node.Checked) {
                    JVal g = JVal.NewObj();
                    Json.Set(g, "id", JVal.NewStr(id));
                    Json.Set(g, "name", JVal.NewStr(name));
                    Json.Set(g, "all", JVal.NewBool(true));
                    groups.A.Add(g);
                }
                else {
                    foreach (TreeNode c in node.Nodes) {
                        if (!c.Checked) continue;
                        string ct = c.Tag as string;
                        if (ct == null) continue;
                        int k = ct.IndexOf('\u001f');
                        if (k < 0) continue;
                        JVal iv = JVal.NewObj();
                        Json.Set(iv, "id", JVal.NewStr(id));
                        Json.Set(iv, "item", JVal.NewStr(ct.Substring(k + 1)));
                        items.A.Add(iv);
                    }
                }
            }
            Json.Set(tp, "promoteGroups", groups);
            Json.Set(tp, "promoteItems", items);
            Json.Set(tp, "mergePromoted", JVal.NewBool(chkMerge.Checked));
            string mt = txtMergeTitle.Text.Trim();
            Json.Set(tp, "mergedTitle", JVal.NewStr(mt.Length > 0 ? mt : "常用工具"));

            JVal arr = JVal.NewArr();
            for (int i = 0; i < grid.Rows.Count; i++) {
                DataGridViewRow r = grid.Rows[i];
                if (r.IsNewRow) continue;
                string cmd = Convert.ToString(r.Cells[2].Value);
                if (cmd == null || cmd.Trim().Length == 0) continue;
                JVal b = JVal.NewObj();
                object en = r.Cells[0].Value;
                Json.Set(b, "cmd", JVal.NewStr(cmd.Trim()));
                Json.Set(b, "label", JVal.NewStr(Convert.ToString(r.Cells[1].Value)));
                Json.Set(b, "icon", JVal.NewStr(IconFromDisplay(Convert.ToString(r.Cells[3].Value))));
                Json.Set(b, "enabled", JVal.NewBool(en != null && en is bool ? (bool)en : true));
                arr.A.Add(b);
            }
            Json.Set(root, "buttons", arr);
        }

        static List<string> SplitLines(string text) {
            List<string> list = new List<string>();
            if (text == null) return list;
            string[] lines = text.Replace("\r\n", "\n").Split('\n');
            for (int i = 0; i < lines.Length; i++) {
                string s = lines[i].Trim();
                if (s.Length > 0) list.Add(s);
            }
            return list;
        }

        // ---------------- actions ----------------
        void OpenDir(string dir) {
            if (!Directory.Exists(dir)) {
                try { Directory.CreateDirectory(dir); } catch { }
            }
            try { Process.Start("explorer.exe", "\"" + dir + "\""); } catch { }
        }

        void InstallSamplePlugins() {
            string src = Path.Combine(Path.Combine(appDir, "plugins-example"), "");
            string dst = Path.Combine(cfgDir, "plugins");
            if (!Directory.Exists(src.Substring(0, src.Length - 1))) {
                MessageBox.Show("找不到示例插件目录：\n" + src, "RightMenuGlass");
                return;
            }
            if (!Directory.Exists(dst)) Directory.CreateDirectory(dst);
            string[] files = Directory.GetFiles(src.Substring(0, src.Length - 1), "*.json");
            for (int i = 0; i < files.Length; i++) {
                File.Copy(files[i], Path.Combine(dst, Path.GetFileName(files[i])), true);
            }
            MessageBox.Show("已安装 " + files.Length.ToString() + " 个示例插件到：\n" + dst, "RightMenuGlass");
        }

        // Renders every tab to a PNG (used for automated visual checks).
        public void SelfShot(string dir) {
            Directory.CreateDirectory(dir);
            if (!IsHandleCreated) CreateControl();
            for (int i = 0; i < tabs.TabPages.Count; i++) {
                tabs.SelectedIndex = i;
                tabs.TabPages[i].PerformLayout();
                Application.DoEvents();
                System.Threading.Thread.Sleep(150);
                using (Bitmap bmp = new Bitmap(Width, Height)) {
                    DrawToBitmap(bmp, new Rectangle(0, 0, Width, Height));
                    bmp.Save(Path.Combine(dir, "cfg_tab" + i.ToString() + ".png"), System.Drawing.Imaging.ImageFormat.Png);
                }
            }
        }

        // ---------------- autostart ----------------
        const string RunKeyPath = @"Software\Microsoft\Windows\CurrentVersion\Run";

        string ReadRunValue() {
            try {
                using (RegistryKey k = Registry.CurrentUser.OpenSubKey(RunKeyPath, false)) {
                    if (k == null) return "";
                    object v = k.GetValue("RightMenuGlass");
                    return v == null ? "" : v.ToString();
                }
            } catch {
                return "";
            }
        }

        void LoadAutostartIntoUi() {
            if (chkAutostart == null) return;
            string val = ReadRunValue();
            chkAutostart.Checked = val.Length > 0;
            chkAutostartWatch.Checked = val.Length > 0 && val.IndexOf("watch", StringComparison.OrdinalIgnoreCase) >= 0;
            UpdateAutostartEnabled();
        }

        void UpdateAutostartEnabled() {
            if (chkAutostartWatch == null) return;
            chkAutostartWatch.Enabled = chkAutostart.Checked;
            if (!chkAutostart.Checked) chkAutostartWatch.Checked = false;
        }

        void StartWatchSilently() {
            string exe = Path.Combine(appDir, "rmgctl.exe");
            if (!File.Exists(exe)) return;
            try {
                ProcessStartInfo si = new ProcessStartInfo(exe, "watch --silent");
                si.UseShellExecute = false;
                si.CreateNoWindow = true;
                Process p = Process.Start(si);
                if (p != null) p.Dispose();
            } catch {
            }
        }

        // 只有和注册表里的现状不一致时才写，避免每次「保存并应用」都重写自启动项。
        void ApplyAutostartFromUi() {
            if (chkAutostart == null) return;
            string cur = ReadRunValue();
            bool curOn = cur.Length > 0;
            bool curWatch = curOn && cur.IndexOf("watch", StringComparison.OrdinalIgnoreCase) >= 0;
            bool wantOn = chkAutostart.Checked;
            bool wantWatch = chkAutostart.Checked && chkAutostartWatch.Checked;
            if (!wantOn) {
                if (curOn) RunCtl("uninstall", false);
                if (curWatch) RunCtl("stop", false);
                return;
            }
            if (!curOn || curWatch != wantWatch) {
                RunCtl(wantWatch ? "install --watch" : "install", false);
                if (wantWatch) StartWatchSilently();
                else RunCtl("stop", false);
            }
        }

        void RunCtl(string args, bool afterMsg) {
            string exe = Path.Combine(appDir, "rmgctl.exe");
            if (!File.Exists(exe)) {
                MessageBox.Show("找不到 rmgctl.exe：\n" + exe, "RightMenuGlass");
                return;
            }
            try {
                ProcessStartInfo si = new ProcessStartInfo();
                si.FileName = exe;
                si.Arguments = args;
                si.UseShellExecute = false;
                si.CreateNoWindow = true;
                si.RedirectStandardOutput = true;
                si.RedirectStandardError = true;
                using (Process p = Process.Start(si)) {
                    string outp = p.StandardOutput.ReadToEnd();
                    string err = p.StandardError.ReadToEnd();
                    p.WaitForExit(20000);
                    statusLabel.Text = ("[" + args + "] " + outp + err).Replace("\r\n", " ").Trim();
                    if (afterMsg && !args.Equals("status")) {
                        MessageBox.Show("完成：\n" + outp + err, "RightMenuGlass");
                    }
                }
            } catch (Exception ex) {
                MessageBox.Show("调用 rmgctl 失败：\n" + ex.Message, "RightMenuGlass");
            }
        }
    }

    static class Program {
        [STAThread]
        static void Main() {
            try { SetProcessDpiAware(); } catch { }
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            string[] argv = Environment.GetCommandLineArgs();
            for (int i = 1; i < argv.Length - 1; i++) {
                if (argv[i] == "--shot") {
                    MainForm f = new MainForm();
                    f.SelfShot(argv[i + 1]);
                    return;
                }
            }
            Application.Run(new MainForm());
        }

        [DllImport("user32.dll")]
        static extern bool SetProcessDpiAware();
    }
}
