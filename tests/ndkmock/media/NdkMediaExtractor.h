#pragma once
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include "NdkMediaFormat.h"
typedef struct AMediaExtractor AMediaExtractor; enum { AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC = 0 };
AMediaExtractor* AMediaExtractor_new(); int AMediaExtractor_delete(AMediaExtractor*);
int AMediaExtractor_setDataSourceFd(AMediaExtractor*, int, off64_t, off64_t); size_t AMediaExtractor_getTrackCount(AMediaExtractor*);
AMediaFormat* AMediaExtractor_getTrackFormat(AMediaExtractor*, size_t); int AMediaExtractor_selectTrack(AMediaExtractor*, size_t);
int AMediaExtractor_seekTo(AMediaExtractor*, int64_t, int); ssize_t AMediaExtractor_readSampleData(AMediaExtractor*, uint8_t*, size_t);
int64_t AMediaExtractor_getSampleTime(AMediaExtractor*); bool AMediaExtractor_advance(AMediaExtractor*);
