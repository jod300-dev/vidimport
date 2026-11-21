// Plataformas sin decodificador nativo (solo las pruebas de escritorio en Linux): decodeNative no hace nada y el muestreador usa ffmpeg.
#include "videosource.hpp"
namespace vid {
bool decodeNative(const DecodeRequest&, const RawSink&, std::string& err, std::atomic<bool>*) {
    err = "no hay decodificador nativo en esta plataforma";
    return false;
}
}
