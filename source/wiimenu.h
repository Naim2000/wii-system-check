#include <stdint.h>
#include <stdbool.h>

extern const char* const wiimenu_version_table[7][20];
extern const char wiimenu_region_table[2][7];

bool wiimenu_version_is_official(uint16_t);
const char* wiimenu_get_version(uint16_t);
char wiimenu_get_region(uint16_t);
void wiimenu_name_version(uint16_t, char* out);
