// Ativador (IMFActivate), fonte de mídia (IMFMediaSourceEx) e stream (IMFMediaStream2) da câmera virtual.
#include "KinectVCam.h"

#define RIF(x) do { HRESULT _hr = (x); if (FAILED(_hr)) return _hr; } while (0)

static const HRESULT E_KS_NOT_FOUND = HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);

// ---------------------------------------------------------------- tipos de mídia
struct FormatDesc { GUID subtype; UINT32 w, h; };

static std::vector<FormatDesc> FormatsFor(CamKind k)
{
    if (k == CamKind::Color)
        return { { MFVideoFormat_NV12, 1920, 1080 }, { MFVideoFormat_YUY2, 1920, 1080 },
                 { MFVideoFormat_NV12, 1280, 720 },  { MFVideoFormat_YUY2, 1280, 720 } };
    return { { MFVideoFormat_NV12, 512, 424 }, { MFVideoFormat_YUY2, 512, 424 } };
}

static HRESULT MakeMediaType(const FormatDesc& f, IMFMediaType** pp)
{
    ComPtr<IMFMediaType> t;
    RIF(MFCreateMediaType(&t));
    bool nv12 = f.subtype == MFVideoFormat_NV12;
    UINT32 stride = nv12 ? f.w : f.w * 2;
    UINT32 size = nv12 ? f.w * f.h * 3 / 2 : f.w * f.h * 2;
    RIF(t->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
    RIF(t->SetGUID(MF_MT_SUBTYPE, f.subtype));
    RIF(MFSetAttributeSize(t.Get(), MF_MT_FRAME_SIZE, f.w, f.h));
    RIF(MFSetAttributeRatio(t.Get(), MF_MT_FRAME_RATE, 30, 1));
    RIF(MFSetAttributeRatio(t.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
    RIF(t->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
    RIF(t->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE));
    RIF(t->SetUINT32(MF_MT_FIXED_SIZE_SAMPLES, TRUE));
    RIF(t->SetUINT32(MF_MT_DEFAULT_STRIDE, stride));
    RIF(t->SetUINT32(MF_MT_SAMPLE_SIZE, size));
    RIF(t->SetUINT32(MF_MT_AVG_BITRATE, size * 8 * 30));
    *pp = t.Detach();
    return S_OK;
}

class MediaSource;

// ---------------------------------------------------------------- stream
class MediaStream : public IMFMediaStream2, public IKsControl
{
public:
    MediaStream(CamKind k) : _kind(k), _frames(k) { g_objects++; }
    ~MediaStream() { g_objects--; }

    HRESULT Initialize(IMFMediaSource* source)
    {
        _source = source;
        RIF(MFCreateEventQueue(&_queue));
        RIF(MFCreateAttributes(&_attrs, 8));
        RIF(_attrs->SetGUID(MF_DEVICESTREAM_STREAM_CATEGORY, PINNAME_VIDEO_CAPTURE_));
        RIF(_attrs->SetUINT32(MF_DEVICESTREAM_STREAM_ID, 0));
        RIF(_attrs->SetUINT32(MF_DEVICESTREAM_FRAMESERVER_SHARED, 1));
        // "Color" também para o IR: senão os apps comuns não listam a câmera.
        RIF(_attrs->SetUINT32(MF_DEVICESTREAM_ATTRIBUTE_FRAMESOURCE_TYPES, MFFrameSourceTypes_Color));

        auto formats = FormatsFor(_kind);
        std::vector<ComPtr<IMFMediaType>> types(formats.size());
        std::vector<IMFMediaType*> raw;
        for (size_t i = 0; i < formats.size(); i++) { RIF(MakeMediaType(formats[i], &types[i])); raw.push_back(types[i].Get()); }
        RIF(MFCreateStreamDescriptor(0, (DWORD)raw.size(), raw.data(), &_descriptor));
        ComPtr<IMFMediaTypeHandler> handler;
        RIF(_descriptor->GetMediaTypeHandler(&handler));
        RIF(handler->SetCurrentMediaType(raw[0]));
        RIF(_attrs->CopyAllItems(_descriptor.Get()));
        return S_OK;
    }

    IMFStreamDescriptor* Descriptor() { return _descriptor.Get(); }
    IMFAttributes* Attributes() { return _attrs.Get(); }
    bool IsActive() { AutoLock l(_lock); return _state != MF_STREAM_STATE_STOPPED; }

    void SetAllocator(IUnknown* a)
    {
        AutoLock l(_lock);
        _allocator.Reset();
        if (a) a->QueryInterface(IID_PPV_ARGS(&_allocator));
    }

    HRESULT Start(IMFMediaType* type)
    {
        AutoLock l(_lock);
        if (_shutdown) return MF_E_SHUTDOWN;
        if (type)
        {
            GUID sub{}; UINT32 w = 0, h = 0;
            RIF(type->GetGUID(MF_MT_SUBTYPE, &sub));
            RIF(MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &w, &h));
            _type = type; _subtype = sub; _w = w; _h = h;
        }
        if (!_type) return MF_E_NOT_INITIALIZED;
        if (_allocator && FAILED(_allocator->InitializeSampleAllocator(10, _type.Get())))
        {
            LogDbg(L"InitializeSampleAllocator falhou; usando buffers proprios");
            _allocator.Reset();
        }
        _state = MF_STREAM_STATE_RUNNING;
        LogDbg(L"%s: stream iniciado %ux%u %s", Info(_kind).friendlyName, _w, _h, _subtype == MFVideoFormat_NV12 ? L"NV12" : L"YUY2");
        PROPVARIANT var; PropVariantInit(&var); var.vt = VT_I8; var.hVal.QuadPart = MFGetSystemTime();
        return _queue->QueueEventParamVar(MEStreamStarted, GUID_NULL, S_OK, &var);
    }

    HRESULT Stop()
    {
        AutoLock l(_lock);
        if (_shutdown) return MF_E_SHUTDOWN;
        if (_state == MF_STREAM_STATE_STOPPED) return S_OK;
        _state = MF_STREAM_STATE_STOPPED;
        if (_allocator) _allocator->UninitializeSampleAllocator();
        LogDbg(L"%s: stream parado", Info(_kind).friendlyName);
        return _queue->QueueEventParamVar(MEStreamStopped, GUID_NULL, S_OK, nullptr);
    }

    void Shutdown()
    {
        AutoLock l(_lock);
        if (_shutdown) return;
        _shutdown = true;
        if (_queue) _queue->Shutdown();
        if (_allocator) _allocator->UninitializeSampleAllocator();
        _allocator.Reset();
        _source.Reset();
    }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == __uuidof(IMFMediaEventGenerator) || riid == __uuidof(IMFMediaStream) || riid == __uuidof(IMFMediaStream2))
            *ppv = static_cast<IMFMediaStream2*>(this);
        else if (riid == IID_IKsControl_) *ppv = static_cast<IKsControl*>(this);
        else { *ppv = nullptr; return E_NOINTERFACE; }
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++_ref; }
    STDMETHODIMP_(ULONG) Release() override { ULONG r = --_ref; if (!r) delete this; return r; }

    // IMFMediaEventGenerator
    STDMETHODIMP GetEvent(DWORD flags, IMFMediaEvent** e) override
    {
        ComPtr<IMFMediaEventQueue> q;
        { AutoLock l(_lock); if (_shutdown) return MF_E_SHUTDOWN; q = _queue; }
        return q->GetEvent(flags, e);
    }
    STDMETHODIMP BeginGetEvent(IMFAsyncCallback* cb, IUnknown* st) override { AutoLock l(_lock); if (_shutdown) return MF_E_SHUTDOWN; return _queue->BeginGetEvent(cb, st); }
    STDMETHODIMP EndGetEvent(IMFAsyncResult* r, IMFMediaEvent** e) override { AutoLock l(_lock); if (_shutdown) return MF_E_SHUTDOWN; return _queue->EndGetEvent(r, e); }
    STDMETHODIMP QueueEvent(MediaEventType t, REFGUID g, HRESULT s, const PROPVARIANT* v) override { AutoLock l(_lock); if (_shutdown) return MF_E_SHUTDOWN; return _queue->QueueEventParamVar(t, g, s, v); }

    // IMFMediaStream
    STDMETHODIMP GetMediaSource(IMFMediaSource** pp) override
    {
        if (!pp) return E_POINTER;
        AutoLock l(_lock);
        if (_shutdown || !_source) return MF_E_SHUTDOWN;
        *pp = _source.Get(); (*pp)->AddRef();
        return S_OK;
    }
    STDMETHODIMP GetStreamDescriptor(IMFStreamDescriptor** pp) override
    {
        if (!pp) return E_POINTER;
        AutoLock l(_lock);
        if (_shutdown) return MF_E_SHUTDOWN;
        *pp = _descriptor.Get(); (*pp)->AddRef();
        return S_OK;
    }
    STDMETHODIMP RequestSample(IUnknown* token) override
    {
        ComPtr<IMFVideoSampleAllocatorEx> alloc;
        ComPtr<IMFMediaEventQueue> queue;
        UINT32 w, h; bool nv12;
        {
            AutoLock l(_lock);
            if (_shutdown) return MF_E_SHUTDOWN;
            if (_state != MF_STREAM_STATE_RUNNING) return MF_E_INVALIDREQUEST;
            alloc = _allocator; queue = _queue; w = _w; h = _h; nv12 = _subtype == MFVideoFormat_NV12;
        }

        ComPtr<IMFSample> sample;
        ComPtr<IMFMediaBuffer> buffer;
        if (alloc && SUCCEEDED(alloc->AllocateSample(&sample)))
        {
            if (FAILED(sample->GetBufferByIndex(0, &buffer))) sample.Reset();
        }
        if (!sample)
        {
            RIF(MFCreateSample(&sample));
            RIF(MFCreate2DMediaBuffer(w, h, nv12 ? MFVideoFormat_NV12.Data1 : MFVideoFormat_YUY2.Data1, FALSE, &buffer));
            RIF(sample->AddBuffer(buffer.Get()));
        }

        DWORD needed = nv12 ? w * h * 3 / 2 : w * h * 2;
        ComPtr<IMF2DBuffer2> b2;
        BYTE* scan0 = nullptr; LONG pitch = 0; BYTE* bufStart = nullptr; DWORD bufLen = 0;
        if (SUCCEEDED(buffer.As(&b2)) && SUCCEEDED(b2->Lock2DSize(MF2DBuffer_LockFlags_Write, &scan0, &pitch, &bufStart, &bufLen)))
        {
            if (pitch > 0) _frames.Produce(scan0, pitch, w, h, nv12, 100);
            b2->Unlock2D();
            DWORD len = (DWORD)(nv12 ? (DWORD)abs(pitch) * h * 3 / 2 : (DWORD)abs(pitch) * h);
            buffer->SetCurrentLength(len <= bufLen ? len : bufLen);
        }
        else
        {
            DWORD maxLen = 0;
            RIF(buffer->Lock(&bufStart, &maxLen, nullptr));
            if (maxLen >= needed) _frames.Produce(bufStart, nv12 ? (LONG)w : (LONG)w * 2, w, h, nv12, 100);
            buffer->Unlock();
            buffer->SetCurrentLength(needed <= maxLen ? needed : maxLen);
        }

        RIF(sample->SetSampleTime(MFGetSystemTime()));
        RIF(sample->SetSampleDuration(333333));
        if (token) RIF(sample->SetUnknown(MFSampleExtension_Token, token));
        return queue->QueueEventParamUnk(MEMediaSample, GUID_NULL, S_OK, sample.Get());
    }

    // IMFMediaStream2
    STDMETHODIMP SetStreamState(MF_STREAM_STATE value) override
    {
        if (value == MF_STREAM_STATE_STOPPED) return Stop();
        if (value == MF_STREAM_STATE_RUNNING) return Start(nullptr);
        AutoLock l(_lock);
        _state = value;
        return S_OK;
    }
    STDMETHODIMP GetStreamState(MF_STREAM_STATE* v) override { if (!v) return E_POINTER; AutoLock l(_lock); *v = _state; return S_OK; }

    // IKsControl (sem controles de câmera)
    STDMETHODIMP KsProperty(PKSPROPERTY, ULONG, LPVOID, ULONG, ULONG*) override { return E_KS_NOT_FOUND; }
    STDMETHODIMP KsMethod(PKSMETHOD, ULONG, LPVOID, ULONG, ULONG*) override { return E_KS_NOT_FOUND; }
    STDMETHODIMP KsEvent(PKSEVENT, ULONG, LPVOID, ULONG, ULONG*) override { return E_KS_NOT_FOUND; }

private:
    std::atomic<ULONG> _ref{ 1 };
    CritSec _lock;
    CamKind _kind;
    bool _shutdown = false;
    MF_STREAM_STATE _state = MF_STREAM_STATE_STOPPED;
    ComPtr<IMFMediaSource> _source;
    ComPtr<IMFMediaEventQueue> _queue;
    ComPtr<IMFAttributes> _attrs;
    ComPtr<IMFStreamDescriptor> _descriptor;
    ComPtr<IMFMediaType> _type;
    ComPtr<IMFVideoSampleAllocatorEx> _allocator;
    GUID _subtype = GUID_NULL;
    UINT32 _w = 0, _h = 0;
    SharedFrameSource _frames;
};

// ---------------------------------------------------------------- fonte
class MediaSource : public IMFMediaSourceEx, public IMFGetService, public IKsControl, public IMFSampleAllocatorControl
{
public:
    MediaSource(CamKind k) : _kind(k) { g_objects++; }
    ~MediaSource() { if (_stream) _stream->Release(); g_objects--; }

    HRESULT Initialize(IMFAttributes* activatorAttrs)
    {
        RIF(MFCreateAttributes(&_attrs, 4));
        if (activatorAttrs) activatorAttrs->CopyAllItems(_attrs.Get());

        ComPtr<IMFSensorProfileCollection> collection;
        ComPtr<IMFSensorProfile> profile;
        if (SUCCEEDED(MFCreateSensorProfileCollection(&collection)) &&
            SUCCEEDED(MFCreateSensorProfile(KSCAMERAPROFILE_Legacy_, 0, nullptr, &profile)) &&
            SUCCEEDED(profile->AddProfileFilter(0, L"((RES==;FRT<=30,1;SUT==))")) &&
            SUCCEEDED(collection->AddProfile(profile.Get())))
        {
            _attrs->SetUnknown(MF_DEVICEMFT_SENSORPROFILE_COLLECTION, collection.Get());
        }

        RIF(MFCreateEventQueue(&_queue));
        _stream = new (std::nothrow) MediaStream(_kind);
        if (!_stream) return E_OUTOFMEMORY;
        RIF(_stream->Initialize(this));
        IMFStreamDescriptor* sd = _stream->Descriptor();
        RIF(MFCreatePresentationDescriptor(1, &sd, &_pd));
        RIF(_pd->SelectStream(0));
        LogDbg(L"%s: fonte de midia criada (pid %u)", Info(_kind).friendlyName, GetCurrentProcessId());
        return S_OK;
    }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == __uuidof(IMFMediaEventGenerator) || riid == __uuidof(IMFMediaSource) || riid == __uuidof(IMFMediaSourceEx))
            *ppv = static_cast<IMFMediaSourceEx*>(this);
        else if (riid == __uuidof(IMFGetService)) *ppv = static_cast<IMFGetService*>(this);
        else if (riid == IID_IKsControl_) *ppv = static_cast<IKsControl*>(this);
        else if (riid == __uuidof(IMFSampleAllocatorControl)) *ppv = static_cast<IMFSampleAllocatorControl*>(this);
        else { *ppv = nullptr; return E_NOINTERFACE; }
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++_ref; }
    STDMETHODIMP_(ULONG) Release() override { ULONG r = --_ref; if (!r) delete this; return r; }

    // IMFMediaEventGenerator
    STDMETHODIMP GetEvent(DWORD flags, IMFMediaEvent** e) override
    {
        ComPtr<IMFMediaEventQueue> q;
        { AutoLock l(_lock); if (_shutdown) return MF_E_SHUTDOWN; q = _queue; }
        return q->GetEvent(flags, e);
    }
    STDMETHODIMP BeginGetEvent(IMFAsyncCallback* cb, IUnknown* st) override { AutoLock l(_lock); if (_shutdown) return MF_E_SHUTDOWN; return _queue->BeginGetEvent(cb, st); }
    STDMETHODIMP EndGetEvent(IMFAsyncResult* r, IMFMediaEvent** e) override { AutoLock l(_lock); if (_shutdown) return MF_E_SHUTDOWN; return _queue->EndGetEvent(r, e); }
    STDMETHODIMP QueueEvent(MediaEventType t, REFGUID g, HRESULT s, const PROPVARIANT* v) override { AutoLock l(_lock); if (_shutdown) return MF_E_SHUTDOWN; return _queue->QueueEventParamVar(t, g, s, v); }

    // IMFMediaSource
    STDMETHODIMP GetCharacteristics(DWORD* c) override { if (!c) return E_POINTER; AutoLock l(_lock); if (_shutdown) return MF_E_SHUTDOWN; *c = MFMEDIASOURCE_IS_LIVE; return S_OK; }
    STDMETHODIMP CreatePresentationDescriptor(IMFPresentationDescriptor** pp) override
    {
        if (!pp) return E_POINTER;
        AutoLock l(_lock);
        if (_shutdown) return MF_E_SHUTDOWN;
        return _pd->Clone(pp);
    }
    STDMETHODIMP Start(IMFPresentationDescriptor* pd, const GUID* timeFormat, const PROPVARIANT* startPos) override
    {
        if (!pd || !startPos) return E_INVALIDARG;
        if (timeFormat && *timeFormat != GUID_NULL) return MF_E_UNSUPPORTED_TIME_FORMAT;
        AutoLock l(_lock);
        if (_shutdown) return MF_E_SHUTDOWN;

        DWORD count = 0;
        RIF(pd->GetStreamDescriptorCount(&count));
        for (DWORD i = 0; i < count; i++)
        {
            BOOL selected = FALSE;
            ComPtr<IMFStreamDescriptor> sd;
            RIF(pd->GetStreamDescriptorByIndex(i, &selected, &sd));
            DWORD id = 0;
            RIF(sd->GetStreamIdentifier(&id));
            if (id != 0) continue;
            bool wasActive = _stream->IsActive();
            if (selected)
            {
                RIF(_queue->QueueEventParamUnk(wasActive ? MEUpdatedStream : MENewStream, GUID_NULL, S_OK, static_cast<IMFMediaStream2*>(_stream)));
                ComPtr<IMFMediaTypeHandler> handler;
                ComPtr<IMFMediaType> type;
                RIF(sd->GetMediaTypeHandler(&handler));
                RIF(handler->GetCurrentMediaType(&type));
                RIF(_stream->Start(type.Get()));
            }
            else if (wasActive)
            {
                RIF(_stream->Stop());
            }
        }
        PROPVARIANT var; PropVariantInit(&var);
        var.vt = VT_I8;
        var.hVal.QuadPart = startPos->vt == VT_I8 ? startPos->hVal.QuadPart : MFGetSystemTime();
        return _queue->QueueEventParamVar(MESourceStarted, GUID_NULL, S_OK, &var);
    }
    STDMETHODIMP Stop() override
    {
        AutoLock l(_lock);
        if (_shutdown) return MF_E_SHUTDOWN;
        if (_stream->IsActive()) RIF(_stream->Stop());
        return _queue->QueueEventParamVar(MESourceStopped, GUID_NULL, S_OK, nullptr);
    }
    STDMETHODIMP Pause() override { return MF_E_INVALID_STATE_TRANSITION; }
    STDMETHODIMP Shutdown() override
    {
        AutoLock l(_lock);
        if (_shutdown) return MF_E_SHUTDOWN;
        _shutdown = true;
        if (_queue) _queue->Shutdown();
        if (_stream) _stream->Shutdown();
        LogDbg(L"%s: fonte de midia encerrada", Info(_kind).friendlyName);
        return S_OK;
    }

    // IMFMediaSourceEx
    STDMETHODIMP GetSourceAttributes(IMFAttributes** pp) override
    {
        if (!pp) return E_POINTER;
        AutoLock l(_lock);
        if (_shutdown) return MF_E_SHUTDOWN;
        *pp = _attrs.Get(); (*pp)->AddRef();
        return S_OK;
    }
    STDMETHODIMP GetStreamAttributes(DWORD id, IMFAttributes** pp) override
    {
        if (!pp) return E_POINTER;
        AutoLock l(_lock);
        if (_shutdown) return MF_E_SHUTDOWN;
        if (id != 0) return MF_E_INVALIDSTREAMNUMBER;
        *pp = _stream->Attributes(); (*pp)->AddRef();
        return S_OK;
    }
    STDMETHODIMP SetD3DManager(IUnknown*) override { return S_OK; }

    // IMFGetService
    STDMETHODIMP GetService(REFGUID, REFIID, LPVOID* ppv) override { if (ppv) *ppv = nullptr; return MF_E_UNSUPPORTED_SERVICE; }

    // IKsControl
    STDMETHODIMP KsProperty(PKSPROPERTY, ULONG, LPVOID, ULONG, ULONG*) override { return E_KS_NOT_FOUND; }
    STDMETHODIMP KsMethod(PKSMETHOD, ULONG, LPVOID, ULONG, ULONG*) override { return E_KS_NOT_FOUND; }
    STDMETHODIMP KsEvent(PKSEVENT, ULONG, LPVOID, ULONG, ULONG*) override { return E_KS_NOT_FOUND; }

    // IMFSampleAllocatorControl
    STDMETHODIMP SetDefaultAllocator(DWORD streamId, IUnknown* allocator) override
    {
        AutoLock l(_lock);
        if (_shutdown) return MF_E_SHUTDOWN;
        if (streamId != 0) return MF_E_INVALIDSTREAMNUMBER;
        _stream->SetAllocator(allocator);
        return S_OK;
    }
    STDMETHODIMP GetAllocatorUsage(DWORD streamId, DWORD* inputStreamId, MFSampleAllocatorUsage* usage) override
    {
        if (!inputStreamId || !usage) return E_POINTER;
        if (streamId != 0) return MF_E_INVALIDSTREAMNUMBER;
        *inputStreamId = streamId;
        *usage = MFSampleAllocatorUsage_UsesProvidedAllocator;
        return S_OK;
    }

private:
    std::atomic<ULONG> _ref{ 1 };
    CritSec _lock;
    CamKind _kind;
    bool _shutdown = false;
    ComPtr<IMFAttributes> _attrs;
    ComPtr<IMFMediaEventQueue> _queue;
    ComPtr<IMFPresentationDescriptor> _pd;
    MediaStream* _stream = nullptr;
};

// ---------------------------------------------------------------- ativador
class Activator : public IMFActivate
{
public:
    Activator(CamKind k) : _kind(k) { g_objects++; }
    ~Activator() { g_objects--; }
    HRESULT Initialize() { return MFCreateAttributes(&_a, 4); }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == __uuidof(IMFAttributes) || riid == __uuidof(IMFActivate))
        { *ppv = static_cast<IMFActivate*>(this); AddRef(); return S_OK; }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++_ref; }
    STDMETHODIMP_(ULONG) Release() override { ULONG r = --_ref; if (!r) delete this; return r; }

    // IMFActivate
    STDMETHODIMP ActivateObject(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        MediaSource* src = new (std::nothrow) MediaSource(_kind);
        if (!src) return E_OUTOFMEMORY;
        HRESULT hr = src->Initialize(_a.Get());
        if (SUCCEEDED(hr)) hr = src->QueryInterface(riid, ppv);
        else LogDbg(L"%s: falha ao criar fonte 0x%08X", Info(_kind).friendlyName, hr);
        src->Release();
        return hr;
    }
    STDMETHODIMP ShutdownObject() override { return S_OK; }
    STDMETHODIMP DetachObject() override { return S_OK; }

    // IMFAttributes (delegado)
    STDMETHODIMP GetItem(REFGUID k, PROPVARIANT* v) override { return _a->GetItem(k, v); }
    STDMETHODIMP GetItemType(REFGUID k, MF_ATTRIBUTE_TYPE* t) override { return _a->GetItemType(k, t); }
    STDMETHODIMP CompareItem(REFGUID k, REFPROPVARIANT v, BOOL* r) override { return _a->CompareItem(k, v, r); }
    STDMETHODIMP Compare(IMFAttributes* t, MF_ATTRIBUTES_MATCH_TYPE m, BOOL* r) override { return _a->Compare(t, m, r); }
    STDMETHODIMP GetUINT32(REFGUID k, UINT32* v) override { return _a->GetUINT32(k, v); }
    STDMETHODIMP GetUINT64(REFGUID k, UINT64* v) override { return _a->GetUINT64(k, v); }
    STDMETHODIMP GetDouble(REFGUID k, double* v) override { return _a->GetDouble(k, v); }
    STDMETHODIMP GetGUID(REFGUID k, GUID* v) override { return _a->GetGUID(k, v); }
    STDMETHODIMP GetStringLength(REFGUID k, UINT32* v) override { return _a->GetStringLength(k, v); }
    STDMETHODIMP GetString(REFGUID k, LPWSTR s, UINT32 n, UINT32* l) override { return _a->GetString(k, s, n, l); }
    STDMETHODIMP GetAllocatedString(REFGUID k, LPWSTR* s, UINT32* l) override { return _a->GetAllocatedString(k, s, l); }
    STDMETHODIMP GetBlobSize(REFGUID k, UINT32* v) override { return _a->GetBlobSize(k, v); }
    STDMETHODIMP GetBlob(REFGUID k, UINT8* b, UINT32 n, UINT32* s) override { return _a->GetBlob(k, b, n, s); }
    STDMETHODIMP GetAllocatedBlob(REFGUID k, UINT8** b, UINT32* s) override { return _a->GetAllocatedBlob(k, b, s); }
    STDMETHODIMP GetUnknown(REFGUID k, REFIID r, LPVOID* p) override { return _a->GetUnknown(k, r, p); }
    STDMETHODIMP SetItem(REFGUID k, REFPROPVARIANT v) override { return _a->SetItem(k, v); }
    STDMETHODIMP DeleteItem(REFGUID k) override { return _a->DeleteItem(k); }
    STDMETHODIMP DeleteAllItems() override { return _a->DeleteAllItems(); }
    STDMETHODIMP SetUINT32(REFGUID k, UINT32 v) override { return _a->SetUINT32(k, v); }
    STDMETHODIMP SetUINT64(REFGUID k, UINT64 v) override { return _a->SetUINT64(k, v); }
    STDMETHODIMP SetDouble(REFGUID k, double v) override { return _a->SetDouble(k, v); }
    STDMETHODIMP SetGUID(REFGUID k, REFGUID v) override { return _a->SetGUID(k, v); }
    STDMETHODIMP SetString(REFGUID k, LPCWSTR v) override { return _a->SetString(k, v); }
    STDMETHODIMP SetBlob(REFGUID k, const UINT8* b, UINT32 n) override { return _a->SetBlob(k, b, n); }
    STDMETHODIMP SetUnknown(REFGUID k, IUnknown* u) override { return _a->SetUnknown(k, u); }
    STDMETHODIMP LockStore() override { return _a->LockStore(); }
    STDMETHODIMP UnlockStore() override { return _a->UnlockStore(); }
    STDMETHODIMP GetCount(UINT32* c) override { return _a->GetCount(c); }
    STDMETHODIMP GetItemByIndex(UINT32 i, GUID* k, PROPVARIANT* v) override { return _a->GetItemByIndex(i, k, v); }
    STDMETHODIMP CopyAllItems(IMFAttributes* d) override { return _a->CopyAllItems(d); }

private:
    std::atomic<ULONG> _ref{ 1 };
    CamKind _kind;
    ComPtr<IMFAttributes> _a;
};

HRESULT CreateActivator(CamKind kind, REFIID riid, void** ppv)
{
    Activator* a = new (std::nothrow) Activator(kind);
    if (!a) return E_OUTOFMEMORY;
    HRESULT hr = a->Initialize();
    if (SUCCEEDED(hr)) hr = a->QueryInterface(riid, ppv);
    a->Release();
    return hr;
}
