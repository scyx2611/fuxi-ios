"""Exercise the lower speaker's real muted probe with a simulated B2 bus."""
import argparse
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / "ksrc/xnu/fuxi-adsp/fuxi_amp.c"

MODEL = r"""
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { kSuccess = 0 };
typedef unsigned DTEntry;
static const uint8_t kAddr[] = {0x40, 0x42};
static struct { uint8_t address; unsigned error; } gBus;
static uint32_t control[3];
static uint8_t plan[29932], ram[0x9020], cal[4216];
static unsigned mode, directory_reads, tuned, bad_dir, bad_tuning;
static unsigned plan_size, calls, fail_at, disables, writes, verified, corrupt, reenable, ignore_disable;

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static int step(void) { return ++calls != fail_at; }
static void IOSleep(unsigned ms) { (void)ms; assert(!"mode 6 only"); }
static int ampTransferDone(void) { return step(); }
static int ampRead(uint8_t address, uint32_t reg, uint32_t *out)
{
    assert(address == 0x40);
    if (mode == 5 && reg >= 0x02800000 && reg < 0x02800000+23*4) {
        unsigned i = (reg-0x02800000)/4;
        *out = 0;
        for (unsigned b=0;b<3;b++) *out |= (uint32_t)ram[(3*i+b)^3] << (b*8);
        if (bad_dir && i==11) *out ^= 1;
        directory_reads++;
        return step();
    }
    if (corrupt && (reg == 0x02000000 || reg == 0x02800000)) {
        *out = 0;
        return step();
    }
    assert(reg == FM_PWR_CTRL1 || reg == FM_PWR_CTRL2 || reg == FM_CCM_CORE_CTRL);
    *out = control[reg == FM_PWR_CTRL1 ? 0 : reg == FM_PWR_CTRL2 ? 1 : 2];
    return step();
}
static int geni_i2c_start_write_bytes(void *bus, const uint8_t *p, unsigned n)
{
    assert(bus == &gBus && gBus.address == 0x40 && n == 8 && be32(p+4) == 0 && !control[0]);
    assert(be32(p) == (disables ? FM_CCM_CORE_CTRL : FM_PWR_CTRL2));
    assert(disables < 2);
    if (!step()) return -1;
    ++disables;
    if (!ignore_disable) control[disables] = 0;
    return 0;
}
static unsigned xm_offset(const uint8_t *p, unsigned n)
{
    uint32_t reg = be32(p);
    uint32_t off = reg - 0x02000000;
    assert(gBus.address == 0x40 && !control[0] && !control[1] && !control[2]);
    if (mode == 5 && writes >= 1809 && n==4) {
        assert(directory_reads==23);
        switch(reg) {
        case 0x034017b0: return 0x9000;
        case 0x02800268: return 0x9004;
        case 0x02800274: return 0x9008;
        case 0x02800270: return 0x900c;
        default: assert(0);
        }
    }
    if (mode >= 4 && reg >= 0x03800000) {
        off = reg - 0x03800000;
        assert(n == 20 && !(off % 20) && off+n <= 20420);
        return 0x4000 + off;
    }
    if (mode >= 4 && reg >= 0x02c00000) {
        off = reg - 0x02c00000;
        assert(n == 12 && !(off % 12) && off+n <= 6000);
        return 0x2000 + off;
    }
    assert(n == 12 && !(off % 12));
    assert(off+n <= 72 || (off >= 156 && off+n <= 684) ||
        (mode >= 4 && ((off >= 972 && off+n <= 2568) || (off >= 2580 && off+n <= 3840))));
    return off;
}
static int geni_i2c_start_write_buffer(void *bus, const uint8_t *p, unsigned length)
{
    assert(bus == &gBus && length >= 8);
    unsigned off = xm_offset(p, length - 4);
    if (writes >= 1809 && length == 16) {
        assert(directory_reads == 23 && be32(p) == 0x02c0015c + 12*(writes-1809));
        if (bad_tuning) return -1;
    }
    if (!step()) return -1;
    memcpy(ram + off, p + 4, length - 4);
    ++writes;
    return 0;
}
static int geni_i2c_start_read_buffer(void *bus, const uint8_t *p, unsigned addr_len, uint8_t *out, unsigned n)
{
    assert(bus == &gBus && addr_len == 4);
    unsigned off = xm_offset(p, n);
    if (!step()) return -1;
    memcpy(out, ram + off, n);
    if (corrupt) out[0] ^= 1;
    if (reenable && off + n == 684) control[2] = 1;
    return 0;
}
static int SecureDTLookupEntry(void *parent, const char *name, DTEntry *entry)
{
    assert(!parent && !strcmp(name, mode>=4 ? "/chosen/fuxi-speaker" : "/chosen")); *entry = 1; return 0;
}
static int SecureDTGetProperty(DTEntry entry, const char *key, const void **p, unsigned *length)
{
    assert(entry == 1);
    if (mode==5 && !strcmp(key,"fuxi,cs35l41-cal")) {
        *p=cal; *length=sizeof(cal); return 0;
    }
    assert(!strcmp(key,"fuxi,cs35l41-prot")); *p=plan; *length=plan_size; return 0;
}
static int record(const char *fmt, ...)
{
    char text[160]; va_list ap;
    va_start(ap,fmt); vsnprintf(text,sizeof(text),fmt,ap); va_end(ap);
    if (strstr(text,"muted HALO XM probe verified bytes=600 sections=2") ||
        strstr(text,"full muted HALO verified bytes=29876 sections=6")) verified++;
    if (strstr(text,"muted bottom tuning verified bytes=4204 calibration=3 controls")) tuned++;
    return 0;
}
#define printf record
"""

TEST = r"""
#undef printf
static void fixture(void)
{
    const uint32_t head[] = {0x31505343,2,0x02000000,72}, second[] = {0x0200009c,528};
    for (unsigned i=0;i<sizeof(plan);i++) plan[i]=(uint8_t)(3*i+1);
    memcpy(plan,head,sizeof(head)); memcpy(plan+88,second,sizeof(second));
    memset(ram,0,sizeof(ram));
    control[0]=0; control[1]=0x3321; control[2]=0x101;
    mode=3; plan_size=624; calls=fail_at=disables=writes=verified=corrupt=reenable=ignore_disable=0;
}
int main(void)
{
    fixture(); ampLoadMutedProtection(0x40, mode);
    assert(verified==1 && disables==2 && writes==50);
    assert(!memcmp(ram,plan+16,72) && !memcmp(ram+156,plan+96,528));
    unsigned operations=calls;
    for (unsigned i=1;i<=operations;i++) {
        fixture(); fail_at=i; ampLoadMutedProtection(0x40, mode); assert(!verified && calls==i);
    }
    fixture(); control[0]=1; ampLoadMutedProtection(0x40, mode); assert(!verified && !disables && !writes);
    fixture(); control[1]=control[2]=0; ampLoadMutedProtection(0x40, mode); assert(verified && !disables);
    fixture(); ignore_disable=1; ampLoadMutedProtection(0x40, mode); assert(!verified && disables==1 && !writes);
    fixture(); corrupt=1; ampLoadMutedProtection(0x40, mode); assert(!verified && writes==1);
    fixture(); reenable=1; ampLoadMutedProtection(0x40, mode); assert(!verified && disables==2 && writes==50);
    fixture(); ampLoadMutedProtection(0x42, mode); assert(!calls);
    fixture(); --plan_size; ampLoadMutedProtection(0x40, mode); assert(!calls);
    fixture(); plan[88]^=1; ampLoadMutedProtection(0x40, mode); assert(!calls);
    fixture(); mode=4; ampLoadMutedProtection(0x40, mode); assert(!calls);
    fixture(); mode=4;
    uint32_t full_header[]={0x32505343,6};
    memcpy(plan,full_header,sizeof(full_header));
    const uint32_t rest[][2]={{0x020003cc,1596},{0x02000a14,1260},{0x02c00000,6000},{0x03800000,20420}};
    for (unsigned i=0;i<4;i++) {
        memcpy(plan+plan_size,rest[i],8);
        plan_size+=8+rest[i][1];
    }
    assert(plan_size==sizeof(plan));
    plan_size--; ampLoadMutedProtection(0x40, mode); assert(!calls); plan_size++;
    ampLoadMutedProtection(0x40, mode);
    assert(verified==1 && writes==1809);
    unsigned offset=624;
    const unsigned mapped[]={972,2580,0x2000,0x4000};
    for (unsigned i=0;i<4;i++) {
        assert(!memcmp(ram+mapped[i],plan+offset+8,rest[i][1]));
        offset+=8+rest[i][1];
    }
    const uint32_t words[]={0x40000,0x30000,2,0x400a4,0x2b01,0x1f4,14,0,0,2,205,0x1d3f01,
        148,80,14,1980,0xf20b,256,374,76,0,0,0xbedead,0};
    for(unsigned i=0;i<24;i++) for(unsigned b=0;b<3;b++)
        plan[16+((i*3+b)^3)] = (uint8_t)(words[i]>>(8*b));
    mode=5; memcpy(cal,"CSB1",4); cal[7]=0x65; cal[11]=0x66;
    for(unsigned i=12;i<sizeof(cal);i++) cal[i]=(uint8_t)i;
    writes=calls=0; cal[11]++; ampLoadMutedProtection(0x40,mode);
    assert(!calls); cal[11]--;
    bad_dir=1; ampLoadMutedProtection(0x40,mode);
    assert(!tuned && directory_reads==12 && writes==1809);
    bad_dir=0; bad_tuning=1; directory_reads=writes=0; ampLoadMutedProtection(0x40,mode);
    assert(!tuned && directory_reads==23 && writes==1809);
    bad_tuning=0; directory_reads=writes=0; ampLoadMutedProtection(0x40,mode);
    assert(tuned==1 && writes==2163 && !memcmp(ram+0x215c,cal+12,4200));
    assert(!memcmp(ram+0x9000,cal+4212,4) && be32(ram+0x9004)==0x65 &&
        be32(ram+0x9008)==0x66 && be32(ram+0x900c)==1);
    printf("PASS muted CS35L41 B2 probe, two and six sections, %u injected bus failures\n",operations);
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="clang")
    args = parser.parse_args()
    source = SOURCE.read_text(encoding="utf-8")
    constants = "\n".join(line for line in source.splitlines() if line.startswith("#define FM_"))
    probe = source[source.index("static int ampWriteZero("):source.index("/* The survey reads")]
    with tempfile.TemporaryDirectory() as tmp:
        directory = Path(tmp)
        path = directory / "probe.c"
        path.write_text(constants + "\n" + MODEL + probe + TEST, encoding="utf-8")
        exe = directory / "probe"
        subprocess.run([args.cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
                        "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                        str(path), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True, timeout=30)


if __name__ == "__main__":
    main()
