// KinectVCam - fonte de mídia (Media Foundation) para as câmeras virtuais "Kinect Cor" e "Kinect Infravermelho".
// Esta DLL é carregada pelo serviço Windows Camera Frame Server. Os quadros vêm do KinectBridge.exe
// por memória compartilhada (Global\KinectVCam_Color / Global\KinectVCam_IR).
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <mfvirtualcamera.h>
#include <ks.h>
#include <ksmedia.h>
#include <ksproxy.h>
#include <wrl/client.h>
#include <atomic>
#include <vector>
#include <cstdint>

using Microsoft::WRL::ComPtr;

enum class CamKind { Color = 0, Infrared = 1 };

extern const CLSID CLSID_KinectVCamColor;
extern const CLSID CLSID_KinectVCamIR;
extern const IID IID_IKsControl_;
extern const GUID PINNAME_VIDEO_CAPTURE_;
extern const GUID KSCAMERAPROFILE_Legacy_;
extern std::atomic<long> g_objects;

void LogDbg(const wchar_t* fmt, ...);

// ---- Memória compartilhada (mesmo layout em src\KinectBridge\SharedFrameWriter.cs) ----
#pragma pack(push, 8)
struct KvcHeader
{
    uint32_t magic;          // 'KVC1'
    uint32_t width;
    uint32_t height;
    uint32_t format;         // 1 = YUY2, 2 = GRAY8
    uint32_t frameBytes;
    volatile int32_t latestSlot;
    volatile int64_t seq;
    volatile int64_t writeTickMs; // GetTickCount64() do escritor
    volatile uint32_t status;     // 1 = Kinect com imagem, 0 = sem sinal
    volatile uint32_t readerTick; // GetTickCount() (32 bits) do último pedido de quadro de um app.
                                  // O KinectBridge só liga o Kinect enquanto isto for recente.
    uint32_t reserved[4];
};
#pragma pack(pop)
static_assert(sizeof(KvcHeader) == 64, "header deve ter 64 bytes");

constexpr uint32_t KvcMagic = 0x3143564B; // "KVC1"
constexpr int KvcSlots = 3;
constexpr uint32_t KvcFmtYuy2 = 1, KvcFmtGray8 = 2;

struct KindInfo
{
    const wchar_t* mapName;
    const wchar_t* friendlyName;
    const CLSID* clsid;
    uint32_t srcW, srcH, srcFmt, srcBytes;
};
const KindInfo& Info(CamKind k);

// ---- Leitor da memória compartilhada + conversão + quadro "SEM SINAL" ----
class SharedFrameSource
{
public:
    explicit SharedFrameSource(CamKind kind);
    ~SharedFrameSource();
    // Preenche dst (NV12 ou YUY2, w x h, pitch) com o quadro mais recente. Espera até maxWaitMs por um quadro novo.
    // Retorna true se a imagem é do Kinect, false se é o quadro "SEM SINAL".
    bool Produce(BYTE* dst, LONG pitch, UINT32 w, UINT32 h, bool nv12, DWORD maxWaitMs);

private:
    void EnsureMapped();
    const KvcHeader* LiveHeader();
    void RenderNoSignal(UINT32 w, UINT32 h);

    CamKind _kind;
    HANDLE _map = nullptr;
    BYTE* _view = nullptr;
    ULONGLONG _lastTry = 0;
    int64_t _lastSeq = -1;
    bool _lastLive = false;
    ULONGLONG _lastDelivery = 0;
    std::vector<BYTE> _noSignal;
    UINT32 _nsW = 0, _nsH = 0;
};

// Conversões (redimensionamento por vizinho mais próximo quando o tamanho difere)
void Yuy2ToNv12(const BYTE* src, UINT32 sw, UINT32 sh, BYTE* dst, LONG pitch, UINT32 dw, UINT32 dh);
void Yuy2ToYuy2(const BYTE* src, UINT32 sw, UINT32 sh, BYTE* dst, LONG pitch, UINT32 dw, UINT32 dh);
void GrayToNv12(const BYTE* src, UINT32 sw, UINT32 sh, BYTE* dst, LONG pitch, UINT32 dw, UINT32 dh);
void GrayToYuy2(const BYTE* src, UINT32 sw, UINT32 sh, BYTE* dst, LONG pitch, UINT32 dw, UINT32 dh);

// ---- Objetos COM ----
HRESULT CreateActivator(CamKind kind, REFIID riid, void** ppv);

struct CritSec
{
    CRITICAL_SECTION cs;
    CritSec() { InitializeCriticalSection(&cs); }
    ~CritSec() { DeleteCriticalSection(&cs); }
};
struct AutoLock
{
    CritSec& c;
    explicit AutoLock(CritSec& x) : c(x) { EnterCriticalSection(&c.cs); }
    ~AutoLock() { LeaveCriticalSection(&c.cs); }
};
