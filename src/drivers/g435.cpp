// Logitech G435 LIGHTSPEED headset on the original 046D:0ACB Realtek receiver. It does not
// speak HID++. The PA bridge is feature report 6 of usage page FFC0. Entering the bridge mutes
// USB audio for ~0.8 s. Enter, PA, and exit are HidD_SetFeature (report 6, 64 bytes), matching
// github.com/fr0styJS/g435-lightspeed-battery (MIT) and WirelessBattery PR #93. PR #72 used
// output reports; this path does not.
//
// The newer 046D:0ADF dongle is a different transport and is read by PollLogitechG435.
//
// This path only sends the battery-read sequence; nothing is written to flash. A successful
// read is cached in memory for 45 minutes. A failed read (headset off) is retried after 5
// minutes and the previous value is shown as asleep. The bootloader is ALWAYS exited (RAII)
// after a successful enter — leaving it open wedges USB. No registry writes.
#include "drivers.h"
#include "g435_protocol.h"

#include <algorithm>
#include <atomic>

namespace {

constexpr USHORT kVid = 0x046D;
constexpr USHORT kPidRealtek = 0x0ACB;
constexpr ULONGLONG kReadEveryMs = 45ull * 60 * 1000;
constexpr ULONGLONG kRetryEveryMs = 5ull * 60 * 1000;
constexpr ULONGLONG kStartupDeferMs = 20ull * 1000;  // no USB bridge traffic until the desktop settles
constexpr BYTE kReport = 0x06;

std::atomic<bool> g_forceRead{ false };
const ULONGLONG g_processStart = GetTickCount64();

struct Cache {
    bool haveReading = false;
    bool lastReadOk = false;    // false -> 5 min retry and show the previous value as asleep
    int lastMv = 0;
    int lastPct = -1;           // sticky display % so ADC noise does not flicker the tray
    ULONGLONG lastAttempt = 0;  // cooldown; never cleared on brief USB dropouts
};
Cache g_cache;

// ---- Realtek (046D:0ACB) — feature report 6, same bytes as fr0styJS / #93 --------------------

bool RealtekSendPa(HidDev& d, std::initializer_list<BYTE> frame) {
    std::vector<BYTE> msg = { kReport, 0x04, 0x03, 0x00, (BYTE)frame.size() };
    msg.insert(msg.end(), frame.begin(), frame.end());
    return d.SetFeature(msg);  // padded to the 64-byte feature report
}

// RAII: always leave the bootloader after a successful enter, even when a query fails.
struct BootloaderGuard {
    HidDev& d;
    bool active = false;
    ~BootloaderGuard() {
        if (!active) return;
        d.SetFeature({ kReport, 0x08, 0x03, 0x00, 0x00, 0x00, 0x00 });
        Sleep(100);
    }
};

int ReadRealtekMv(HidDev& d) {
    // Enter is a feature report. A failed SetFeature did not enter, so there is nothing to exit.
    if (!d.SetFeature({ kReport, 0x01, 0x03, 0x00, 0x00, 0x00, 0x00 })) return 0;
    BootloaderGuard guard{ d, true };
    Sleep(250);

    std::vector<BYTE> r;
    d.GetFeature(kReport, r);  // one drain, same as fr0styJS / #93

    if (!RealtekSendPa(d, { 0x50, 0x41, 0x0E, 0x00, 0x02, 0xE1, 0x01 })) return 0;  // remote
    Sleep(150);

    // Several samples while the bridge is open; median beats a single noisy ADC hit.
    // The window matches #93 (800 ms, 30 ms between polls).
    std::vector<int> samples;
    if (RealtekSendPa(d, { 0x50, 0x41, 0x06, 0x00, 0x01, 0x00, 0x31 })) {
        ULONGLONG end = GetTickCount64() + 800;
        while (samples.size() < 5 && GetTickCount64() < end) {
            if (d.GetFeature(kReport, r)) {
                int parsed = 0;
                if (G435::ParseRealtekBattery(r.data(), r.size(), parsed) && G435::IsPlausible(parsed))
                    samples.push_back(parsed);
            }
            if (samples.size() >= 5 || GetTickCount64() >= end) break;
            DWORD nap = (DWORD)(end - GetTickCount64());
            Sleep(nap > 30 ? 30 : nap);
        }
    }

    RealtekSendPa(d, { 0x50, 0x41, 0x0E, 0x00, 0x02, 0xE1, 0x00 });  // local
    Sleep(50);
    if (samples.empty()) return 0;
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];  // guard exits bootloader
}

}  // namespace

void G435RequestRead() { g_forceRead = true; }

void PollG435(PollContext& ctx) {
    // Feature report 6 only — #93's match. The OUT pipe is not used.
    // 046D:0ADF is PollLogitechG435; this driver does not open it.
    if (ctx.IsClaimed(kVid, kPidRealtek)) return;
    auto realtek = ctx.Find(kVid, kPidRealtek, [](const HidColl& c) {
        return c.page() == 0xFFC0 && c.featLen() == 64;
    });
    if (realtek.empty()) return;  // keep cooldown across brief USB dropouts (bootloader re-enumerate)

    ctx.Claim(kVid, kPidRealtek);

    ULONGLONG now = GetTickCount64();
    // A failed read must not keep the 45-minute schedule just because an older value exists.
    ULONGLONG wait = (g_cache.haveReading && g_cache.lastReadOk) ? kReadEveryMs : kRetryEveryMs;
    bool force = g_forceRead.exchange(false);
    bool startupHold = !force && g_cache.lastAttempt == 0 && now - g_processStart < kStartupDeferMs;
    bool due = !startupHold && (force || g_cache.lastAttempt == 0 || now - g_cache.lastAttempt >= wait);
    if (due) {
        HidDev d;
        if (d.Open(*realtek[0])) {
            // Record the attempt BEFORE the read so a DEVICECHANGE mid-read cannot start another.
            g_cache.lastAttempt = now;
            int mv = ReadRealtekMv(d);
            if (mv) {
                g_cache.haveReading = true;
                g_cache.lastReadOk = true;
                g_cache.lastMv = mv;
                g_cache.lastPct = G435::StickyPercent(mv, g_cache.lastPct);
            } else {
                g_cache.lastReadOk = false;
            }
        }
    }
    if (!g_cache.haveReading) return;

    DeviceStatus s;
    s.key = L"g435:" + Hex4(kPidRealtek);
    s.name = L"Logitech G435";
    s.via = L"LIGHTSPEED receiver";
    s.kind = DeviceKind::Headset;
    s.millivolts = g_cache.lastMv;
    if (g_cache.lastPct < 0) g_cache.lastPct = G435::StickyPercent(g_cache.lastMv, -1);
    s.percent = g_cache.lastPct;
    s.graceSec = 1800;  // receiver unplugged: UI keeps this value, dimmed, for 30 minutes
    s.asleep = startupHold || !g_cache.lastReadOk;
    ctx.out.push_back(s);
}
