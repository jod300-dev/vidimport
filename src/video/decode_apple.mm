// macOS / iOS: AVFoundation. AVAssetReader decodifica con el hardware del dispositivo (VideoToolbox) y entrega BGRA directamente.
// Escrito sin depender de ARC: solo usa constructores de conveniencia (objetos autoreleased) dentro de un @autoreleasepool.
#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#include "videosource.hpp"

namespace vid {

bool decodeNative(const DecodeRequest& rq, const RawSink& sink, std::string& err, std::atomic<bool>* cancel) {
    @autoreleasepool {
        NSString* p = [NSString stringWithUTF8String:rq.path.c_str()];
        if (!p) { err = "AVFoundation: ruta no valida"; return false; }
        AVURLAsset* asset = [AVURLAsset URLAssetWithURL:[NSURL fileURLWithPath:p] options:nil];
        NSArray* tracks = [asset tracksWithMediaType:AVMediaTypeVideo];
        if (tracks.count == 0) { err = "AVFoundation: el archivo no tiene video (o el codec no esta soportado)"; return false; }
        AVAssetTrack* track = [tracks objectAtIndex:0];
        NSError* nsErr = nil;
        AVAssetReader* reader = [AVAssetReader assetReaderWithAsset:asset error:&nsErr];
        if (!reader) { err = std::string("AVFoundation: ") + (nsErr ? [[nsErr localizedDescription] UTF8String] : "no se pudo abrir"); return false; }
        NSDictionary* settings = @{ (__bridge id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA) };
        AVAssetReaderTrackOutput* out = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:track outputSettings:settings];
        out.alwaysCopiesSampleData = NO;
        if (![reader canAddOutput:out]) { err = "AVFoundation: no se pudo leer la pista de video"; return false; }
        [reader addOutput:out];
        const double s = std::max(0.0, rq.start - 0.5), e = std::max(s + 0.1, rq.end + 0.5);
        reader.timeRange = CMTimeRangeMake(CMTimeMakeWithSeconds(s, 600), CMTimeMakeWithSeconds(e - s, 600));
        if (![reader startReading]) {
            err = std::string("AVFoundation: ") + (reader.error ? [[reader.error localizedDescription] UTF8String] : "no se pudo empezar a leer");
            return false;
        }
        int delivered = 0;
        for (;;) {
            if (cancel && cancel->load(std::memory_order_relaxed)) break;
            bool cont = true;
            @autoreleasepool {
                CMSampleBufferRef sb = [out copyNextSampleBuffer];
                if (!sb) break;                                   // (break dentro de @autoreleasepool sale del bucle for: es un break de C)
                CVImageBufferRef ib = CMSampleBufferGetImageBuffer(sb);
                if (ib) {
                    const double pts = CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(sb));
                    CVPixelBufferLockBaseAddress(ib, kCVPixelBufferLock_ReadOnly);
                    const uint8_t* base = (const uint8_t*)CVPixelBufferGetBaseAddress(ib);
                    if (base) {
                        cont = sink(base, (int)CVPixelBufferGetWidth(ib), (int)CVPixelBufferGetHeight(ib), (int)CVPixelBufferGetBytesPerRow(ib), PixFmt::BGRA, pts);
                        ++delivered;
                    }
                    CVPixelBufferUnlockBaseAddress(ib, kCVPixelBufferLock_ReadOnly);
                }
                CFRelease(sb);
            }
            if (!cont) break;
        }
        [reader cancelReading];
        if (delivered == 0) { err = "AVFoundation: no devolvio ningun frame"; return false; }
        return true;
    }
}

} // namespace vid
