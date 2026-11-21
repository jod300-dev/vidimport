#pragma once
#include <stdint.h>
typedef struct AMediaFormat AMediaFormat;
int AMediaFormat_delete(AMediaFormat*); bool AMediaFormat_getString(AMediaFormat*, const char*, const char**); bool AMediaFormat_getInt32(AMediaFormat*, const char*, int32_t*);
void AMediaFormat_setInt32(AMediaFormat*, const char*, int32_t);
