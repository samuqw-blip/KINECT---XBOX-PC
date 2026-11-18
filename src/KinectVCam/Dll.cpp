// Entrada da DLL: fábrica COM, registro (regsvr32) e funções para ligar/desligar as câmeras virtuais.
#include "KinectVCam.h"
#include <strsafe.h>
#include <cstdio>

// {8A7D6C21-4F3B-4E9A-9C1D-2B5E7F0A1C01}
const CLSID CLSID_KinectVCamColor = { 0x8a7d6c21, 0x4f3b, 0x4e9a, { 0x9c, 0x1d, 0x2b, 0x5e, 0x7f, 0x0a, 0x1c, 0x01 } };
// {8A7D6C21-4F3B-4E9A-9C1D-2B5E7F0A1C02}
const CLSID CLSID_KinectVCamIR = { 0x8a7d6c21, 0x4f3b, 0x4e9a, { 0x9c, 0x1d, 0x2b, 0x5e, 0x7f, 0x0a, 0x1c, 0x02 } };
const IID IID_IKsControl_ = { 0x28f54685, 0x06fd, 0x11d2, { 0xb2, 0x7a, 0x00, 0xa0, 0xc9, 0x22, 0x31, 0x96 } };
const GUID PINNAME_VIDEO_CAPTURE_ = { 0xfb6c4281, 0x0353, 0x11d1, { 0x90, 0x5f, 0x00, 0x00, 0xc0, 0xcc, 0x16, 0xba } };
const GUID KSCAMERAPROFILE_Legacy_ = { 0xb4894d81, 0x62b7, 0x4eec, { 0x87, 0x40, 0x80, 0x65, 0x8c, 0x4a, 0x9d, 0x3e } };

std::atomic<long> g_objects{ 0 };
static std::atomic<long> g_locks{ 0 };
static HMODULE g_module = nullptr;

// Log de diagnóstico em C:\ProgramData\KinectVCam\vcam.log (o instalador dá permissão ao serviço).
void LogDbg(const wchar_t* fmt, ...)
{
    wchar_t msg[1024];
    va_list ap;
    va_start(ap, fmt);
    StringCchVPrintfW(msg, ARRAYSIZE(msg), fmt, ap);
    va_end(ap);
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t line[1200];
    StringCchPrintfW(line, ARRAYSIZE(line), L"%04u-%02u-%02u %02u:%02u:%02u.%03u [%u] %s\r\n",
                     st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, GetCurrentProcessId(), msg);
    OutputDebugStringW(line);

    wchar_t path[MAX_PATH];
    if (!GetEnvironmentVariableW(L"ProgramData", path, MAX_PATH)) StringCchCopyW(path, MAX_PATH, L"C:\\ProgramData");
    StringCchCatW(path, MAX_PATH, L"\\KinectVCam");
    CreateDirectoryW(path, nullptr);
    StringCchCatW(path, MAX_PATH, L"\\vcam.log");
    HANDLE f = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(f, &size) && size.QuadPart > 5 * 1024 * 1024) { CloseHandle(f); DeleteFileW(path); return; }
    char utf8[2400];
    int n = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, sizeof(utf8), nullptr, nullptr);
    DWORD written;
    if (n > 1) WriteFile(f, utf8, n - 1, &written, nullptr);
    CloseHandle(f);
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) { g_module = module; DisableThreadLibraryCalls(module); }
    return TRUE;
}

// ---------------------------------------------------------------- fábrica
class ClassFactory : public IClassFactory
{
public:
    explicit ClassFactory(CamKind k) : _kind(k) {}
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) { *ppv = static_cast<IClassFactory*>(this); AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++_ref; }
    STDMETHODIMP_(ULONG) Release() override { ULONG r = --_ref; if (!r) delete this; return r; }
    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override
    {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        return CreateActivator(_kind, riid, ppv);
    }
    STDMETHODIMP LockServer(BOOL lock) override { if (lock) g_locks++; else g_locks--; return S_OK; }
private:
    std::atomic<ULONG> _ref{ 1 };
    CamKind _kind;
};

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    CamKind kind;
    if (clsid == CLSID_KinectVCamColor) kind = CamKind::Color;
    else if (clsid == CLSID_KinectVCamIR) kind = CamKind::Infrared;
    else return CLASS_E_CLASSNOTAVAILABLE;
    auto* f = new (std::nothrow) ClassFactory(kind);
    if (!f) return E_OUTOFMEMORY;
    HRESULT hr = f->QueryInterface(riid, ppv);
    f->Release();
    return hr;
}

STDAPI DllCanUnloadNow() { return (g_objects == 0 && g_locks == 0) ? S_OK : S_FALSE; }

// ---------------------------------------------------------------- registro COM (HKLM, requer admin)
static void ClsidString(const CLSID& c, wchar_t* buf, int n) { StringFromGUID2(c, buf, n); }

STDAPI DllRegisterServer()
{
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(g_module, path, MAX_PATH)) return HRESULT_FROM_WIN32(GetLastError());
    for (int i = 0; i < 2; i++)
    {
        const KindInfo& ki = Info((CamKind)i);
        wchar_t clsid[64], key[256];
        ClsidString(*ki.clsid, clsid, 64);
        StringCchPrintfW(key, 256, L"SOFTWARE\\Classes\\CLSID\\%s", clsid);
        HKEY h;
        LSTATUS s = RegCreateKeyExW(HKEY_LOCAL_MACHINE, key, 0, nullptr, 0, KEY_WRITE, nullptr, &h, nullptr);
        if (s != ERROR_SUCCESS) return HRESULT_FROM_WIN32(s);
        RegSetValueExW(h, nullptr, 0, REG_SZ, (const BYTE*)ki.friendlyName, (DWORD)(wcslen(ki.friendlyName) + 1) * 2);
        HKEY hs;
        s = RegCreateKeyExW(h, L"InprocServer32", 0, nullptr, 0, KEY_WRITE, nullptr, &hs, nullptr);
        if (s == ERROR_SUCCESS)
        {
            RegSetValueExW(hs, nullptr, 0, REG_SZ, (const BYTE*)path, (DWORD)(wcslen(path) + 1) * 2);
            RegSetValueExW(hs, L"ThreadingModel", 0, REG_SZ, (const BYTE*)L"Both", 10);
            RegCloseKey(hs);
        }
        RegCloseKey(h);
        if (s != ERROR_SUCCESS) return HRESULT_FROM_WIN32(s);
    }
    return S_OK;
}

STDAPI DllUnregisterServer()
{
    for (int i = 0; i < 2; i++)
    {
        wchar_t clsid[64], key[256];
        ClsidString(*Info((CamKind)i).clsid, clsid, 64);
        StringCchPrintfW(key, 256, L"SOFTWARE\\Classes\\CLSID\\%s", clsid);
        RegDeleteTreeW(HKEY_LOCAL_MACHINE, key);
    }
    return S_OK;
}

// ---------------------------------------------------------------- ligar / desligar (chamado pelo KinectBridge.exe)
// on = TRUE: registra as duas câmeras (vida "System": continuam registradas após o programa fechar e após reiniciar).
// on = FALSE: remove as duas câmeras.
extern "C" HRESULT __stdcall KvcSetCameras(BOOL on)
{
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) { if (SUCCEEDED(hrCo)) CoUninitialize(); return hr; }
    HRESULT result = S_OK;
    for (int i = 0; i < 2; i++)
    {
        const KindInfo& ki = Info((CamKind)i);
        wchar_t clsid[64];
        ClsidString(*ki.clsid, clsid, 64);
        ComPtr<IMFVirtualCamera> cam;
        hr = MFCreateVirtualCamera(MFVirtualCameraType_SoftwareCameraSource, MFVirtualCameraLifetime_System,
                                   MFVirtualCameraAccess_CurrentUser, ki.friendlyName, clsid, nullptr, 0, &cam);
        if (SUCCEEDED(hr)) hr = on ? cam->Start(nullptr) : cam->Remove();
        if (cam) cam->Shutdown();
        LogDbg(L"%s: %s -> 0x%08X", ki.friendlyName, on ? L"ligar" : L"desligar", hr);
        if (FAILED(hr) && SUCCEEDED(result)) result = hr;
    }
    MFShutdown();
    if (SUCCEEDED(hrCo)) CoUninitialize();
    return result;
}

// Bit 0 = "Kinect Cor" presente, bit 1 = "Kinect Infravermelho" presente (enumerando as câmeras do sistema).
extern "C" int __stdcall KvcGetCameraStatus()
{
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    int status = 0;
    if (SUCCEEDED(MFStartup(MF_VERSION)))
    {
        ComPtr<IMFAttributes> a;
        IMFActivate** devs = nullptr; UINT32 count = 0;
        if (SUCCEEDED(MFCreateAttributes(&a, 1)) &&
            SUCCEEDED(a->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID)) &&
            SUCCEEDED(MFEnumDeviceSources(a.Get(), &devs, &count)))
        {
            for (UINT32 i = 0; i < count; i++)
            {
                wchar_t* name = nullptr; UINT32 len = 0;
                if (SUCCEEDED(devs[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &len)))
                {
                    if (wcsncmp(name, Info(CamKind::Color).friendlyName, wcslen(Info(CamKind::Color).friendlyName)) == 0) status |= 1;
                    if (wcsncmp(name, Info(CamKind::Infrared).friendlyName, wcslen(Info(CamKind::Infrared).friendlyName)) == 0) status |= 2;
                    CoTaskMemFree(name);
                }
                devs[i]->Release();
            }
            CoTaskMemFree(devs);
        }
        MFShutdown();
    }
    if (SUCCEEDED(hrCo)) CoUninitialize();
    return status;
}
