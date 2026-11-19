#include <stddef.h>
#include "fuxi_audio_smmu.h"

extern const unsigned char _bss_start[], _bss_end[], _payload_start[], _payload_end[];
extern uint32_t probe_safe_rd32(uint64_t address, uint32_t *faulted);
extern unsigned probe_safe_wr32(uint64_t address, uint32_t value);
extern void __real_probe_main(uintptr_t fdt);

/* Part of the loaded Image's zeroed BSS and outside the kernel RAM bank. */
static unsigned char audio_dma[FUXI_AUDIO_DMA_BYTES] __attribute__((aligned(FUXI_AUDIO_DMA_ALIGNMENT)));

/* Clang lowers the shared register snapshot's aggregate initialization to
 * freestanding memory calls. probe.c has private versions for its own use.
 */
void *memset(void *destination, int value, size_t size)
{
    volatile unsigned char *out = destination;
    for (size_t i = 0; i < size; ++i) {
        out[i] = (unsigned char)value;
    }
    return destination;
}

void *memcpy(void *destination, const void *source, size_t size)
{
    volatile unsigned char *out = destination;
    const unsigned char *in = source;
    for (size_t i = 0; i < size; ++i) {
        out[i] = in[i];
    }
    return destination;
}

static bool read_register(void *context, uint32_t offset, uint32_t *value)
{
    (void)context;
    uint32_t faulted = 0;
    *value = probe_safe_rd32(FUXI_APPS_SMMU_BASE + offset, &faulted);
    return !faulted && *value != UINT32_MAX;
}

static bool write_register(void *context, uint32_t offset, uint32_t value)
{
    (void)context;
    return probe_safe_wr32(FUXI_APPS_SMMU_BASE + offset, value) == 0;
}

/* loader_main has verified the live FDT before entering probe_main. The
 * wrapper runs on return, after the monitor accepted the XNU launch command.
 * The packer pins the location of the aligned handoff in the embedded ADT.
 */
void __wrap_probe_main(uintptr_t fdt)
{
    __real_probe_main(fdt);
    const uintptr_t pa = (uintptr_t)audio_dma;
    const uintptr_t payload_start = (uintptr_t)_payload_start;
    const uintptr_t payload_end = (uintptr_t)_payload_end;
    const uintptr_t handoff_offset = FUXI_AUDIO_HANDOFF_OFFSET;
    const struct fuxi_audio_dma_handoff candidate = {
        FUXI_AUDIO_DMA_MAGIC, FUXI_AUDIO_DMA_BYTES, (uint32_t)pa, ~(uint32_t)pa,
    };
    if (pa > UINT32_MAX || pa < (uintptr_t)_bss_start || pa > (uintptr_t)_bss_end ||
        (uintptr_t)_bss_end - pa < sizeof(audio_dma) ||
        !fuxi_audio_dma_handoff_valid(&candidate) ||
        (handoff_offset & 3u) || payload_end < payload_start ||
        handoff_offset > payload_end - payload_start ||
        payload_end - payload_start - handoff_offset < sizeof(candidate)) {
        return;
    }
    volatile struct fuxi_audio_dma_handoff *handoff = (void *)(payload_start + handoff_offset);
    if (handoff->magic != FUXI_AUDIO_DMA_MAGIC || handoff->bytes != FUXI_AUDIO_DMA_BYTES ||
        handoff->pa || handoff->inverted_pa) {
        return;
    }
    if (fuxi_audio_smmu_attach(read_register, write_register, 0)) {
        handoff->pa = candidate.pa;
        handoff->inverted_pa = candidate.inverted_pa;
        __asm__ volatile("dsb sy" ::: "memory");
    }
}
