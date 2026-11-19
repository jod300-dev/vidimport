// Lectura de MP4/MOV/WMV/AVI con Media Foundation (Windows). Sin dependencias externas. Se compila SIN Geode (solo la API de Windows).
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include "source.hpp"

namespace vx {
namespace {

template <class T> struct Com {
    T* p = nullptr;
    Com() = default; Com(const Com&) = delete; Com& operator=(const Com&) = delete;
    ~Com() { if (p) p->Release(); }
    T* operator->() const { return p; }
};

struct MfSession {
    bool co = false, mf = false;
    MfSession() { HRESULT h = CoInitializeEx(nullptr, COINIT_MULTITHREADED); co = SUCCEEDED(h); mf = SUCCEEDED(MFStartup(MF_VERSION)); }
    ~MfSession() { if (mf) MFShutdown(); if (co) CoUninitialize(); }
};

constexpr DWORD kVideo = (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM;

struct Reader {
    Com<IMFSourceReader> rd;
    UINT32 w = 0, h = 0; LONG stride = 0; double fps = 30, duration = 0;
};

bool openReader(const fs::path& path, Reader& R, std::string& err) {
    Com<IMFAttributes> at;
    if (FAILED(MFCreateAttributes(&at.p, 1))) { err = "Media Foundation: no se pudo iniciar"; return false; }
    at->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);                      // permite convertir YUV -> RGB32
    HRESULT hr = MFCreateSourceReaderFromURL(path.c_str(), at.p, &R.rd.p);
    if (FAILED(hr)) { err = "Windows no puede abrir este video (falta el codec o el formato no es compatible)"; return false; }
    R.rd->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
    R.rd->SetStreamSelection(kVideo, TRUE);
    Com<IMFMediaType> mt;
    MFCreateMediaType(&mt.p);
    mt->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    mt->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    hr = R.rd->SetCurrentMediaType(kVideo, nullptr, mt.p);
    if (FAILED(hr)) { err = "Windows no pudo convertir el video a RGB (codec no disponible)"; return false; }
    Com<IMFMediaType> cur;
    if (FAILED(R.rd->GetCurrentMediaType(kVideo, &cur.p))) { err = "No se pudo leer el formato del video"; return false; }
    MFGetAttributeSize(cur.p, MF_MT_FRAME_SIZE, &R.w, &R.h);
    R.stride = (LONG)MFGetAttributeUINT32(cur.p, MF_MT_DEFAULT_STRIDE, 0);
    if (R.stride == 0) R.stride = (LONG)(R.w * 4);
    UINT32 n = 0, d = 1;
    if (SUCCEEDED(MFGetAttributeRatio(cur.p, MF_MT_FRAME_RATE, &n, &d)) && d > 0 && n > 0) R.fps = double(n) / double(d);
    PROPVARIANT pv; PropVariantInit(&pv);
    if (SUCCEEDED(R.rd->GetPresentationAttribute((DWORD)MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &pv)) && pv.vt == VT_UI8) R.duration = double(pv.uhVal.QuadPart) / 1e7;
    PropVariantClear(&pv);
    return R.w > 0 && R.h > 0;
}

} // namespace

bool probeWmf(const fs::path& path, VideoInfo& info) {
    MfSession ses; if (!ses.mf) return false;
    Reader R; std::string err;
    if (!openReader(path, R, err)) return false;
    info.w = int(R.w); info.h = int(R.h); info.fps = R.fps; info.duration = R.duration; info.ok = true; info.how = "Media Foundation";
    return true;
}

bool decodeWmf(const DecodeRequest& rq, Clip& out, std::string& err) {
    MfSession ses; if (!ses.mf) { err = "Media Foundation no disponible"; return false; }
    Reader R;
    if (!openReader(rq.path, R, err)) return false;
    out.srcW = int(R.w); out.srcH = int(R.h); out.srcFps = R.fps; out.srcDuration = R.duration;
    if (rq.startSec > 0.01) {
        PROPVARIANT v; PropVariantInit(&v); v.vt = VT_I8; v.hVal.QuadPart = (LONGLONG)(rq.startSec * 1e7);
        R.rd->SetCurrentPosition(GUID_NULL, v);
        PropVariantClear(&v);
    }
    const double dt = 1.0 / rq.fps, srcDt = 1.0 / std::max(1.0, R.fps), endSec = rq.startSec + rq.maxSeconds;
    double tNext = rq.startSec;
    const double span = std::max(0.1, std::min(rq.maxSeconds, R.duration > 0 ? R.duration - rq.startSec : rq.maxSeconds));
    const bool bottomUp = R.stride < 0; const size_t pitch = size_t(bottomUp ? -R.stride : R.stride);
    while (tNext < endSec - 1e-9) {
        DWORD si = 0, flags = 0; LONGLONG ts = 0; Com<IMFSample> smp;
        HRESULT hr = R.rd->ReadSample(kVideo, 0, &si, &flags, &ts, &smp.p);
        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) break;
        if (!smp.p) continue;
        const double tSec = double(ts) / 1e7;
        if (tSec + 0.5 * srcDt < tNext) continue;                                          // muestra anterior a lo pedido (el seek cae en un keyframe)
        Com<IMFMediaBuffer> buf;
        if (FAILED(smp->ConvertToContiguousBuffer(&buf.p))) continue;
        BYTE* data = nullptr; DWORD maxLen = 0, curLen = 0;
        if (FAILED(buf->Lock(&data, &maxLen, &curLen))) continue;
        Image im; im.w = int(R.w); im.h = int(R.h); im.rgba.resize(size_t(R.w) * R.h * 4);
        for (UINT32 y = 0; y < R.h; ++y) {
            const BYTE* row = data + (bottomUp ? size_t(R.h - 1 - y) : size_t(y)) * pitch;
            if (size_t(row - data) + R.w * 4 > curLen) break;
            uint8_t* d = &im.rgba[size_t(y) * R.w * 4];
            for (UINT32 x = 0; x < R.w; ++x) { d[x * 4] = row[x * 4 + 2]; d[x * 4 + 1] = row[x * 4 + 1]; d[x * 4 + 2] = row[x * 4]; d[x * 4 + 3] = 255; }   // BGRX -> RGBA
        }
        buf->Unlock();
        Image red = src::reduce(im, rq.maxSide, rq.gray);
        while (tSec + 0.5 * srcDt >= tNext && tNext < endSec - 1e-9) { out.frames.push_back(red); tNext += dt; }
        gdx::report(rq.prog, std::min(1.f, float((tNext - rq.startSec) / span)));
        if (gdx::isCancelled(rq.prog)) break;
    }
    if (out.frames.empty()) { err = "No se pudo leer ningun frame con Media Foundation"; return false; }
    return true;
}

} // namespace vx
#endif
