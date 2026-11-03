
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;

public class MacTile : Control {
    public Image Icon;
    public bool Hover;
    public Color Base = Color.FromArgb(30, 30, 33);
    public string Payload;

    public bool WasLongPress;
    private Timer hold;
    private Point downPt;
    private bool armed;
    private bool dragging;

    public event EventHandler LongPress;
    public event EventHandler DragMoveScreen;
    public event EventHandler DragRelease;

    public MacTile() {
        SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.UserPaint | ControlStyles.ResizeRedraw, true);
        Cursor = Cursors.Hand;
        hold = new Timer();
        hold.Interval = 420;
        hold.Tick += delegate {
            hold.Stop();
            if (armed && !dragging) {
                dragging = true;
                WasLongPress = true;
                Cursor = Cursors.SizeAll;
                if (LongPress != null) LongPress(this, EventArgs.Empty);
            }
        };
    }

    protected override void OnMouseDown(MouseEventArgs e) {
        base.OnMouseDown(e);
        if (e.Button != MouseButtons.Left) return;
        downPt = e.Location;
        armed = true;
        dragging = false;
        WasLongPress = false;
        hold.Start();
        Capture = true;
    }

    protected override void OnMouseMove(MouseEventArgs e) {
        base.OnMouseMove(e);
        if (!armed) return;
        if (!dragging) {
            if (Math.Abs(e.X - downPt.X) > 5 || Math.Abs(e.Y - downPt.Y) > 5) { hold.Stop(); armed = false; }
            return;
        }
        if (DragMoveScreen != null) DragMoveScreen(this, EventArgs.Empty);
    }

    protected override void OnMouseUp(MouseEventArgs e) {
        base.OnMouseUp(e);
        hold.Stop();
        Capture = false;
        if (dragging) {
            dragging = false;
            Cursor = Cursors.Hand;
            if (DragRelease != null) DragRelease(this, EventArgs.Empty);
        }
        armed = false;
    }

    protected override void OnMouseEnter(EventArgs e) { Hover = true; Invalidate(); base.OnMouseEnter(e); }
    protected override void OnMouseLeave(EventArgs e) { Hover = false; Invalidate(); base.OnMouseLeave(e); }

    protected override void OnPaint(PaintEventArgs e) {
        Graphics g = e.Graphics;
        g.SmoothingMode = SmoothingMode.AntiAlias;
        using (SolidBrush bg = new SolidBrush(Base)) g.FillRectangle(bg, ClientRectangle);
        if (Hover) {
            using (GraphicsPath p = Round(new Rectangle(3, 3, Width - 7, Height - 7), 12)) {
                using (SolidBrush b = new SolidBrush(Color.FromArgb(34, 255, 255, 255))) g.FillPath(b, p);
            }
        }
        int isz = 44;
        int top = 12;
        if (Icon != null) {
            g.InterpolationMode = InterpolationMode.HighQualityBicubic;
            g.DrawImage(Icon, (Width - isz) / 2, top, isz, isz);
        }
        Rectangle tr;
        TextFormatFlags tf = TextFormatFlags.HorizontalCenter | TextFormatFlags.WordBreak | TextFormatFlags.EndEllipsis | TextFormatFlags.NoPrefix;
        if (Icon != null) {
            tr = new Rectangle(4, top + 52, Width - 8, Height - top - 58);
        } else {
            tr = new Rectangle(4, 0, Width - 8, Height);
            tf |= TextFormatFlags.VerticalCenter;
        }
        TextRenderer.DrawText(g, Text, Font, tr, ForeColor, tf);
    }

    public static GraphicsPath Round(Rectangle r, int rad) {
        GraphicsPath p = new GraphicsPath();
        int d = rad * 2;
        if (d > r.Width) d = r.Width;
        if (d > r.Height) d = r.Height;
        p.AddArc(r.X, r.Y, d, d, 180, 90);
        p.AddArc(r.Right - d, r.Y, d, d, 270, 90);
        p.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90);
        p.AddArc(r.X, r.Bottom - d, d, d, 90, 90);
        p.CloseFigure();
        return p;
    }
}

public class MacRow : Control {
    public bool Hover;
    public bool Active;
    public bool DropTarget;
    public Color Base = Color.FromArgb(30, 30, 33);

    public MacRow() {
        SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.UserPaint | ControlStyles.ResizeRedraw, true);
        Cursor = Cursors.Hand;
    }
    protected override void OnMouseEnter(EventArgs e) { Hover = true; Invalidate(); base.OnMouseEnter(e); }
    protected override void OnMouseLeave(EventArgs e) { Hover = false; Invalidate(); base.OnMouseLeave(e); }

    protected override void OnPaint(PaintEventArgs e) {
        Graphics g = e.Graphics;
        g.SmoothingMode = SmoothingMode.AntiAlias;
        using (SolidBrush bg = new SolidBrush(Base)) g.FillRectangle(bg, ClientRectangle);
        Rectangle r = new Rectangle(8, 3, Width - 16, Height - 6);
        if (DropTarget) {
            using (GraphicsPath p = MacTile.Round(r, 9)) {
                using (SolidBrush b = new SolidBrush(Color.FromArgb(70, 10, 132, 255))) g.FillPath(b, p);
                using (Pen pen = new Pen(Color.FromArgb(255, 10, 132, 255), 3)) g.DrawPath(pen, p);
            }
        } else if (Active) {
            using (GraphicsPath p = MacTile.Round(r, 9)) {
                using (SolidBrush b = new SolidBrush(Color.FromArgb(230, 10, 132, 255))) g.FillPath(b, p);
            }
        } else if (Hover) {
            using (GraphicsPath p = MacTile.Round(r, 9)) {
                using (SolidBrush b = new SolidBrush(Color.FromArgb(30, 255, 255, 255))) g.FillPath(b, p);
            }
        }
        Color fg = (Active || DropTarget) ? Color.White : Color.FromArgb(228, 230, 236);
        TextRenderer.DrawText(g, Text, Font, new Rectangle(22, 0, Width - 60, Height), fg,
            TextFormatFlags.VerticalCenter | TextFormatFlags.Left | TextFormatFlags.EndEllipsis | TextFormatFlags.NoPrefix);
    }
}

public class Win32 {
    [System.Runtime.InteropServices.DllImport("user32.dll")]
    public static extern bool SetProcessDPIAware();

    [System.Runtime.InteropServices.DllImport("user32.dll")]
    public static extern int SetWindowCompositionAttribute(IntPtr hwnd, ref WindowCompositionAttributeData data);

    [System.Runtime.InteropServices.DllImport("shell32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
    static extern IntPtr SHGetFileInfo(string pszPath, uint dwFileAttributes, ref SHFILEINFO psfi, uint cbFileInfo, uint uFlags);
    [System.Runtime.InteropServices.DllImport("user32.dll")]
    static extern bool DestroyIcon(IntPtr hIcon);
    [System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential, CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
    struct SHFILEINFO {
        public IntPtr hIcon; public int iIcon; public uint dwAttributes;
        [System.Runtime.InteropServices.MarshalAs(System.Runtime.InteropServices.UnmanagedType.ByValTStr, SizeConst = 260)] public string szDisplayName;
        [System.Runtime.InteropServices.MarshalAs(System.Runtime.InteropServices.UnmanagedType.ByValTStr, SizeConst = 80)] public string szTypeName;
    }

    public static System.Drawing.Icon GetShellIcon(string path) {
        SHFILEINFO info = new SHFILEINFO();
        IntPtr res = SHGetFileInfo(path, 0, ref info, (uint)System.Runtime.InteropServices.Marshal.SizeOf(info), 0x100);
        if (info.hIcon == IntPtr.Zero) return null;
        System.Drawing.Icon ic = (System.Drawing.Icon)System.Drawing.Icon.FromHandle(info.hIcon).Clone();
        DestroyIcon(info.hIcon);
        return ic;
    }

    [System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
    public struct WindowCompositionAttributeData { public int Attribute; public IntPtr Data; public int SizeOfData; }
    [System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
    public struct AccentPolicy { public int AccentState; public int AccentFlags; public int GradientColor; public int AnimationId; }

    public static void EnableAcrylic(IntPtr hwnd, int abgr) {
        AccentPolicy policy = new AccentPolicy();
        policy.AccentState = 4;
        policy.AccentFlags = 2;
        policy.GradientColor = abgr;
        int size = System.Runtime.InteropServices.Marshal.SizeOf(policy);
        IntPtr ptr = System.Runtime.InteropServices.Marshal.AllocHGlobal(size);
        System.Runtime.InteropServices.Marshal.StructureToPtr(policy, ptr, false);
        WindowCompositionAttributeData data = new WindowCompositionAttributeData();
        data.Attribute = 19;
        data.Data = ptr;
        data.SizeOfData = size;
        SetWindowCompositionAttribute(hwnd, ref data);
        System.Runtime.InteropServices.Marshal.FreeHGlobal(ptr);
    }
}