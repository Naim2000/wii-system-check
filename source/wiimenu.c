#include <stdio.h>
#include <string.h>

#include "wiimenu.h"

// thank you YAWM ModMii Edition
static const uint16_t wiimenu_versions[] =
{
//	J		U		E		K

	64,		33,		66,					// 1.0
	128,	97,		130,				// 2.0
					162,				// 2.1
	192,	193,	194,				// 2.2
	224,	225,	226,				// 3.0
	256,	257,	258,				// 3.1
	288,	289,	290,				// 3.2
	352,	353,	354,	326,		// 3.3
	384,	385,	386, 				// 3.4
							390, 		// 3.5
	416,	417,	418,				// 4.0
	448,	449,	450,	454, 		// 4.1
	480,	481,	482,	486, 		// 4.2
	512,	513, 	514,	518, 		// 4.3/vWii 1.0.0
	544,	545,	546,				// vWii 4.0.0
	608,	609,	610,				// vWii 5.2.0

	0
};

const char* const wiimenu_version_table[7][20] =
{
//		0		1		2		3		4		5		6		7		8		9		10		11		12		13		14		15		16		17		18		19
	{	"",		"",		"1.0",	"",		"2.0",	"",		"2.2",	"3.0",	"3.1",	"3.2",	"",		"3.3",	"3.4",	"4.0",	"4.1",	"4.2",	"4.3",	"4.3",	"",		"4.3"	},
	{	"",		"1.0",	"",		"2.0",	"",		"",		"2.2",	"3.0",	"3.1",	"3.2",	"",		"3.3",	"3.4",	"4.0",	"4.1",	"4.2",	"4.3",	"4.3",	"",		"4.3"	},
	{	"",		"",		"1.0",	"",		"2.0",	"2.1",	"2.2",	"3.0",	"3.1",	"3.2",	"",		"3.3",	"3.4",	"4.0",	"4.1",	"4.2",	"4.3",	"4.3",	"",		"4.3"	},
	{	"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		""		},
	{	"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		""		},
	{	"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		""		},
	{	"",		"",		"",		"",		"",		"",		"",		"",		"",		"",		"3.3",	"",		"3.5",	"",		"4.1",	"4.2",	"4.3",	"",		"",		""		},
};

const char wiimenu_region_table[2][7] = {
	{
		[0] = 'J',
		[1] = 'U',
		[2] = 'E',
		[6] = 'K',
	},

	{
		[0] = 'J',
		[1] = 'E',
		[2] = 'P',
		[6] = 'K',
	}
};

bool wiimenu_version_is_official(uint16_t rev) {
	for (int i = 0; wiimenu_versions[i]; i++)
		if (rev == wiimenu_versions[i])
			return true;

	return false;
}

inline const char* wiimenu_get_version(uint16_t rev) {
	return wiimenu_version_is_official(rev) ? wiimenu_version_table[(rev & 0x001F)][(rev & 0xFFE0) >> 5] : "?.?";
}

inline char wiimenu_get_region(uint16_t rev) {
	return wiimenu_version_is_official(rev) ? wiimenu_region_table[0][(rev & 0x1F)] : '?';
}

void wiimenu_name_version(uint16_t rev, char* out) {
	out = stpcpy(out, "Wii System Menu ");

	if (wiimenu_version_is_official(rev)) {
		sprintf(out, "(Ver. %s%c)", wiimenu_get_version(rev), wiimenu_get_region(rev));
	} else {
		sprintf(out, "(v%hu?)", rev);
	}

}
