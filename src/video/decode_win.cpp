// Windows: Media Foundation (IMFSourceReader). Viene con el sistema; decodifica H.264 sin instalar nada
// (en Windows "N" hace falta el Media Feature Pack: entonces se avisa y se usa ffmpeg si esta instalado).
// El lector de fuentes convierte YUV -> RGB32 por nosotros (MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING).
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <cstring>
#include "videosource.hpp"

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

namespace vid {
namespace {

template <class T> struct ComPtr {
    T* p = nullptr;
    ComPtr() = default;
    ~ComPtr() { reset(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    void reset() { if (p) { p->Release(); p = nullptr; } }
    T** put() { reset(); return &p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(std::max(n, 0)), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), w.data(), n);
    return w;
}

std::string hrText(const char* what, HRESULT hr) {
    char b[160];
    std::snprintf(b, sizeof b, "Media Foundation: %s fallo (0x%08lX)%s", what, static_cast<unsigned long>(hr),
                  hr == MF_E_TOPO_CODEC_NOT_FOUND || hr == MF_E_INVALIDMEDIATYPE ? " - falta el decodificador de video de Windows" : "");
    return b;
}

// Lee tamano y paso (stride) de la salida actual del lector.
bool readFormat(IMFSourceReader* rd, UINT32& w, UINT32& h, LONG& stride) {
    ComPtr<IMFMediaType> mt;
    if (FAILED(rd->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, mt.put()))) return false;
    if (FAILED(MFGetAttributeSize(mt.p, MF_MT_FRAME_SIZE, &w, &h)) || w == 0 || h == 0) return false;
    UINT32 s = 0;
    if (SUCCEEDED(mt->GetUINT32(MF_MT_DEFAULT_STRIDE, &s))) stride = LONG(INT32(s));
    else stride = LONG(w) * 4;
    if (stride == 0) stride = LONG(w) * 4;
    return true;
}
} // namespace

bool decodeNative(const DecodeRequest& rq, const RawSink& sink, std::string& err, std::atomic<bool>* cancel) {
    const HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool coInit = SUCCEEDED(hrCo);                       // RPC_E_CHANGED_MODE: ya habia otro modo en este hilo, se sigue sin desinicializar
    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (FAILED(hr)) { err = hrText("MFStartup", hr); if (coInit) CoUninitialize(); return false; }

    bool ok = false;
    {
        ComPtr<IMFAttributes> attr;
        ComPtr<IMFSourceReader> rd;
        hr = MFCreateAttributes(attr.put(), 2);
        if (SUCCEEDED(hr)) hr = attr->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);     // YUV -> RGB32 dentro del lector
        if (SUCCEEDED(hr)) hr = attr->SetUINT32(MF_SOURCE_READER_DISABLE_DXVA, TRUE);                // decodificacion por CPU: salida en memoria normal y estable
        if (SUCCEEDED(hr)) hr = MFCreateSourceReaderFromURL(widen(rq.path).c_str(), attr.p, rd.put());
        if (FAILED(hr)) { err = hrText("abrir el archivo", hr); goto done; }

        rd->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
        hr = rd->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
        if (FAILED(hr)) { err = hrText("seleccionar el video", hr); goto done; }

        {
            ComPtr<IMFMediaType> want;
            hr = MFCreateMediaType(want.put());
            if (SUCCEEDED(hr)) hr = want->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            if (SUCCEEDED(hr)) hr = want->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
            if (SUCCEEDED(hr)) hr = rd->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, want.p);
            if (FAILED(hr)) { err = hrText("pedir RGB32", hr); goto done; }
        }

        UINT32 w = 0, h = 0; LONG stride = 0;
        if (!readFormat(rd.p, w, h, stride)) { err = "Media Foundation: no se pudo leer el formato de salida"; goto done; }

        if (rq.start > 0.05) {                                   // ir (cerca de) al principio del tramo; el muestreador descarta lo anterior
            PROPVARIANT pv; PropVariantInit(&pv);
            pv.vt = VT_I8; pv.hVal.QuadPart = LONGLONG(rq.start * 1e7);
            rd->SetCurrentPosition(GUID_NULL, pv);
            PropVariantClear(&pv);
        }

        std::vector<uint8_t> flipped;
        const LONGLONG endT = LONGLONG(rq.end * 1e7);
        int delivered = 0;
        for (;;) {
            if (cancel && cancel->load(std::memory_order_relaxed)) break;
            DWORD streamIdx = 0, flags = 0; LONGLONG ts = 0;
            ComPtr<IMFSample> sample;
            hr = rd->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &streamIdx, &flags, &ts, sample.put());
            if (FAILED(hr)) { err = hrText("leer un frame", hr); goto done; }
            if (flags & MF_SOURCE_READERF_ERROR) { err = "Media Foundation: error al decodificar"; goto done; }
            if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) readFormat(rd.p, w, h, stride);
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
            if (!sample) continue;                               // hueco en el flujo (MF_SOURCE_READERF_STREAMTICK)

            ComPtr<IMFMediaBuffer> buf;
            if (FAILED(sample->ConvertToContiguousBuffer(buf.put()))) continue;
            BYTE* data = nullptr; DWORD maxLen = 0, curLen = 0;
            if (FAILED(buf->Lock(&data, &maxLen, &curLen)) || !data) continue;
            const LONG absStride = stride < 0 ? -stride : stride;
            const size_t need = size_t(absStride) * h;
            bool cont = true;
            {
                const uint8_t* top = data;
                if (stride < 0) {                                // DIB de abajo arriba: se voltea a filas de arriba abajo
                    flipped.resize(need);
                    for (UINT32 y = 0; y < h; ++y) std::memcpy(&flipped[size_t(y) * absStride], data + size_t(h - 1 - y) * absStride, size_t(absStride));
                    top = flipped.data();
                }
                if (curLen >= need) {                            // (si el buffer es mas corto de lo esperado se ignora el frame en vez de leer fuera)
                    cont = sink(top, int(w), int(h), int(absStride), PixFmt::BGRA, double(ts) / 1e7);   // RGB32: el 4o byte no es alfa; el sumidero lo ignora
                    ++delivered;
                }
            }
            buf->Unlock();
            if (!cont) break;
            if (ts > endT + LONGLONG(2e7)) break;                // 2 s mas alla del final: ya no hace falta
        }
        ok = delivered > 0;
        if (!ok && err.empty()) err = "Media Foundation: no devolvio ningun frame";
    }
done:
    MFShutdown();
    if (coInit) CoUninitialize();
    return ok;
}

} // namespace vid
#endif // _WIN32
