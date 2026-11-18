// Leitura da memória compartilhada, conversão de formato e quadro "SEM SINAL".
#include "KinectVCam.h"
#include <sddl.h>

static const KindInfo g_info[2] = {
    { L"Global\\KinectVCam_Color", L"Kinect Cor",           &CLSID_KinectVCamColor, 1920, 1080, KvcFmtYuy2,  1920 * 1080 * 2 },
    { L"Global\\KinectVCam_IR",    L"Kinect Infravermelho", &CLSID_KinectVCamIR,     512,  424, KvcFmtGray8,  512 * 424 },
};
const KindInfo& Info(CamKind k) { return g_info[(int)k]; }

constexpr ULONGLONG StaleMs = 1500;      // sem quadro novo há 1,5 s => "SEM SINAL"
constexpr ULONGLONG NoSignalPeriod = 66; // ~15 fps para o quadro "SEM SINAL"

SharedFrameSource::SharedFrameSource(CamKind kind) : _kind(kind) {}

SharedFrameSource::~SharedFrameSource()
{
    if (_view) UnmapViewOfFile(_view);
    if (_map) CloseHandle(_map);
}

void SharedFrameSource::EnsureMapped()
{
    if (_view) return;
    ULONGLONG now = GetTickCount64();
    if (now - _lastTry < 250) return;
    _lastTry = now;

    const KindInfo& ki = Info(_kind);
    DWORD size = (DWORD)(sizeof(KvcHeader) + (size_t)KvcSlots * ki.srcBytes);

    // SYSTEM, LOCAL SERVICE, Administradores e usuário interativo/autenticado podem ler e escrever.
    PSECURITY_DESCRIPTOR sd = nullptr;
    ConvertStringSecurityDescriptorToSecurityDescriptorW(
        L"D:(A;;GA;;;SY)(A;;GA;;;LS)(A;;GA;;;BA)(A;;GA;;;IU)(A;;GA;;;AU)", SDDL_REVISION_1, &sd, nullptr);
    SECURITY_ATTRIBUTES sa{ sizeof(sa), sd, FALSE };

    HANDLE h = CreateFileMappingW(INVALID_HANDLE_VALUE, sd ? &sa : nullptr, PAGE_READWRITE, 0, size, ki.mapName);
    DWORD err = GetLastError();
    if (sd) LocalFree(sd);
    if (!h)
    {
        h = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, ki.mapName);
        if (!h)
        {
            static DWORD lastErr = 0;
            if (err != lastErr) { LogDbg(L"%s: CreateFileMapping falhou (%u)", ki.mapName, err); lastErr = err; }
            return;
        }
    }
    BYTE* v = (BYTE*)MapViewOfFile(h, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, size);
    if (!v)
    {
        LogDbg(L"%s: MapViewOfFile falhou (%u)", ki.mapName, GetLastError());
        CloseHandle(h);
        return;
    }
    _map = h;
    _view = v;
    LogDbg(L"%s: memoria compartilhada aberta (%s)", ki.mapName, err == ERROR_ALREADY_EXISTS ? L"existente" : L"criada");
}

const KvcHeader* SharedFrameSource::LiveHeader()
{
    if (!_view) return nullptr;
    const KindInfo& ki = Info(_kind);
    auto* hdr = (const KvcHeader*)_view;
    if (hdr->magic != KvcMagic || hdr->width != ki.srcW || hdr->height != ki.srcH ||
        hdr->format != ki.srcFmt || hdr->frameBytes != ki.srcBytes) return nullptr;
    if (hdr->status != 1 || hdr->seq <= 0) return nullptr;
    if (GetTickCount64() - (ULONGLONG)hdr->writeTickMs > StaleMs) return nullptr;
    int slot = hdr->latestSlot;
    if (slot < 0 || slot >= KvcSlots) return nullptr;
    return hdr;
}

bool SharedFrameSource::Produce(BYTE* dst, LONG pitch, UINT32 w, UINT32 h, bool nv12, DWORD maxWaitMs)
{
    EnsureMapped();
    // Sinal de "câmera em uso": o KinectBridge liga o sensor enquanto isto estiver sendo atualizado.
    if (_view) ((KvcHeader*)_view)->readerTick = (uint32_t)GetTickCount64();
    const KindInfo& ki = Info(_kind);
    ULONGLONG start = GetTickCount64();

    for (;;)
    {
        const KvcHeader* hdr = LiveHeader();
        ULONGLONG now = GetTickCount64();
        if (hdr)
        {
            int64_t seq = hdr->seq;
            if (seq != _lastSeq || now - start >= maxWaitMs)
            {
                MemoryBarrier();
                int slot = hdr->latestSlot;
                const BYTE* src = _view + sizeof(KvcHeader) + (size_t)slot * ki.srcBytes;
                if (ki.srcFmt == KvcFmtYuy2)
                {
                    if (nv12) Yuy2ToNv12(src, ki.srcW, ki.srcH, dst, pitch, w, h);
                    else Yuy2ToYuy2(src, ki.srcW, ki.srcH, dst, pitch, w, h);
                }
                else
                {
                    if (nv12) GrayToNv12(src, ki.srcW, ki.srcH, dst, pitch, w, h);
                    else GrayToYuy2(src, ki.srcW, ki.srcH, dst, pitch, w, h);
                }
                _lastSeq = seq;
                if (!_lastLive) LogDbg(L"%s: recebendo imagem do Kinect", ki.friendlyName);
                _lastLive = true;
                _lastDelivery = GetTickCount64();
                return true;
            }
        }
        else
        {
            // Sem sinal: entrega o quadro "SEM SINAL" a ~15 fps.
            ULONGLONG due = _lastDelivery + NoSignalPeriod;
            if (now < due) Sleep((DWORD)(due - now));
            if (_nsW != w || _nsH != h) RenderNoSignal(w, h);
            if (nv12) GrayToNv12(_noSignal.data(), w, h, dst, pitch, w, h);
            else GrayToYuy2(_noSignal.data(), w, h, dst, pitch, w, h);
            if (_lastLive) LogDbg(L"%s: SEM SINAL", ki.friendlyName);
            _lastLive = false;
            _lastDelivery = GetTickCount64();
            EnsureMapped();
            return false;
        }
        Sleep(2);
    }
}

void SharedFrameSource::RenderNoSignal(UINT32 w, UINT32 h)
{
    _noSignal.assign((size_t)w * h, 40);
    _nsW = w; _nsH = h;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = (LONG)w;
    bi.bmiHeader.biHeight = -(LONG)h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bmp = dc ? CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
    if (!dc || !bmp || !bits)
    {
        if (bmp) DeleteObject(bmp);
        if (dc) DeleteDC(dc);
        return; // fica cinza liso
    }
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    RECT rc{ 0, 0, (LONG)w, (LONG)h };
    HBRUSH br = CreateSolidBrush(RGB(40, 40, 40));
    FillRect(dc, &rc, br);
    DeleteObject(br);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(240, 240, 240));

    int big = (int)h / 7, smallH = (int)h / 22;
    HFONT f1 = CreateFontW(-big, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Segoe UI");
    HFONT f2 = CreateFontW(-smallH, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(dc, f1);
    RECT r1{ 0, (LONG)h / 2 - big, (LONG)w, (LONG)h / 2 + big / 8 };
    DrawTextW(dc, L"SEM SINAL", -1, &r1, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    SelectObject(dc, f2);
    RECT r2{ 0, (LONG)h / 2 + big / 4, (LONG)w, (LONG)h / 2 + big / 4 + smallH * 2 };
    DrawTextW(dc, _kind == CamKind::Color ? L"Kinect Cor - aguardando o Kinect..." : L"Kinect Infravermelho - aguardando o Kinect...",
              -1, &r2, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    SelectObject(dc, oldFont);
    GdiFlush();

    const BYTE* p = (const BYTE*)bits;
    for (size_t i = 0; i < (size_t)w * h; i++)
        _noSignal[i] = (BYTE)(16 + p[i * 4 + 1] * 219 / 255);

    DeleteObject(f1);
    DeleteObject(f2);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
}

// ---------------- Conversões ----------------
static inline UINT32 MapX(UINT32 x, UINT32 sw, UINT32 dw) { return (UINT32)((uint64_t)x * sw / dw); }

void Yuy2ToNv12(const BYTE* src, UINT32 sw, UINT32 sh, BYTE* dst, LONG pitch, UINT32 dw, UINT32 dh)
{
    BYTE* uvPlane = dst + (size_t)pitch * dh;
    for (UINT32 y = 0; y < dh; y++)
    {
        const BYTE* s = src + (size_t)MapX(y, sh, dh) * sw * 2;
        BYTE* d = dst + (size_t)y * pitch;
        if (dw == sw) { for (UINT32 x = 0; x < dw; x++) d[x] = s[x * 2]; }
        else { for (UINT32 x = 0; x < dw; x++) d[x] = s[MapX(x, sw, dw) * 2]; }
        if ((y & 1) == 0)
        {
            BYTE* uv = uvPlane + (size_t)(y / 2) * pitch;
            for (UINT32 x = 0; x + 1 < dw; x += 2)
            {
                UINT32 sx = (dw == sw ? x : MapX(x, sw, dw)) & ~1u;
                uv[x] = s[sx * 2 + 1];
                uv[x + 1] = s[sx * 2 + 3];
            }
        }
    }
}

void Yuy2ToYuy2(const BYTE* src, UINT32 sw, UINT32 sh, BYTE* dst, LONG pitch, UINT32 dw, UINT32 dh)
{
    for (UINT32 y = 0; y < dh; y++)
    {
        const BYTE* s = src + (size_t)MapX(y, sh, dh) * sw * 2;
        BYTE* d = dst + (size_t)y * pitch;
        if (dw == sw) { memcpy(d, s, (size_t)dw * 2); continue; }
        for (UINT32 x = 0; x + 1 < dw; x += 2)
        {
            UINT32 sx0 = MapX(x, sw, dw) & ~1u, sx1 = MapX(x + 1, sw, dw);
            d[x * 2] = s[sx0 * 2];
            d[x * 2 + 1] = s[sx0 * 2 + 1];
            d[x * 2 + 2] = s[sx1 * 2];
            d[x * 2 + 3] = s[sx0 * 2 + 3];
        }
    }
}

void GrayToNv12(const BYTE* src, UINT32 sw, UINT32 sh, BYTE* dst, LONG pitch, UINT32 dw, UINT32 dh)
{
    for (UINT32 y = 0; y < dh; y++)
    {
        const BYTE* s = src + (size_t)MapX(y, sh, dh) * sw;
        BYTE* d = dst + (size_t)y * pitch;
        if (dw == sw) memcpy(d, s, dw);
        else for (UINT32 x = 0; x < dw; x++) d[x] = s[MapX(x, sw, dw)];
    }
    BYTE* uv = dst + (size_t)pitch * dh;
    for (UINT32 y = 0; y < dh / 2; y++) memset(uv + (size_t)y * pitch, 128, dw);
}

void GrayToYuy2(const BYTE* src, UINT32 sw, UINT32 sh, BYTE* dst, LONG pitch, UINT32 dw, UINT32 dh)
{
    for (UINT32 y = 0; y < dh; y++)
    {
        const BYTE* s = src + (size_t)MapX(y, sh, dh) * sw;
        BYTE* d = dst + (size_t)y * pitch;
        for (UINT32 x = 0; x < dw; x++)
        {
            d[x * 2] = s[dw == sw ? x : MapX(x, sw, dw)];
            d[x * 2 + 1] = 128;
        }
    }
}
