/*
 * Pure C11 decision, process-isolation, and ModuleConfiguration.plist repair/guard
 * helpers for FuxiFlashlight.c and FuxiShape.c (host-tested under ASan/UBSan).
 *
 * References (AGENTS.md section 0):
 *   - 20H392 dyld_shared_cache disassembly of ControlCenterServices:
 *     CCSModuleMetadata +_requiredCapabilitiesForInfoDictionary:
 *     CCSModuleSettingsProvider -[_queue_loadSettings], -[_queue_saveSettings],
 *     and -[setAndSaveOrderedUserEnabledModuleIdentifiers:].
 *   - Pure C11 host-testable pattern from FuxiCaptureStubLogic.h / FuxiAtxFixLogic.h.
 */
#ifndef FUXI_FLASHLIGHT_LOGIC_H
#define FUXI_FLASHLIGHT_LOGIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define FUXI_FLASHLIGHT_MODULE_ID "com.apple.control-center.FlashlightModule"
#define FUXI_FLASHLIGHT_MUTE_ID   "com.apple.control-center.MuteModule"
#define FUXI_FLASHLIGHT_CAP_FLASH "camera-flash"
#define FUXI_FLASHLIGHT_MAX_CAPS  32u
#define FUXI_CC_MAX_MODULES       128u
#define FUXI_CC_MAX_ID_LEN        128u

enum fuxi_flashlight_process_mode {
	FUXI_FLASHLIGHT_MODE_NONE = 0,
	FUXI_FLASHLIGHT_MODE_METADATA_ONLY = 1, /* Preferences */
	FUXI_FLASHLIGHT_MODE_FULL = 2,          /* SpringBoard */
};

enum fuxi_flashlight_action_bits {
	FUXI_FLASHLIGHT_ACT_NONE = 0u,
	FUXI_FLASHLIGHT_ACT_METADATA = 1u << 0,
	FUXI_FLASHLIGHT_ACT_SETTINGS_GUARD = 1u << 1,
	FUXI_FLASHLIGHT_ACT_AVFLASHLIGHT = 1u << 2,
	FUXI_FLASHLIGHT_ACT_LEASE_TIMER = 1u << 3,
};

static inline enum fuxi_flashlight_process_mode
fuxi_flashlight_process_mode_for_name(const char *progname)
{
	if (progname == NULL) {
		return FUXI_FLASHLIGHT_MODE_NONE;
	}
	if (strcmp(progname, "SpringBoard") == 0) {
		return FUXI_FLASHLIGHT_MODE_FULL;
	}
	if (strcmp(progname, "Preferences") == 0) {
		return FUXI_FLASHLIGHT_MODE_METADATA_ONLY;
	}
	return FUXI_FLASHLIGHT_MODE_NONE;
}

static inline unsigned
fuxi_flashlight_allowed_actions(enum fuxi_flashlight_process_mode mode)
{
	switch (mode) {
	case FUXI_FLASHLIGHT_MODE_METADATA_ONLY:
		return FUXI_FLASHLIGHT_ACT_METADATA |
		       FUXI_FLASHLIGHT_ACT_SETTINGS_GUARD;
	case FUXI_FLASHLIGHT_MODE_FULL:
		return FUXI_FLASHLIGHT_ACT_METADATA |
		       FUXI_FLASHLIGHT_ACT_SETTINGS_GUARD |
		       FUXI_FLASHLIGHT_ACT_AVFLASHLIGHT |
		       FUXI_FLASHLIGHT_ACT_LEASE_TIMER;
	default:
		return FUXI_FLASHLIGHT_ACT_NONE;
	}
}

static inline bool
fuxi_flashlight_is_target_module(const char *bundle_id)
{
	return bundle_id != NULL && strcmp(bundle_id, FUXI_FLASHLIGHT_MODULE_ID) == 0;
}

static inline bool
fuxi_flashlight_capability_should_drop(const char *capability)
{
	return capability != NULL && strcmp(capability, FUXI_FLASHLIGHT_CAP_FLASH) == 0;
}

/*
 * Given bundle_id, torch_is_ready, and capabilities[0..count-1], populates keep_flags[0..count-1].
 * Returns true if at least one "camera-flash" capability should be removed for FlashlightModule.
 */
static inline bool
fuxi_flashlight_plan_cap_filter(const char *bundle_id, bool torch_is_ready,
    const char *const *capabilities, size_t count, bool *keep_flags, size_t *dropped_count)
{
	size_t dropped = 0u;

	if (keep_flags != NULL) {
		for (size_t i = 0; i < count; i++) {
			keep_flags[i] = true;
		}
	}
	if (dropped_count != NULL) {
		*dropped_count = 0u;
	}
	if (!torch_is_ready || !fuxi_flashlight_is_target_module(bundle_id) ||
	    capabilities == NULL || keep_flags == NULL || count == 0u) {
		return false;
	}
	for (size_t i = 0; i < count; i++) {
		if (fuxi_flashlight_capability_should_drop(capabilities[i])) {
			keep_flags[i] = false;
			dropped++;
		}
	}
	if (dropped_count != NULL) {
		*dropped_count = dropped;
	}
	return dropped > 0u;
}

enum fuxi_flashlight_meta_step_result {
	FUXI_FLASHLIGHT_META_ALREADY_HOOKED = 0,
	FUXI_FLASHLIGHT_META_CLASS_MISSING,
	FUXI_FLASHLIGHT_META_NOT_READY,
	FUXI_FLASHLIGHT_META_ABI_MISMATCH,
	FUXI_FLASHLIGHT_META_SELF_IMP_DETECTED,
	FUXI_FLASHLIGHT_META_INSTALL_NOW,
};

static inline enum fuxi_flashlight_meta_step_result
fuxi_flashlight_meta_decide_install(bool already_hooked, bool class_present, bool torch_is_ready,
    bool sig_valid, const void *current_imp, const void *replacement_imp)
{
	if (already_hooked) {
		return FUXI_FLASHLIGHT_META_ALREADY_HOOKED;
	}
	if (!class_present) {
		return FUXI_FLASHLIGHT_META_CLASS_MISSING;
	}
	if (!torch_is_ready) {
		return FUXI_FLASHLIGHT_META_NOT_READY;
	}
	if (!sig_valid || current_imp == NULL || replacement_imp == NULL) {
		return FUXI_FLASHLIGHT_META_ABI_MISMATCH;
	}
	if (current_imp == replacement_imp) {
		return FUXI_FLASHLIGHT_META_SELF_IMP_DETECTED;
	}
	return FUXI_FLASHLIGHT_META_INSTALL_NOW;
}

struct fuxi_cc_id_list {
	size_t count;
	bool overflow;
	char items[FUXI_CC_MAX_MODULES][FUXI_CC_MAX_ID_LEN];
};

static inline void
fuxi_cc_list_init(struct fuxi_cc_id_list *list)
{
	if (list == NULL) {
		return;
	}
	list->count = 0u;
	list->overflow = false;
}

static inline bool
fuxi_cc_list_find(const struct fuxi_cc_id_list *list, const char *id, size_t *index_out)
{
	if (list == NULL || id == NULL) {
		return false;
	}
	for (size_t i = 0; i < list->count && i < FUXI_CC_MAX_MODULES; i++) {
		if (strcmp(list->items[i], id) == 0) {
			if (index_out != NULL) {
				*index_out = i;
			}
			return true;
		}
	}
	return false;
}

static inline bool
fuxi_cc_list_contains(const struct fuxi_cc_id_list *list, const char *id)
{
	return fuxi_cc_list_find(list, id, NULL);
}

static inline bool
fuxi_cc_list_append(struct fuxi_cc_id_list *list, const char *id)
{
	size_t len;

	if (list == NULL || id == NULL || id[0] == '\0') {
		if (list != NULL) {
			list->overflow = true;
		}
		return false;
	}
	len = strlen(id);
	if (len >= FUXI_CC_MAX_ID_LEN || list->count >= FUXI_CC_MAX_MODULES) {
		list->overflow = true;
		return false;
	}
	memcpy(list->items[list->count], id, len + 1u);
	list->count++;
	return true;
}

static inline bool
fuxi_cc_list_insert_at(struct fuxi_cc_id_list *list, size_t pos, const char *id)
{
	size_t len;

	if (list == NULL || id == NULL || id[0] == '\0') {
		if (list != NULL) {
			list->overflow = true;
		}
		return false;
	}
	len = strlen(id);
	if (len >= FUXI_CC_MAX_ID_LEN || list->count >= FUXI_CC_MAX_MODULES) {
		list->overflow = true;
		return false;
	}
	if (pos > list->count) {
		pos = list->count;
	}
	for (size_t i = list->count; i > pos; i--) {
		memcpy(list->items[i], list->items[i - 1u], FUXI_CC_MAX_ID_LEN);
	}
	memcpy(list->items[pos], id, len + 1u);
	list->count++;
	return true;
}

static inline bool
fuxi_cc_list_insert_after_or_append(struct fuxi_cc_id_list *list, const char *anchor_id, const char *new_id)
{
	size_t anchor_idx = 0u;

	if (list == NULL) {
		return false;
	}
	if (anchor_id != NULL && fuxi_cc_list_find(list, anchor_id, &anchor_idx)) {
		return fuxi_cc_list_insert_at(list, anchor_idx + 1u, new_id);
	}
	return fuxi_cc_list_insert_at(list, list->count, new_id);
}

static inline size_t
fuxi_cc_list_remove_all(struct fuxi_cc_id_list *list, const char *target_id)
{
	size_t write_idx = 0u;
	size_t removed = 0u;

	if (list == NULL || target_id == NULL) {
		return 0u;
	}
	for (size_t i = 0; i < list->count && i < FUXI_CC_MAX_MODULES; i++) {
		if (strcmp(list->items[i], target_id) == 0) {
			removed++;
			continue;
		}
		if (write_idx != i) {
			memcpy(list->items[write_idx], list->items[i], FUXI_CC_MAX_ID_LEN);
		}
		write_idx++;
	}
	list->count = write_idx;
	return removed;
}

static inline bool
fuxi_cc_list_dedup_target(struct fuxi_cc_id_list *list, const char *target_id)
{
	size_t write_idx = 0u;
	bool seen = false;
	bool removed = false;

	if (list == NULL || target_id == NULL) {
		return false;
	}
	for (size_t i = 0; i < list->count && i < FUXI_CC_MAX_MODULES; i++) {
		if (strcmp(list->items[i], target_id) == 0) {
			if (seen) {
				removed = true;
				continue;
			}
			seen = true;
		}
		if (write_idx != i) {
			memcpy(list->items[write_idx], list->items[i], FUXI_CC_MAX_ID_LEN);
		}
		write_idx++;
	}
	list->count = write_idx;
	return removed;
}

enum fuxi_cc_repair_action {
	FUXI_CC_REPAIR_NONE = 0,
	FUXI_CC_REPAIR_DISABLED_BY_GATE,
	FUXI_CC_REPAIR_SKIPPED_USER_DISABLED,
	FUXI_CC_REPAIR_SKIPPED_OVERFLOW,
	FUXI_CC_REPAIR_RESTORED,
	FUXI_CC_REPAIR_CLEARED_DISABLED,
};

/*
 * Repairs ModuleConfiguration.plist lists on load:
 * - Never overwrites user ordering when FlashlightModule is already present.
 * - If FlashlightModule is absent and user_disabled_marker_present is false (while
 *   torch_is_ready && adt_guard_enabled), inserts FlashlightModule right after
 *   MuteModule if present, otherwise appends at the end, and clears any stale
 *   FlashlightModule entry from user_disabled_ids.
 */
static inline enum fuxi_cc_repair_action
fuxi_cc_repair_module_lists(bool torch_is_ready, bool adt_guard_enabled,
    bool user_disabled_marker_present,
    const struct fuxi_cc_id_list *fixed_ids,
    const struct fuxi_cc_id_list *user_enabled_fixed_ids,
    struct fuxi_cc_id_list *user_enabled_ids,
    struct fuxi_cc_id_list *user_disabled_ids)
{
	bool in_fixed;
	bool in_enabled;
	bool deduped = false;
	size_t cleared_disabled = 0u;
	size_t cleared_enabled = 0u;

	if (user_enabled_ids == NULL || user_disabled_ids == NULL) {
		return FUXI_CC_REPAIR_SKIPPED_OVERFLOW;
	}
	if (!torch_is_ready || !adt_guard_enabled) {
		return FUXI_CC_REPAIR_DISABLED_BY_GATE;
	}
	if ((fixed_ids != NULL && fixed_ids->overflow) ||
	    (user_enabled_fixed_ids != NULL && user_enabled_fixed_ids->overflow) ||
	    user_enabled_ids->overflow || user_disabled_ids->overflow) {
		return FUXI_CC_REPAIR_SKIPPED_OVERFLOW;
	}
	if (user_disabled_marker_present) {
		return FUXI_CC_REPAIR_SKIPPED_USER_DISABLED;
	}

	in_fixed = (fixed_ids != NULL && fuxi_cc_list_contains(fixed_ids, FUXI_FLASHLIGHT_MODULE_ID)) ||
	           (user_enabled_fixed_ids != NULL &&
	            fuxi_cc_list_contains(user_enabled_fixed_ids, FUXI_FLASHLIGHT_MODULE_ID));
	in_enabled = fuxi_cc_list_contains(user_enabled_ids, FUXI_FLASHLIGHT_MODULE_ID);

	if (in_fixed) {
		cleared_enabled = fuxi_cc_list_remove_all(user_enabled_ids, FUXI_FLASHLIGHT_MODULE_ID);
		cleared_disabled = fuxi_cc_list_remove_all(user_disabled_ids, FUXI_FLASHLIGHT_MODULE_ID);
		if (cleared_enabled > 0u || cleared_disabled > 0u) {
			return FUXI_CC_REPAIR_CLEARED_DISABLED;
		}
		return FUXI_CC_REPAIR_NONE;
	}

	if (in_enabled) {
		deduped = fuxi_cc_list_dedup_target(user_enabled_ids, FUXI_FLASHLIGHT_MODULE_ID);
		cleared_disabled = fuxi_cc_list_remove_all(user_disabled_ids, FUXI_FLASHLIGHT_MODULE_ID);
		if (deduped || cleared_disabled > 0u) {
			return FUXI_CC_REPAIR_CLEARED_DISABLED;
		}
		return FUXI_CC_REPAIR_NONE;
	}

	if (user_enabled_ids->count >= FUXI_CC_MAX_MODULES) {
		return FUXI_CC_REPAIR_SKIPPED_OVERFLOW;
	}
	(void)fuxi_cc_list_remove_all(user_disabled_ids, FUXI_FLASHLIGHT_MODULE_ID);
	if (!fuxi_cc_list_insert_after_or_append(user_enabled_ids, FUXI_FLASHLIGHT_MUTE_ID,
	    FUXI_FLASHLIGHT_MODULE_ID)) {
		return FUXI_CC_REPAIR_SKIPPED_OVERFLOW;
	}
	return FUXI_CC_REPAIR_RESTORED;
}

enum fuxi_cc_save_policy {
	FUXI_CC_SAVE_ALLOW = 0,
	FUXI_CC_SAVE_ALLOW_MARK_USER_DISABLED,
	FUXI_CC_SAVE_ALLOW_CLEAR_USER_DISABLED,
	FUXI_CC_SAVE_GUARDED_RESTORE,
};

/*
 * Guards setAndSaveOrderedUserEnabledModuleIdentifiers:
 * - Allows removal and marks user-disabled ONLY when running in Preferences
 *   (METADATA_ONLY) with metadata_hook_active == true.
 * - Re-adding FlashlightModule clears the user-disabled marker and allows save.
 * - Unexpected removal in SpringBoard or with metadata_hook_active == false is
 *   intercepted and FlashlightModule is preserved in order.
 */
static inline enum fuxi_cc_save_policy
fuxi_cc_guard_save_identifiers(enum fuxi_flashlight_process_mode proc_mode,
    bool metadata_hook_active, bool torch_is_ready, bool adt_guard_enabled,
    const struct fuxi_cc_id_list *old_enabled_ids,
    const struct fuxi_cc_id_list *new_enabled_ids,
    struct fuxi_cc_id_list *out_patched_ids)
{
	bool had_torch;
	bool has_torch;

	if (out_patched_ids != NULL && new_enabled_ids != NULL) {
		*out_patched_ids = *new_enabled_ids;
	}
	if (!torch_is_ready || !adt_guard_enabled ||
	    old_enabled_ids == NULL || new_enabled_ids == NULL ||
	    old_enabled_ids->overflow || new_enabled_ids->overflow) {
		return FUXI_CC_SAVE_ALLOW;
	}

	had_torch = fuxi_cc_list_contains(old_enabled_ids, FUXI_FLASHLIGHT_MODULE_ID);
	has_torch = fuxi_cc_list_contains(new_enabled_ids, FUXI_FLASHLIGHT_MODULE_ID);

	if (!had_torch && has_torch) {
		if (out_patched_ids != NULL) {
			(void)fuxi_cc_list_dedup_target(out_patched_ids, FUXI_FLASHLIGHT_MODULE_ID);
		}
		return FUXI_CC_SAVE_ALLOW_CLEAR_USER_DISABLED;
	}

	if (had_torch && !has_torch) {
		size_t old_idx = 0u;
		bool inserted = false;

		if (proc_mode == FUXI_FLASHLIGHT_MODE_METADATA_ONLY && metadata_hook_active) {
			return FUXI_CC_SAVE_ALLOW_MARK_USER_DISABLED;
		}
		if (out_patched_ids == NULL) {
			return FUXI_CC_SAVE_GUARDED_RESTORE;
		}
		if (out_patched_ids->count >= FUXI_CC_MAX_MODULES) {
			return FUXI_CC_SAVE_ALLOW;
		}
		if (fuxi_cc_list_find(old_enabled_ids, FUXI_FLASHLIGHT_MODULE_ID, &old_idx)) {
			for (size_t back = old_idx; back > 0u; back--) {
				size_t anchor_pos = 0u;
				if (fuxi_cc_list_find(out_patched_ids, old_enabled_ids->items[back - 1u],
				    &anchor_pos)) {
					inserted = fuxi_cc_list_insert_at(out_patched_ids, anchor_pos + 1u,
					    FUXI_FLASHLIGHT_MODULE_ID);
					break;
				}
			}
			if (!inserted) {
				for (size_t fwd = old_idx + 1u;
				    fwd < old_enabled_ids->count && fwd < FUXI_CC_MAX_MODULES; fwd++) {
					size_t next_pos = 0u;
					if (fuxi_cc_list_find(out_patched_ids, old_enabled_ids->items[fwd],
					    &next_pos)) {
						inserted = fuxi_cc_list_insert_at(out_patched_ids, next_pos,
						    FUXI_FLASHLIGHT_MODULE_ID);
						break;
					}
				}
			}
		}
		if (!inserted) {
			inserted = fuxi_cc_list_insert_after_or_append(out_patched_ids,
			    FUXI_FLASHLIGHT_MUTE_ID, FUXI_FLASHLIGHT_MODULE_ID);
		}
		if (!inserted) {
			return FUXI_CC_SAVE_ALLOW;
		}
		return FUXI_CC_SAVE_GUARDED_RESTORE;
	}

	return FUXI_CC_SAVE_ALLOW;
}

/*
 * Bounded latest-wins coalescing state for SBUIFlashlightController UI/KVO handoff.
 * Prevents boot-time utility-queue stalls and burst replay while preserving 'off' (0).
 */
struct fuxi_torch_coalesce_state {
	unsigned latest_level;
	bool     pending;
	bool     in_flight;
};

static inline void
fuxi_torch_coalesce_init(struct fuxi_torch_coalesce_state *st)
{
	if (st == NULL) {
		return;
	}
	st->latest_level = 0u;
	st->pending = false;
	st->in_flight = false;
}

static inline bool
fuxi_torch_coalesce_submit(struct fuxi_torch_coalesce_state *st, unsigned level, bool is_power_off)
{
	if (st == NULL) {
		return false;
	}
	if (is_power_off || level > 4u) {
		level = is_power_off ? 0u : 4u;
	}
	st->latest_level = level;
	st->pending = true;
	if (!st->in_flight) {
		st->in_flight = true;
		return true;
	}
	return false;
}

static inline bool
fuxi_torch_coalesce_take(struct fuxi_torch_coalesce_state *st, unsigned *out_level)
{
	if (st == NULL) {
		return false;
	}
	if (st->pending) {
		if (out_level != NULL) {
			*out_level = st->latest_level;
		}
		st->pending = false;
		return true;
	}
	st->in_flight = false;
	return false;
}

#endif /* FUXI_FLASHLIGHT_LOGIC_H */
