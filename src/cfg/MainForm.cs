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

        // 亚克力
        CheckBox chkAcrylic, chkAutoTint;
        NumericUpDown numBlur, numOpacity, numNoise, numCorner, numShadow, numMargin;
        ColorSwatch swTint, swLightTint;

        // 文字
        CheckBox chkColors, chkPerPixel, chkShowShortcut, chkShowCheckmarks;
        ColorSwatch swDark, swLight, swExtraDark, swExtraLight;
        NumericUpDown numThreshold, numSoftness, numHaloW, numHaloA, numFontSize, numItemHeight, numIconSize, numMinW, numMaxW, numMaxH, numHoverDelay;
        ComboBox cboFont;
        TextBox txtFontFile;
        Panel previewPanel;

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

        void SaveConfig() {
            CollectFromUi();
            try {
                if (!Directory.Exists(cfgDir)) Directory.CreateDirectory(cfgDir);
                File.WriteAllText(cfgPath, Json.Stringify(root, true) + "\n", new UTF8Encoding(false));
                statusLabel.Text = "已保存：" + cfgPath;
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
            string shownCfg = cfgPath;
            string localApp = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
            if (localApp.Length > 0 && shownCfg.StartsWith(localApp, StringComparison.OrdinalIgnoreCase)) {
                shownCfg = "%LOCALAPPDATA%" + shownCfg.Substring(localApp.Length);
            }
            statusLabel.Text = "配置：" + shownCfg;
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

            GroupBox g1 = MkGroup("文件", 20, 112, 770, 96);
            g1.Controls.Add(MkBtn("打开配置目录", 16, 26, 130, delegate (object s, EventArgs e) { OpenDir(cfgDir); }));
            g1.Controls.Add(MkBtn("打开日志文件", 156, 26, 130, delegate (object s, EventArgs e) {
                string log = Path.Combine(cfgDir, "dll.log");
                if (File.Exists(log)) Process.Start("notepad.exe", "\"" + log + "\"");
                else MessageBox.Show("还没有日志文件，先用右键菜单弹出一次。", "RightMenuGlass");
            }));
            g1.Controls.Add(MkBtn("打开程序目录", 296, 26, 130, delegate (object s, EventArgs e) { OpenDir(appDir); }));
            p.Controls.Add(g1);

            GroupBox g2 = MkGroup("插件", 20, 220, 770, 96);
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

            GroupBox g3 = MkGroup("应用到资源管理器", 20, 328, 770, 130);
            g3.Controls.Add(MkBtn("仅重新注入 DLL", 16, 26, 140, delegate (object s, EventArgs e) { RunCtl("inject", true); }));
            g3.Controls.Add(MkBtn("重启资源管理器并注入", 166, 26, 170, delegate (object s, EventArgs e) {
                if (MessageBox.Show("会关闭所有文件夹窗口（不会丢失已保存的文件），桌面会闪烁一下。继续？", "RightMenuGlass", MessageBoxButtons.OKCancel, MessageBoxIcon.Question) == DialogResult.OK)
                    RunCtl("restart", true);
            }));
            g3.Controls.Add(MkBtn("查看注入状态", 346, 26, 120, delegate (object s, EventArgs e) { RunCtl("status", false); }));
            g3.Controls.Add(MkHint("第一次使用、或升级了 DLL 之后，需要点「重启资源管理器并注入」。日常修改配置不需要。", 16, 66, 730, 44));
            p.Controls.Add(g3);

            return p;
        }

        TabPage BuildAppearanceTab() {
            TabPage p = NewTab("外观");

            GroupBox g = MkGroup("亚克力 / 毛玻璃", 20, 16, 770, 420);

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

            g.Controls.Add(MkHint("所有尺寸都会按当前显示器的 DPI 自动缩放。", 16, 360, 730, 22));

            p.Controls.Add(g);
            return p;
        }

        TabPage BuildTextTab() {
            TabPage p = NewTab("文字");

            GroupBox g1 = MkGroup("文字颜色（按背景亮度逐像素取色）", 20, 12, 770, 206);
            chkColors = MkCheck("启用智能文字颜色", 16, 26, 300);
            chkPerPixel = MkCheck("逐像素判断（同一个字的笔画可以是不同颜色）", 16, 52, 460);

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
            g1.Controls.Add(MkHint("过渡宽度默认 6 像素（0 = 硬切换）；描边给文字加一圈反色光晕，背景越花越有用。", 16, 176, 730, 20));

            g1.Controls.Add(chkColors);
            g1.Controls.Add(chkPerPixel);

            GroupBox g2 = MkGroup("字体与尺寸", 20, 226, 770, 300);
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
            int soft = (int)numSoftness.Value;
            // 过渡带总宽度就是 soft 像素（DLL 里平均窗口半径取一半，正好铺满这条带）。
            int band = soft > 0 ? Math.Max(2, soft) : 0;
            int threshold = (int)numThreshold.Value;
            const int lumMax = 245, lumMin = 28;   // 与下面画出来的背景两端一致

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
                using (Bitmap mask = new Bitmap(w, h, PixelFormat.Format32bppArgb)) {
                    using (Graphics mg = Graphics.FromImage(mask)) {
                        mg.Clear(Color.Transparent);
                        mg.TextRenderingHint = TextRenderingHint.AntiAliasGridFit;
                        using (Font f = new Font(cboFont.SelectedItem as string != null ? (string)cboFont.SelectedItem : "Microsoft YaHei UI", (float)numFontSize.Value))
                        using (StringFormat sf = new StringFormat()) {
                            sf.LineAlignment = StringAlignment.Center;
                            using (SolidBrush b = new SolidBrush(Color.White))
                                mg.DrawString(sample, f, b, new RectangleF(6, 0, w - 12, h), sf);
                        }
                    }

                    BitmapData cd = canvas.LockBits(rr, ImageLockMode.ReadWrite, PixelFormat.Format32bppArgb);
                    BitmapData md = mask.LockBits(rr, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
                    try {
                        int[] cb = new int[w * h];
                        int[] mb = new int[w * h];
                        Marshal.Copy(cd.Scan0, cb, 0, cb.Length);
                        Marshal.Copy(md.Scan0, mb, 0, mb.Length);
                        // 先铺描边光晕：只在文字蒙版为 0 的像素上，取邻域最大覆盖度，
                        // 用与文字相反的一侧颜色（暗底加亮边、亮底加暗边）混一层，提升可读性。
                        int hw = (int)numHaloW.Value;
                        int ha = (int)numHaloA.Value;
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
                                    int halo = lum < threshold ? 255 : 0;
                                    int aa2 = best * ha / 100;
                                    int nr = (br * (255 - aa2) + halo * aa2) / 255;
                                    int ng = (bg2 * (255 - aa2) + halo * aa2) / 255;
                                    int nb = (bb * (255 - aa2) + halo * aa2) / 255;
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
                            double t;
                            int spanUp = lumMax - threshold, spanDown = threshold - lumMin;
                            if (soft <= 0 || spanUp <= 0 || spanDown <= 0) {
                                t = lum > threshold ? 1.0 : 0.0;
                            } else {
                                // 两侧分别按实测亮度端点归一化：最亮处纯黑、最暗处纯白，
                                // 中间按 smoothstep 平滑过渡（与 DLL 中的规则一致）。
                                if (lum >= threshold) t = 0.5 + 0.5 * (lum - threshold) / (double)spanUp;
                                else t = 0.5 - 0.5 * (threshold - lum) / (double)spanDown;
                                if (t < 0) t = 0; else if (t > 1) t = 1;
                                t = t * t * (3 - 2 * t);
                            }
                            int fg = (int)(255 * (1.0 - t));     // 亮背景 -> 近黑，暗背景 -> 近白
                            int nr = (br * (255 - a) + fg * a) / 255;
                            int ng = (bg2 * (255 - a) + fg * a) / 255;
                            int nb = (bb * (255 - a) + fg * a) / 255;
                            cb[i] = (a << 24) | (nb << 16) | (ng << 8) | nr;
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
            chkPerPixel.Checked = Json.GetBool(co, "perPixel", true);
            swDark.Hex = Json.GetStr(co, "darkText", "#000000");
            swLight.Hex = Json.GetStr(co, "lightText", "#FFFFFF");
            numThreshold.Value = Clamp(Json.GetInt(co, "threshold", 140), numThreshold.Minimum, numThreshold.Maximum);
            numSoftness.Value = Clamp(Json.GetInt(co, "softness", 6), numSoftness.Minimum, numSoftness.Maximum);
            numHaloW.Value = Clamp(Json.GetInt(co, "haloWidth", 1), numHaloW.Minimum, numHaloW.Maximum);
            numHaloA.Value = Clamp(Json.GetInt(co, "haloAlpha", 40), numHaloA.Minimum, numHaloA.Maximum);
            swExtraDark.Hex = Json.GetStr(co, "extraDark", "#000000");
            swExtraLight.Hex = Json.GetStr(co, "extraLight", "#FFFFFF");

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
            numMinW.Value = Clamp(Json.GetInt(st, "minWidth", 180), numMinW.Minimum, numMinW.Maximum);
            numMaxW.Value = Clamp(Json.GetInt(st, "maxWidth", 520), numMaxW.Minimum, numMaxW.Maximum);
            numMaxH.Value = Clamp(Json.GetInt(st, "maxHeight", 720), numMaxH.Minimum, numMaxH.Maximum);
            chkShowShortcut.Checked = Json.GetBool(st, "showShortcut", true);
            chkShowCheckmarks.Checked = Json.GetBool(st, "showCheckmarks", true);
            numHoverDelay.Value = Clamp(Json.GetInt(st, "hoverDelayMs", 320), numHoverDelay.Minimum, numHoverDelay.Maximum);

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
            Json.Set(co, "perPixel", JVal.NewBool(chkPerPixel.Checked));
            Json.Set(co, "darkText", JVal.NewStr(swDark.Hex));
            Json.Set(co, "lightText", JVal.NewStr(swLight.Hex));
            Json.Set(co, "threshold", JVal.NewInt((int)numThreshold.Value));
            Json.Set(co, "softness", JVal.NewInt((int)numSoftness.Value));
            Json.Set(co, "haloWidth", JVal.NewInt((int)numHaloW.Value));
            Json.Set(co, "haloAlpha", JVal.NewInt((int)numHaloA.Value));
            Json.Set(co, "extraDark", JVal.NewStr(swExtraDark.Hex == "#000000" ? "" : swExtraDark.Hex));
            Json.Set(co, "extraLight", JVal.NewStr(swExtraLight.Hex == "#FFFFFF" ? "" : swExtraLight.Hex));

            JVal st = Json.Obj(root, "style");
            string fam = cboFont.SelectedItem as string;
            Json.Set(st, "fontFamily", JVal.NewStr(fam == null ? "Microsoft YaHei UI" : fam));
            Json.Set(st, "fontFile", JVal.NewStr(txtFontFile.Text.Trim()));
            Json.Set(st, "fontSize", JVal.NewInt((int)numFontSize.Value));
            Json.Set(st, "itemHeight", JVal.NewInt((int)numItemHeight.Value));
            Json.Set(st, "iconSize", JVal.NewInt((int)numIconSize.Value));
            Json.Set(st, "minWidth", JVal.NewInt((int)numMinW.Value));
            Json.Set(st, "maxWidth", JVal.NewInt((int)numMaxW.Value));
            Json.Set(st, "maxHeight", JVal.NewInt((int)numMaxH.Value));
            Json.Set(st, "showShortcut", JVal.NewBool(chkShowShortcut.Checked));
            Json.Set(st, "showCheckmarks", JVal.NewBool(chkShowCheckmarks.Checked));
            Json.Set(st, "hoverDelayMs", JVal.NewInt((int)numHoverDelay.Value));

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
