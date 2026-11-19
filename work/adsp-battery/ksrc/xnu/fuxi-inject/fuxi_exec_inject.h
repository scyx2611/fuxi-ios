/*
 * fxinj=N, or ADT /chosen u32 "fuxi,inject"=N (FUXI): DYLD_INSERT_LIBRARIES added by the kernel at exec, for some programs (patch 0118).
 *
 * launchd (20H392) drops every DYLD_ variable except DYLD_PRINT_* from a job's EnvironmentVariables
 * ("Disallowing environment variable: %s") and lets them through only for a job whose label does not start
 * with "com.apple.", so a library named in SpringBoard's launchd job never reaches dyld. HackiOS adds the
 * variable in the kernel for the same reason (hakios-xnu-kernel xnu/hakios.patch, kern.exec_inject, MIT).
 * Ported from the GPU line's patch 0121 without its launch-boost hook (no fuxi_perf_launch here).
 *
 *   bit 0 (1)  the main executable of an application (".../NAME.app/EXECUTABLE"), SpringBoard excepted:
 *              /usr/local/lib/fuxi/libfuxi-app.dylib
 *   bit 1 (2)  SpringBoard: /usr/local/lib/fuxi/libfuxi-sb.dylib (userland/fuxi-shape: the status bar
 *              follows the punch hole instead of the iPhone X notch)
 *   bit 2 (4)  mediaserverd: the same libfuxi-sb.dylib (its haptic server hands vibration patterns to
 *              the HAP525 runtime, sysctl kern.fuxivibrate; the library checks which program it is in)
 *   bit 3 (8)  wifid: native Wi-Fi diagnostics.
 *   bit 4 (16) Preferences: manual Wi-Fi password prompt when password sharing is unavailable.
 *   bit 5 (32) /usr/sbin/BlueTool: the library answers com.apple.BlueTool for the WCN7850 that
 *              fuxi,bt=5 has booted (the stock tool only knows Broadcom); FuxiBlueTool.c.
 *   bit 6 (64) /usr/sbin/bluetoothd: diagnostics and the Qualcomm controller hooks.
 *
 * dyld honours it because the H1 AMFI dyld policy answer (patch 0103, request 0x5a) allows everything,
 * including a library that is missing. The variable follows the program's own environment.
 * Pure functions only, no strrchr (not every kernel string routine is exported).
 */
#define FUXI_INJECT_APPS        1u
#define FUXI_INJECT_SPRINGBOARD 2u
#define FUXI_INJECT_MEDIASERVER 4u
#define FUXI_INJECT_WIFI_AUDIT  8u
#define FUXI_INJECT_WIFI_PROMPT 16u
#define FUXI_INJECT_BLUETOOL    32u
#define FUXI_INJECT_BLUETOOTHD  64u
#define FUXI_INJECT_ALL         (FUXI_INJECT_APPS | FUXI_INJECT_SPRINGBOARD | FUXI_INJECT_MEDIASERVER | \
                                FUXI_INJECT_WIFI_AUDIT | FUXI_INJECT_WIFI_PROMPT | FUXI_INJECT_BLUETOOL | \
                                FUXI_INJECT_BLUETOOTHD)
#define FUXI_INJECT_APP_LIB     "/usr/local/lib/fuxi/libfuxi-app.dylib"
#define FUXI_INJECT_SB_LIB      "/usr/local/lib/fuxi/libfuxi-sb.dylib"
#define FUXI_INJECT_ENV_MAX     128
#define FUXI_INJECT_SAID        24      /* the first programs given a library are logged */

/* path is ".../X.app/EXECUTABLE": an application's main executable, not an .appex or a helper deeper inside. */
static inline bool
fuxi_inject_is_app(const char *path)
{
	const char *slash = NULL;
	const char *s;

	for (s = path; *s != '\0'; s++) {
		if (*s == '/') {
			slash = s;
		}
	}
	return slash != NULL && slash[1] != '\0' && slash - path >= 5 && memcmp(slash - 4, ".app", 4) == 0 &&
	       slash[-5] != '/';
}

/* The library this program gets under mode, or NULL. name is the last path component. */
static inline const char *
fuxi_inject_library(unsigned mode, const char *path, const char *name)
{
	if (path == NULL || name == NULL) {
		return NULL;
	}
	if (strcmp(name, "SpringBoard") == 0) {
		return (mode & FUXI_INJECT_SPRINGBOARD) != 0 ? FUXI_INJECT_SB_LIB : NULL;
	}
	if (strcmp(name, "mediaserverd") == 0) {
		return (mode & FUXI_INJECT_MEDIASERVER) != 0 && strcmp(path, "/usr/sbin/mediaserverd") == 0 ?
		       FUXI_INJECT_SB_LIB : NULL;
	}
	if (strcmp(name, "wifid") == 0) {
		return (mode & FUXI_INJECT_WIFI_AUDIT) != 0 && strcmp(path, "/usr/sbin/wifid") == 0 ?
		       FUXI_INJECT_SB_LIB : NULL;
	}
	if (strcmp(name, "BlueTool") == 0) {
		return (mode & FUXI_INJECT_BLUETOOL) != 0 && strcmp(path, "/usr/sbin/BlueTool") == 0 ?
		       FUXI_INJECT_SB_LIB : NULL;
	}
	if (strcmp(name, "bluetoothd") == 0) {
		return (mode & FUXI_INJECT_BLUETOOTHD) != 0 && strcmp(path, "/usr/sbin/bluetoothd") == 0 ?
		       FUXI_INJECT_SB_LIB : NULL;
	}
	if ((mode & FUXI_INJECT_WIFI_PROMPT) && strcmp(name, "Preferences") == 0 &&
	    strcmp(path, "/Applications/Preferences.app/Preferences") == 0) {
		return FUXI_INJECT_SB_LIB;
	}
	return (mode & FUXI_INJECT_APPS) != 0 && fuxi_inject_is_app(path) ? FUXI_INJECT_APP_LIB : NULL;
}

/* "DYLD_INSERT_LIBRARIES=library" into env; false without a library or room */
static inline bool
fuxi_inject_env(const char *library, char *env, size_t size)
{
	int n;

	if (library == NULL || size == 0) {
		return false;
	}
	n = snprintf(env, size, "DYLD_INSERT_LIBRARIES=%s", library);
	return n > 0 && (size_t)n < size;
}
