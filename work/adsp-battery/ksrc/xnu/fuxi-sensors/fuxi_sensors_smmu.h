/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* SM8550 (fuxi) apps-smmu pass-through setup for the ADSP sensors PD FastRPC compute context bank.
 * Reuses fuxi_audio_smmu.h's register inspection, ARM SMMU overlap rule, and verified bypass bank
 * (FUXI_AUDIO_BYPASS_BANK = 4, shared with ABL USB DWC3 and audio DMA).
 *
 * Verified SM8550 fuxi live DT (qcom,msm_fastrpc_compute_cb10..14, label "adsprpc-smd"):
 *   bank 3 (cb10): 0x1003 mask 0x80 (0x1003/0x1083), 0x1063 mask 0x00
 *   bank 4 (cb11): 0x1004 mask 0x80 (0x1004/0x1084), 0x1064 mask 0x00  (default)
 *   bank 5 (cb12): 0x1005 mask 0x80 (0x1005/0x1085), 0x1065 mask 0x00
 *   bank 6 (cb13): 0x1006 mask 0x80 (0x1006/0x1086), 0x1066 mask 0x00
 *   bank 7 (cb14): 0x1007 mask 0x80 (0x1007/0x1087), 0x1067 mask 0x00
 */
#ifndef FUXI_SENSORS_SMMU_H
#define FUXI_SENSORS_SMMU_H

#include <stdbool.h>
#include <stdint.h>
#include "fuxi_audio_smmu.h"

#define FUXI_SENSORS_SMMU_BANK_MIN 3u
#define FUXI_SENSORS_SMMU_BANK_MAX 7u
#define FUXI_SENSORS_SMMU_DEFAULT_BANK 4u

#define FUXI_SMMU_GR0_GFSR 0x48u
#define FUXI_SMMU_GR0_GFSYNR1 0x54u
#define FUXI_SMMU_GR0_TLBIALLNSNH 0x68u
#define FUXI_SMMU_GR0_TLBGSYNC 0x70u
#define FUXI_SMMU_GR0_TLBGSTATUS 0x74u

enum fuxi_sensors_smmu_status {
    FUXI_SENSORS_SMMU_OK = 0,
    FUXI_SENSORS_SMMU_BAD_BANK = -1,
    FUXI_SENSORS_SMMU_READ_ERROR = -2,
    FUXI_SENSORS_SMMU_UNSUPPORTED = -3,
    FUXI_SENSORS_SMMU_BYPASS_NOT_READY = -4,
    FUXI_SENSORS_SMMU_CONFLICT = -5,
    FUXI_SENSORS_SMMU_NO_SLOTS = -6,
    FUXI_SENSORS_SMMU_WRITE_ERROR = -7,
    FUXI_SENSORS_SMMU_VERIFY_ERROR = -8,
};

struct fuxi_sensors_smmu_info {
    uint32_t bank;
    uint32_t smr0, smr1;
    int slots[2];
    uint32_t gfsr_before, gfsr_after, gfsynr1;
    uint8_t already_ready;
    uint8_t tlb_synced;
};

static inline uint32_t
fuxi_sensors_smmu_smr0(uint32_t bank)
{
    return 0x80801000u | (bank & 0xfu);
}

static inline uint32_t
fuxi_sensors_smmu_smr1(uint32_t bank)
{
    return 0x80001060u | (bank & 0xfu);
}

static inline bool
fuxi_sensors_smmu_scan(fuxi_audio_smmu_read_fn read_reg, void *context,
    unsigned slot_count, uint32_t bank,
    struct fuxi_audio_smmu_stream out_streams[2], int blank_slots[2], unsigned *blank_count)
{
    const uint32_t sid0 = 0x1000u | (bank & 0xfu);
    const uint32_t sid1 = 0x1060u | (bank & 0xfu);
    out_streams[0] = (struct fuxi_audio_smmu_stream){.slot = -1};
    out_streams[1] = (struct fuxi_audio_smmu_stream){.slot = -1};
    blank_slots[0] = -1;
    blank_slots[1] = -1;
    *blank_count = 0;
    for (unsigned i = 0; i < slot_count; ++i) {
        uint32_t smr = 0, s2cr = 0;
        if (!read_reg(context, 0x800u + 4u * i, &smr) ||
            !read_reg(context, 0xc00u + 4u * i, &s2cr)) {
            return false;
        }
        if (smr & (1u << 31)) {
            fuxi_audio_smmu_match(&out_streams[0], sid0, 0x80u, i, smr, s2cr);
            fuxi_audio_smmu_match(&out_streams[1], sid1, 0u, i, smr, s2cr);
        } else if (s2cr == FUXI_SMMU_FAULT_S2CR) {
            if (*blank_count < 2) {
                blank_slots[*blank_count] = (int)i;
            }
            (*blank_count)++;
        }
    }
    return true;
}

static inline bool
fuxi_sensors_smmu_routes_ready(const struct fuxi_audio_smmu_stream streams[2], uint32_t bank)
{
    return streams[0].matches == 1 && streams[1].matches == 1 &&
        streams[0].smr == fuxi_sensors_smmu_smr0(bank) &&
        streams[1].smr == fuxi_sensors_smmu_smr1(bank) &&
        streams[0].s2cr == FUXI_AUDIO_BYPASS_BANK &&
        streams[1].s2cr == FUXI_AUDIO_BYPASS_BANK;
}

static inline bool
fuxi_sensors_smmu_tlb_sync(fuxi_audio_smmu_read_fn read_reg, fuxi_audio_smmu_write_fn write_reg, void *context)
{
    if (!write_reg(context, FUXI_SMMU_GR0_TLBIALLNSNH, 0u) ||
        !write_reg(context, FUXI_SMMU_GR0_TLBGSYNC, 0u)) {
        return false;
    }
    for (unsigned tries = 0; tries < 4096u; ++tries) {
        uint32_t status = 1u;
        if (!read_reg(context, FUXI_SMMU_GR0_TLBGSTATUS, &status)) {
            return false;
        }
        if ((status & 1u) == 0u) {
            return true;
        }
    }
    return false;
}

static inline enum fuxi_sensors_smmu_status
fuxi_sensors_smmu_attach(fuxi_audio_smmu_read_fn read_reg, fuxi_audio_smmu_write_fn write_reg,
    void *context, uint32_t bank, struct fuxi_sensors_smmu_info *info_out)
{
    struct fuxi_sensors_smmu_info info = {
        .bank = bank,
        .smr0 = fuxi_sensors_smmu_smr0(bank),
        .smr1 = fuxi_sensors_smmu_smr1(bank),
        .slots = {-1, -1},
    };
    if (bank < FUXI_SENSORS_SMMU_BANK_MIN || bank > FUXI_SENSORS_SMMU_BANK_MAX) {
        if (info_out) *info_out = info;
        return FUXI_SENSORS_SMMU_BAD_BANK;
    }
    struct fuxi_audio_smmu_state state;
    enum fuxi_audio_smmu_result rs = fuxi_audio_smmu_read_state(read_reg, context, &state);
    if (rs == FUXI_AUDIO_SMMU_READ_ERROR) {
        if (info_out) *info_out = info;
        return FUXI_SENSORS_SMMU_READ_ERROR;
    }
    if (rs != FUXI_AUDIO_SMMU_OK) {
        if (info_out) *info_out = info;
        return FUXI_SENSORS_SMMU_UNSUPPORTED;
    }
    info.gfsr_before = state.gfsr;
    info.gfsr_after = state.gfsr;
    (void)read_reg(context, FUXI_SMMU_GR0_GFSYNR1, &info.gfsynr1);
    if (!fuxi_audio_smmu_bypass_ready(read_reg, context, &state)) {
        if (info_out) *info_out = info;
        return FUXI_SENSORS_SMMU_BYPASS_NOT_READY;
    }

    struct fuxi_audio_smmu_stream streams[2];
    int blanks[2];
    unsigned blank_count = 0;
    if (!fuxi_sensors_smmu_scan(read_reg, context, state.slots, bank, streams, blanks, &blank_count)) {
        if (info_out) *info_out = info;
        return FUXI_SENSORS_SMMU_READ_ERROR;
    }
    if (fuxi_sensors_smmu_routes_ready(streams, bank)) {
        info.slots[0] = streams[0].slot;
        info.slots[1] = streams[1].slot;
        info.already_ready = 1;
        info.tlb_synced = 1;
        if (info_out) *info_out = info;
        return FUXI_SENSORS_SMMU_OK;
    }
    if (streams[0].matches || streams[1].matches) {
        info.slots[0] = streams[0].slot;
        info.slots[1] = streams[1].slot;
        if (info_out) *info_out = info;
        return FUXI_SENSORS_SMMU_CONFLICT;
    }
    if (blank_count < 2) {
        if (info_out) *info_out = info;
        return FUXI_SENSORS_SMMU_NO_SLOTS;
    }

    const uint32_t matches[2] = {info.smr0, info.smr1};
    struct { uint32_t smr, s2cr, old_smr; } target[2];
    for (unsigned i = 0; i < 2; ++i) {
        uint32_t s2cr = 0;
        const uint32_t offset = 4u * (uint32_t)blanks[i];
        target[i].smr = 0x800u + offset;
        target[i].s2cr = 0xc00u + offset;
        if (!read_reg(context, target[i].smr, &target[i].old_smr) || (target[i].old_smr & (1u << 31)) ||
            !read_reg(context, target[i].s2cr, &s2cr) || s2cr != FUXI_SMMU_FAULT_S2CR) {
            if (info_out) *info_out = info;
            return FUXI_SENSORS_SMMU_CONFLICT;
        }
        info.slots[i] = blanks[i];
    }

    unsigned touched = 0;
    enum fuxi_sensors_smmu_status rc = FUXI_SENSORS_SMMU_WRITE_ERROR;
    for (; touched < 2; ++touched) {
        if (!fuxi_audio_smmu_write_check(read_reg, write_reg, context, target[touched].s2cr, FUXI_AUDIO_BYPASS_BANK) ||
            !fuxi_audio_smmu_write_check(read_reg, write_reg, context, target[touched].smr, matches[touched])) {
            ++touched;
            break;
        }
        if (touched == 1) {
            info.tlb_synced = fuxi_sensors_smmu_tlb_sync(read_reg, write_reg, context) ? 1u : 0u;
            struct fuxi_audio_smmu_state check_state;
            struct fuxi_audio_smmu_stream check_streams[2];
            int check_blanks[2];
            unsigned check_blank_count = 0;
            if (fuxi_audio_smmu_read_state(read_reg, context, &check_state) == FUXI_AUDIO_SMMU_OK &&
                fuxi_sensors_smmu_scan(read_reg, context, check_state.slots, bank,
                    check_streams, check_blanks, &check_blank_count) &&
                check_state.gfsr == info.gfsr_before &&
                fuxi_audio_smmu_bypass_ready(read_reg, context, &check_state) &&
                fuxi_sensors_smmu_routes_ready(check_streams, bank)) {
                info.gfsr_after = check_state.gfsr;
                (void)read_reg(context, FUXI_SMMU_GR0_GFSYNR1, &info.gfsynr1);
                if (info_out) *info_out = info;
                return FUXI_SENSORS_SMMU_OK;
            }
            (void)read_reg(context, FUXI_SMMU_GR0_GFSR, &info.gfsr_after);
            (void)read_reg(context, FUXI_SMMU_GR0_GFSYNR1, &info.gfsynr1);
            rc = FUXI_SENSORS_SMMU_VERIFY_ERROR;
            ++touched;
            break;
        }
    }

    while (touched > 0) {
        --touched;
        if (fuxi_audio_smmu_write_check(read_reg, write_reg, context, target[touched].smr, target[touched].old_smr)) {
            (void)fuxi_audio_smmu_write_check(read_reg, write_reg, context,
                target[touched].s2cr, FUXI_SMMU_FAULT_S2CR);
        }
    }
    (void)fuxi_sensors_smmu_tlb_sync(read_reg, write_reg, context);
    if (info_out) *info_out = info;
    return rc;
}

#endif
