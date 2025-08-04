#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>

#include <ogc/system.h>
#include <ogc/video.h>
#include <ogc/console.h>
#include <ogc/ios.h>
#include <ogc/ipc.h>
#include <ogc/es.h>
#include <ogc/isfs.h>
#include <fat.h>

#include "common.h"
#include "pad.h"
#include "prodinfo.h"
#include "boot1.h"
#include "wiimenu.h"
#include "hbc.h"
#include "ciosinfo.h"
#include "sys.h"
#include "otp.h"
#include "realcode.h"

struct PrintConsole console;

#define IS_BOOTMII(slot, rev) ((slot == 254) && (rev == 0xFF01 || rev == 31338))
typedef struct ios {
	union {
		uint16_t revision;
		struct {
			uint8_t rev_ma, rev_mi;
		};
	};
	bool installed: 1;
	bool stubbed: 1;
	bool vIOS: 1;

	// *cIOS*
	struct cios_info cios;

	// Patches
	union {
		unsigned patches;
		struct {
			unsigned SignaturePatches: 2; // 1: sigpatches, 2: trucha
			unsigned ESIdentify: 1;
			unsigned FSPermissions: 2; // 1: permissions patch, 2: root access
			unsigned FlashAccess: 1;
			unsigned USB2: 2; // 1: EHCI, 2: ehcmodule
		};
	};
} ios;

static struct SystemInfo {
	// console
	struct {
		const char* type;
		uint32_t id;
		uint32_t hollywood_rev;
		bool latte;
		char model[32];
		char area[8];
		char serial[24];
	} console;

	struct {
		union {
			uint32_t value;
			struct {
				uint32_t year: 16;
				uint32_t month: 8;
				uint32_t day: 8;
			};
		} rel_date;

		int dvd_support;
		int rev;
		int dev_code;
		int error;
	} drive;

	// boot1/boot2
	const struct boot1* boot1;
	uint32_t boot2version;

	// IOS
	ios iosList[0x100];

	// system-menu
	struct {
		const char* version;
		int revision;
		int region;
		int ios;
		int priiloader;
	} sysmenu;

	struct {
		const struct hbcVersion* version;
		int revision;
		int ios;
	} hbc;
} SystemInfo = {};

int getConsoleInfo() {
	SystemInfo.console.hollywood_rev = SYS_GetHollywoodRevision();
	SystemInfo.console.latte = IS_WIIU;
	ES_GetDeviceID(&SystemInfo.console.id);

	ProductInfo_Init();
	SystemInfo.console.type = ProductInfo_GetConsoleType(SystemInfo.console.model);
	ProductInfo_GetSerial(SystemInfo.console.serial);

	if (!ProductInfo_Find("AREA", SystemInfo.console.area, sizeof SystemInfo.console.area))
		strcpy(SystemInfo.console.area, "???");

	return 0;
}

int getBootInfo() {
	if (IS_WIIU) // not applicable
		return -1;

	static struct boot1 fallback = { "boot1<?>", {}, false };

	otp_read(0, 5, fallback.hash);

	for (const struct boot1* boot1 = boot1versions; boot1->name != NULL; boot1++) {
		if (memcmp(fallback.hash, boot1->hash, sizeof boot1->hash) == 0) {
			SystemInfo.boot1 = boot1;
		}
	}

	if (!SystemInfo.boot1) {
		errorf("Unrecognized boot1 version");
		errorf("{ %08X %08X %08X %08X %08X }\n", fallback.hash[0], fallback.hash[1], fallback.hash[2], fallback.hash[3], fallback.hash[4]);
		SystemInfo.boot1 = &fallback;
	}

	ES_GetBoot2Version(&SystemInfo.boot2version);

	return 0;
}

/* Go check out https://github.com/Aeplet/DiscDriveDateChecker guys */
#include <di/di.h>
int getDriveInfo() {
	int ret;
	DI_DriveID drive_id;

	DI_Init();
	ret = DI_Identify(&drive_id);
	if (ret == 0) {
		SystemInfo.drive.rel_date.value = drive_id.rel_date;
		SystemInfo.drive.rev = drive_id.rev;
		SystemInfo.drive.dev_code = drive_id.dev_code;

		// The cursed.
		const uint32_t D3_REL_DATE = 0x20080714;
		if (drive_id.rel_date < D3_REL_DATE)
			SystemInfo.drive.dvd_support = 1;

		else if (drive_id.rel_date == D3_REL_DATE)
			SystemInfo.drive.dvd_support = 2;

		else
			SystemInfo.drive.dvd_support = 0;

	} else {
		if (ret == EIO)
			errorf(CONSOLE_BG_RED "Where is your disc drive?" CONSOLE_RESET);

		SystemInfo.drive.rel_date.value = 0;
		SystemInfo.drive.error = ret;
		ret = -1;
	}

	DI_Close();
	return ret;
}

static signed_blob* sys_certificates = NULL;
static unsigned int sys_certificates_len = 0;

int readSysCertificates() {
	int ret, fd;
	fstats filestats [[gnu::aligned(0x20)]] = {};

	fd = ret = ISFS_Open("/sys/cert.sys", 1);
	if (ret < 0) {
		print_error("ISFS_Open(/sys/cert.sys)", ret);
		return ret;
	}

	ISFS_GetFileStats(fd, &filestats);

	sys_certificates_len = filestats.file_length;
	sys_certificates = aligned_alloc(0x40, sys_certificates_len);
	if (!sys_certificates) {
		errorf("memory allocation failed (%#x)", sys_certificates_len);
		ISFS_Close(fd);
		return -1;
	}

	ISFS_Read(fd, sys_certificates, sys_certificates_len);
	ret = ISFS_Close(fd); // if an error happened then read will give a short length; close will give us the error code
	if (ret < 0)
		print_error("ISFS_Read(/sys/cert.sys)", ret);

	return ret;
}

int processInstalledTitles() {
	int ret;
	uint32_t  count;
	uint64_t* titles;

	puts("Getting title list...");

	ret = ES_GetNumTitles(&count);
	if (ret < 0) {
		print_error("ES_GetNumTitles", ret);
		return ret;
	}
	printf("Titles count: %u\n", count);

	titles = calloc(count, sizeof(uint64_t));
	if (!titles) {
		errorf("Memory allocation failed");
		return -1;
	}

	ret = ES_GetTitles(titles, count);
	if (ret < 0) {
		print_error("ES_GetTitles", ret);
		return ret;
	}

	puts("Processing title list...");
	for (int i = 0; i < count; i++) {
		uint64_t title = titles[i];
		uint32_t viewsize = 0;
		static uint8_t _view_buffer[offsetof(tmd_view, contents[MAX_NUM_TMD_CONTENTS])];
		tmd_view *view = (tmd_view *)_view_buffer;

		if ((title - 0x100000002LL) < 256) { // <system title>
			ret = ES_GetTMDViewSize(title, &viewsize);
			if (ret < 0) {
				print_error("ES_GetTMDViewSize(%016llx)", ret, title);
				if (ret == -106 && title != 0x100000002LL) ES_DeleteTitle(title);
				continue;
			}

			ret = ES_GetTMDView(title, view, viewsize);
			if (ret < 0) {
				print_error("ES_GetTMDView(%016llx)", ret, title);
				continue;
			}

			if (title == 0x100000002LL) { // System menu
				SystemInfo.sysmenu.revision = view->title_version;
				SystemInfo.sysmenu.version = wiimenu_get_version(view->title_version);
				SystemInfo.sysmenu.region = wiimenu_get_region(view->title_version);
				SystemInfo.sysmenu.ios = view->sys_version;
			}

			if (title - 0x100000003LL < 253) {
				int slot = title & 0xFF;
				struct ios* ios = &SystemInfo.iosList[slot];

				ios->installed = true;
				ios->revision  = view->title_version;
				ios->stubbed   = ((ios->revision & 0x00FF) == 0 || ios->revision == 404);

				if (ios->stubbed || IS_BOOTMII(slot, ios->revision))
					continue;

				for (int j = 0; j < view->num_contents; j++) {
					tmd_view_content* p = &view->contents[j];
					if (view->contents[j].index == 0) {
						// index 0 is not shared; that'd be kind of dumb
						char path[64];
						sprintf(path, "/title/00000001/%08x/content/%08x.app", slot, p->cid);
						int fd = ret = ISFS_Open(path, ISFS_OPEN_READ);
						if (ret < 0) {
							print_error("ISFS_Open(%016llx/%08x.app)", ret, title, p->cid);
						} else {
							static uint8_t index0buffer[0x40] [[gnu::aligned(0x20)]];
							ISFS_Read(fd, index0buffer, 0x40);
							ISFS_Close(fd);

							struct cios_info* cios = (struct cios_info *)index0buffer;
							if (cios->magic == CIOS_HDR_MAGIC && cios->header_version == CIOS_HDR_VERSION)
								memcpy(&ios->cios, index0buffer, sizeof index0buffer);

						}

						break;
					}
				}
			}
		}
		else if (title >> 32 == 0x00010001) {
			for (const struct hbcVersion* hbc = hbcversions; hbc->titleID; hbc++) {
				if ((title & 0xFFFFFFFF) == hbc->titleID) {
					ret = ES_GetTMDViewSize(title, &viewsize);
					if (ret < 0) {
						print_error("ES_GetTMDViewSize(%016llx)", ret, title);
						if (ret == -106 && title != 0x100000002LL) ES_DeleteTitle(title);
						continue;
					}

					ret = ES_GetTMDView(title, view, viewsize);
					if (ret < 0) {
						print_error("ES_GetTMDView(%016llx)", ret, title);
						continue;
					}

					SystemInfo.hbc.version = hbc;
					SystemInfo.hbc.revision = view->title_version;
					SystemInfo.hbc.ios = view->sys_version;
				}
			}
		}
	}

	free(titles);

	return ret;
}

int testFSPermissions(void) {
	ISFS_Initialize();
	ISFS_CreateFile("/tmp/test", 0, 0, 0, 0);
	int ret = ISFS_SetAttr("/tmp/test", 0x0, 0x0, 0, 0, 0, 0); // uid 0
	if (ret != 0) {
		int fd = ISFS_Open("/tmp/test", 4);
		if (fd >= 0) {
			ISFS_Close(fd);
			ret = 1; // Permissions patch.
		} else {
			ret = 0;
		}
	} else {
		ret = 2; // Root access.
	}

	ISFS_Delete("/tmp/test");
	ISFS_Deinitialize();
	return ret;
}

#include "es_structs.h"

static const TitleMetadata sample_tmd [[gnu::aligned(0x40)]] = {
	.signature = {
		.type = SIG_RSA2048_SHA1,
		.issuer = "Root-CA00000001-CP00000004",
	},

	.title_id = 0x0001000150494B41,
	.group_id = 0x1,

	.num_contents = 1, // We need 1 otherwise libogc will try allocate 0 bytes and promptly fail. (ES_Identify)
	.contents = {
		[0] = {
			.cid = 0x70696B61,
			.index = 0,
			.type = 0x8001,
			.size = 0,
			.hash = { 0x67, 0x45, 0x23, 0x01, 0xEF, 0xCD, 0xAB, 0x89, 0x98, 0xBA ,0xDC, 0xFE, 0x10, 0x32, 0x54, 0x76, 0xC3, 0xD2, 0xE1, 0xF0 },
		},
	},
};

static const TitleMetadata fakesigned_tmd [[gnu::aligned(0x40)]] = {
	.signature = {
		.type = SIG_RSA2048_SHA1,
		.issuer = "Root-CA00000001-CP00000004",
	},

	.title_id = 0x0001000150494B41,
	.group_id = 0x1,

	.minor_version = 0x60,
};

static const Ticket sample_ticket [[gnu::aligned(0x40)]] = {
	.signature = {
		.type = SIG_RSA2048_SHA1,
		.issuer = "Root-CA00000001-XS00000003",
	},

	.title_key = "thepikachugamer",
	.ticket_id = 0x0001000070696B61,
	.title_id = 0x0001000150494B41,
};

static const Ticket fakesigned_ticket [[gnu::aligned(0x40)]] = {
	.signature = {
		.type = SIG_RSA2048_SHA1,
		.issuer = "Root-CA00000001-XS00000003",
	},

	.title_key = "thepikachugamer",
	.ticket_id = 0x0001000070696B61,
	.title_id = 0x0001000150494B41,

	.padding3 = { 0xA0, 0x00 },
};

static const tikview ticket_view [[gnu::aligned(0x20)]] = {
	.ticketid = sample_ticket.ticket_id,
	.titleid = sample_ticket.title_id,
};

static const SignerCert sample_cert [[gnu::aligned(0x40)]] = {
	.signature = {
		.type = SIG_RSA2048_SHA1,
		.issuer = "Root-CA00000001",
	},

	.header = {
		.type = KEY_RSA2048,
		.name = "XS50494B41",
		.keyid = 0x70696B61,
	},
};

int testESIdenitfy(void) {
	unsigned int keyslot;
	// sizeof on the TMD does not work
	int ret = ES_Identify((signed_blob *)&sample_cert, sizeof sample_cert, (signed_blob *)&sample_tmd, S_TMD_SIZE(&sample_tmd), (signed_blob *)&sample_ticket, sizeof sample_ticket, &keyslot);
	if (ret == 0) // Shouldn't happen really
		ES_DeleteTitle(sample_tmd.title_id);

	// Conventional success code: -1027
	return (ret != -1017 && ret != ES_EINVAL);
}

int testSignaturePatches(void) {
	uint32_t count;
	// A little painful: this function does not exist in earlier IOSes
	// We can check for -1017
	int ret = ES_GetNumStoredTMDContents((signed_blob *)&sample_tmd, S_TMD_SIZE(&sample_tmd), &count);
	if (ret != -1017) {
		// Dolphin does not check the signature on the provided TMD, unlike ES
		if (ret == 0)
			return 1; // Signature patches

		ret = ES_GetNumStoredTMDContents((signed_blob *)&fakesigned_tmd, S_TMD_SIZE(&fakesigned_tmd), &count);
		if (ret == 0)
			return 2; // Trucha bug!

		return 0;
	}

	// Okay, ticket time
	if (sys_certificates) {
		ret = ES_AddTicket(sys_certificates, sys_certificates_len, (signed_blob *)&sample_ticket, sizeof sample_ticket, NULL, 0);
		if (ret == 0) {
			ES_DeleteTicket(&ticket_view);
			return 1;
		}

		ret = ES_AddTicket(sys_certificates, sys_certificates_len, (signed_blob *)&fakesigned_ticket, sizeof fakesigned_ticket, NULL, 0);
		if (ret == 0) {
			ES_DeleteTicket(&ticket_view);
			return 2;
		}
	} else {
		errorf("where are the sys_certificates?");
	}

	return 0;
}

int testFlashAccess(void) {
	int fd = IOS_Open("/dev/flash", 0);
	if (fd >= 0) IOS_Close(fd);

	return (fd >= 0);
}

struct STMVersion {
	// YY MM DD hh mm ss
	char    datetimestr[12]; // vwii: 120112152854, wii: 090727100523
	uint8_t things[6]; // vwii: { 0, 0, 2, 4, 0xFF, 0 }, wii: <same>
	uint8_t padding[14];
};
CHECK_STRUCT_SIZE(struct STMVersion, 0x20);

[[gnu::format(printf, 1, 2)]]
int writeReportF(const char* fmt, ...);
bool isvIOS(void) {
	struct STMVersion* const version = (struct STMVersion *)0x90001000; // random m2 address in fear of Starlet being stupid

	int fd = IOS_Open("/dev/stm/immediate", 0);
	if (fd < 0) {
		print_error("IOS_Open(/dev/stm/immediate)", fd);
		return false;
	}

	memset(version, 0, sizeof *version);
	IOS_Ioctl(fd, 0x7001, NULL, 0, version, sizeof(struct STMVersion));
	IOS_Close(fd);

	uint32_t build_date = 0, build_time = 0;
	sscanf(version->datetimestr, "%06x%06x", &build_date, &build_time);
	if (build_date != 0x000101) // IOS9 killing me
		return (build_date >= 0x120101);

	// Fk it who cares about IOS9
	return SystemInfo.console.latte;
}

int testUSB2(void) {
	int fd;

	fd = IOS_Open("/dev/usb123", 0);
	if (fd >= 0) {
		IOS_Close(fd);
		return 2;
	}

	fd = IOS_Open("/dev/usb2", 0);
	if (fd >= 0) {
		IOS_Close(fd);
		return 2;
	}

	fd = IOS_Open("/dev/usb/ehc", 0);
	if (fd >= 0) {
		IOS_Close(fd);
		return 2;
	}

	else if (fd == -1) { // EPERM; we are not USB_VEN
		return 1;
	}

	return 0;
}

int testIOS(int i) {
	struct ios* ios = &SystemInfo.iosList[i];

	if (!ios->installed || ios->stubbed || IS_BOOTMII(i, ios->revision))
		return -1;

	printf("Testing IOS%i...\n", i);
	IOS_ReloadIOS(i);

	ios->vIOS = isvIOS();
	ios->FSPermissions = testFSPermissions();
	ios->ESIdentify = testESIdenitfy();
	ios->SignaturePatches = testSignaturePatches();
	ios->FlashAccess = testFlashAccess();
	ios->USB2 = testUSB2();

	return 0;
}

static char reportBuffer[0x4000] [[gnu::aligned(0x40)]];
static int reportBufferLen = 0;

int writeReport_(const char* buf, int len) {
	int maxlen = (sizeof reportBuffer - reportBufferLen) - 1;
	if (len > maxlen)
		len = maxlen;

	strncpy(reportBuffer + reportBufferLen, buf, maxlen);
	reportBufferLen += len;
	fwrite(buf, 1, len, stdout);
	return len;
}

inline int writeReport(const char* str) {
	return writeReport_(str, strlen(str));
}

[[gnu::format(printf, 1, 2)]]
int writeReportF(const char* fmt, ...) {
	static char tmpbuffer[256];

	va_list ap;
	va_start(ap, fmt);
	int len = vsnprintf(tmpbuffer, sizeof tmpbuffer, fmt, ap);
	va_end(ap);

	return writeReport_(tmpbuffer, len);
}

int main(int argc, const char *argv[]) {
	VIDEO_Init();
	consoleInit(&console);
	consoleSetWindow(&console, 2, 2, console.con_cols - 2, console.con_rows - 2);
	puts("Hello World!");

	realcode_init();
	ISFS_Initialize();
	{
		getConsoleInfo();
		getBootInfo();
		getDriveInfo();
		processInstalledTitles();
		readSysCertificates();
	}
	ISFS_Deinitialize();
	realcode_close();

	int startupIOS = IOS_GetVersion();
	for (int i = 0; i < 256; i++) {
		testIOS(i);
	}
	IOS_ReloadIOS(startupIOS);
	realcode_init();

	unsigned nIOS = 0, nStubbed = 0;
	for (int i = 0; i < 256; i++) {
		if (SystemInfo.iosList[i].installed) {
			nIOS++;
			if (SystemInfo.iosList[i].stubbed)
				nStubbed++;
		}
	}

	writeReportF("Console type: %s\n", SystemInfo.console.type);
	writeReportF("Console model: %s\n", SystemInfo.console.model);
	writeReportF("Console ID: %08x\n", SystemInfo.console.id);
	writeReportF("Hollywood revision: %#x\n", SystemInfo.console.hollywood_rev);
	writeReportF("Serial number: %s\n", SystemInfo.console.serial);
	writeReportF("Console area: %s\n\n", SystemInfo.console.area);

	if (!SystemInfo.console.latte) {
		writeReportF("Boot1 version: %s [%s]\n", SystemInfo.boot1->name, SystemInfo.boot1->trucha ? "unpatched" : "patched");
		writeReportF("Boot2 version: %u\n\n", SystemInfo.boot2version);
	}

	if (!SystemInfo.drive.rel_date.value) {
		writeReportF("DI_Idenitfy failed (%i)\n", SystemInfo.drive.error);
	} else {
		writeReportF("Drive revision date: %04x/%02x/%02x\n", SystemInfo.drive.rel_date.year, SystemInfo.drive.rel_date.month, SystemInfo.drive.rel_date.day);
		writeReportF("DVD support: %s\n", ((const char* [3]){ "No", "Yes", "Maybe..." })[SystemInfo.drive.dvd_support]);
		if (SystemInfo.drive.dvd_support == 2)
			writeReportF("(rev=%#x dev_code=%#x)\n", SystemInfo.drive.rev, SystemInfo.drive.dev_code);

		writeReport("\n");
	}

	writeReportF("System menu %s%c (v%hu) running on IOS%u\n", SystemInfo.sysmenu.version, SystemInfo.sysmenu.region, SystemInfo.sysmenu.revision, SystemInfo.sysmenu.ios);

	if (SystemInfo.hbc.version) {
		writeReportF("Homebrew channel \"%s\" installed, revision %u.%u, running on IOS%u\n\n",
			   SystemInfo.hbc.version->name, SystemInfo.hbc.revision >> 8, SystemInfo.hbc.revision & 0xFF, SystemInfo.hbc.ios);
	} else {
		writeReport("Homebrew channel not installed\n\n");
	}

	writeReportF("%u IOSes are installed.\n", nIOS);
	writeReportF("%u of these IOSes are stubbed.\n\n", nStubbed);

	for (int i = 0; i < 0x100; i++) {
		struct ios* ios = &SystemInfo.iosList[i];

		if (!ios->installed)
			continue;

		if (ios->cios.magic == CIOS_HDR_MAGIC) {
			writeReportF("%s%i[%i] (v%hu, %s-v%u%s): ",
					  ios->vIOS ? "vIOS" : "IOS", i, ios->cios.base, ios->revision, ios->cios.name, ios->cios.version, ios->cios.verstr);
		} else {
			writeReportF("%s%i (v%u.%u): ",
					  ios->vIOS ? "vIOS" : "IOS", i, ios->rev_ma, ios->rev_mi);
		}

		if (ios->stubbed) {
			writeReport("Stub\n");
		} else if (IS_BOOTMII(i, ios->revision)) {
			writeReport("BootMii IOS\n");
		} else if (!ios->patches) {
			writeReport("No patches\n");
		} else {
#define print_patch(x, s) { if ((x)++) { writeReport(", "); } writeReport(s); }
			int x = 0;

			if (ios->USB2 == 1) print_patch(x, "USB 2.0");
			if (ios->USB2 >= 2) print_patch(x, "USB 2.0 (ehcmodule)");

			if (ios->SignaturePatches == 1) print_patch(x, "Signature patch");
			if (ios->SignaturePatches >= 2) print_patch(x, "Trucha bug");

			if (ios->ESIdentify) print_patch(x, "ES_Identify");

			if (ios->FSPermissions == 1) print_patch(x, "FS permissions");
			if (ios->FSPermissions == 2) print_patch(x, "Root access");

			if (ios->FlashAccess) print_patch(x, "/dev/flash");

			writeReport("\n");
		}
	}


	printf("report buffer length=%i\n", reportBufferLen);
	fatInitDefault();
	FILE* fp = fopen("system_check.log", "w");
	if (!fp) {
		perror("fopen");
	} else {
		fwrite(reportBuffer, 1, reportBufferLen, fp);
		fclose(fp);
	}

	input_init();
	input_wait(0);
	return 0;
}
