#include <stdint.h>
#include "common.h"

enum {
	CIOS_HDR_MAGIC = 0x1EE7C105,
	CIOS_HDR_VERSION = 1,
};

struct cios_info {
	uint32_t magic;
	uint32_t header_version;
	uint32_t version;
	uint32_t base;
	char     name[16];
	char     verstr[16];

	unsigned char padding[16];
};

CHECK_STRUCT_SIZE(struct cios_info, 0x40);
