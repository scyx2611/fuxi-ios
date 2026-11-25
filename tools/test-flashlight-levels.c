#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "FuxiTorchLevels.h"
#include "../userland/fuxi-shape/FuxiFlashlightLogic.h"

int main(void)
{
    const float canonical[] = {0.0f, 0.25f, 0.5f, 0.85f, 1.0f};
    unsigned decoded = 99;
    for (unsigned index = 0; index <= 4; index++) {
        assert(fuxi_torch_ui_decode(canonical[index], &decoded));
        assert(decoded == index);
        assert(fuxi_torch_ui_encode(index) == canonical[index]);
        assert(fuxi_torch_ui_decode(fuxi_torch_ui_encode(index), &decoded));
        assert(decoded == index);
    }
    for (unsigned index = 4; index > 0; index--) {
        assert(fuxi_torch_ui_decode(canonical[index], &decoded));
        assert(decoded == index);
        assert(fuxi_torch_ui_decode(canonical[index - 1], &decoded));
        assert(decoded == index - 1);
    }
    for (unsigned index = 1; index < 4; index++) {
        float boundary = canonical[index];
        assert(fuxi_torch_ui_decode(nextafterf(boundary, 0.0f), &decoded));
        assert(decoded == index);
        assert(fuxi_torch_ui_decode(nextafterf(boundary, 1.0f), &decoded));
        assert(decoded == index + 1);
    }
    assert(fuxi_torch_ui_decode(nextafterf(0.0f, 1.0f), &decoded) && decoded == 1);
    assert(fuxi_torch_ui_decode(-0.0f, &decoded) && decoded == 0);
    unsigned previous = 0;
    for (unsigned sample = 0; sample <= 10000; sample++) {
        float value = (float)sample / 10000.0f;
        assert(fuxi_torch_ui_decode(value, &decoded));
        assert(decoded >= previous && decoded <= 4);
        assert((value == 0.0f) == (decoded == 0));
        previous = decoded;
    }
    const float invalid[] = {-1.0f, 1.01f, INFINITY, -INFINITY, NAN};
    for (unsigned index = 0; index < sizeof(invalid) / sizeof(invalid[0]); index++) {
        decoded = 99;
        assert(!fuxi_torch_ui_decode(invalid[index], &decoded));
        assert(decoded == 99);
    }
    assert(!fuxi_torch_ui_decode(0.5f, 0));
    assert(fuxi_torch_ui_encode(5) == 0.0f);

    /* Coalescing burst stress: 56 rapid updates coalesce to latest without dropping 0 */
    struct fuxi_torch_coalesce_state cs;
    unsigned out_level = 99u;
    fuxi_torch_coalesce_init(&cs);
    assert(fuxi_torch_coalesce_submit(&cs, 1u, false) == true);
    for (unsigned step = 0u; step < 55u; step++) {
        assert(fuxi_torch_coalesce_submit(&cs, (step % 4u) + 1u, false) == false);
    }
    assert(fuxi_torch_coalesce_submit(&cs, 3u, false) == false);
    assert(fuxi_torch_coalesce_take(&cs, &out_level) == true && out_level == 3u);
    assert(fuxi_torch_coalesce_take(&cs, &out_level) == false && cs.in_flight == false);

    assert(fuxi_torch_coalesce_submit(&cs, 4u, false) == true);
    assert(fuxi_torch_coalesce_submit(&cs, 2u, true) == false); /* explicit off wins over non-zero level */
    assert(fuxi_torch_coalesce_take(&cs, &out_level) == true && out_level == 0u);
    assert(fuxi_torch_coalesce_take(&cs, &out_level) == false);

    puts("PASS flashlight canonical levels, coalescing burst (56->latest & off=0), and invalid requests");
    return 0;
}
