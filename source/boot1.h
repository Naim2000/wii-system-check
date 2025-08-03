struct boot1 {
	const char* name;
	uint32_t hash[5];
	bool trucha;
};

static const struct boot1 boot1versions[] = {
	{ "boot1a", { 0xb30c32b9, 0x62c7cd08, 0xabe33d01, 0x5b9b8b1d, 0xb1097544 }, true  },
	{ "boot1b", { 0xef3ef781, 0x09608d56, 0xdf5679a6, 0xf92e13f7, 0x8bbddfdf }, true  },
	{ "boot1c", { 0xd220c8a4, 0x86c631d0, 0xdf5adb31, 0x96ecbc66, 0x8780cc8d }, false },
	{ "boot1d", { 0xf793068a, 0x09e80986, 0xe2a023c0, 0xc23f0614, 0x0ed16974 }, false },
    {},
};
