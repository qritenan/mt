// BigBigWon Armor X Pro.
//  On its 2.4G dongle the controller poses as a wired Xbox pad (045E:0B12), so Windows only ever
//  reports "Full". It also keeps a Bluetooth LE link open for the phone app, and that link carries
//  the real level in the standard Battery Level characteristic (0x2A19). The controller is not
//  paired with Windows: pairing would add a second gamepad in games and lock out the phone.
//  A background advertisement watcher (restarted when Bluetooth comes back) remembers the
//  controller's address; each poll connects, reads one byte and closes, so the phone can still
//  connect between polls.
#include "drivers.h"
#include "armorx_trend.h"

#include <roapi.h>
#include <robuffer.h>
#include <winstring.h>

#include <atomic>

#include <windows.devices.bluetooth.h>
#include <windows.devices.bluetooth.advertisement.h>
#include <windows.devices.bluetooth.genericattributeprofile.h>
#include <windows.storage.streams.h>

int g_armorxFirstWaitMs = 0;
void (*g_armorxHeard)() = nullptr;

namespace armorx_abi {
using namespace ABI::Windows::Devices::Bluetooth;
using namespace ABI::Windows::Devices::Bluetooth::GenericAttributeProfile;
using ABI::Windows::Foundation::IAsyncOperation;

// MinGW's headers stop at IBluetoothLEDevice; the uuid-filtered GATT lookup lives on version 3.
struct IBluetoothLEDevice3 : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_DeviceAccessInformation(IInspectable** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE RequestAccessAsync(IInspectable** op) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetGattServicesAsync(IAsyncOperation<GattDeviceServicesResult*>** op) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetGattServicesWithCacheModeAsync(BluetoothCacheMode mode,
                                                                        IAsyncOperation<GattDeviceServicesResult*>** op) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetGattServicesForUuidAsync(GUID uuid, IAsyncOperation<GattDeviceServicesResult*>** op) = 0;
};
}  // namespace armorx_abi
__CRT_UUID_DECL(armorx_abi::IBluetoothLEDevice3, 0xaee9e493, 0x44ac, 0x40dc, 0xaf, 0x33, 0xb2, 0xc1, 0x3c, 0x01, 0xca, 0x46)

namespace {

using namespace ABI::Windows::Devices::Bluetooth;
using namespace ABI::Windows::Devices::Bluetooth::Advertisement;
using namespace ABI::Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Foundation::Collections;
using armorx_abi::IBluetoothLEDevice3;

constexpr GUID kBatteryService = { 0x0000180F, 0x0000, 0x1000, { 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB } };
constexpr GUID kBatteryLevel = { 0x00002A19, 0x0000, 0x1000, { 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB } };
// Above the usual 1 s: the controller's Bluetooth side is weak (about -100 dBm), and connect plus
// service discovery often takes 2-3 s.
constexpr DWORD kReadBudgetMs = 4000;

std::atomic<UINT64> g_address{ 0 };

template <class T>
void Release(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

void Close(IUnknown* p) {
    IClosable* c = nullptr;
    if (p && SUCCEEDED(p->QueryInterface(__uuidof(IClosable), (void**)&c)) && c) {
        c->Close();
        c->Release();
    }
}

std::wstring FromHString(HSTRING h) {
    UINT32 len = 0;
    const wchar_t* p = WindowsGetStringRawBuffer(h, &len);
    return p ? std::wstring(p, len) : std::wstring();
}

using ReceivedHandler = ITypedEventHandler<BluetoothLEAdvertisementWatcher*, BluetoothLEAdvertisementReceivedEventArgs*>;

struct OnReceived final : ReceivedHandler {
    std::atomic<ULONG> refs{ 1 };
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IAgileObject) || iid == __uuidof(ReceivedHandler)) {
            *out = static_cast<ReceivedHandler*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG n = --refs;
        if (!n) delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE Invoke(IBluetoothLEAdvertisementWatcher*, IBluetoothLEAdvertisementReceivedEventArgs* args) override {
        IBluetoothLEAdvertisement* adv = nullptr;
        HSTRING name = nullptr;
        if (SUCCEEDED(args->get_Advertisement(&adv)) && adv && SUCCEEDED(adv->get_LocalName(&name)) && name) {
            std::wstring n = FromHString(name);
            UINT64 addr = 0;
            if (n.find(L"ARMOR-X") != std::wstring::npos && SUCCEEDED(args->get_BluetoothAddress(&addr)) && addr)
                if (!g_address.exchange(addr) && g_armorxHeard) g_armorxHeard();  // first sighting: poll now
            WindowsDeleteString(name);
        }
        if (adv) adv->Release();
        return S_OK;
    }
};

// Keeps the watcher running for the life of the process. It aborts when Bluetooth is off or the
// radio resets (sleep, toggling it), so every poll restarts it if needed. Returns true while scanning.
bool EnsureScanning() {
    static IBluetoothLEAdvertisementWatcher* w = [] {
        IBluetoothLEAdvertisementWatcher* r = nullptr;
        RoInitialize(RO_INIT_MULTITHREADED);
        const wchar_t* cls = L"Windows.Devices.Bluetooth.Advertisement.BluetoothLEAdvertisementWatcher";
        HSTRING name = nullptr;
        IInspectable* inst = nullptr;
        if (FAILED(WindowsCreateString(cls, (UINT32)wcslen(cls), &name))) return r;
        RoActivateInstance(name, &inst);
        WindowsDeleteString(name);
        if (inst && SUCCEEDED(inst->QueryInterface(__uuidof(IBluetoothLEAdvertisementWatcher), (void**)&r)) && r) {
            r->put_ScanningMode(BluetoothLEScanningMode_Active);  // the name arrives in the scan response
            auto* h = new OnReceived;
            EventRegistrationToken tok;
            r->add_Received(h, &tok);
            h->Release();
        }
        Release(inst);
        return r;  // deliberately never released
    }();
    if (!w) return false;
    BluetoothLEAdvertisementWatcherStatus st = BluetoothLEAdvertisementWatcherStatus_Aborted;
    w->get_Status(&st);
    if (st == BluetoothLEAdvertisementWatcherStatus_Started) return true;
    if (st == BluetoothLEAdvertisementWatcherStatus_Stopping) return false;  // try again next poll
    return SUCCEEDED(w->Start());
}

// Waits for an async operation and hands back its result. Cancels it when the deadline passes.
template <class Op, class Res>
bool Await(Op* op, ULONGLONG deadline, Res** result) {
    if (!op) return false;
    IAsyncInfo* info = nullptr;
    if (FAILED(op->QueryInterface(__uuidof(IAsyncInfo), (void**)&info)) || !info) return false;
    AsyncStatus st = AsyncStatus::Started;
    while (SUCCEEDED(info->get_Status(&st)) && st == AsyncStatus::Started && GetTickCount64() < deadline) Sleep(20);
    if (st == AsyncStatus::Started) info->Cancel();
    info->Release();
    return st == AsyncStatus::Completed && SUCCEEDED(op->GetResults(result)) && *result;
}

// One connect / read / close. Returns 0..100, or -1.
int ReadLevel(UINT64 address) {
    ULONGLONG deadline = GetTickCount64() + kReadBudgetMs;
    static IBluetoothLEDeviceStatics* statics = [] {
        IBluetoothLEDeviceStatics* s = nullptr;
        const wchar_t* cls = L"Windows.Devices.Bluetooth.BluetoothLEDevice";
        HSTRING name = nullptr;
        if (SUCCEEDED(WindowsCreateString(cls, (UINT32)wcslen(cls), &name))) {
            RoGetActivationFactory(name, __uuidof(IBluetoothLEDeviceStatics), (void**)&s);
            WindowsDeleteString(name);
        }
        return s;
    }();
    if (!statics) return -1;

    int level = -1;
    IAsyncOperation<BluetoothLEDevice*>* devOp = nullptr;
    IBluetoothLEDevice* dev = nullptr;
    IBluetoothLEDevice3* dev3 = nullptr;
    IAsyncOperation<GattDeviceServicesResult*>* svcOp = nullptr;
    IGattDeviceServicesResult* svcRes = nullptr;
    IVectorView<GattDeviceService*>* svcs = nullptr;
    IGattDeviceService* svc = nullptr;
    IGattDeviceService3* svc3 = nullptr;
    IAsyncOperation<GattCharacteristicsResult*>* chOp = nullptr;
    IGattCharacteristicsResult* chRes = nullptr;
    IVectorView<GattCharacteristic*>* chars = nullptr;
    IGattCharacteristic* ch = nullptr;
    IAsyncOperation<GattReadResult*>* readOp = nullptr;
    IGattReadResult* read = nullptr;
    ABI::Windows::Storage::Streams::IBuffer* buf = nullptr;
    Windows::Storage::Streams::IBufferByteAccess* bytes = nullptr;
    UINT32 n = 0, len = 0;
    BYTE* data = nullptr;
    GattCommunicationStatus gs = GattCommunicationStatus_Unreachable;

    if (FAILED(statics->FromBluetoothAddressAsync(address, &devOp)) || !Await(devOp, deadline, &dev)) goto done;
    if (FAILED(dev->QueryInterface(__uuidof(IBluetoothLEDevice3), (void**)&dev3)) || !dev3) goto done;
    if (FAILED(dev3->GetGattServicesForUuidAsync(kBatteryService, &svcOp)) || !Await(svcOp, deadline, &svcRes)) goto done;
    if (FAILED(svcRes->get_Status(&gs)) || gs != GattCommunicationStatus_Success) goto done;
    if (FAILED(svcRes->get_Services(&svcs)) || !svcs || FAILED(svcs->get_Size(&n)) || !n) goto done;
    if (FAILED(svcs->GetAt(0, &svc)) || !svc) goto done;
    if (FAILED(svc->QueryInterface(__uuidof(IGattDeviceService3), (void**)&svc3)) || !svc3) goto done;
    if (FAILED(svc3->GetCharacteristicsForUuidWithCacheModeAsync(kBatteryLevel, BluetoothCacheMode_Uncached, &chOp)) ||
        !Await(chOp, deadline, &chRes))
        goto done;
    if (FAILED(chRes->get_Status(&gs)) || gs != GattCommunicationStatus_Success) goto done;
    if (FAILED(chRes->get_Characteristics(&chars)) || !chars || FAILED(chars->get_Size(&n)) || !n) goto done;
    if (FAILED(chars->GetAt(0, &ch)) || !ch) goto done;
    if (FAILED(ch->ReadValueWithCacheModeAsync(BluetoothCacheMode_Uncached, &readOp)) || !Await(readOp, deadline, &read)) goto done;
    if (FAILED(read->get_Status(&gs)) || gs != GattCommunicationStatus_Success) goto done;
    if (FAILED(read->get_Value(&buf)) || !buf || FAILED(buf->get_Length(&len)) || !len) goto done;
    if (SUCCEEDED(buf->QueryInterface(__uuidof(Windows::Storage::Streams::IBufferByteAccess), (void**)&bytes)) && bytes &&
        SUCCEEDED(bytes->Buffer(&data)) && data && data[0] <= 100)
        level = data[0];

done:
    Release(bytes);
    Release(buf);
    Release(read);
    Release(readOp);
    Release(ch);
    Release(chars);
    Release(chRes);
    Release(chOp);
    Release(svc3);
    Close(svc);  // closing the service and device drops the link so the phone can connect
    Release(svc);
    Release(svcs);
    Release(svcRes);
    Release(svcOp);
    Release(dev3);
    Close(dev);
    Release(dev);
    Release(devOp);
    return level;
}

}  // namespace

void PollArmorX(PollContext& ctx) {
    static bool firstPoll = true;
    if (EnsureScanning() && firstPoll)
        for (int waited = 0; !g_address && waited < g_armorxFirstWaitMs; waited += 100) Sleep(100);
    firstPoll = false;
    UINT64 address = g_address;
    if (!address) return;
    // The dongle's "Full" is never real. 0B12 is also a real Xbox Series pad, so only hide it
    // once an Armor X has been heard.
    ctx.Claim(0x045E, 0x0B12);
    DeviceStatus s;
    s.key = L"armorx";  // one entry even if the controller's address changes
    s.name = L"Armor X Pro";
    s.via = L"Bluetooth";
    s.kind = DeviceKind::Gamepad;
    s.graceSec = 1800;  // its Bluetooth side is very weak (about -100 dBm), so reads drop out often
    int raw = ReadLevel(address);
    if (raw < 0) return;
    static ArmorX::Trend trend;  // the controller sends no charging flag; guess it from the trend
    s.percent = trend.Add(raw, s.charging);
    ctx.out.push_back(s);
}
