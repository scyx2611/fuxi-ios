"""Exercise the production PMIC channel lookup with the PCM cache off and on."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent
source = (root / "ksrc/xnu/fuxi-adsp/fuxi_pmic.c").read_text(encoding="utf-8")
lookup = source[source.index("static int fp_find("):source.index("static int fp_read_registers(")]
model = """
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
static int fp_core, fp_config;
static uint32_t mapping[768], ownership[768];
static unsigned fp_apids = 768, reads, fp_haptic_apid_plus_one[2];
static uint32_t fp_read32(int *window, unsigned address) {
    reads++;
    if (window == &fp_core) {
        assert(address >= 0x2000 && (address-0x2000)/4 < 768);
        return mapping[(address-0x2000)/4];
    }
    assert(window == &fp_config && address >= 0x1000 && (address-0x1000)/4 < 768);
    return ownership[(address-0x1000)/4];
}
"""
checks = """
int main(void) {
    unsigned owner = 7;
    mapping[130] = mapping[631] = 0x7f000;
    ownership[130] = 3;
    mapping[632] = 0x7f100;
    mapping[556] = 0x7f200;
    ownership[556] = 3;
    assert(fp_find(7, 0xf0, &owner) == 631 && !owner && reads == 770);
    fp_haptic_apid_plus_one[0] = 632;
    fp_haptic_apid_plus_one[1] = 633;
    reads = 0;
    assert(fp_find(7, 0xf0, &owner) == 631 && !owner && reads == 2);
    assert(fp_find(7, 0xf1, &owner) == 632 && !owner && reads == 4);
    assert(fp_find(7, 0xf2, &owner) == 556 && owner == 3);
    ownership[631] = 2;
    assert(fp_find(7, 0xf0, &owner) < 0);
    mapping[632] = 0x7f000;
    assert(fp_find(7, 0xf1, &owner) < 0);
    fp_haptic_apid_plus_one[0] = 769;
    assert(fp_find(7, 0xf0, &owner) < 0);
    puts("PASS haptic APID lookup, live PPID/owner, bounded cached and uncached paths");
}
"""
with tempfile.TemporaryDirectory() as temporary:
    directory = Path(temporary)
    path = directory / "apid.c"
    path.write_text(model + lookup + checks, encoding="utf-8")
    exe = directory / "apid"
    subprocess.run(["clang", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
                    "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                    str(path), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
