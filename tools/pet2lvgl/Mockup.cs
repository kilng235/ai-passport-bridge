// tools/pet2lvgl/Mockup.cs —— render a layout mockup of the 通知/桌宠 panels.
// Build: csc /nologo /optimize+ /out:layoutmock.exe /r:System.Drawing.dll Mockup.cs
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;

internal static class Mockup
{
    static readonly Color BG     = C(0x071017);
    static readonly Color PANEL  = C(0x0D1B23);
    static readonly Color GRID   = C(0x24404A);
    static readonly Color TEXT   = C(0xF4F0DE);
    static readonly Color MUTED  = C(0x849BA0);
    static readonly Color AMBER  = C(0xFFB74D);
    static readonly Color CYAN   = Color.FromArgb(0x22, 0xD3, 0xEE);
    static readonly Color GREEN  = C(0x4ED39A);

    static Color C(int v) { return Color.FromArgb((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF); }

    static readonly Font F14 = new Font("Microsoft YaHei", 12f, FontStyle.Regular, GraphicsUnit.Pixel);
    static readonly Font F20 = new Font("Microsoft YaHei", 17f, FontStyle.Regular, GraphicsUnit.Pixel);

    static void Divider(Graphics g, int x, int y, int w)
    {
        using (var p = new Pen(GRID, 1f)) g.DrawLine(p, x, y, x + w, y);
    }

    static void Center(Graphics g, string s, Font f, Color col, int cx, int y)
    {
        var sz = g.MeasureString(s, f);
        using (var b = new SolidBrush(col)) g.DrawString(s, f, b, cx - sz.Width / 2f, y);
    }

    static void Right(Graphics g, string s, Font f, Color col, int rightX, int y)
    {
        var sz = g.MeasureString(s, f);
        using (var b = new SolidBrush(col)) g.DrawString(s, f, b, rightX - sz.Width, y);
    }

    static void RoundRect(Graphics g, int x, int y, int w, int h, int r, Color fill)
    {
        using (var path = new GraphicsPath())
        {
            path.AddArc(x, y, r, r, 180, 90);
            path.AddArc(x + w - r, y, r, r, 270, 90);
            path.AddArc(x + w - r, y + h - r, r, r, 0, 90);
            path.AddArc(x, y + h - r, r, r, 90, 90);
            path.CloseFigure();
            using (var b = new SolidBrush(fill)) g.FillPath(b, path);
        }
    }

    static void Header(Graphics g, string title)
    {
        using (var b = new SolidBrush(AMBER)) g.DrawString(title, F20, b, 12, 8);
        Right(g, "09/12 21:08", F14, MUTED, 228, 12);   // 时钟:页头右侧
        Divider(g, 12, 40, 216);
    }

    static Bitmap PanelNotifySingle()
    {
        var bmp = new Bitmap(240, 320);
        using (var g = Graphics.FromImage(bmp))
        {
            g.TextRenderingHint = System.Drawing.Text.TextRenderingHint.AntiAlias;
            using (var b = new SolidBrush(BG)) g.FillRectangle(b, 0, 0, 240, 320);
            Header(g, "通知");
            Center(g, "在线 ・ 等待推送", F14, MUTED, 120, 48);
            Center(g, "任务进行中", F20, CYAN, 120, 94);
            Center(g, "(单行模式:状态 + 详情)", F14, MUTED, 120, 210);
            Divider(g, 12, 292, 216);
            Center(g, "上下切换 ・ 长按返回", F14, MUTED, 120, 298);
        }
        return bmp;
    }

    static Bitmap PanelNotifyList()
    {
        var bmp = new Bitmap(240, 320);
        using (var g = Graphics.FromImage(bmp))
        {
            g.TextRenderingHint = System.Drawing.Text.TextRenderingHint.AntiAlias;
            using (var b = new SolidBrush(BG)) g.FillRectangle(b, 0, 0, 240, 320);
            Header(g, "通知");
            Center(g, "在线 ・ 等待推送", F14, MUTED, 120, 48);
            Center(g, "任务进行中 ・ 2 个会话", F14, CYAN, 120, 74);

            string[] names = { "FoloToy-AI-Passport-full…", "Hello Kitty风格角色多表情图" };
            Color[] dots = { CYAN, AMBER };
            for (int i = 0; i < 2; i++)
            {
                int y = 100 + i * 40;
                RoundRect(g, 12, y, 216, 34, 8, PANEL);
                using (var b = new SolidBrush(dots[i])) g.FillEllipse(b, 22, y + 12, 10, 10);
                using (var b = new SolidBrush(TEXT)) g.DrawString(names[i], F14, b, 42, y + 8);
            }
            Divider(g, 12, 292, 216);
            Center(g, "上下切换 ・ 长按返回", F14, MUTED, 120, 298);
        }
        return bmp;
    }

    static void KeyGreen(Bitmap bmp)
    {
        var rect = new Rectangle(0, 0, bmp.Width, bmp.Height);
        var bits = bmp.LockBits(rect, ImageLockMode.ReadWrite, PixelFormat.Format32bppArgb);
        int stride = bits.Stride;
        byte[] px = new byte[stride * bmp.Height];
        Marshal.Copy(bits.Scan0, px, 0, px.Length);
        for (int y = 0; y < bmp.Height; y++)
            for (int x = 0; x < bmp.Width; x++)
            {
                int o = y * stride + x * 4;
                int b = px[o], gg = px[o + 1], r = px[o + 2];
                int maxRB = Math.Max(r, b);
                int gr = gg - maxRB;
                int a = gr <= 12 ? 255 : (gr >= 60 ? 0 : 255 * (60 - gr) / 48);
                if (gg > maxRB) gg = maxRB;
                px[o] = (byte)b; px[o + 1] = (byte)gg; px[o + 2] = (byte)r; px[o + 3] = (byte)a;
            }
        Marshal.Copy(px, 0, bits.Scan0, px.Length);
        bmp.UnlockBits(bits);
    }

    static Bitmap PetFrame(string sheetPath)
    {
        using (var sheet = new Bitmap(sheetPath))
        {
            int cw = sheet.Width / 4, ch = sheet.Height / 4;
            var big = new Bitmap(96, 96, PixelFormat.Format32bppArgb);
            using (var g = Graphics.FromImage(big))
            {
                g.InterpolationMode = InterpolationMode.HighQualityBicubic;
                g.DrawImage(sheet, new Rectangle(0, 0, 96, 96), new Rectangle(0, 0, cw, ch), GraphicsUnit.Pixel);
            }
            KeyGreen(big);
            return big;
        }
    }

    static Bitmap PanelPet(string sheetPath)
    {
        var bmp = new Bitmap(240, 320);
        using (var g = Graphics.FromImage(bmp))
        {
            g.TextRenderingHint = System.Drawing.Text.TextRenderingHint.AntiAlias;
            using (var b = new SolidBrush(BG)) g.FillRectangle(b, 0, 0, 240, 320);
            Header(g, "桌宠");

            using (var pet = PetFrame(sheetPath))
                g.DrawImage(pet, new Rectangle(72, 84, 96, 96));
            // 96x96 占位框(示意动画区域)
            using (var p = new Pen(Color.FromArgb(60, GRID), 1f)) g.DrawRectangle(p, 72, 84, 95, 95);

            Center(g, "空闲", F20, MUTED, 120, 194);
            Divider(g, 12, 292, 216);
            Center(g, "上下切换 ・ 长按返回", F14, MUTED, 120, 298);
        }
        return bmp;
    }

    static int Main(string[] args)
    {
        string sheet = args.Length > 0 ? args[0] : "desk-pet-spritesheet.png";
        string outPath = args.Length > 1 ? args[1] : "layout-mockup.png";

        var panels = new[] { PanelNotifySingle(), PanelNotifyList(), PanelPet(sheet) };
        var captions = new[] { "① 通知 · 单行", "② 通知 · 会话列表", "③ 桌宠 · 动画" };

        int scale = 2, gap = 16, margin = 16, capH = 26;
        int pw = 240 * scale, ph = 320 * scale;
        int W = margin * 2 + panels.Length * pw + (panels.Length - 1) * gap;
        int H = margin + capH + ph + margin;

        var canvas = new Bitmap(W, H, PixelFormat.Format32bppArgb);
        using (var g = Graphics.FromImage(canvas))
        {
            g.Clear(Color.FromArgb(0x0A, 0x0A, 0x0A));
            g.InterpolationMode = InterpolationMode.NearestNeighbor;
            g.PixelOffsetMode = PixelOffsetMode.Half;
            using (var capf = new Font("Microsoft YaHei", 20f, FontStyle.Bold, GraphicsUnit.Pixel))
            {
                for (int i = 0; i < panels.Length; i++)
                {
                    int x = margin + i * (pw + gap);
                    using (var b = new SolidBrush(AMBER)) g.DrawString(captions[i], capf, b, x, margin - 2);
                    g.DrawImage(panels[i], new Rectangle(x, margin + capH, pw, ph));
                    using (var p = new Pen(GRID, 1f)) g.DrawRectangle(p, x, margin + capH, pw - 1, ph - 1);
                    panels[i].Dispose();
                }
            }
        }
        canvas.Save(outPath, ImageFormat.Png);
        Console.WriteLine("wrote " + outPath + " (" + W + "x" + H + ")");
        return 0;
    }
}
