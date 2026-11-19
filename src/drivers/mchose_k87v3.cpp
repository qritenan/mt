// MCHOSE K87 V3 (VID 3837, PID 1015): a tri-mode 87-key keyboard on its 2.4 GHz receiver.
//
// The receiver answers the same "modern" M HUB device-info frame as the K7 V2 Ultra+ mouse
// (see mchose_modern.cpp), but it exposes it on vendor collection FF80:0001 (64-byte input and
// output reports, report id 0x4D) instead of FF01:0001 - which is why the mouse driver's filter
// never matched this keyboard.
//   request: output report id 0x4D:
//            [4D][01][flags 01 = read][len 00][cmd lo][cmd hi][biz][seq][xor of bytes 2..7]
//            command 0x0900 = read device info, which carries the battery
//   reply:   input report id 0x4D, same header, then len bytes of data and a checksum:
//            [4D][01][flags][len][cmd lo][cmd hi][biz][seq][data ...][xor of bytes 2..7+len]
//            data[0..1] = vendor id (3837), data[2..3] = model id (0x2023 on this keyboard),
//            data[8..9] = firmware, data[10] = link status (2 = linked to the receiver),
//            data[11] = charge status (1 = charging), data[12] = battery percent,
//            data[13] = game mode
// The keyboard answers with len = 20 (mice answer 14), so the payload length is not fixed and the
// checksum is taken from the frame instead.
// Verified on real hardware on 2026-11-19 (3837:1015): 99 %, link 2, checksum byte matched.
// Like the other keyboard drivers the receiver is asked at most every few minutes: the keyboard
// sleeps on its own after 30 minutes idle and polling should not keep waking it.
#include "drivers.h"

#include <algorithm>

namespace {

constexpr USHORT kVid = 0x3837;
constexpr USHORT kPid = 0x1015;
constexpr BYTE kReport = 0x4D;
constexpr USHORT kCmdDeviceInfo = 0x0900;
constexpr size_t kData = 8;  // first byte of the data block in both the request and the reply
constexpr ULONGLONG kAskIntervalMs = 3 * 60 * 1000;

BYTE Xor(const std::vector<BYTE>& b, size_t from, size_t to)  // both ends inclusive
{
    BYTE x = 0;
    for (size_t i = from; i <= to && i < b.size(); i++) x ^= b[i];
    return x;
}

// Returns 1 for a reading, 0 for a valid frame that says the keyboard is not linked, -1 otherwise.
int ParseInfo(const std::vector<BYTE>& r, int& percent, bool& charging)
{
    if (r.size() < kData + 2 || r[0] != kReport) return -1;
    if ((USHORT)(r[4] | (r[5] << 8)) != kCmdDeviceInfo) return -1;
    const size_t len = r[3];
    if (kData + len >= r.size()) return -1;
    // Switched off, the receiver still answers with the header only and a single 0xFF in place of
    // the model id, so anything shorter than the device-info payload means "not linked".
    if (len < 14) return 0;
    if (r[kData + len] != Xor(r, 2, 7 + len)) return -1;
    if ((USHORT)(r[kData] | (r[kData + 1] << 8)) != kVid) return -1;
    if (r[kData + 10] == 0) return 0;  // not linked: keyboard off or asleep
    if (r[kData + 12] > 100) return -1;
    percent = r[kData + 12];
    charging = r[kData + 11] == 1;
    return 1;
}

std::wstring DisplayName(const HidColl& c)
{
    std::wstring name = HidProduct(c);
    const wchar_t* suffix = L" 2.4G";
    const size_t n = wcslen(suffix);
    if (name.size() > n && name.compare(name.size() - n, n, suffix) == 0) name.resize(name.size() - n);
    return name.empty() ? std::wstring(L"MCHOSE keyboard") : name;
}

}  // namespace

void PollMchoseK87V3(PollContext& ctx)
{
    if (ctx.IsClaimed(kVid, kPid)) return;
    static ULONGLONG lastAsk = 0;
    static int lastPercent = -1;
    static bool lastCharging = false;

    std::vector<const HidColl*> cs = ctx.Find(kVid, kPid, [](const HidColl& c) {
        return c.page() == 0xFF80 && c.outLen() >= 64 && c.inLen() >= 21;
    });
    if (cs.empty()) return;  // receiver not present

    const ULONGLONG now = GetTickCount64();
    const bool ask = !lastAsk || now - lastAsk >= kAskIntervalMs;
    int percent = -1;
    bool charging = false;
    if (ask) {
        lastAsk = now;
        // One absolute deadline for the whole collection: a receiver that stops answering must not
        // stall the poll (a missing device gets ~2.5 s at most, retries included).
        const ULONGLONG deadline = now + 2500;
        for (const HidColl* c : cs) {
            HidDev d;
            if (!d.Open(*c)) continue;
            d.Flush();
            for (int attempt = 0; attempt < 3 && percent < 0; attempt++) {
                if (attempt) {
                    if (GetTickCount64() + 150 >= deadline) break;
                    Sleep(150);
                }
                std::vector<BYTE> req(c->outLen(), 0);
                req[0] = kReport;
                req[1] = 0x01;
                req[2] = 0x01;  // read
                req[4] = (BYTE)(kCmdDeviceInfo & 0xFF);
                req[5] = (BYTE)(kCmdDeviceInfo >> 8);
                req[kData] = Xor(req, 2, 7);
                if (!d.Write(req)) continue;
                ULONGLONG end = std::min<ULONGLONG>(GetTickCount64() + 1000, deadline);
                std::vector<BYTE> r;
                while ((LONGLONG)end - (LONGLONG)GetTickCount64() > 0) {
                    int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
                    if (d.Read(r, left) < 0) break;
                    int st = ParseInfo(r, percent, charging);
                    if (st == 1) break;
                    if (st == 0) {  // receiver answered "not linked": keep the last value
                        percent = lastPercent;
                        charging = lastCharging;
                        break;
                    }
                }
            }
            if (percent >= 0) break;
        }
    }
    if (percent < 0 && !ask) {  // between asks: keep showing the previous reading
        percent = lastPercent;
        charging = lastCharging;
    }
    if (percent < 0 && lastPercent < 0) {
        // The receiver is there but the keyboard is off/asleep. Report it once so the widget does
        // not keep a phantom entry alive, without a percentage.
        DeviceStatus s;
        s.key = L"mchose:3837:1015";
        s.name = DisplayName(*cs.front());
        s.kind = DeviceKind::Keyboard;
        s.via = L"2.4 GHz receiver";
        s.asleep = true;
        s.graceSec = 1800;
        ctx.Add(s, kVid, kPid);
        return;
    }
    if (percent >= 0) {
        lastPercent = percent;
        lastCharging = charging;
    }

    DeviceStatus s;
    s.key = L"mchose:3837:1015";
    s.name = DisplayName(*cs.front());
    s.kind = DeviceKind::Keyboard;
    s.via = L"2.4 GHz receiver";
    s.graceSec = 1800;
    s.percent = Clamp100(percent);
    s.charging = charging;
    ctx.Add(s, kVid, kPid);
}
