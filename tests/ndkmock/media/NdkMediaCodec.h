#pragma once
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
typedef struct AMediaCodec AMediaCodec; typedef struct AMediaFormat AMediaFormat;
typedef struct { int32_t offset, size; int64_t presentationTimeUs; uint32_t flags; } AMediaCodecBufferInfo;
typedef int media_status_t; enum { AMEDIA_OK = 0 };
enum { AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM = 4, AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED = -2, AMEDIACODEC_INFO_TRY_AGAIN_LATER = -1 };
AMediaCodec* AMediaCodec_createDecoderByType(const char*); media_status_t AMediaCodec_delete(AMediaCodec*); media_status_t AMediaCodec_stop(AMediaCodec*);
media_status_t AMediaCodec_configure(AMediaCodec*, const AMediaFormat*, void*, void*, uint32_t); media_status_t AMediaCodec_start(AMediaCodec*);
ssize_t AMediaCodec_dequeueInputBuffer(AMediaCodec*, int64_t); uint8_t* AMediaCodec_getInputBuffer(AMediaCodec*, size_t, size_t*);
media_status_t AMediaCodec_queueInputBuffer(AMediaCodec*, size_t, off_t, size_t, uint64_t, uint32_t);
ssize_t AMediaCodec_dequeueOutputBuffer(AMediaCodec*, AMediaCodecBufferInfo*, int64_t); uint8_t* AMediaCodec_getOutputBuffer(AMediaCodec*, size_t, size_t*);
media_status_t AMediaCodec_releaseOutputBuffer(AMediaCodec*, size_t, bool); AMediaFormat* AMediaCodec_getOutputFormat(AMediaCodec*);
