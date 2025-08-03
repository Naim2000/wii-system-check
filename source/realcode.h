#include <stdint.h>

int realcode_init();
void realcode_close();

void* IOS_VirtualToPhysical(void* addr);
void IOS_InvalidateDCache(const void* addr, uint32_t len);
void IOS_FlushDCache(void* addr, uint32_t len);
void IOS_InvalidateICache(void);
int IOS_SetUID(uint32_t uid, uint16_t gid);

int realcode_patch(void* start, unsigned len, const void* pattern, unsigned pattern_len, const void* patch, unsigned patch_len);
