/* SPDX-License-Identifier: MIT */
/*
 * Opt-in read-only in-kernel HFS+ ramdisk mounted on /Developer holding
 * /Developer/libfuxi-camera.dylib and /Developer/libfuxi-bb.dylib (default off;
 * enabled by boot-arg fuxicamdylib=1, ADT /chosen u32 "fuxi,camera-dylib"=1,
 * or ADT /chosen u32 "fuxi,modem-bb"!=0).
 *
 * Keeps the phone's on-disk HFS+ rootfs untouched while letting kern_exec.c
 * inject /Developer/libfuxi-camera.dylib into /usr/sbin/mediaserverd or
 * /Developer/libfuxi-bb.dylib into /sbin/launchd, and sweet_amfi.c grant
 * CS_PLATFORM_BINARY for binaries on /Developer.
 */
#include <pexpert/arm64/board_config.h>
#include <sys/param.h>
#include <sys/systm.h>

#if defined(FUXI)
#include <kern/startup.h>
#include <sys/mount.h>
#include <sys/mount_internal.h>
#include <sys/vnode.h>
#include <pexpert/pexpert.h>
#include <pexpert/device_tree.h>
#include "fuxi_camera_dylib_img.h"

extern dev_t mdevadd(int devid, uint64_t base, unsigned int size, int phys);

static TUNABLE(uint32_t, fuxi_cam_dylib_bootarg, "fuxicamdylib", 0);
static volatile int fuxi_cam_dylib_mounted;

static uint32_t
fuxi_camera_dylib_mode(void)
{
	uint32_t mode = fuxi_cam_dylib_bootarg;
	if (!mode) {
		DTEntry chosen;
		void const *prop = NULL;
		unsigned int bytes = 0;
		if (SecureDTLookupEntry(NULL, "/chosen", &chosen) == kSuccess &&
		    SecureDTGetProperty(chosen, "fuxi,camera-dylib", &prop, &bytes) == kSuccess &&
		    bytes == sizeof(uint32_t)) {
			memcpy(&mode, prop, sizeof(mode));
		}
	}
	return mode;
}

int
fuxi_camera_dylib_enabled(void)
{
	return fuxi_camera_dylib_mode() != 0;
}

uint32_t
fuxi_modem_bb_mode(void)
{
	static int cached = -1;
	static uint32_t value = 0;
	if (cached < 0) {
		uint32_t mode = 0;
		DTEntry chosen;
		void const *prop = NULL;
		unsigned int bytes = 0;
		if (SecureDTLookupEntry(NULL, "/chosen", &chosen) == kSuccess &&
		    SecureDTGetProperty(chosen, "fuxi,modem-bb", &prop, &bytes) == kSuccess &&
		    bytes == sizeof(uint32_t)) {
			memcpy(&mode, prop, sizeof(mode));
		}
		value = mode;
		cached = 1;
	}
	return value;
}

int
fuxi_camera_dylib_is_mounted(void)
{
	return fuxi_cam_dylib_mounted;
}

void
fuxi_camera_dylib_mount_init(void)
{
	uint32_t cam_mode = fuxi_camera_dylib_mode();
	uint32_t bb_mode = fuxi_modem_bb_mode();
	if ((!cam_mode && !bb_mode) || fuxi_cam_dylib_mounted) {
		return;
	}
	const uintptr_t addr = (uintptr_t)fuxi_camera_hfs_img;
	const size_t bytes = sizeof(fuxi_camera_hfs_img);
	if ((addr & 0xfffu) != 0 || (bytes & 0xfffu) != 0 || bytes == 0) {
		printf("FUXI CAMERA: dylib image misaligned (addr=%p size=%zu)\n",
		    (const void *)fuxi_camera_hfs_img, bytes);
		return;
	}
	dev_t dev = mdevadd(1, ((uint64_t)addr) >> 12, (unsigned int)(bytes >> 12), 0);
	if (dev == (dev_t)-1) {
		printf("FUXI CAMERA: mdevadd(md1) failed for /Developer\n");
		return;
	}
	char devpath[] = "/dev/md1";
	char *devp = devpath;
	int err = kernel_mount("hfs", NULLVP, NULLVP, "/Developer", &devp, 0,
	    MNT_RDONLY | MNT_DONTBROWSE,
	    KERNEL_MOUNT_NOAUTH | KERNEL_MOUNT_BASESYSTEMROOT,
	    vfs_context_kernel());
	if (err != 0) {
		printf("FUXI CAMERA: failed to mount /Developer from /dev/md1 (err=%d)\n", err);
		return;
	}
	fuxi_cam_dylib_mounted = 1;
	printf("FUXI CAMERA: mounted /Developer read-only from /dev/md1 (%zu B, cam=%u bb=%#x)\n",
	    bytes, cam_mode, bb_mode);
}
#else
int
fuxi_camera_dylib_enabled(void)
{
	return 0;
}

uint32_t
fuxi_modem_bb_mode(void)
{
	return 0;
}

int
fuxi_camera_dylib_is_mounted(void)
{
	return 0;
}

void
fuxi_camera_dylib_mount_init(void)
{
}
#endif /* FUXI */
