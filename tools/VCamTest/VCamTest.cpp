// Ferramenta de teste: lista câmeras (Media Foundation, como Chrome/Edge/Teams fazem), abre uma pelo nome,
// lê N quadros com IMFSourceReader, mede fps e salva o último quadro como BMP.
// Uso: VCamTest.exe                      -> lista câmeras
//      VCamTest.exe "Kinect Cor" 60 saida.bmp [NV12|YUY2] [largura]
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <cstdio>
#include <vector>
#include <string>
using Microsoft::WRL::ComPtr;

static BYTE Clamp(int v) { return (BYTE)(v < 0 ? 0 : v > 255 ? 255 : v); }

static void SaveBmp(const wchar_t* path, const std::vector<BYTE>& bgr, int w, int h)
{
    int stride = (w * 3 + 3) & ~3;
    BITMAPFILEHEADER fh{}; BITMAPINFOHEADER ih{};
    fh.bfType = 0x4D42; fh.bfOffBits = sizeof(fh) + sizeof(ih); fh.bfSize = fh.bfOffBits + stride * h;
    ih.biSize = sizeof(ih); ih.biWidth = w; ih.biHeight = -h; ih.biPlanes = 1; ih.biBitCount = 24;
    FILE* f = _wfopen(path, L"wb"); if (!f) return;
    fwrite(&fh, sizeof(fh), 1, f); fwrite(&ih, sizeof(ih), 1, f);
    std::vector<BYTE> row(stride, 0);
    for (int y = 0; y < h; y++) { memcpy(row.data(), &bgr[(size_t)y * w * 3], w * 3); fwrite(row.data(), stride, 1, f); }
    fclose(f);
}

static void YuvPixel(int Y, int U, int V, BYTE* o)
{
    int c = 298 * (Y - 16), d = U - 128, e = V - 128;
    o[0] = Clamp((c + 516 * d + 128) >> 8); o[1] = Clamp((c - 100 * d - 208 * e + 128) >> 8); o[2] = Clamp((c + 409 * e + 128) >> 8);
}

int wmain(int argc, wchar_t** argv)
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION);
    ComPtr<IMFAttributes> a; MFCreateAttributes(&a, 1);
    a->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    IMFActivate** devs = nullptr; UINT32 count = 0;
    HRESULT hr = MFEnumDeviceSources(a.Get(), &devs, &count);
    wprintf(L"MFEnumDeviceSources hr=0x%08X, %u camera(s):\n", hr, count);
    int pick = -1;
    for (UINT32 i = 0; i < count; i++)
    {
        wchar_t* name = nullptr; UINT32 len;
        devs[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &len);
        wprintf(L"  [%u] %s\n", i, name ? name : L"?");
        if (argc > 1 && name && pick < 0 && wcsncmp(name, argv[1], wcslen(argv[1])) == 0) pick = (int)i;
        CoTaskMemFree(name);
    }
    if (argc < 2) return 0;
    if (pick < 0) { wprintf(L"Camera '%s' nao encontrada.\n", argv[1]); return 2; }
    int frames = argc > 2 ? _wtoi(argv[2]) : 60;
    const wchar_t* out = argc > 3 ? argv[3] : L"frame.bmp";
    GUID want = (argc > 4 && wcscmp(argv[4], L"YUY2") == 0) ? MFVideoFormat_YUY2 : MFVideoFormat_NV12;
    UINT32 wantW = argc > 5 ? (UINT32)_wtoi(argv[5]) : 0;

    ComPtr<IMFMediaSource> src;
    hr = devs[pick]->ActivateObject(IID_PPV_ARGS(&src));
    wprintf(L"ActivateObject hr=0x%08X\n", hr);
    if (FAILED(hr)) return 3;
    ComPtr<IMFSourceReader> reader;
    hr = MFCreateSourceReaderFromMediaSource(src.Get(), nullptr, &reader);
    if (FAILED(hr)) { wprintf(L"SourceReader hr=0x%08X\n", hr); return 4; }

    // Lista formatos e escolhe o pedido
    UINT32 w = 0, h = 0; bool chosen = false;
    for (DWORD i = 0;; i++)
    {
        ComPtr<IMFMediaType> t;
        if (FAILED(reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, i, &t))) break;
        GUID sub; UINT32 tw, th, n, d; t->GetGUID(MF_MT_SUBTYPE, &sub);
        MFGetAttributeSize(t.Get(), MF_MT_FRAME_SIZE, &tw, &th); MFGetAttributeRatio(t.Get(), MF_MT_FRAME_RATE, &n, &d);
        wprintf(L"  formato %u: %s %ux%u @ %u/%u\n", i, sub == MFVideoFormat_NV12 ? L"NV12" : sub == MFVideoFormat_YUY2 ? L"YUY2" : L"outro", tw, th, n, d);
        if (!chosen && sub == want && (wantW == 0 || tw == wantW))
        {
            hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, t.Get());
            if (SUCCEEDED(hr)) { chosen = true; w = tw; h = th; }
        }
    }
    if (!chosen) { wprintf(L"Formato pedido nao disponivel.\n"); return 5; }
    wprintf(L"Lendo %d quadros em %s %ux%u...\n", frames, want == MFVideoFormat_NV12 ? L"NV12" : L"YUY2", w, h);

    std::vector<BYTE> last; DWORD t0 = GetTickCount(), tFirst = 0; int got = 0;
    for (int i = 0; i < frames; i++)
    {
        DWORD flags = 0; LONGLONG ts; ComPtr<IMFSample> s;
        hr = reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, &ts, &s);
        if (FAILED(hr)) { wprintf(L"ReadSample hr=0x%08X\n", hr); break; }
        if (!s) continue;
        if (!got) tFirst = GetTickCount();
        got++;
        ComPtr<IMFMediaBuffer> b; s->ConvertToContiguousBuffer(&b);
        BYTE* p; DWORD len; b->Lock(&p, nullptr, &len); last.assign(p, p + len); b->Unlock();
    }
    DWORD t1 = GetTickCount();
    wprintf(L"Recebidos %d quadros; primeiro em %u ms; %.1f fps\n", got, tFirst - t0, got > 1 ? (got - 1) * 1000.0 / (t1 - tFirst) : 0.0);
    if (!last.empty())
    {
        std::vector<BYTE> bgr((size_t)w * h * 3);
        for (UINT32 y = 0; y < h; y++)
            for (UINT32 x = 0; x < w; x++)
            {
                int Y, U, V;
                if (want == MFVideoFormat_NV12)
                {
                    Y = last[(size_t)y * w + x];
                    const BYTE* uv = &last[(size_t)w * h + (size_t)(y / 2) * w + (x & ~1u)];
                    U = uv[0]; V = uv[1];
                }
                else
                {
                    const BYTE* m = &last[(size_t)y * w * 2 + (x & ~1u) * 2];
                    Y = (x & 1) ? m[2] : m[0]; U = m[1]; V = m[3];
                }
                YuvPixel(Y, U, V, &bgr[((size_t)y * w + x) * 3]);
            }
        SaveBmp(out, bgr, (int)w, (int)h);
        wprintf(L"Salvo: %s\n", out);
    }
    src->Shutdown();
    return got > 0 ? 0 : 6;
}
