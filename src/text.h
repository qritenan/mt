#pragma once
// Text rasterizer shared by the taskbar widget and the tray icons: turns a string into a
// grayscale coverage mask that the callers tint and blend themselves.
#include <windows.h>
#include <string>
#include <vector>

// Per-pixel coverage (0..255) of a string, cropped to its ink bounds. Empty when nothing was drawn.
struct TextMask {
    int w = 0, h = 0;
    std::vector<BYTE> a;
};

// Renders `s` at `px` pixels in `face`, or in `fallback` when that face is not installed.
// `weight` is a GDI weight (FW_NORMAL, FW_BOLD); `spacing` adds that many pixels after each glyph.
// Glyphs are rasterised by DirectWrite with the same grayscale anti-aliasing the taskbar uses
// for its own text (GitHub issue #41); GDI is used only when DirectWrite is unavailable.
TextMask RenderText(const std::wstring& s, const wchar_t* face, const wchar_t* fallback, int px, int weight,
                    int spacing = 0);
