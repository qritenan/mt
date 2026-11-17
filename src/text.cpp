#include "text.h"

#include <dwrite_3.h>
#include <algorithm>

namespace {

// Crops a W x H coverage buffer (one byte per pixel) to the pixels that have ink.
TextMask Crop(const BYTE* p, int W, int H) {
    TextMask m;
    int x0 = W, y0 = H, x1 = -1, y1 = -1;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (p[y * W + x]) { x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
    if (x1 < 0) return m;
    m.w = x1 - x0 + 1;
    m.h = y1 - y0 + 1;
    m.a.resize((size_t)m.w * m.h);
    for (int y = 0; y < m.h; y++)
        for (int x = 0; x < m.w; x++) m.a[y * m.w + x] = p[(y0 + y) * W + x0 + x];
    return m;
}

// ---- DirectWrite ----------------------------------------------------------------------------
// dwrite.dll is loaded at run time, so the build needs no extra import library and the GDI path
// below keeps working on a system where DirectWrite cannot be created.

IDWriteFactory3* Factory() {
    static IDWriteFactory3* factory = [] {
        IDWriteFactory3* f = nullptr;
        HMODULE dll = LoadLibraryW(L"dwrite.dll");
        if (!dll) return f;
        using CreateFn = HRESULT(WINAPI*)(DWRITE_FACTORY_TYPE, REFIID, IUnknown**);
        auto create = reinterpret_cast<CreateFn>(GetProcAddress(dll, "DWriteCreateFactory"));
        if (!create || FAILED(create(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory3), reinterpret_cast<IUnknown**>(&f))))
            f = nullptr;
        return f;
    }();
    return factory;
}

// Font face for the first installed family of `face` / `fallback`, or null when neither exists.
IDWriteFontFace* FindFace(IDWriteFactory3* factory, const wchar_t* face, const wchar_t* fallback, int weight) {
    IDWriteFontCollection* fonts = nullptr;
    // IDWriteFactory3 overloads this with a typographic-family version; the legacy family names
    // ("Segoe UI Variable Text") live in the base interface's collection.
    if (FAILED(static_cast<IDWriteFactory*>(factory)->GetSystemFontCollection(&fonts, FALSE))) return nullptr;
    IDWriteFontFace* out = nullptr;
    for (const wchar_t* name : { face, fallback }) {
        UINT32 index = 0;
        BOOL exists = FALSE;
        if (FAILED(fonts->FindFamilyName(name, &index, &exists)) || !exists) continue;
        IDWriteFontFamily* family = nullptr;
        if (FAILED(fonts->GetFontFamily(index, &family))) continue;
        IDWriteFont* font = nullptr;
        if (SUCCEEDED(family->GetFirstMatchingFont((DWRITE_FONT_WEIGHT)weight, DWRITE_FONT_STRETCH_NORMAL,
                                                   DWRITE_FONT_STYLE_NORMAL, &font))) {
            font->CreateFontFace(&out);
            font->Release();
        }
        family->Release();
        if (out) break;
    }
    fonts->Release();
    return out;
}

bool RenderDirectWrite(const std::wstring& s, const wchar_t* face, const wchar_t* fallback, int px, int weight,
                       int spacing, TextMask& out) {
    IDWriteFactory3* factory = Factory();
    if (!factory) return false;
    IDWriteFontFace* fontFace = FindFace(factory, face, fallback, weight);
    if (!fontFace) return false;

    std::vector<UINT32> codepoints;
    for (size_t i = 0; i < s.size(); i++) {
        UINT32 c = s[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < s.size()) c = 0x10000 + ((c - 0xD800) << 10) + (s[++i] - 0xDC00);
        codepoints.push_back(c);
    }
    const UINT32 n = (UINT32)codepoints.size();
    std::vector<UINT16> glyphs(n);
    std::vector<DWRITE_GLYPH_METRICS> metrics(n);
    std::vector<FLOAT> advances(n);
    DWRITE_FONT_METRICS fm{};
    fontFace->GetMetrics(&fm);
    bool ok = n > 0 && SUCCEEDED(fontFace->GetGlyphIndices(codepoints.data(), n, glyphs.data()));
    // Glyph 0 is the font's "missing character" box. GDI would borrow the glyph from another
    // font in that case, so let it handle any string this font cannot show in full.
    for (UINT32 i = 0; ok && i < n; i++) ok = glyphs[i] != 0;
    ok = ok && SUCCEEDED(fontFace->GetDesignGlyphMetrics(glyphs.data(), n, metrics.data(), FALSE));
    for (UINT32 i = 0; ok && i < n; i++)
        advances[i] = (FLOAT)metrics[i].advanceWidth * px / fm.designUnitsPerEm + spacing;

    DWRITE_GLYPH_RUN run{};
    run.fontFace = fontFace;
    run.fontEmSize = (FLOAT)px;
    run.glyphCount = n;
    run.glyphIndices = glyphs.data();
    run.glyphAdvances = advances.data();

    // Natural symmetric grayscale is what the shell's own XAML text uses, so the digits sit next
    // to the clock without looking heavier or crisper than it.
    IDWriteGlyphRunAnalysis* analysis = nullptr;
    ok = ok && SUCCEEDED(factory->CreateGlyphRunAnalysis(&run, nullptr, DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC,
                                                         DWRITE_MEASURING_MODE_NATURAL, DWRITE_GRID_FIT_MODE_DEFAULT,
                                                         DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE, 0.f, 0.f, &analysis));
    RECT r{};
    ok = ok && SUCCEEDED(analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_ALIASED_1x1, &r));
    if (ok) {
        const int W = r.right - r.left, H = r.bottom - r.top;
        if (W > 0 && H > 0) {
            // With grayscale anti-aliasing the "aliased" texture type yields one coverage byte per pixel.
            std::vector<BYTE> cov((size_t)W * H);
            ok = SUCCEEDED(analysis->CreateAlphaTexture(DWRITE_TEXTURE_ALIASED_1x1, &r, cov.data(), (UINT32)cov.size()));
            if (ok) out = Crop(cov.data(), W, H);
        } else {
            out = TextMask{};  // e.g. a space: nothing to draw is a valid result
        }
    }
    if (analysis) analysis->Release();
    fontFace->Release();
    return ok;
}

// ---- GDI fallback ---------------------------------------------------------------------------

// Creates a font, falling back when the preferred face is not installed (GDI would silently substitute).
HFONT MakeFont(int px, int weight, const wchar_t* face, const wchar_t* fallback) {
    for (const wchar_t* f : { face, fallback }) {
        HFONT h = CreateFontW(-px, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              ANTIALIASED_QUALITY, DEFAULT_PITCH, f);
        HDC dc = CreateCompatibleDC(nullptr);
        HGDIOBJ o = SelectObject(dc, h);
        wchar_t got[LF_FACESIZE] = {};
        GetTextFaceW(dc, LF_FACESIZE, got);
        SelectObject(dc, o);
        DeleteDC(dc);
        if (!_wcsicmp(got, f) || f == fallback) return h;
        DeleteObject(h);
    }
    return nullptr;
}

// White-on-black into a scratch bitmap; the brightness of each pixel is its coverage.
TextMask RenderGdi(const std::wstring& s, const wchar_t* face, const wchar_t* fallback, int px, int weight, int spacing) {
    TextMask m;
    HFONT font = MakeFont(px, weight, face, fallback);
    if (!font) return m;
    HDC dc = CreateCompatibleDC(nullptr);
    HGDIOBJ of = SelectObject(dc, font);
    SetTextCharacterExtra(dc, spacing);
    SIZE sz;
    GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
    int W = sz.cx + 8, H = sz.cy + 8;
    BITMAPINFO bi{};
    bi.bmiHeader = { sizeof(BITMAPINFOHEADER), W, -H, 1, 32, BI_RGB };
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ ob = SelectObject(dc, bmp);
    memset(bits, 0, (size_t)W * H * 4);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    TextOutW(dc, 4, 4, s.c_str(), (int)s.size());
    GdiFlush();
    auto* p = static_cast<DWORD*>(bits);
    std::vector<BYTE> cov((size_t)W * H);
    for (int i = 0; i < W * H; i++) cov[i] = (BYTE)(p[i] & 0xFF);
    m = Crop(cov.data(), W, H);
    SelectObject(dc, ob);
    SelectObject(dc, of);
    DeleteObject(bmp);
    DeleteDC(dc);
    DeleteObject(font);
    return m;
}

}  // namespace

TextMask RenderText(const std::wstring& s, const wchar_t* face, const wchar_t* fallback, int px, int weight, int spacing) {
    TextMask m;
    if (RenderDirectWrite(s, face, fallback, px, weight, spacing, m)) return m;
    return RenderGdi(s, face, fallback, px, weight, spacing);
}
