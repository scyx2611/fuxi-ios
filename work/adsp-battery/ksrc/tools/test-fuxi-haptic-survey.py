import argparse
import importlib.util
import os
from pathlib import Path
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
PMIC = ROOT / 'xnu/fuxi-adsp/fuxi_pmic.c'
SPEC = importlib.util.spec_from_file_location('fifo_runner', ROOT / 'tools/test-fuxi-haptic-fifo.py')
RUNNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNNER)

PREFIX = r'''
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "fuxi_haptic_rtp.h"

typedef unsigned DTEntry;
struct fp_window { unsigned unused; };
static struct fp_window fp_core, fp_config, fp_observer;
static unsigned fp_apids, fp_started, fp_torch_audit_enabled;
static uint8_t fp_poisoned[1024];
static int fp_lock;
enum { kSuccess = 0, KERN_SUCCESS = 0 };
#ifndef __ATOMIC_ACQ_REL
#define __ATOMIC_ACQ_REL 0
#endif

static struct {
    uint8_t registers[512];
    unsigned mode, haptic, by_dt, reads, owner_checks, maps, legacy, flash, torch, diagnostic;
    unsigned observations, summaries, valid, stable, identity, attempts, observed_fifo, contradictory;
    unsigned fail_read, poison_failure, owner_change, apid_change, mutate_read, selector_race, selector_reads;
    unsigned status_reads, invalid_rows, tlra_lines, vibrate, vibrate_selftest;
    unsigned wave_bytes, hal, vmax;
    uint8_t wave[FUXI_RTP_BYTES];
} model;

static void lck_mtx_lock(int *lock) { assert(!*lock); *lock = 1; }
static void lck_mtx_unlock(int *lock) { assert(*lock); *lock = 0; }
static int fp_find(unsigned sid, unsigned peripheral, unsigned *owner)
{
    assert(fp_lock && sid == 7 && (peripheral == 0xf0 || peripheral == 0xf1));
    model.owner_checks++;
    *owner = model.owner_checks == model.owner_change ? 3u : 0u;
    return (int)(631u + peripheral - 0xf0u + (model.owner_checks == model.apid_change));
}
static int fp_read(unsigned sid, unsigned address, uint8_t *value)
{
    assert(!fp_lock && sid == 7 && address >= 0xf000 && address < 0xf200);
    assert(address != 0xf120 && address != 0xf121 && address != 0xf122 && address != 0xf123 && address != 0xf12c);
    model.reads++;
    if (model.reads == model.fail_read) {
        if (model.poison_failure) fp_poisoned[631u + (address >> 8) - 0xf0u] = 1;
        return -1;
    }
    if (model.reads == model.mutate_read) model.registers[0x124] ^= 1;
    *value = model.registers[address - 0xf000u];
    if (address == 0xf070 && ++model.selector_reads == 2 && model.selector_race) *value ^= 1;
    if (address == 0xf009 || address == 0xf00a) model.status_reads++;
    return 0;
}
static int log_printf(const char *format, ...)
{
    char text[1024];
    va_list arguments;
    va_start(arguments, format);
    int length = vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);
    assert(length > 0 && (unsigned)length < sizeof(text));
    if (strstr(text, "haptic-fifo-ro round=") && strstr(text, " addr=")) {
        unsigned round, address, owner_before, owner_after, value, valid;
        int apid_before, apid_after, result;
        assert(sscanf(text, "FUXI PMIC: haptic-fifo-ro round=%u addr=%x APID=%d/%d owner=%u/%u value=%x rc=%d valid=%u",
            &round, &address, &apid_before, &apid_after, &owner_before, &owner_after, &value, &result, &valid) == 9);
        assert(round >= 1 && round <= 2 && address >= 0xf000 && address < 0xf200);
        if (valid) assert(!result && apid_before == apid_after && !owner_before && !owner_after);
        else model.invalid_rows++;
        model.observations++;
    }
    if (strstr(text, "FIFO-status=observed")) model.observed_fifo++;
    if (strstr(text, "FIFO-status=contradictory")) model.contradictory++;
    if (strstr(text, "TLRA-valid=1 TLRA=5710us")) model.tlra_lines++;
    if (strstr(text, "haptic-fifo-ro end")) {
        unsigned rounds, gate, start, writes, cutoff, route;
        assert(sscanf(text, "FUXI PMIC: haptic-fifo-ro end rounds=%u attempts=%u valid=%u stable=%u identity-valid=%u active-gate=%u start-attempted=%u writes=%u cutoff-verified=%u route-verified=%u",
            &rounds, &model.attempts, &model.valid, &model.stable, &model.identity,
            &gate, &start, &writes, &cutoff, &route) == 10);
        assert(rounds == 2 && !gate && !start && !writes && !cutoff && !route);
        model.summaries++;
    }
    return length;
}
static unsigned exchange(unsigned *target, unsigned value, unsigned order)
{
    unsigned previous = *target;
    (void)order;
    *target = value;
    return previous;
}
#define __atomic_exchange_n exchange
#define printf log_printf
static int PE_parse_boot_argn(const char *name, void *value, unsigned bytes)
{
    assert(bytes == sizeof(unsigned));
    if (!strcmp(name, "fuxihalhaptic")) { memcpy(value, &model.hal, bytes); return model.hal != 0; }
    if (model.by_dt) return 0;
    if (!strcmp(name, "fuxipmic")) { memcpy(value, &model.mode, bytes); return model.mode != 0; }
    if (!strcmp(name, "fuxihaptic")) { memcpy(value, &model.haptic, bytes); return model.haptic != 0; }
    assert(0);
    return 0;
}
static int SecureDTLookupEntry(void *parent, const char *path, DTEntry *entry)
{
    (void)parent;
    assert(!strcmp(path, "/chosen"));
    *entry = 1;
    return model.by_dt ? 0 : -1;
}
static int SecureDTGetProperty(DTEntry entry, const char *name, void **value, unsigned *bytes)
{
    assert(entry == 1);
    *bytes = sizeof(unsigned);
    if (!strcmp(name, "fuxi,pmic")) *value = &model.mode;
    else if (!strcmp(name, "fuxi,haptic")) *value = &model.haptic;
    else if (!strcmp(name, "fuxi,haptic-rtp") && model.wave_bytes) {
        *value = model.wave;
        *bytes = model.wave_bytes;
    }
    else if (!strcmp(name, "fuxi,haptic-rtp-vmax")) *value = &model.vmax;
    else return -1;
    return 0;
}
static int fp_map(struct fp_window *window) { (void)window; model.maps++; return 0; }
static uint32_t fp_read32(const struct fp_window *window, unsigned offset)
{
    assert(window == &fp_core);
    return offset ? 1024u : 0x70000000u;
}
static void fp_survey(unsigned sid, unsigned base, const char *name)
{
    (void)sid; (void)base; (void)name; model.legacy++;
}
static void fp_flash_pulse(void) { model.flash++; }
static void fp_torch_start(void) { model.torch++; }
static void fp_haptic_diagnostic(void) { model.diagnostic++; }
static unsigned fp_vibrate_selftest, fp_vibrate_transient, fp_rtp_pcm_enabled;
static uint32_t fp_rtp_vmax;
static const uint8_t *fp_rtp_wave;
static void fp_vibrate_runtime_start(void) { model.vibrate++; model.vibrate_selftest = fp_vibrate_selftest; }
'''

TESTS = r'''
#undef printf
static void fixture(unsigned mode)
{
    memset(&model, 0, sizeof(model));
    memset(fp_poisoned, 0, sizeof(fp_poisoned));
    fp_started = fp_apids = fp_torch_audit_enabled = 0;
    fp_vibrate_selftest = fp_vibrate_transient = fp_rtp_pcm_enabled = fp_rtp_vmax = 0;
    fp_rtp_wave = NULL;
    fp_lock = 0;
    model.mode = mode;
    model.registers[0x01] = model.registers[0x101] = 4;
    model.registers[0x04] = model.registers[0x104] = 0x15;
    model.registers[0x05] = 0x0a; model.registers[0x105] = 0x0b;
    model.registers[0x5c] = 4; model.registers[0x5d] = 0x76;
    model.registers[0x4a] = 0x84; model.registers[0x4d] = 0xc8;
    model.registers[0x72] = 4; model.registers[0x1a0] = 0x84;
}
static void run(void)
{
    uint8_t before[512];
    memcpy(before, model.registers, sizeof(before));
    fuxi_pmic_init();
    assert(!fp_lock && model.observations <= 62 && model.owner_checks <= 124);
    if (!model.mutate_read) assert(!memcmp(before, model.registers, sizeof(before)));
}
int main(void)
{
    unsigned index;
    for (index = 0; index < 2; index++) {
        fixture(0); model.by_dt = index; run();
        assert(!model.maps && !model.legacy && !model.reads && !model.owner_checks && !model.summaries);
        fixture(1); model.by_dt = index; run();
        assert(model.maps == 3 && model.legacy == 4 && model.reads == 58 && model.owner_checks == 116);
        assert(model.summaries == 1 && model.valid && model.stable && model.identity && model.tlra_lines == 2);
        assert(!model.status_reads && !model.observed_fifo && !model.flash && !model.torch && !model.diagnostic);
        run(); assert(model.reads == 58 && model.summaries == 1);
    }
    for (index = 2; index <= 4; index++) {
        fixture(index); run();
        assert(!model.observations && !model.reads && !model.owner_checks);
        assert(model.flash == (unsigned)(index == 2) && model.torch == (unsigned)(index == 3));
    }
    fixture(0); model.haptic = 1; run();
    assert(model.diagnostic == 1 && !model.observations && !model.vibrate);
    for (index = 2; index <= 4; index++) {
        fixture(0); model.haptic = index; fp_vibrate_selftest = 7; run();
        assert(model.vibrate == 1 && model.vibrate_selftest == (index == 3) && !model.diagnostic);
        assert(fp_vibrate_transient == (index == 4) && !fp_rtp_wave);
        assert(model.maps == 3 && !model.observations && !model.flash && !model.torch);
    }
    for (index = 0; index < 2; index++) {
        fixture(0); model.by_dt = 1; model.haptic = 5;
        model.wave_bytes = index ? FUXI_RTP_BYTES - 1 : 0; run();
        assert(!model.vibrate && !fp_rtp_wave && !fp_rtp_pcm_enabled);
        fixture(0); model.by_dt = 1; model.haptic = 5; model.wave_bytes = FUXI_RTP_BYTES;
        model.hal = index ? 3 : 1; model.vmax = FUXI_RTP_TEST_VMAX_LIMIT + index; run();
        assert(model.vibrate == 1 && !model.vibrate_selftest && !fp_vibrate_transient);
        assert(fp_rtp_wave == model.wave && fp_rtp_pcm_enabled == index);
        assert(fp_rtp_vmax == (index ? 0u : FUXI_RTP_TEST_VMAX_LIMIT));
    }
    fixture(0); model.haptic = 6; run();
    assert(!model.vibrate && !model.maps && !model.legacy && !model.reads && !model.diagnostic);
    for (index = 1; index <= 58; index++) {
        fixture(1); model.fail_read = index; run();
        assert(model.invalid_rows && !model.valid && !model.stable && model.observations == 58);
    }
    for (index = 1; index <= 116; index++) {
        fixture(1); model.owner_change = index; run();
        assert(model.invalid_rows && !model.valid && !model.stable);
        fixture(1); model.apid_change = index; run();
        assert(model.invalid_rows && !model.valid && !model.stable);
    }
    fixture(1); model.fail_read = model.poison_failure = 1; run();
    assert(fp_poisoned[631] && model.reads < 58 && !model.valid && model.observations == 58);
    fixture(1); model.mutate_read = 30; run(); assert(model.valid && !model.stable);
    fixture(1); model.selector_race = 1; run(); assert(!model.valid && !model.stable);
    fixture(1); model.registers[0x70] = 5; run(); assert(!model.status_reads && !model.observed_fifo);
    fixture(1); model.registers[0x70] = 5; model.registers[0x71] = 0x80;
    model.registers[0x09] = 0x40; run();
    assert(model.reads == 62 && model.owner_checks == 124 && model.status_reads == 4);
    assert(model.observed_fifo == 2 && model.valid && model.stable && model.attempts == 62);
    fixture(1); model.registers[0x70] = 5; model.registers[0x71] = 0x80;
    model.registers[0x09] = 0x40; model.registers[0x0a] = 1; run();
    assert(model.contradictory == 2 && !model.valid && !model.stable);
    fixture(1); model.registers[0x70] = 5; model.registers[0x71] = 0x80;
    model.registers[0x09] = 0x40; model.selector_race = 1; run();
    assert(!model.valid && !model.stable && model.observed_fifo == 1);
    fixture(1); model.registers[0x101] = 3; model.registers[0x70] = 5; model.registers[0x71] = 0x80; run();
    assert(!model.identity && !model.status_reads && !model.observed_fifo && !model.tlra_lines);
    puts("PASS: actual PMIC mode1 survey/init snippets; default-off bootarg/DT, 58 read failures, "
         "116 owner and APID changes, selector races, quarantine, FIFO false-empty, "
         "62-attempt bound, zero PMIC writes, and unchanged mode2/mode3 dispatch");
    return 0;
}
'''

SYNTAX_PREFIX = r'''
#include <stdint.h>
static int fp_lock;
static uint8_t fp_poisoned[1024];
extern void lck_mtx_lock(int *lock);
extern void lck_mtx_unlock(int *lock);
extern int fp_find(unsigned sid, unsigned peripheral, unsigned *owner);
extern int fp_read(unsigned sid, unsigned address, uint8_t *value);
extern int printf(const char *format, ...);
'''


def source_snippets():
    text = PMIC.read_text(encoding='utf-8')
    start = text.index('static int fp_haptic_fifo_observe(')
    end = text.index('static void fp_haptic_diagnostic(', start)
    survey = text[start:end]
    assert not re.search(r'\b(?:fp_write32|fp_haptic_write|fp_haptic_prepare|fp_flash_write|fp_torch_start)\s*\(', survey)
    assert 'if (mode != 1) return;' in survey
    assert re.search(r'FuxiHapticIo io = \{[^;\n]*,\s*0\};', text)
    start = text.index('void fuxi_pmic_init(void)\n{')
    end = text.index('\n}\n', start) + 3
    initializer = text[start:end]
    assert initializer.count('fp_haptic_fifo_survey(mode);') == 1
    # 三種 arbiter 交易（observer 讀、flash 寫、haptic 寫）都要在與按鍵輪詢共用的匯流排鎖內完成，
    # 鎖序固定 fp_lock -> bus，且交易途中不得跳過解鎖。
    assert '#include "sweet_spmi_bus.h"' in text
    guarded_writes = 0
    for name in ('static int fp_read_registers(', 'static int fp_flash_write(', 'static int fp_haptic_transfer('):
        start = text.index(name)
        body = text[start:text.index('\n}', start)]
        assert body.count('sweet_spmi_bus_lock();') == 1 and body.count('sweet_spmi_bus_unlock();') == 1, name
        locked = body.index('sweet_spmi_bus_lock();')
        label = body.index('bus_done:\n    sweet_spmi_bus_unlock();\ndone:\n    lck_mtx_unlock(&fp_lock);')
        assert body.index('lck_mtx_lock(&fp_lock);') < locked < body.index('fp_write32(') < label, name
        assert 'goto done;' not in body[locked:label] and 'return' not in body[locked:label], name
        assert body.count('fp_write32(') == len(re.findall(r'fp_write32\(', body[locked:label])), name
        guarded_writes += body.count('fp_write32(')
    assert guarded_writes == 5 == len(re.findall(r'^\s+fp_write32\(', text, re.M))
    return survey, initializer


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cc', default=os.environ.get('CC'))
    parser.add_argument('--syntax-only', action='store_true')
    parser.add_argument('--target', default='arm64-apple-darwin')
    args = parser.parse_args()
    compiler = RUNNER.find_compiler(args.cc, args.syntax_only)
    microsoft = Path(compiler).name.lower() in ('cl', 'cl.exe')
    survey, initializer = source_snippets()
    with tempfile.TemporaryDirectory(prefix='fuxi-haptic-survey-') as temporary:
        directory = Path(temporary).resolve()
        source = directory / 'survey.c'
        if args.syntax_only:
            if microsoft:
                raise SystemExit('ARM64 syntax requires clang; MSVC supports host assertions.')
            source.write_text(SYNTAX_PREFIX + survey +
                              '\nvoid syntax_survey(unsigned mode) { fp_haptic_fifo_survey(mode); }\n', encoding='utf-8')
            command = [compiler, '-target', args.target, '-ffreestanding', '-mgeneral-regs-only',
                       '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsyntax-only', str(source)]
            subprocess.run(command, check=True, timeout=120)
            print(f'PASS: actual PMIC FIFO survey functions freestanding syntax; '
                  f'compiler={compiler}; target={args.target}; not a full XNU build', flush=True)
            print('SKIP: host compile/link, runtime assertions and ASan/UBSan (syntax-only mode)', flush=True)
            return
        source.write_text(PREFIX + survey + initializer + TESTS, encoding='utf-8')
        binary = directory / ('survey.exe' if os.name == 'nt' else 'survey')
        environment = RUNNER.msvc_environment(compiler) if microsoft else os.environ.copy()
        if microsoft:
            command = [compiler, '/nologo', '/std:c11', '/W4', '/WX', '/TC', '/D_CRT_SECURE_NO_WARNINGS',
                       '/I' + str(PMIC.parent), '/Fo' + str(directory / 'survey.obj'), '/Fe' + str(binary), str(source)]
        else:
            command = [compiler, '-std=c11', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
                       '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                       '-fno-omit-frame-pointer', '-I', str(PMIC.parent), str(source), '-o', str(binary)]
        subprocess.run(command, check=True, cwd=directory, env=environment, timeout=120)
        print(f'PASS: host compile/link; compiler={compiler}', flush=True)
        if microsoft:
            print('SKIP: ASan/UBSan (MSVC host mode does not enable sanitizer instrumentation)', flush=True)
        subprocess.run([str(binary)], check=True, cwd=directory, env=environment, timeout=120)
        print('PASS: host runtime assertions' +
              ('; ASan/UBSan instrumentation enabled' if not microsoft else ''), flush=True)


if __name__ == '__main__':
    main()
