#ifndef FUXI_TORCH_RUNTIME_H
#define FUXI_TORCH_RUNTIME_H
#include <stdint.h>

typedef struct {
    unsigned level;
    unsigned failed;
    uint64_t pulses;
    uint64_t deadline;
} FuxiTorchRuntime;

static inline int fuxi_torch_name_eq(const char *name, const char *expected)
{
    unsigned index = 0;
    for (; expected[index]; index++) {
        if (name[index] != expected[index]) return 0;
    }
    return name[index] == 0;
}

static inline int fuxi_torch_caller_allowed(unsigned root, unsigned platform, const char *name)
{
    if (root) return 1;
    if (!platform || !name) return 0;
    return fuxi_torch_name_eq(name, "SpringBoard") || fuxi_torch_name_eq(name, "mediaserverd");
}

static inline int fuxi_torch_request(FuxiTorchRuntime *state, unsigned level, uint64_t now)
{
    if (!state || level > 4 || (level && (state->failed || now > UINT64_MAX - 2000000000ull))) return -1;
    state->level = level;
    state->deadline = level ? now + 2000000000ull : 0;
    return 0;
}

static inline unsigned fuxi_torch_requested(FuxiTorchRuntime *state, uint64_t now)
{
    if (state->failed || (state->level && now >= state->deadline)) state->level = 0;
    return state->level;
}
#endif
