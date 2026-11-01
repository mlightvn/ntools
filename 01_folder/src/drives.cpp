// External drives: detection (USB / removable) and "safely remove" (eject) via Config Manager.
#include "app.h"
#include <winioctl.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <thread>

namespace {

const GUID kDiskInterface = {0x53f56307, 0xb6bf, 0x11d0, {0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b}};  // GUID_DEVINTERFACE_DISK

HANDLE OpenVolume(const wstring& root) {
    wstring dev = L"\\\\.\\" + root.substr(0, 2);
    return CreateFileW(dev.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
}

bool DeviceNumber(HANDLE h, STORAGE_DEVICE_NUMBER& n) {
    DWORD got = 0;
    return DeviceIoControl(h, IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &n, sizeof(n), &got, nullptr) != 0;
}

// the disk device (devnode) that holds the volume
DEVINST DiskDevInst(const STORAGE_DEVICE_NUMBER& want) {
    HDEVINFO set = SetupDiGetClassDevsW(&kDiskInterface, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return 0;
    DEVINST found = 0;
    SP_DEVICE_INTERFACE_DATA ifd{sizeof(ifd)};
    for (DWORD i = 0; !found && SetupDiEnumDeviceInterfaces(set, nullptr, &kDiskInterface, i, &ifd); i++) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &ifd, nullptr, 0, &need, nullptr);
        vector<BYTE> buf(need);
        auto detail = (SP_DEVICE_INTERFACE_DETAIL_DATA_W*)buf.data();
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        SP_DEVINFO_DATA dev{sizeof(dev)};
        if (!SetupDiGetDeviceInterfaceDetailW(set, &ifd, detail, need, nullptr, &dev)) continue;
        HANDLE h = CreateFileW(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;
        STORAGE_DEVICE_NUMBER n{};
        if (DeviceNumber(h, n) && n.DeviceNumber == want.DeviceNumber && n.DeviceType == want.DeviceType) found = dev.DevInst;
        CloseHandle(h);
    }
    SetupDiDestroyDeviceInfoList(set);
    return found;
}

struct EjectResult { wstring root, error; };

}  // namespace

bool IsExternalDrive(const wstring& root) {
    if (root.size() < 2) return false;
    UINT t = GetDriveTypeW(root.substr(0, 2).append(L"\\").c_str());
    if (t == DRIVE_REMOVABLE) return true;
    if (t != DRIVE_FIXED) return false;
    HANDLE h = OpenVolume(root);  // fixed but connected by USB / FireWire / SD (external HDD/SSD)
    if (h == INVALID_HANDLE_VALUE) return false;
    STORAGE_PROPERTY_QUERY q{StorageDeviceProperty, PropertyStandardQuery};
    BYTE buf[1024] = {};
    DWORD got = 0;
    bool ext = false;
    if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q), buf, sizeof(buf), &got, nullptr)) {
        auto d = (STORAGE_DEVICE_DESCRIPTOR*)buf;
        ext = d->BusType == BusTypeUsb || d->BusType == BusType1394 || d->BusType == BusTypeSd || d->BusType == BusTypeMmc;
    }
    CloseHandle(h);
    return ext;
}

// Runs in the background; result comes back to g_main as WM_APP_EJECTED (lParam = EjectResult*).
void EjectDrive(const wstring& root) {
    ReleaseDrive(root);  // panes leave the drive so our own folder watchers don't block the removal
    std::thread([root] {
        auto r = new EjectResult{root};
        Sleep(300);  // let watcher threads close their handles
        HANDLE h = OpenVolume(root);
        STORAGE_DEVICE_NUMBER n{};
        bool ok = h != INVALID_HANDLE_VALUE && DeviceNumber(h, n);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        DEVINST disk = ok ? DiskDevInst(n) : 0;
        if (!disk) {
            r->error = L"Windows did not report a removable device for this drive.";
        } else {
            DEVINST parent = 0;
            CM_Get_Parent(&parent, disk, 0);  // the USB device that owns the disk
            for (int attempt = 0; attempt < 3; attempt++) {
                PNP_VETO_TYPE veto = PNP_VetoTypeUnknown;
                wchar_t vetoName[MAX_PATH] = L"";
                CONFIGRET cr = CM_Request_Device_EjectW(parent ? parent : disk, &veto, vetoName, MAX_PATH, 0);
                if (cr == CR_SUCCESS && veto == PNP_VetoTypeUnknown) { r->error.clear(); break; }
                r->error = vetoName[0] ? L"The drive is in use by:\n" + wstring(vetoName) : L"The drive is in use (veto type " + std::to_wstring(veto) + L").";
                Sleep(500);
            }
        }
        if (!PostMessageW(g_main, WM_APP_EJECTED, 0, (LPARAM)r)) delete r;
    }).detach();
}

void OnEjected(LPARAM l) {
    std::unique_ptr<EjectResult> r((EjectResult*)l);
    wstring d = r->root.substr(0, 2);
    if (r->error.empty()) {
        wstring msg = L"It is now safe to remove drive " + d + L".";
        MessageBoxW(g_main, msg.c_str(), L"Eject", MB_ICONINFORMATION);
    } else {
        wstring msg = L"Could not eject drive " + d + L".\n\n" + r->error + L"\n\nClose the programs or files using it and try again.";
        MessageBoxW(g_main, msg.c_str(), L"Eject", MB_ICONWARNING);
    }
    SidebarRefreshDrives();
}
