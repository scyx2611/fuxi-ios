/* fuxibacklight=4 policy: brightness request decoding, fade and the panel's DBV limits. No hardware access. */
#ifndef FUXI_DSI_BACKLIGHT_LOGIC_H
#define FUXI_DSI_BACKLIGHT_LOGIC_H
#include <stdint.h>
#define FDBL_MAX 2047u          /* panel normal maximum DBV (mi,mdss-dsi-fac-bl-max-level 0x7ff); HBM 0xfff is not offered */
#define FDBL_MIN_SENT 8u        /* below this the panel goes fully dark */
#define FDBL_INITIAL 1023u      /* the boot loader's level is not read back */
#define FDBL_FADE_MAX_MS 5000u
#define FDBL_STEP_MS 16u
#define FDBL_PENDING (1ull << 63)
#define FDBL_MIN_NITS_FIXED (2u << 16)
#define FDBL_MID_NITS_FIXED (100u << 16)
#define FDBL_MAX_NITS_FIXED (500u << 16)
#define FDBL_CURR_MID_FIXED (10u << 16)
#define FDBL_CURR_MAX_FIXED (20u << 16)
static inline int fdbl_enabled(int present, int value)
{
	return present && value == 4;
}
/* An OSNumber of `bits` bits (a CFNumber from user space): a set sign bit is a negative request, 0. */
static inline uint32_t fdbl_goal(uint64_t raw, unsigned bits)
{
	if (bits >= 1 && bits <= 64 && ((raw >> (bits - 1)) & 1u)) return 0;
	return raw > FDBL_MAX ? FDBL_MAX : (uint32_t)raw;
}
/* brightness-nits is signed 16.16 fixed-point nits (0..500.0 nits -> DBV 0..FDBL_MAX). */
static inline uint32_t fdbl_nits_goal(uint64_t raw, unsigned bits)
{
	if (bits >= 1 && bits <= 64 && ((raw >> (bits - 1)) & 1u)) return 0;
	if (raw >= FDBL_MAX_NITS_FIXED) return FDBL_MAX;
	return (uint32_t)((raw * (uint64_t)FDBL_MAX + ((uint64_t)FDBL_MAX_NITS_FIXED / 2u)) /
	    (uint64_t)FDBL_MAX_NITS_FIXED);
}
/* FadePeriod is 16.16 fixed-point seconds. */
static inline uint32_t fdbl_fade_ms(uint64_t fixed)
{
	if (fixed > (((uint64_t)FDBL_FADE_MAX_MS << 16) / 1000u) + 1u) return FDBL_FADE_MAX_MS;
	uint64_t ms = (fixed * 1000u) >> 16;
	return ms > FDBL_FADE_MAX_MS ? FDBL_FADE_MAX_MS : (uint32_t)ms;
}
/* The level actually sent: the panel's range, never fully dark. */
static inline uint32_t fdbl_level(uint32_t goal)
{
	return goal > FDBL_MAX ? FDBL_MAX : goal < FDBL_MIN_SENT ? FDBL_MIN_SENT : goal;
}
/* setProperties -> work loop handoff in one 64-bit word: pending bit, goal, fade milliseconds. */
static inline uint64_t fdbl_request(uint32_t goal, uint32_t ms)
{
	return FDBL_PENDING | ((uint64_t)(goal & 0xffffu) << 32) | ms;
}
static inline uint32_t fdbl_request_goal(uint64_t request)
{
	return (uint32_t)(request >> 32) & 0xffffu;
}
static inline uint32_t fdbl_request_ms(uint64_t request)
{
	return (uint32_t)request;
}
/* Linear fade from `from` to `to`; elapsed and period in the same time unit. */
static inline uint32_t fdbl_fade_at(uint32_t from, uint32_t to, uint64_t elapsed, uint64_t period)
{
	if (!period || elapsed >= period) return to;
	if (to >= from) return from + (uint32_t)((uint64_t)(to - from) * elapsed / period);
	return from - (uint32_t)((uint64_t)(from - to) * elapsed / period);
}
static inline int fdbl_log_this(uint32_t event)
{
	return event && (event <= 64 || (event & 255u) == 0);
}
#endif
