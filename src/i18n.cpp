#include "i18n.h"

#include <windows.h>

#include <cwchar>

namespace {

struct Entry { const wchar_t* en; const wchar_t* tr; };

// Turkish. A second translation would need a language setting with more than two values.
const Entry kTurkish[] = {
    // Device status (tray tooltip, menu, taskbar tooltip).
    { L"off / not connected", L"kapal\u0131 / ba\u011fl\u0131 de\u011fil" },
    { L"% (last known)", L"% (son bilinen)" },
    { L"unknown", L"bilinmiyor" },
    { L", charging", L", \u015farj oluyor" },
    { L", asleep", L", uykuda" },
    { L"No devices with battery info found", L"Pil bilgisi veren cihaz bulunamad\u0131" },
    { L"Low battery", L"D\u00fc\u015f\u00fck pil" },
    { L"%s is at %d%%", L"%s: %d%% kald\u0131" },
    // Tray menu.
    { L"Point at a device to show, hide or move it", L"G\u00f6stermek, gizlemek ya da ta\u015f\u0131mak i\u00e7in cihaz\u0131n \u00fczerine gelin" },
    { L"Show on the taskbar", L"G\u00f6rev \u00e7ubu\u011funda g\u00f6ster" },
    { L"Show in the tray", L"Bildirim alan\u0131nda g\u00f6ster" },
    { L"Move left", L"Sola ta\u015f\u0131" },
    { L"Move right", L"Sa\u011fa ta\u015f\u0131" },
    { L"Move up", L"Yukar\u0131 ta\u015f\u0131" },
    { L"Move down", L"A\u015fa\u011f\u0131 ta\u015f\u0131" },
    { L"Hide last known / asleep devices", L"Son bilinen / uykudaki cihazlar\u0131 gizle" },
    { L"Show on the taskbar (icon + percentage)", L"G\u00f6rev \u00e7ubu\u011funda g\u00f6ster (simge + y\u00fczde)" },
    { L"Show as tray icons", L"Bildirim alan\u0131nda simge olarak g\u00f6ster" },
    { L"Background behind taskbar text (for transparent taskbars)", L"G\u00f6rev \u00e7ubu\u011fu yaz\u0131s\u0131n\u0131n arkas\u0131na zemin (saydam g\u00f6rev \u00e7ubuklar\u0131 i\u00e7in)" },
    { L"Refresh now", L"\u015eimdi yenile" },
    { L"Start with Windows", L"Windows ile ba\u015flat" },
    { L"Language", L"Dil" },
    { L"Request support for a device...", L"Bir cihaz i\u00e7in destek iste..." },
    { L"Support the project...", L"Projeyi destekle..." },
    { L"Exit", L"\u00c7\u0131k\u0131\u015f" },
    // Battery levels of devices that only report a coarse level.
    { L"Full", L"Dolu" },
    { L"Good", L"\u0130yi" },
    { L"Medium", L"Orta" },
    { L"Low", L"D\u00fc\u015f\u00fck" },
    { L"Critical", L"Kritik" },
    { L"Empty", L"Bo\u015f" },
    // How a device is connected (DeviceStatus::via). Product names (Bluetooth, Xbox, HeadsetControl ...) stay as they are.
    { L"USB cable", L"USB kablosu" },
    { L"2.4 GHz receiver", L"2.4 GHz al\u0131c\u0131" },
    { L"2.4 GHz receiver / USB", L"2.4 GHz al\u0131c\u0131 / USB" },
    { L"2.4 GHz receiver or USB", L"2.4 GHz al\u0131c\u0131 ya da USB" },
    { L"2.4 GHz dongle", L"2.4 GHz al\u0131c\u0131" },
    { L"2.4 GHz dongle or USB", L"2.4 GHz al\u0131c\u0131 ya da USB" },
    { L"Logitech receiver", L"Logitech al\u0131c\u0131" },
    { L"Razer receiver", L"Razer al\u0131c\u0131" },
    { L"Corsair receiver", L"Corsair al\u0131c\u0131" },
    { L"Wireless adapter", L"Kablosuz adapt\u00f6r" },
    { L"UWB base station", L"UWB baz istasyonu" },
    { L"USB base station", L"USB baz istasyonu" },
};

bool g_turkish = false;

}  // namespace

bool WindowsIsTurkish() { return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_TURKISH; }
void SetUiTurkish(bool on) { g_turkish = on; }
bool UiTurkish() { return g_turkish; }

const wchar_t* T(const wchar_t* en) {
    if (!g_turkish) return en;
    for (const auto& e : kTurkish)
        if (!wcscmp(e.en, en)) return e.tr;
    return en;
}
