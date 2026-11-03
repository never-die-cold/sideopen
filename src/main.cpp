// SideOpen — native Win32 edge launcher
// Win32 + GDI+ layered windows. No runtime dependency beyond the OS.
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <gdiplus.h>
#include <dwmapi.h>
#include <cstdarg>
#include <cwctype>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cstdio>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

using namespace Gdiplus;
using std::wstring;
using std::vector;

static int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int Scale(double s, int v) { return (int)(v * s + 0.5); }

// ---------------------------------------------------------------- config
struct Config {
    wstring edge = L"right";
    wstring root;
    int railWidth = 236;
    int rowHeight = 40;
    int tileW = 106;
    int tileH = 120;
    int hotZone = 6;
    int minCols = 3;
    int hideDelayMs = 220;
    int opacity = 238;
    int iconSize = 44;
    wstring bg = L"1E1E21";
    wstring fg = L"E8EAF0";
    wstring dim = L"949AA3";
    wstring accent = L"0A84FF";
    bool hideFullscreen = true;
    wstring hotkey = L"Ctrl+Alt+Space";
};

static Color HexColor(const wstring& hex, BYTE a = 255) {
    unsigned int v = 0;
    if (swscanf_s(hex.c_str(), L"%x", &v) != 1) v = 0;
    return Color(a, (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

static wstring IniPath() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    wstring p = n ? wstring(buf, n) : wstring(L".");
    p += L"\\SideOpen";
    CreateDirectoryW(p.c_str(), nullptr);
    return p + L"\\config.ini";
}

static void LoadConfig(Config& c) {
    wstring p = IniPath();
    wchar_t buf[1024];
    auto getStr = [&](const wchar_t* key, const wstring& def) {
        GetPrivateProfileStringW(L"general", key, def.c_str(), buf, 1024, p.c_str());
        return wstring(buf);
    };
    auto getInt = [&](const wchar_t* key, int def) { return (int)GetPrivateProfileIntW(L"general", key, def, p.c_str()); };

    c.edge = getStr(L"edge", c.edge);
    c.root = getStr(L"root", L"");
    c.railWidth = getInt(L"railWidth", c.railWidth);
    c.rowHeight = getInt(L"rowHeight", c.rowHeight);
    c.tileW = getInt(L"tileW", c.tileW);
    c.tileH = getInt(L"tileH", c.tileH);
    c.hotZone = getInt(L"hotZone", c.hotZone);
    c.minCols = getInt(L"minCols", c.minCols);
    c.hideDelayMs = getInt(L"hideDelayMs", c.hideDelayMs);
    c.opacity = getInt(L"opacity", c.opacity);
    c.iconSize = getInt(L"iconSize", c.iconSize);
    c.bg = getStr(L"bg", c.bg);
    c.fg = getStr(L"fg", c.fg);
    c.dim = getStr(L"dim", c.dim);
    c.accent = getStr(L"accent", c.accent);
    c.hideFullscreen = getInt(L"hideFullscreen", 1) != 0;
    c.hotkey = getStr(L"hotkey", c.hotkey);

    if (c.root.empty()) {
        PWSTR psz = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &psz))) { c.root = psz; CoTaskMemFree(psz); }
    }
}

static void SaveConfigDefault() {
    wstring p = IniPath();
    if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) return;
    Config c;
    WritePrivateProfileStringW(L"general", L"edge", c.edge.c_str(), p.c_str());
    WritePrivateProfileStringW(L"general", L"hotZone", L"6", p.c_str());
    WritePrivateProfileStringW(L"general", L"railWidth", L"236", p.c_str());
    WritePrivateProfileStringW(L"general", L"tileW", L"106", p.c_str());
    WritePrivateProfileStringW(L"general", L"tileH", L"120", p.c_str());
    WritePrivateProfileStringW(L"general", L"opacity", L"238", p.c_str());
    WritePrivateProfileStringW(L"general", L"bg", L"1E1E21", p.c_str());
    WritePrivateProfileStringW(L"general", L"accent", L"0A84FF", p.c_str());
    WritePrivateProfileStringW(L"general", L"hideFullscreen", L"1", p.c_str());
    WritePrivateProfileStringW(L"general", L"hotkey", L"Ctrl+Alt+Space", p.c_str());
}

// ---------------------------------------------------------------- model
struct Item { wstring path; wstring label; bool isDir = false; };
struct Category { wstring folder; wstring path; vector<Item> items; int cols = 3; };

struct App {
    Config cfg;
    vector<Category> cats;
    HWND rail = nullptr, fly = nullptr, ghost = nullptr, msgw = nullptr;
    bool railOn = false, flyOn = false, dragging = false, searching = false, fullscreenHidden = false;
    int curCat = -1, hoverRow = -1, missTicks = 0;
    int dragCat = -1, dragItem = -1;
    POINT dragStart{ 0,0 }, dragPos{ 0,0 };
    DWORD pressTick = 0;
    bool pressPending = false;
    vector<Item*> searchHits;
    int searchSel = 0;
    wstring query;
    int dpi = 96; double s = 1.0;
    int scrW = 0, scrH = 0;
};
static App g;
// ---------------------------------------------------------------- surface
struct Surface {
    HWND hwnd = nullptr;
    HDC memdc = nullptr;
    HBITMAP dib = nullptr;
    void* bits = nullptr;
    HBITMAP oldBmp = nullptr;
    int w = 0, h = 0;
    Bitmap* gbmp = nullptr;
    Graphics* g = nullptr;

    void Release() {
        if (g) { delete g; g = nullptr; }
        if (gbmp) { delete gbmp; gbmp = nullptr; }
        if (memdc) { SelectObject(memdc, oldBmp); DeleteDC(memdc); memdc = nullptr; }
        if (dib) { DeleteObject(dib); dib = nullptr; bits = nullptr; }
        w = h = 0;
    }

    void Ensure(int nw, int nh) {
        if (nw == w && nh == h && g) return;
        Release();
        w = nw; h = nh;
        HDC screen = GetDC(nullptr);
        memdc = CreateCompatibleDC(screen);
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        dib = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        ReleaseDC(nullptr, screen);
        oldBmp = (HBITMAP)SelectObject(memdc, dib);
        gbmp = new Bitmap(w, h, w * 4, PixelFormat32bppPARGB, (BYTE*)bits);
        g = new Graphics(gbmp);
        g->SetSmoothingMode(SmoothingModeAntiAlias);
        g->SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        g->SetPixelOffsetMode(PixelOffsetModeHalf);
    }

    void Present(int x, int y) {
        if (g) g->Flush(FlushIntentionSync);
        HDC screen = GetDC(nullptr);
        POINT dst{ x, y }, src{ 0,0 };
        SIZE sz{ w, h };
        BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        UpdateLayeredWindow(hwnd, screen, &dst, &sz, memdc, &src, 0, &bf, ULW_ALPHA);
        ReleaseDC(nullptr, screen);
    }
};

static Surface surfRail, surfFly, surfGhost, surfSearch;
static HWND gSearch = nullptr;

// ---------------------------------------------------------------- draw
static void FillRound(Graphics& g, RectF r, float rad, Color c) {
    GraphicsPath p;
    float d = rad * 2;
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
    p.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
    p.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
    p.CloseFigure();
    SolidBrush b(c);
    g.FillPath(&b, &p);
}

static void StrokeRound(Graphics& g, RectF r, float rad, Color c, float w) {
    GraphicsPath p;
    float d = rad * 2;
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
    p.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
    p.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
    p.CloseFigure();
    Pen pen(c, w);
    g.DrawPath(&pen, &p);
}

struct Fonts {
    Font* row = nullptr;
    Font* tile = nullptr;
    Font* head = nullptr;
    Font* big = nullptr;
    Font* count = nullptr;

    void Init(double s) {
        FontFamily fam(L"Microsoft YaHei UI");
        row = new Font(&fam, (float)(10.5 * s), FontStyleRegular, UnitPixel);
        tile = new Font(&fam, (float)(8.5 * s), FontStyleRegular, UnitPixel);
        head = new Font(&fam, (float)(9.0 * s), FontStyleBold, UnitPixel);
        big = new Font(&fam, (float)(12.0 * s), FontStyleRegular, UnitPixel);
        count = new Font(&fam, (float)(8.5 * s), FontStyleRegular, UnitPixel);
    }
    void Free() { delete row; delete tile; delete head; delete big; delete count; row = tile = head = big = count = nullptr; }
};
static Fonts gFonts;

static void DrawText2(Graphics& g, const wstring& s, Font* f, RectF r, Color c, StringAlignment ha = StringAlignmentCenter) {
    SolidBrush b(c);
    StringFormat sf;
    sf.SetAlignment(ha);
    sf.SetLineAlignment(StringAlignmentCenter);
    sf.SetTrimming(StringTrimmingEllipsisCharacter);
    sf.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsNoClip);
    g.DrawString(s.c_str(), (INT)s.size(), f, r, &sf, &b);
}

static void DrawTextWrap(Graphics& g, const wstring& s, Font* f, RectF r, Color c) {
    SolidBrush b(c);
    StringFormat sf;
    sf.SetAlignment(StringAlignmentCenter);
    sf.SetLineAlignment(StringAlignmentNear);
    sf.SetTrimming(StringTrimmingEllipsisCharacter);
    g.DrawString(s.c_str(), (INT)s.size(), f, r, &sf, &b);
}

// ---------------------------------------------------------------- icons
static std::map<wstring, HBITMAP> gIconCache;

static HBITMAP LoadIconFor(const wstring& path, int want) {
    auto it = gIconCache.find(path);
    if (it != gIconCache.end()) return it->second;

    HBITMAP hb = nullptr;
    IShellItemImageFactory* f = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&f))) && f) {
        SIZE sz{ want, want };
        if (FAILED(f->GetImage(sz, (SIIGBF)(SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK), &hb))) hb = nullptr;
        f->Release();
    }
    if (!hb) {
        SHFILEINFOW sfi{};
        if (SHGetFileInfoW(path.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON)) {
            HICON ic = sfi.hIcon;
            HDC dc = GetDC(nullptr);
            hb = CreateCompatibleBitmap(dc, 32, 32);
            HDC mdc = CreateCompatibleDC(dc);
            HBITMAP old = (HBITMAP)SelectObject(mdc, hb);
            PatBlt(mdc, 0, 0, 32, 32, BLACKNESS);
            DrawIconEx(mdc, 0, 0, ic, 32, 32, 0, nullptr, DI_NORMAL);
            SelectObject(mdc, old);
            DeleteDC(mdc);
            ReleaseDC(nullptr, dc);
            DestroyIcon(ic);
        }
    }
    gIconCache[path] = hb;
    return hb;
}

// ---------------------------------------------------------------- geometry
struct Geom {
    int railW, railH, railX, railY;
    int flyW, flyH, flyX, flyY;
    int hotX, hotY, hotW, hotH;
    int headH, rowH, pad;
};

static Geom ComputeRailGeom() {
    Geom o{};
    double s = g.s;
    o.pad = Scale(s, 14);
    o.headH = Scale(s, 46);
    o.rowH = Scale(s, g.cfg.rowHeight);
    o.railW = Scale(s, g.cfg.railWidth);
    o.railH = o.headH + (int)g.cats.size() * o.rowH + Scale(s, 6);

    int hot = Scale(s, g.cfg.hotZone);
    if (g.cfg.edge == L"right") {
        o.railX = g.scrW - o.railW - hot;
        o.railY = (g.scrH - o.railH) / 2;
        o.hotX = g.scrW - hot; o.hotY = o.railY; o.hotW = hot; o.hotH = o.railH;
    } else if (g.cfg.edge == L"left") {
        o.railX = hot;
        o.railY = (g.scrH - o.railH) / 2;
        o.hotX = 0; o.hotY = o.railY; o.hotW = hot; o.hotH = o.railH;
    } else if (g.cfg.edge == L"top") {
        o.railX = (g.scrW - o.railW) / 2;
        o.railY = hot;
        o.hotX = o.railX; o.hotY = 0; o.hotW = o.railW; o.hotH = hot;
    } else {
        o.railX = (g.scrW - o.railW) / 2;
        o.railY = g.scrH - o.railH - hot;
        o.hotX = o.railX; o.hotY = g.scrH - hot; o.hotW = o.railW; o.hotH = hot;
    }
    return o;
}

static void ComputeFlyGeom(int idx, Geom& o) {
    double s = g.s;
    auto& cat = g.cats[idx];
    o.flyW = Scale(s, g.cfg.tileW) * cat.cols + Scale(s, 8) * (cat.cols - 1) + Scale(s, 36);
    int rows = (int)((cat.items.size() + cat.cols - 1) / cat.cols);
    if (rows < 1) rows = 1;
    o.flyH = o.headH + rows * Scale(s, g.cfg.tileH) + (rows - 1) * Scale(s, 8) + Scale(s, 14);

    if (g.cfg.edge == L"right") {
        o.flyX = o.railX - o.flyW;
        o.flyY = o.railY + o.headH + idx * o.rowH - Scale(s, 10);
    } else if (g.cfg.edge == L"left") {
        o.flyX = o.railX + o.railW;
        o.flyY = o.railY + o.headH + idx * o.rowH - Scale(s, 10);
    } else if (g.cfg.edge == L"top") {
        o.flyX = o.railX + o.railW + Scale(s, 8);
        o.flyY = o.railY;
    } else {
        o.flyX = o.railX + o.railW + Scale(s, 8);
        o.flyY = o.railY - o.flyH - Scale(s, 8);
    }
    if (o.flyY < 0) o.flyY = 0;
    if (o.flyY + o.flyH > g.scrH) o.flyY = g.scrH - o.flyH;
    if (o.flyX < 0) o.flyX = 0;
}

// ---------------------------------------------------------------- loading
static void LoadCategories() {
    g.cats.clear();
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((g.cfg.root + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    vector<wstring> dirs;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (fd.cFileName[0] == L'.') continue;
        wstring n = fd.cFileName;
        if (n.size() < 3) continue;
        if (!iswdigit(n[0]) || !iswdigit(n[1]) || n[2] != L'_') continue;
        dirs.push_back(n);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(dirs.begin(), dirs.end());

    for (auto& d : dirs) {
        Category c;
        c.folder = d;
        c.path = g.cfg.root + L"\\" + d;
        HANDLE h2 = FindFirstFileW((c.path + L"\\*").c_str(), &fd);
        if (h2 != INVALID_HANDLE_VALUE) {
            do {
                if (fd.cFileName[0] == L'.') continue;
                Item it;
                it.path = c.path + L"\\" + fd.cFileName;
                it.isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                wstring name = fd.cFileName;
                size_t dot = name.find_last_of(L'.');
                it.label = (dot == wstring::npos || it.isDir) ? name : name.substr(0, dot);
                c.items.push_back(it);
            } while (FindNextFileW(h2, &fd));
            FindClose(h2);
        }
        std::sort(c.items.begin(), c.items.end(), [](const Item& a, const Item& b) {
            if (a.isDir != b.isDir) return a.isDir > b.isDir;
            return _wcsicmp(a.label.c_str(), b.label.c_str()) < 0;
        });
        g.cats.push_back(c);
    }
}

static void ComputeColumns() {
    double s = g.s;
    int tileHp = Scale(s, g.cfg.tileH);
    int gap = Scale(s, 8);
    int limit = (int)((g.scrH * 0.84 - Scale(s, 46)) / (tileHp + gap));
    if (limit < 3) limit = 3;
    for (auto& c : g.cats) {
        int n = (int)c.items.size(); if (n < 1) n = 1;
        int cols = (n + limit - 1) / limit;
        if (cols < g.cfg.minCols) cols = g.cfg.minCols;
        c.cols = cols;
    }
}


// ---------------------------------------------------------------- paint
static int gHoverTile = -1;
static int ShadowPx() { return Scale(g.s, 20); }

// soft drop shadow drawn as stacked, shrinking rounded rects
static void DrawSoftShadow(Graphics& g2, RectF r, float rad) {
    int px = ShadowPx();
    for (int i = px; i >= 1; i--) {
        float t = (float)i / (float)px;
        int a = (int)(26 * (1.0f - t) * (1.0f - t));
        if (a <= 0) continue;
        RectF rr(r.X - i, r.Y - i + 2, r.Width + i * 2, r.Height + i * 2);
        FillRound(g2, rr, rad + i * 0.6f, Color(a, 0, 0, 0));
    }
}

static void DrawPanelChrome(Graphics& g2, RectF r, float rad) {
    // material: vertical gradient so it reads as a translucent sheet, not a flat box
    LinearGradientBrush br(r, Color(236, 74, 74, 82), Color(242, 30, 30, 35), LinearGradientModeVertical);
    GraphicsPath p;
    float d = rad * 2;
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
    p.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
    p.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
    p.CloseFigure();
    g2.FillPath(&br, &p);

    // rim light inside, dark outline outside
    StrokeRound(g2, RectF(r.X + 0.5f, r.Y + 0.5f, r.Width - 1, r.Height - 1), rad, Color(38, 255, 255, 255), 1.0f);
    StrokeRound(g2, RectF(r.X - 0.5f, r.Y - 0.5f, r.Width + 1, r.Height + 1), rad, Color(70, 0, 0, 0), 1.0f);
}

static void PaintRail(Geom& o) {
    int m = ShadowPx();
    surfRail.Ensure(o.railW + 2 * m, o.railH + 2 * m);
    Graphics& g2 = *surfRail.g;
    g2.ResetTransform();
    g2.Clear(Color(0, 0, 0, 0));
    g2.TranslateTransform((float)m, (float)m);

    float rad = (float)Scale(g.s, 20);
    RectF body(0, 0, (float)o.railW, (float)o.railH);
    DrawSoftShadow(g2, body, rad);
    DrawPanelChrome(g2, body, rad);

    DrawText2(g2, L"应用", gFonts.head,
        RectF((float)Scale(g.s, 20), (float)Scale(g.s, 14), 200.f, (float)Scale(g.s, 22)),
        Color(115, 255, 255, 255), StringAlignmentNear);

    float rowTop = (float)o.headH - (float)Scale(g.s, 6);
    for (size_t i = 0; i < g.cats.size(); i++) {
        float y = rowTop + (float)((int)i * o.rowH);
        RectF row((float)Scale(g.s, 8), y, (float)(o.railW - Scale(g.s, 16)), (float)(o.rowH - Scale(g.s, 4)));
        bool active = ((int)i == g.curCat) && g.flyOn;
        bool hovering = ((int)i == g.hoverRow) && g.railOn;
        if (active) {
            FillRound(g2, row, (float)Scale(g.s, 7), HexColor(g.cfg.accent, 235));
            StrokeRound(g2, RectF(row.X + 0.5f, row.Y + 0.5f, row.Width - 1, row.Height - 1), (float)Scale(g.s, 7), Color(40, 255, 255, 255), 1.0f);
        } else if (hovering) {
            FillRound(g2, row, (float)Scale(g.s, 7), Color(26, 255, 255, 255));
        }

        Color fg = active ? Color(255, 255, 255, 255) : Color(255, 246, 246, 248);
        wstring label = g.cats[i].folder;
        DrawText2(g2, label, gFonts.row,
            RectF(row.X + (float)Scale(g.s, 12), row.Y, row.Width - (float)Scale(g.s, 46), row.Height),
            fg, StringAlignmentNear);

        Color cn = active ? Color(200, 255, 255, 255) : Color(135, 255, 255, 255);
        DrawText2(g2, std::to_wstring(g.cats[i].items.size()), gFonts.count,
            RectF(row.X, row.Y, row.Width - (float)Scale(g.s, 10), row.Height), cn, StringAlignmentFar);
    }
}

static void PaintFly(int idx, Geom& o) {
    if (idx < 0 || idx >= (int)g.cats.size()) return;
    int m = ShadowPx();
    surfFly.Ensure(o.flyW + 2 * m, o.flyH + 2 * m);
    Graphics& g2 = *surfFly.g;
    g2.ResetTransform();
    g2.Clear(Color(0, 0, 0, 0));
    g2.TranslateTransform((float)m, (float)m);

    float rad = (float)Scale(g.s, 24);
    RectF body(0, 0, (float)o.flyW, (float)o.flyH);
    DrawSoftShadow(g2, body, rad);
    DrawPanelChrome(g2, body, rad);

    DrawText2(g2, g.cats[idx].folder, gFonts.head,
        RectF((float)Scale(g.s, 22), (float)Scale(g.s, 14), 300.f, (float)Scale(g.s, 22)),
        Color(150, 255, 255, 255), StringAlignmentNear);
    {
        float ly = (float)Scale(g.s, 44);
        FillRound(g2, RectF((float)Scale(g.s, 20), ly, (float)(o.flyW - Scale(g.s, 40)), 1.0f), 0.5f, Color(22, 255, 255, 255));
    }

    int pad = Scale(g.s, 20);
    int tw = Scale(g.s, g.cfg.tileW);
    int th = Scale(g.s, g.cfg.tileH);
    int gap = Scale(g.s, 6);
    int isz = Scale(g.s, g.cfg.iconSize);
    int headY = o.headH + Scale(g.s, 6);

    auto& cat = g.cats[idx];
    for (size_t k = 0; k < cat.items.size(); k++) {
        int col = (int)k % cat.cols;
        int row = (int)k / cat.cols;
        float x = (float)(pad + col * (tw + gap));
        float y = (float)(headY + row * (th + gap));

        bool hot = ((int)k == gHoverTile);
        if (hot) {
            FillRound(g2, RectF(x + 3, y + 2, (float)tw - 6, (float)th - 6), (float)Scale(g.s, 14), Color(24, 255, 255, 255));
            StrokeRound(g2, RectF(x + 3.5f, y + 2.5f, (float)tw - 7, (float)th - 7), (float)Scale(g.s, 14), Color(28, 255, 255, 255), 1.0f);
        }

        HBITMAP hb = LoadIconFor(cat.items[k].path, 256);
        if (hb) {
            Bitmap ib(hb, nullptr);
            float iy = y + (float)Scale(g.s, 14) - (hot ? 1.0f : 0.0f);
            g2.DrawImage(&ib, RectF(x + (tw - isz) / 2.0f, iy, (float)isz, (float)isz));
        }
        RectF tr(x + 6, y + (float)Scale(g.s, 14 + g.cfg.iconSize + 8), (float)(tw - 12),
                 (float)(th - Scale(g.s, 14 + g.cfg.iconSize + 8) - 8));
        DrawTextWrap(g2, cat.items[k].label, gFonts.tile, tr, Color(255, 240, 240, 243));
    }
}

// ---------------------------------------------------------------- windows
static const wchar_t* kWndClass = L"SideOpenPanel";
static const wchar_t* kSearchClass = L"SideOpenSearch";
static const UINT WM_TRAY = WM_APP + 1;
static const UINT_PTR kTimer = 1;

static Geom gRailGeom;
static Geom gFlyGeom;
static int gFlyCat = -1;
static bool gPin = false;

static void HideFly();

static RECT RailRect() { RECT r{ gRailGeom.railX, gRailGeom.railY, gRailGeom.railX + gRailGeom.railW, gRailGeom.railY + gRailGeom.railH }; return r; }
static RECT FlyRect() { RECT r{ gFlyGeom.flyX, gFlyGeom.flyY, gFlyGeom.flyX + gFlyGeom.flyW, gFlyGeom.flyY + gFlyGeom.flyH }; return r; }
static RECT HotRect() { RECT r{ gRailGeom.hotX, gRailGeom.hotY, gRailGeom.hotX + gRailGeom.hotW, gRailGeom.hotY + gRailGeom.hotH }; return r; }

static void ShowRail() {
    if (g.railOn) return;
    g.railOn = true;
    ComputeColumns();
    gRailGeom = ComputeRailGeom();
    PaintRail(gRailGeom);
    surfRail.Present(gRailGeom.railX - ShadowPx(), gRailGeom.railY - ShadowPx());
    ShowWindow(g.rail, SW_SHOWNOACTIVATE);
}

static void HideRail() {
    if (!g.railOn) return;
    g.railOn = false;
    g.curCat = -1;
    g.hoverRow = -1;
    ShowWindow(g.rail, SW_HIDE);
    HideFly();
}

static void HideFly() {
    if (!g.flyOn) return;
    g.flyOn = false;
    g.curCat = -1;
    gFlyCat = -1;
    ShowWindow(g.fly, SW_HIDE);
    if (g.railOn) { PaintRail(gRailGeom); surfRail.Present(gRailGeom.railX - ShadowPx(), gRailGeom.railY - ShadowPx()); }
}

static void ShowFly(int idx) {
    if (idx < 0 || idx >= (int)g.cats.size()) return;
    if (g.flyOn && gFlyCat == idx) return;
    g.curCat = idx;
    gFlyCat = idx;
    g.flyOn = true;
    Geom gm = gRailGeom;
    ComputeFlyGeom(idx, gm);
    gFlyGeom = gm;
    PaintFly(idx, gFlyGeom);
    ShowWindow(g.fly, SW_SHOWNOACTIVATE);
    surfFly.Present(gFlyGeom.flyX - ShadowPx(), gFlyGeom.flyY - ShadowPx());
    PaintRail(gRailGeom);
    surfRail.Present(gRailGeom.railX - ShadowPx(), gRailGeom.railY - ShadowPx());
}

// ---------------------------------------------------------------- actions
static void Launch(const wstring& path) { ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }

static int RowAt(POINT p) {
    if (p.x < gRailGeom.railX || p.x >= gRailGeom.railX + gRailGeom.railW) return -1;
    int rel = p.y - gRailGeom.railY - gRailGeom.headH;
    if (rel < 0) return -1;
    int i = rel / gRailGeom.rowH;
    if (i < 0 || i >= (int)g.cats.size()) return -1;
    return i;
}

static int TileAt(POINT p, int catIdx) {
    if (catIdx < 0 || catIdx >= (int)g.cats.size()) return -1;
    int pad = Scale(g.s, 18);
    int tw = Scale(g.s, g.cfg.tileW);
    int th = Scale(g.s, g.cfg.tileH);
    int gap = Scale(g.s, 8);
    int rx = p.x - gFlyGeom.flyX - pad;
    int ry = p.y - gFlyGeom.flyY - gFlyGeom.headH;
    if (rx < 0 || ry < 0) return -1;
    int col = rx / (tw + gap);
    int row = ry / (th + gap);
    if (col >= g.cats[catIdx].cols) return -1;
    int tx = pad + col * (tw + gap);
    int ty = row * (th + gap);
    if (p.x - gFlyGeom.flyX - tx > tw) return -1;
    if (p.y - gFlyGeom.flyY - gFlyGeom.headH - ty > th) return -1;
    int idx = row * g.cats[catIdx].cols + col;
    if (idx < 0 || idx >= (int)g.cats[catIdx].items.size()) return -1;
    return idx;
}

static bool FullscreenActive() {
    QUERY_USER_NOTIFICATION_STATE st;
    if (FAILED(SHQueryUserNotificationState(&st))) return false;
    return st == QUNS_RUNNING_D3D_FULL_SCREEN || st == QUNS_PRESENTATION_MODE || st == QUNS_BUSY;
}

static wstring UniqueTarget(const wstring& dir, const wstring& name) {
    wstring base = name, ext;
    size_t dot = name.find_last_of(L'.');
    if (dot != wstring::npos) { base = name.substr(0, dot); ext = name.substr(dot); }
    wstring cand = dir + L"\\" + name;
    if (GetFileAttributesW(cand.c_str()) == INVALID_FILE_ATTRIBUTES) return cand;
    for (int i = 2; i < 100; i++) {
        cand = dir + L"\\" + base + L" (" + std::to_wstring(i) + L")" + ext;
        if (GetFileAttributesW(cand.c_str()) == INVALID_FILE_ATTRIBUTES) return cand;
    }
    return dir + L"\\" + name;
}

static void GhostShow(int catIdx, int itemIdx) {
    auto& it = g.cats[catIdx].items[itemIdx];
    int tw = Scale(g.s, g.cfg.tileW);
    int th = Scale(g.s, g.cfg.tileH);
    surfGhost.Ensure(tw, th);
    Graphics& gg = *surfGhost.g;
    gg.Clear(Color(0, 0, 0, 0));
    FillRound(gg, RectF(2, 2, (float)tw - 4, (float)th - 4), (float)Scale(g.s, 10), HexColor(g.cfg.bg, 240));
    StrokeRound(gg, RectF(2.5f, 2.5f, (float)tw - 5, (float)th - 5), (float)Scale(g.s, 10), HexColor(g.cfg.accent, 220), 2.0f);
    int isz = Scale(g.s, g.cfg.iconSize);
    HBITMAP hb = LoadIconFor(it.path, 256);
    if (hb) { Bitmap ib(hb, nullptr); gg.DrawImage(&ib, RectF((tw - isz) / 2.0f, (float)Scale(g.s, 12), (float)isz, (float)isz)); }
    DrawTextWrap(gg, it.label, gFonts.tile, RectF(4, (float)Scale(g.s, 12 + g.cfg.iconSize + 6), (float)(tw - 8),
        (float)(th - Scale(g.s, 12 + g.cfg.iconSize + 6) - 6)), HexColor(g.cfg.fg, 235));
    ShowWindow(g.ghost, SW_SHOWNOACTIVATE);
}

static void GhostMove(POINT p) {
    int tw = Scale(g.s, g.cfg.tileW);
    int th = Scale(g.s, g.cfg.tileH);
    surfGhost.Present(p.x - tw / 2, p.y - th / 2);
}

static void DropAt(POINT p) {
    int targetRow = RowAt(p);
    if (g.dragCat < 0 || g.dragCat >= (int)g.cats.size()) return;
    if (targetRow < 0 || targetRow == g.dragCat) return;

    auto& src = g.cats[g.dragCat].items[g.dragItem];
    wstring from = src.path;
    wstring name = from.substr(from.find_last_of(L'\\') + 1);
    wstring to = UniqueTarget(g.cats[targetRow].path, name);

    if (MoveFileW(from.c_str(), to.c_str())) {
        gIconCache.clear();
        LoadCategories();
        ComputeColumns();
        gRailGeom = ComputeRailGeom();
        PaintRail(gRailGeom);
        surfRail.Present(gRailGeom.railX - ShadowPx(), gRailGeom.railY - ShadowPx());
        ShowFly(targetRow);
    }
}

// ---------------------------------------------------------------- tray
static NOTIFYICONDATAW gNid{};

static void TrayMenu(HWND hwnd) {
    POINT pt; GetCursorPos(&pt);
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, 1001, L"显示 / 隐藏");
    AppendMenuW(m, MF_STRING, 1002, L"打开配置");
    AppendMenuW(m, MF_STRING, 1003, L"重新加载");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 1004, L"退出");
    SetForegroundWindow(hwnd);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(m);
    if (cmd == 1001) { if (g.railOn) HideRail(); else ShowRail(); }
    else if (cmd == 1002) ShellExecuteW(nullptr, L"open", IniPath().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    else if (cmd == 1003) { LoadCategories(); ComputeColumns(); gRailGeom = ComputeRailGeom(); if (g.railOn) { PaintRail(gRailGeom); surfRail.Present(gRailGeom.railX - ShadowPx(), gRailGeom.railY - ShadowPx()); } }
    else if (cmd == 1004) PostQuitMessage(0);
}

static void TrayAdd(HWND hwnd) {
    gNid.cbSize = sizeof(gNid);
    gNid.hWnd = hwnd;
    gNid.uID = 1;
    gNid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    gNid.uCallbackMessage = WM_TRAY;
    gNid.hIcon = (HICON)LoadImageW(nullptr, IDI_APPLICATION, IMAGE_ICON, 16, 16, LR_SHARED);
    wcscpy_s(gNid.szTip, L"SideOpen");
    Shell_NotifyIconW(NIM_ADD, &gNid);
}

// ---------------------------------------------------------------- search
static void SearchBuild() {
    g.searchHits.clear();
    if (g.query.empty()) return;
    for (auto& c : g.cats) {
        for (auto& it : c.items) {
            if (StrStrIW(it.label.c_str(), g.query.c_str())) {
                g.searchHits.push_back(&it);
                if (g.searchHits.size() >= 9) return;
            }
        }
    }
}

static void SearchPaint() {
    int w = Scale(g.s, 640);
    int rowH = Scale(g.s, 46);
    int n = (int)std::min<size_t>(g.searchHits.size(), 8);
    if (n < 1) n = 1;
    int h = Scale(g.s, 76) + n * rowH + Scale(g.s, 12);
    surfSearch.Ensure(w, h);
    Graphics& gg = *surfSearch.g;
    gg.Clear(Color(0, 0, 0, 0));
    FillRound(gg, RectF(0, 0, (float)w, (float)h), (float)Scale(g.s, 16), HexColor(g.cfg.bg, (BYTE)g.cfg.opacity));
    StrokeRound(gg, RectF(0.5f, 0.5f, (float)w - 1, (float)h - 1), (float)Scale(g.s, 16), Color(48, 255, 255, 255), 1.0f);

    wstring shown = g.query.empty() ? L"输入以搜索…" : g.query;
    Color qc = g.query.empty() ? HexColor(g.cfg.dim) : HexColor(g.cfg.fg);
    DrawText2(gg, shown, gFonts.big, RectF((float)Scale(g.s, 22), (float)Scale(g.s, 16), (float)w - 44, (float)Scale(g.s, 36)), qc, StringAlignmentNear);

    int y = Scale(g.s, 64);
    for (size_t i = 0; i < g.searchHits.size() && i < 8; i++) {
        RectF r((float)Scale(g.s, 10), (float)y, (float)w - (float)Scale(g.s, 20), (float)rowH - (float)Scale(g.s, 4));
        if ((int)i == g.searchSel) FillRound(gg, r, (float)Scale(g.s, 10), HexColor(g.cfg.accent, 220));
        HBITMAP hb = LoadIconFor(g.searchHits[i]->path, 256);
        if (hb) { Bitmap ib(hb, nullptr); gg.DrawImage(&ib, RectF(r.X + (float)Scale(g.s, 10), r.Y + (float)Scale(g.s, 6), (float)Scale(g.s, 28), (float)Scale(g.s, 28))); }
        Color tc = ((int)i == g.searchSel) ? Color(255, 255, 255, 255) : HexColor(g.cfg.fg, 235);
        DrawText2(gg, g.searchHits[i]->label, gFonts.row, RectF(r.X + (float)Scale(g.s, 50), r.Y, r.Width - (float)Scale(g.s, 60), r.Height), tc, StringAlignmentNear);
        y += rowH;
    }

    int x = (g.scrW - w) / 2;
    surfSearch.Present(x, Scale(g.s, 120));
}

static void SearchShow() {
    g.searching = true;
    g.query.clear();
    g.searchSel = 0;
    SearchBuild();
    SearchPaint();
    SetForegroundWindow(gSearch);
    SetFocus(gSearch);
}

static void SearchHide() {
    g.searching = false;
    ShowWindow(gSearch, SW_HIDE);
}

static LRESULT CALLBACK SearchProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CHAR: {
        wchar_t ch = (wchar_t)wp;
        if (ch == 27) { SearchHide(); return 0; }
        if (ch == L'\r') {
            if (!g.searchHits.empty()) {
                int i = Clamp(g.searchSel, 0, (int)g.searchHits.size() - 1);
                Launch(g.searchHits[i]->path);
                SearchHide();
            }
            return 0;
        }
        if (ch == L'\b') { if (!g.query.empty()) g.query.pop_back(); }
        else if (ch >= 32) g.query.push_back(ch);
        g.searchSel = 0;
        SearchBuild();
        SearchPaint();
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { SearchHide(); return 0; }
        if (wp == VK_DOWN) { if (g.searchSel + 1 < (int)g.searchHits.size()) { g.searchSel++; SearchPaint(); } return 0; }
        if (wp == VK_UP) { if (g.searchSel > 0) { g.searchSel--; SearchPaint(); } return 0; }
        return 0;
    case WM_KILLFOCUS:
        SearchHide();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------- tick
static void Tick() {
    static bool prevDown = false;
    POINT p; GetCursorPos(&p);

    if (gPin) {
        if (!g.railOn) ShowRail();
        if (g.flyOn == false && !g.cats.empty()) ShowFly(0);
        prevDown = false;
        return;
    }

    bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

    if (g.cfg.hideFullscreen && FullscreenActive()) {
        if (!g.fullscreenHidden) { HideFly(); HideRail(); g.fullscreenHidden = true; }
        prevDown = down;
        return;
    }
    g.fullscreenHidden = false;

    RECT rr = RailRect(), fr = FlyRect(), hr = HotRect();
    bool inHot = PtInRect(&hr, p) != 0;
    bool inRail = g.railOn && PtInRect(&rr, p) != 0;
    bool inFly = g.flyOn && PtInRect(&fr, p) != 0;

    if (g.flyOn && !g.dragging) {
        int hv = inFly ? TileAt(p, gFlyCat) : -1;
        if (hv != gHoverTile) {
            gHoverTile = hv;
            PaintFly(gFlyCat, gFlyGeom);
            surfFly.Present(gFlyGeom.flyX - ShadowPx(), gFlyGeom.flyY - ShadowPx());
        }
    } else if (!g.flyOn && gHoverTile != -1) {
        gHoverTile = -1;
    }

    if (down && !prevDown) {
        g.pressTick = GetTickCount();
        g.pressPending = true;
        g.dragStart = p;
        g.dragItem = -1;
        g.dragCat = -1;
        if (g.flyOn && inFly) {
            int idx = TileAt(p, gFlyCat);
            if (idx >= 0) { g.dragCat = gFlyCat; g.dragItem = idx; }
        }
    }

    if (g.pressPending && down && !g.dragging) {
        int dx = abs(p.x - g.dragStart.x), dy = abs(p.y - g.dragStart.y);
        if (dx > Scale(g.s, 5) || dy > Scale(g.s, 5)) g.pressPending = false;
        else if (GetTickCount() - g.pressTick > 420 && g.dragItem >= 0) {
            g.dragging = true;
            GhostShow(g.dragCat, g.dragItem);
            GhostMove(p);
        }
    }

    if (g.dragging) {
        GhostMove(p);
        if (!down) {
            g.dragging = false;
            ShowWindow(g.ghost, SW_HIDE);
            DropAt(p);
            g.dragItem = -1;
            g.dragCat = -1;
        }
        prevDown = down;
        return;
    }

    if (!down && prevDown && g.pressPending) {
        if (g.flyOn && inFly) {
            int idx = TileAt(p, gFlyCat);
            if (idx >= 0) Launch(g.cats[gFlyCat].items[idx].path);
        } else if (g.railOn && inRail) {
            int row = RowAt(p);
            if (row >= 0) ShowFly(row);
        }
        g.pressPending = false;
    }
    prevDown = down;

    if (inHot || inRail || inFly) {
        g.missTicks = 0;
        if (!g.railOn) ShowRail();
        if (inRail) {
            int row = RowAt(p);
            if (row != g.hoverRow) {
                g.hoverRow = row;
                PaintRail(gRailGeom);
                surfRail.Present(gRailGeom.railX - ShadowPx(), gRailGeom.railY - ShadowPx());
            }
            if (row >= 0 && row != g.curCat) ShowFly(row);
        }
    } else {
        if (++g.missTicks >= 3) { HideFly(); HideRail(); }
    }
}

// ---------------------------------------------------------------- procs
static LRESULT CALLBACK PanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK MsgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_TIMER: if (wp == kTimer) Tick(); return 0;
    case WM_HOTKEY: if (wp == 1) { if (g.searching) SearchHide(); else SearchShow(); } return 0;
    case WM_TRAY: if (lp == WM_RBUTTONUP || lp == WM_LBUTTONUP) TrayMenu(hwnd); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------- main
static void RegisterClasses(HINSTANCE hInst) {
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = PanelProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kWndClass;
    RegisterClassExW(&wc);

    wc.lpfnWndProc = SearchProc;
    wc.lpszClassName = kSearchClass;
    RegisterClassExW(&wc);

    wc.lpfnWndProc = MsgProc;
    wc.lpszClassName = L"SideOpenMsg";
    RegisterClassExW(&wc);
}

static HWND CreatePanel(HINSTANCE hInst, int w, int h) {
    return CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        kWndClass, L"SideOpen", WS_POPUP, 0, 0, w, h, nullptr, nullptr, hInst, nullptr);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    GdiplusStartupInput gsi;
    ULONG_PTR token = 0;
    GdiplusStartup(&token, &gsi, nullptr);

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"SideOpen.SingleInstance");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    SaveConfigDefault();
    LoadConfig(g.cfg);

    g.dpi = GetDpiForSystem();
    g.s = g.dpi / 96.0;
    g.scrW = GetSystemMetrics(SM_CXSCREEN);
    g.scrH = GetSystemMetrics(SM_CYSCREEN);

    gFonts.Init(g.s);
    LoadCategories();
    ComputeColumns();
    gRailGeom = ComputeRailGeom();

    RegisterClasses(hInst);

    HWND msgw = CreateWindowExW(0, L"SideOpenMsg", L"SideOpenMsg", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInst, nullptr);
    g.msgw = msgw;

    g.rail = CreatePanel(hInst, gRailGeom.railW, gRailGeom.railH);
    g.fly = CreatePanel(hInst, gRailGeom.railW, gRailGeom.railH);
    g.ghost = CreatePanel(hInst, 100, 100);
    surfRail.hwnd = g.rail;
    surfFly.hwnd = g.fly;
    surfGhost.hwnd = g.ghost;

    gSearch = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kSearchClass, L"SideOpenSearch",
        WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, hInst, nullptr);
    surfSearch.hwnd = gSearch;

    TrayAdd(msgw);
    RegisterHotKey(msgw, 1, MOD_CONTROL | MOD_ALT, VK_SPACE);
    SetTimer(msgw, kTimer, 60, nullptr);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool forceShow = false;
    for (int i = 1; i < argc; i++) {
        if (_wcsicmp(argv[i], L"/show") == 0) forceShow = true;
        if (_wcsicmp(argv[i], L"/pin") == 0) gPin = true;
    }
    if (argv) LocalFree(argv);

    if (forceShow && !g.cats.empty()) {
        ShowRail();
        ShowFly(0);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Shell_NotifyIconW(NIM_DELETE, &gNid);
    gFonts.Free();
    GdiplusShutdown(token);
    return 0;
}
