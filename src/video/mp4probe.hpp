// ============================================================================
//  vid :: lectura de la CABECERA de un MP4 / MOV / M4V (sin decodificar nada).
//  Sin dependencias de Geode ni de ninguna libreria: se compila y se prueba en cualquier PC.
//
//  Sirve para: saber cuanto dura el video, a cuantos fps va, de que tamano es y con que codec esta codificado ANTES de
//  intentar decodificarlo (y poder avisar "HEVC no soportado en este sistema" en vez de fallar sin explicacion).
//  Lee solo las cajas de nivel superior y la caja `moov` (nunca el contenido de `mdat`), asi que es instantaneo
//  aunque el archivo pese varios GB.
// ============================================================================
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace vid {

struct Info {
    bool ok = false;
    std::string error;
    int width = 0, height = 0;            // tamano CODIFICADO del primer track de video
    int rotation = 0;                     // 0 / 90 / 180 / 270 (matriz del track: moviles en vertical)
    int displayW = 0, displayH = 0;       // tamano al REPRODUCIRLO (con la rotacion aplicada)
    double duration = 0;                  // segundos
    double fps = 0;                       // fotogramas medios por segundo
    uint32_t frames = 0;                  // numero de muestras de video
    std::string codec;                    // fourcc de la entrada de muestra: avc1, hvc1, av01, vp09...
    int avcProfile = 0;                   // (solo avc1/avc3) 66 baseline, 77 main, 100 high...
    bool hasAudio = false;
    bool decodable() const { return codec == "avc1" || codec == "avc3"; }   // H.264: lo unico que entienden TODOS los decodificadores del sistema
    std::string codecName() const {
        if (codec == "avc1" || codec == "avc3") return "H.264";
        if (codec == "hvc1" || codec == "hev1") return "H.265 (HEVC)";
        if (codec == "av01") return "AV1";
        if (codec == "vp09") return "VP9";
        if (codec == "mp4v") return "MPEG-4";
        return codec.empty() ? "?" : codec;
    }
};

namespace detail {
inline uint32_t be32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }
inline uint16_t be16(const uint8_t* p) { return uint16_t((p[0] << 8) | p[1]); }
inline uint64_t be64(const uint8_t* p) { return (uint64_t(be32(p)) << 32) | be32(p + 4); }
inline std::string fourcc(const uint8_t* p) {
    std::string s;
    for (int i = 0; i < 4; ++i) s += (p[i] >= 32 && p[i] < 127) ? char(p[i]) : '?';
    return s;
}

struct Box { std::string type; size_t payload = 0, end = 0; };   // posiciones dentro del buffer

// Itera las cajas hijas de [from, to) del buffer. Devuelve false si una caja no cabe (archivo cortado).
template <class F>
inline void forEachBox(const std::vector<uint8_t>& b, size_t from, size_t to, F&& fn) {
    size_t pos = from;
    while (pos + 8 <= to) {
        uint64_t sz = be32(&b[pos]);
        std::string type = fourcc(&b[pos + 4]);
        size_t hdr = 8;
        if (sz == 1) { if (pos + 16 > to) return; sz = be64(&b[pos + 8]); hdr = 16; }
        else if (sz == 0) sz = to - pos;
        if (sz < hdr || pos + sz > to) return;
        Box bx; bx.type = type; bx.payload = pos + hdr; bx.end = size_t(pos + sz);
        if (!fn(bx)) return;
        pos += size_t(sz);
    }
}

struct Track {
    bool video = false, audio = false;
    uint32_t timescale = 0; uint64_t mdur = 0;
    int w = 0, h = 0, rotation = 0;
    uint32_t samples = 0;
    std::string codec; int avcProfile = 0;
    uint32_t sttsCount = 0; uint64_t sttsDur = 0;
    uint32_t trackId = 0;
    uint32_t trexDur = 0;                // (fragmentado) duracion por defecto de muestra, de mvex/trex
};

inline void parseStbl(const std::vector<uint8_t>& b, const Box& stbl, Track& t) {
    forEachBox(b, stbl.payload, stbl.end, [&](const Box& c) {
        const uint8_t* p = &b[c.payload];
        const size_t len = c.end - c.payload;
        if (c.type == "stsd" && len >= 16) {
            const uint32_t n = be32(p + 4);
            if (n >= 1 && len >= 8 + 8) {
                const size_t e = c.payload + 8;                         // primera entrada: size(4) format(4) ...
                const size_t esz = be32(&b[e]);
                t.codec = fourcc(&b[e + 4]);
                if (esz >= 86 && e + 86 <= c.end) {                     // entrada de video: ancho/alto en el offset 32
                    t.w = be16(&b[e + 32]); t.h = be16(&b[e + 34]);
                    forEachBox(b, e + 86, std::min(c.end, e + esz), [&](const Box& cc) {
                        if (cc.type == "avcC" && cc.end - cc.payload >= 4) t.avcProfile = b[cc.payload + 1];
                        return true;
                    });
                }
            }
        } else if (c.type == "stts" && len >= 8) {
            const uint32_t n = be32(p + 4);
            for (uint32_t i = 0; i < n && 8 + size_t(i) * 8 + 8 <= len; ++i) {
                const uint32_t cnt = be32(p + 8 + size_t(i) * 8), dl = be32(p + 12 + size_t(i) * 8);
                t.sttsCount += cnt; t.sttsDur += uint64_t(cnt) * dl;
            }
        } else if (c.type == "stsz" && len >= 12) {
            t.samples = be32(p + 8);
        }
        return true;
    });
}

inline Track parseTrak(const std::vector<uint8_t>& b, const Box& trak) {
    Track t;
    int tkW = 0, tkH = 0;
    forEachBox(b, trak.payload, trak.end, [&](const Box& c) {
        if (c.type == "tkhd") {
            const uint8_t* p = &b[c.payload];
            const size_t len = c.end - c.payload;
            const int ver = p[0];
            if (len >= 24) t.trackId = ver == 1 ? be32(p + 20) : be32(p + 12);
            const size_t mat = ver == 1 ? 52 : 40;         // offset de la matriz 3x3 (36 bytes); tras ella: ancho y alto en 16.16
            if (len >= mat + 36 + 8) {
                const int32_t a = int32_t(be32(p + mat)), bb = int32_t(be32(p + mat + 4));
                int deg = int(std::lround(std::atan2(double(bb), double(a)) * 180.0 / 3.14159265358979323846));
                deg = ((deg % 360) + 360) % 360;
                t.rotation = (deg == 90 || deg == 180 || deg == 270) ? deg : 0;
                tkW = int(be32(p + mat + 36) >> 16); tkH = int(be32(p + mat + 40) >> 16);
            }
        } else if (c.type == "mdia") {
            forEachBox(b, c.payload, c.end, [&](const Box& m) {
                const uint8_t* p = &b[m.payload];
                const size_t len = m.end - m.payload;
                if (m.type == "mdhd" && len >= 24) {
                    if (p[0] == 1 && len >= 32) { t.timescale = be32(p + 20); t.mdur = be64(p + 24); }
                    else { t.timescale = be32(p + 12); t.mdur = be32(p + 16); if (t.mdur == 0xFFFFFFFFull) t.mdur = 0; }
                } else if (m.type == "hdlr" && len >= 12) {
                    const std::string h = fourcc(p + 8);
                    t.video = h == "vide"; t.audio = h == "soun";
                } else if (m.type == "minf") {
                    forEachBox(b, m.payload, m.end, [&](const Box& s) { if (s.type == "stbl") parseStbl(b, s, t); return true; });
                }
                return true;
            });
        }
        return true;
    });
    if (t.w <= 0 || t.h <= 0) { t.w = tkW; t.h = tkH; }
    return t;
}
} // namespace detail

namespace detail {
struct FragSum { uint64_t dur = 0; uint32_t samples = 0; };

// Suma duracion y muestras de la pista `trackId` en UN moof (buffer = payload de la caja moof).
inline void sumMoof(const std::vector<uint8_t>& b, uint32_t trackId, uint32_t trexDur, FragSum& out) {
    forEachBox(b, 0, b.size(), [&](const Box& traf) {
        if (traf.type != "traf") return true;
        uint32_t tid = 0, defDur = trexDur; bool mine = false;
        forEachBox(b, traf.payload, traf.end, [&](const Box& c) {
            const uint8_t* p = &b[c.payload]; const size_t len = c.end - c.payload;
            if (c.type == "tfhd" && len >= 8) {
                const uint32_t fl = (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
                tid = be32(p + 4); mine = tid == trackId;
                size_t o = 8;
                if (fl & 0x01) o += 8;
                if (fl & 0x02) o += 4;
                if ((fl & 0x08) && o + 4 <= len) defDur = be32(p + o);
            } else if (c.type == "trun" && mine && len >= 8) {
                const uint32_t fl = (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
                const uint32_t n = be32(p + 4);
                size_t o = 8;
                if (fl & 0x001) o += 4;
                if (fl & 0x004) o += 4;
                const size_t per = ((fl & 0x100) ? 4 : 0) + ((fl & 0x200) ? 4 : 0) + ((fl & 0x400) ? 4 : 0) + ((fl & 0x800) ? 4 : 0);
                out.samples += n;
                if (fl & 0x100) { for (uint32_t i = 0; i < n && o + per <= len; ++i, o += per) out.dur += be32(p + o); }
                else out.dur += uint64_t(n) * defDur;
            }
            return true;
        });
        return true;
    });
}
} // namespace detail

// Lee la cabecera de un archivo. `maxMoov`: tope de tamano de la caja moov que se carga en memoria.
// Soporta MP4/MOV normales Y FRAGMENTADOS (OBS, navegadores, grabadores de pantalla...): en esos el moov no trae la duracion
// (stts vacio, mdhd/mvhd = 0) y hay que sumar los fragmentos moof/trun.
inline Info probe(const std::string& path, size_t maxMoov = 256u * 1024u * 1024u) {
    Info info;
#ifdef _WIN32
    FILE* f = nullptr;                                   // la ruta llega en UTF-8: fopen(char*) fallaria con tildes, eñes, emojis...
    {
        std::wstring w;                                  // UTF-8 -> UTF-16 sin depender de windows.h
        for (size_t i = 0; i < path.size();) {
            const unsigned char c = (unsigned char)path[i];
            uint32_t cp = c; int extra = 0;
            if (c >= 0xF0) { cp = c & 0x07; extra = 3; } else if (c >= 0xE0) { cp = c & 0x0F; extra = 2; } else if (c >= 0xC0) { cp = c & 0x1F; extra = 1; }
            ++i;
            for (int k = 0; k < extra && i < path.size(); ++k, ++i) cp = (cp << 6) | ((unsigned char)path[i] & 0x3F);
            if (cp >= 0x10000) { cp -= 0x10000; w += wchar_t(0xD800 + (cp >> 10)); w += wchar_t(0xDC00 + (cp & 0x3FF)); }
            else w += wchar_t(cp);
        }
        f = _wfopen(w.c_str(), L"rb");
    }
#else
    FILE* f = std::fopen(path.c_str(), "rb");
#endif
    if (!f) { info.error = "No se pudo abrir el archivo"; return info; }
#ifdef _WIN32
    auto seek = [&](uint64_t off) { return _fseeki64(f, int64_t(off), SEEK_SET) == 0; };
    _fseeki64(f, 0, SEEK_END); const uint64_t fileSize = uint64_t(_ftelli64(f));
#else
    auto seek = [&](uint64_t off) { return fseeko(f, off_t(off), SEEK_SET) == 0; };
    fseeko(f, 0, SEEK_END); const uint64_t fileSize = uint64_t(ftello(f));
#endif
    // ---- fase 1: cajas de nivel superior (solo cabeceras; mdat se salta). Se anotan moov y todos los moof.
    struct Pos { uint64_t at, size; size_t hdr; };
    Pos moovPos{0, 0, 0}; bool haveMoov = false, sawFtyp = false;
    std::vector<Pos> moofs;
    for (uint64_t pos = 0; pos + 8 <= fileSize;) {
        uint8_t h[16];
        if (!seek(pos) || std::fread(h, 1, 8, f) != 8) break;
        uint64_t sz = detail::be32(h);
        const std::string type = detail::fourcc(h + 4);
        size_t hdr = 8;
        if (sz == 1) { if (std::fread(h + 8, 1, 8, f) != 8) break; sz = detail::be64(h + 8); hdr = 16; }
        else if (sz == 0) sz = fileSize - pos;
        if (sz < hdr) break;
        if (type == "ftyp") sawFtyp = true;
        else if (type == "moov" && !haveMoov) { moovPos = {pos, sz, hdr}; haveMoov = true; }
        else if (type == "moof" && moofs.size() < 400000) moofs.push_back({pos, sz, hdr});
        pos += sz;
    }
    auto readBox = [&](const Pos& p, std::vector<uint8_t>& dst, size_t cap) {
        const uint64_t pl = p.size - p.hdr;
        if (pl > cap || p.at + p.size > fileSize) return false;
        dst.resize(size_t(pl));
        return seek(p.at + p.hdr) && (pl == 0 || std::fread(dst.data(), 1, size_t(pl), f) == size_t(pl));
    };
    std::vector<uint8_t> moov;
    if (!haveMoov) {
        std::fclose(f);
        info.error = sawFtyp ? "El MP4 no tiene cabecera moov (descarga incompleta?)" : "No es un MP4/MOV valido";
        return info;
    }
    if (!readBox(moovPos, moov, maxMoov)) { std::fclose(f); info.error = "Archivo danado o incompleto (caja moov)"; return info; }

    // ---- fase 2: cabecera
    uint32_t mvTs = 0; uint64_t mvDur = 0, mehdDur = 0;
    std::vector<std::pair<uint32_t, uint32_t>> trex;       // (track_ID, default_sample_duration)
    detail::Track best; bool haveVideo = false;
    detail::forEachBox(moov, 0, moov.size(), [&](const detail::Box& c) {
        if (c.type == "mvhd") {
            const uint8_t* p = &moov[c.payload];
            const size_t len = c.end - c.payload;
            if (p[0] == 1 && len >= 32) { mvTs = detail::be32(p + 20); mvDur = detail::be64(p + 24); }
            else if (len >= 20) { mvTs = detail::be32(p + 12); mvDur = detail::be32(p + 16); if (mvDur == 0xFFFFFFFFull) mvDur = 0; }
        } else if (c.type == "mvex") {
            detail::forEachBox(moov, c.payload, c.end, [&](const detail::Box& m) {
                const uint8_t* p = &moov[m.payload]; const size_t len = m.end - m.payload;
                if (m.type == "mehd" && len >= 8) mehdDur = p[0] == 1 && len >= 12 ? detail::be64(p + 4) : detail::be32(p + 4);
                else if (m.type == "trex" && len >= 20) trex.push_back({detail::be32(p + 4), detail::be32(p + 12)});
                return true;
            });
        } else if (c.type == "trak") {
            detail::Track t = detail::parseTrak(moov, c);
            if (t.audio) info.hasAudio = true;
            if (t.video && !haveVideo) { best = t; haveVideo = true; }
        }
        return true;
    });
    if (!haveVideo) { std::fclose(f); info.error = "El archivo no tiene pista de video"; return info; }
    for (auto& te : trex) if (te.first == best.trackId) best.trexDur = te.second;

    // ---- fase 3: fragmentos (MP4 fragmentado): duracion y numero de frames = suma de los trun de la pista de video
    detail::FragSum fs;
    if (!moofs.empty()) {
        std::vector<uint8_t> buf;
        for (const auto& mp : moofs) {
            if (!readBox(mp, buf, 64u * 1024u * 1024u)) continue;
            detail::sumMoof(buf, best.trackId, best.trexDur, fs);
        }
    }
    std::fclose(f);

    info.codec = best.codec;
    info.avcProfile = best.avcProfile;
    info.width = best.w; info.height = best.h; info.rotation = best.rotation;
    const bool swap = best.rotation == 90 || best.rotation == 270;
    info.displayW = swap ? best.h : best.w; info.displayH = swap ? best.w : best.h;
    info.frames = best.sttsCount ? best.sttsCount : (fs.samples ? fs.samples : best.samples);
    if (best.timescale > 0 && best.sttsDur > 0) info.duration = double(best.sttsDur) / best.timescale;
    else if (best.timescale > 0 && fs.dur > 0) info.duration = double(fs.dur) / best.timescale;                 // fragmentado
    else if (best.timescale > 0 && best.mdur > 0) info.duration = double(best.mdur) / best.timescale;
    else if (mvTs > 0 && mvDur > 0) info.duration = double(mvDur) / mvTs;
    else if (mvTs > 0 && mehdDur > 0) info.duration = double(mehdDur) / mvTs;
    if (info.duration <= 0 && info.frames > 0 && best.timescale > 0 && best.trexDur > 0)
        info.duration = double(info.frames) * best.trexDur / best.timescale;
    if (info.duration > 0 && info.frames > 0) info.fps = double(info.frames) / info.duration;
    if (info.width <= 0 || info.height <= 0) { info.error = "No se pudo leer el tamano del video"; return info; }
    if (info.duration <= 0) {
        info.error = std::string("No se pudo leer la duracion del video (") + (moofs.empty() ? "sin fragmentos" : "MP4 fragmentado") +
                     "). Prueba a re-exportarlo o a pasarlo por ffmpeg: ffmpeg -i in.mp4 -c copy -movflags +faststart out.mp4";
        return info;
    }
    info.ok = true;
    return info;
}

} // namespace vid
