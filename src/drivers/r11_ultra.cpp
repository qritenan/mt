#include "drivers.h"

namespace {

constexpr USHORT kVid = 0x3554;
const USHORT kPids[] = { 0xFB44, 0xFB35, 0xF517, 0xFB43, 0xF516, 0xF515 };

bool IsWireless(USHORT pid) {
    return pid == 0xFB44 || pid == 0xFB35 || pid == 0xF517;
}

bool ParseBattery(const std::vector<BYTE>& r, DeviceStatus& s) {
    if (r.size() < 17 || r[0] != 0x08 || r[1] != 0x04 || r[2] != 0 || r[6] > 100)
        return false;
    unsigned checksum = 0;
    for (size_t i = 0; i < 17; ++i) checksum += r[i];
    if ((checksum & 0xFF) != 0x55) return false;
    s.percent = r[6];
    s.charging = r[7] != 0 && s.percent < 100;
    int voltage = (int(r[8]) << 8) | r[9];
    if (voltage >= 2500 && voltage <= 5000) s.millivolts = voltage;
    return true;
}

bool ReadBattery(HidDev& d, DeviceStatus& s) {
    std::vector<BYTE> request(17, 0);
    request[0] = 0x08;
    request[1] = 0x04;
    request[16] = 0x49;
    d.Flush();
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (!d.Write(request)) return false;
        ULONGLONG end = GetTickCount64() + 900;
        while (GetTickCount64() < end) {
            std::vector<BYTE> response;
            int remaining = (int)(end - GetTickCount64());
            if (remaining <= 0 || d.Read(response, remaining) < 0) break;
            if (ParseBattery(response, s)) return true;
        }
    }
    return false;
}

}

void PollR11Ultra(PollContext& ctx) {
    for (USHORT pid : kPids) {
        if (ctx.IsClaimed(kVid, pid)) continue;
        for (const HidColl* c : ctx.Find(kVid, pid, [](const HidColl& c) {
                 return c.page() == 0xFF02 && c.usage() == 0x0002 && c.inLen() >= 17 && c.outLen() >= 17;
             })) {
            HidDev d;
            if (!d.Open(*c)) continue;
            DeviceStatus s;
            if (!ReadBattery(d, s)) continue;
            s.key = L"attackshark:r11ultra";
            s.name = L"Attack Shark R11 Ultra";
            s.kind = DeviceKind::Mouse;
            s.via = IsWireless(pid) ? L"2.4 GHz receiver" : L"USB cable";
            if (IsWireless(pid)) s.graceSec = 1800;
            ctx.Add(s, kVid, pid);
            for (USHORT sibling : kPids) ctx.Claim(kVid, sibling);
            return;
        }
    }
}
