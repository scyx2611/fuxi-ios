#ifndef FUXI_AUDIO_SMMU_H
#define FUXI_AUDIO_SMMU_H

#include <stdbool.h>
#include <stdint.h>

#define FUXI_APPS_SMMU_BASE 0x15000000ull
#define FUXI_APPS_SMMU_BYTES 0x100000u
#define FUXI_SMMU_MAX_BANKS 128u
#define FUXI_AUDIO_DMA_MAGIC 0x31445541u
#define FUXI_AUDIO_DMA_BYTES 0x10000u
#define FUXI_AUDIO_DMA_ALIGNMENT 0x4000u
#define FUXI_AUDIO_DMA_STREAM 1u /* low nibble of audio SID 0x1001 */
#define FUXI_AUDIO_BYPASS_BANK 4u
#define FUXI_AUDIO_DMA_SMR0 0x80801001u
#define FUXI_AUDIO_DMA_SMR1 0x80001061u
#define FUXI_SMMU_FAULT_S2CR (2u << 16)

struct fuxi_audio_dma_handoff {
    uint32_t magic, bytes, pa, inverted_pa;
};

enum fuxi_audio_smmu_result {
    FUXI_AUDIO_SMMU_OK, FUXI_AUDIO_SMMU_READ_ERROR, FUXI_AUDIO_SMMU_UNSUPPORTED
};

struct fuxi_audio_smmu_stream {
    unsigned matches;
    int slot;
    uint32_t smr, s2cr;
};

struct fuxi_audio_smmu_state {
    uint32_t scr0, idr0, idr1, idr2, gfsr;
    unsigned slots, banks, stride, bank_base, blank_slots, unused_banks;
    int first_slots[2], first_bank;
    struct fuxi_audio_smmu_stream audio[2], wlan, usb;
};

typedef bool (*fuxi_audio_smmu_read_fn)(void *context, uint32_t offset, uint32_t *value);
typedef bool (*fuxi_audio_smmu_write_fn)(void *context, uint32_t offset, uint32_t value);

/* Legacy SMR format. DT msm-audio-ion supplies the two audio IDs and masks.
 * Use the ARM SMMU overlap rule, including the bits ignored by either mask.
 */
static inline void
fuxi_audio_smmu_match(struct fuxi_audio_smmu_stream *stream, uint32_t sid, uint32_t mask,
    unsigned slot, uint32_t smr, uint32_t s2cr)
{
    const uint32_t smr_mask = (smr >> 16) & 0x7fffu;
    if (((smr ^ sid) & ~(smr_mask | mask) & 0x7fffu) != 0) {
        return;
    }
    if (stream->matches++ == 0) {
        stream->slot = (int)slot;
        stream->smr = smr;
        stream->s2cr = s2cr;
    }
}

/* Read-only inventory using the register layout already used by fuxi_wlan.c.
 * Blank slots and inactive banks are observations. They require revalidation
 * alongside the other apps_smmu clients when audio actually claims a mapping.
 */
static inline enum fuxi_audio_smmu_result
fuxi_audio_smmu_read_state(fuxi_audio_smmu_read_fn read_reg, void *context,
    struct fuxi_audio_smmu_state *state)
{
    *state = (struct fuxi_audio_smmu_state){
        .first_slots = {-1, -1}, .first_bank = -1,
        .audio = {{.slot = -1}, {.slot = -1}}, .wlan = {.slot = -1}, .usb = {.slot = -1},
    };
    if (!read_reg(context, 0x0u, &state->scr0) ||
        !read_reg(context, 0x20u, &state->idr0) ||
        !read_reg(context, 0x24u, &state->idr1) ||
        !read_reg(context, 0x28u, &state->idr2) ||
        !read_reg(context, 0x48u, &state->gfsr)) {
        return FUXI_AUDIO_SMMU_READ_ERROR;
    }

    const unsigned stride = (state->idr1 & (1u << 31)) ? 0x10000u : 0x1000u;
    const unsigned bank_base = (1u << (((state->idr1 >> 28) & 7u) + 1)) * stride;
    const unsigned stage2_banks = (state->idr1 >> 16) & 255u;
    state->stride = stride;
    state->bank_base = bank_base;
    state->slots = state->idr0 & 255u;
    state->banks = state->idr1 & 255u;
    if ((state->scr0 & 9u) || (state->idr0 & 0x48000000u) != 0x48000000u ||
        !(state->idr2 & 0x1000u) || !state->slots || !state->banks ||
        state->banks > FUXI_SMMU_MAX_BANKS || stage2_banks > state->banks ||
        bank_base > FUXI_APPS_SMMU_BYTES ||
        state->banks > (FUXI_APPS_SMMU_BYTES - bank_base) / stride) {
        return FUXI_AUDIO_SMMU_UNSUPPORTED;
    }

    bool referenced[FUXI_SMMU_MAX_BANKS] = {false};
    for (unsigned i = 0; i < state->slots; ++i) {
        uint32_t smr, s2cr;
        if (!read_reg(context, 0x800u + 4u * i, &smr) ||
            !read_reg(context, 0xc00u + 4u * i, &s2cr)) {
            return FUXI_AUDIO_SMMU_READ_ERROR;
        }
        const unsigned bank = s2cr & 255u;
        if (bank < state->banks) {
            referenced[bank] = true;
        }
        if (smr & (1u << 31)) {
            fuxi_audio_smmu_match(&state->audio[0], 0x1001u, 0x80u, i, smr, s2cr);
            fuxi_audio_smmu_match(&state->audio[1], 0x1061u, 0, i, smr, s2cr);
            fuxi_audio_smmu_match(&state->wlan, 0x1401u, 0, i, smr, s2cr);
            fuxi_audio_smmu_match(&state->usb, 0x40u, 0, i, smr, s2cr);
        } else if (s2cr == FUXI_SMMU_FAULT_S2CR) {
            if (state->blank_slots < 2) {
                state->first_slots[state->blank_slots] = (int)i;
            }
            state->blank_slots++;
        }
    }

    bool used_asid[FUXI_SMMU_MAX_BANKS] = {false};
    for (unsigned i = 0; i < state->banks; ++i) {
        const uint32_t base = bank_base + stride * i;
        uint32_t sctlr, ttbr0, ttbr1;
        if (!read_reg(context, base, &sctlr)) {
            return FUXI_AUDIO_SMMU_READ_ERROR;
        }
        if (sctlr & 1u) {
            referenced[i] = true;
            if (!read_reg(context, base + 0x24u, &ttbr0) ||
                !read_reg(context, base + 0x2cu, &ttbr1)) {
                return FUXI_AUDIO_SMMU_READ_ERROR;
            }
            if ((ttbr0 >> 16) < state->banks) {
                used_asid[ttbr0 >> 16] = true;
            }
            if ((ttbr1 >> 16) < state->banks) {
                used_asid[ttbr1 >> 16] = true;
            }
        }
    }
    for (unsigned i = stage2_banks; i < state->banks; ++i) {
        if (!referenced[i] && !used_asid[i]) {
            if (state->unused_banks++ == 0) {
                state->first_bank = (int)i;
            }
        }
    }
    return FUXI_AUDIO_SMMU_OK;
}

static inline bool
fuxi_audio_dma_handoff_valid(const struct fuxi_audio_dma_handoff *handoff)
{
    return handoff->magic == FUXI_AUDIO_DMA_MAGIC && handoff->bytes == FUXI_AUDIO_DMA_BYTES &&
        handoff->pa != 0 && (handoff->pa & (FUXI_AUDIO_DMA_ALIGNMENT - 1u)) == 0 &&
        handoff->pa <= UINT32_MAX - (FUXI_AUDIO_DMA_BYTES - 1u) &&
        handoff->inverted_pa == ~handoff->pa;
}

/* Earlier Fuxi playback and touch attach to ABL's DWC3 bank. Require that
 * route, a disabled stage 1 and stage 2 bypass before using physical addresses.
 * Neither the loader nor the audio driver reprograms the shared bank.
 */
static inline bool
fuxi_audio_smmu_bypass_ready(fuxi_audio_smmu_read_fn read_reg, void *context,
    const struct fuxi_audio_smmu_state *state)
{
    const unsigned bank = FUXI_AUDIO_BYPASS_BANK;
    uint32_t sctlr, cbar;
    return state->banks > bank && state->usb.matches == 1 && state->usb.s2cr == bank &&
        read_reg(context, state->bank_base + state->stride * bank, &sctlr) && !(sctlr & 1u) &&
        read_reg(context, state->stride + 4u * bank, &cbar) && ((cbar >> 16) & 3u) == 1u;
}

static inline bool
fuxi_audio_smmu_routes_ready(const struct fuxi_audio_smmu_state *state)
{
    return state->audio[0].matches == 1 && state->audio[1].matches == 1 &&
        state->audio[0].smr == FUXI_AUDIO_DMA_SMR0 && state->audio[1].smr == FUXI_AUDIO_DMA_SMR1 &&
        state->audio[0].s2cr == FUXI_AUDIO_BYPASS_BANK && state->audio[1].s2cr == FUXI_AUDIO_BYPASS_BANK;
}

static inline bool
fuxi_audio_smmu_write_check(fuxi_audio_smmu_read_fn read_reg, fuxi_audio_smmu_write_fn write_reg,
    void *context, uint32_t offset, uint32_t value)
{
    uint32_t actual;
    return write_reg(context, offset, value) && read_reg(context, offset, &actual) && actual == value;
}

/* Called only by the single-threaded loader after the USB monitor hands off.
 * Reserve exact audio stream match entries for this boot. Keep a failed or
 * partially rolled back installation from publishing a DMA handoff.
 */
static inline bool
fuxi_audio_smmu_attach(fuxi_audio_smmu_read_fn read_reg, fuxi_audio_smmu_write_fn write_reg, void *context)
{
    struct fuxi_audio_smmu_state state;
    if (fuxi_audio_smmu_read_state(read_reg, context, &state) != FUXI_AUDIO_SMMU_OK ||
        !fuxi_audio_smmu_bypass_ready(read_reg, context, &state)) {
        return false;
    }
    if (fuxi_audio_smmu_routes_ready(&state)) {
        return true;
    }
    if (state.audio[0].matches || state.audio[1].matches || state.blank_slots < 2) {
        return false;
    }

    const uint32_t matches[] = {FUXI_AUDIO_DMA_SMR0, FUXI_AUDIO_DMA_SMR1};
    struct {uint32_t smr, s2cr, old_smr;} slots[2];
    for (unsigned i = 0; i < 2; ++i) {
        uint32_t s2cr;
        const unsigned offset = 4u * (unsigned)state.first_slots[i];
        slots[i].smr = 0x800u + offset;
        slots[i].s2cr = 0xc00u + offset;
        if (!read_reg(context, slots[i].smr, &slots[i].old_smr) || (slots[i].old_smr & (1u << 31)) ||
            !read_reg(context, slots[i].s2cr, &s2cr) || s2cr != FUXI_SMMU_FAULT_S2CR) {
            return false;
        }
    }

    unsigned touched = 0;
    for (; touched < 2; ++touched) {
        if (!fuxi_audio_smmu_write_check(read_reg, write_reg, context, slots[touched].s2cr, FUXI_AUDIO_BYPASS_BANK) ||
            !fuxi_audio_smmu_write_check(read_reg, write_reg, context, slots[touched].smr, matches[touched])) {
            ++touched;
            break;
        }
        if (touched == 1) {
            const uint32_t old_fault = state.gfsr;
            if (fuxi_audio_smmu_read_state(read_reg, context, &state) == FUXI_AUDIO_SMMU_OK &&
                state.gfsr == old_fault && fuxi_audio_smmu_bypass_ready(read_reg, context, &state) &&
                fuxi_audio_smmu_routes_ready(&state)) {
                return true;
            }
        }
    }
    while (touched > 0) {
        --touched;
        if (fuxi_audio_smmu_write_check(read_reg, write_reg, context, slots[touched].smr, slots[touched].old_smr)) {
            (void)fuxi_audio_smmu_write_check(read_reg, write_reg, context,
                slots[touched].s2cr, FUXI_SMMU_FAULT_S2CR);
        }
    }
    return false;
}

#endif
