import argparse
import json
import os
from pathlib import Path
import runpy
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'xnu/fuxi-wlan'

HOST = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "fuxi_wlan_arena.h"
#include "fuxi_wlan_ath12k.h"
static unsigned checks;
#define CHECK(test) do { checks++; if (!(test)) { fprintf(stderr,"line %d: %s\n",__LINE__,#test); exit(1); } } while (0)
static uint32_t registers[1024], last_write;
static unsigned reads, writes, fw_faults, wla_failed, wla_express = 0x70;
#define WL_EPCFG UINT64_C(0x60100000)
static uint32_t wl_prd(uint64_t address) {
    CHECK(address >= WL_EPCFG && address - WL_EPCFG < sizeof(registers) && !(address & 3));
    reads++; return registers[(address - WL_EPCFG) / 4];
}
static void wl_pwr(uint64_t address, uint32_t value) {
    CHECK(address >= WL_EPCFG && address - WL_EPCFG < sizeof(registers) && !(address & 3));
    writes++; last_write = value; registers[(address - WL_EPCFG) / 4] = value;
}
@CONFIG@
static struct fwa_arena wla_pool;
static struct fwa_block *wla_blocks;
static struct { uintptr_t va; } wl_ath_arena;
static struct { void *memory; size_t bytes; } wla_firmware[4];
static unsigned normal_frees;
@DMA_FREE@
static void wla_free(void *memory) {
    if (memory) { normal_frees++; free(memory); }
}
@RELEASE@
static void verify(const struct fwa_arena *arena) {
    uint32_t offset = 0;
    CHECK(arena->count <= arena->capacity);
    for (uint32_t index = 0; index < arena->count; index++) {
        const struct fwa_block *block = &arena->blocks[index];
        CHECK(block->offset == offset && block->size && block->requested <= block->size);
        CHECK(block->size <= arena->bytes - offset);
        if (index) CHECK(block->requested || arena->blocks[index - 1].requested);
        offset += block->size;
    }
    CHECK(offset == arena->bytes);
}
static void config_tests(void) {
    unsigned offset = 0;
    CHECK(!fwa_config_offset(0x80000008u, 2, 0x70, &offset) && offset == 0x78);
    CHECK(fwa_config_offset(0x80000008u, 2, 0, &offset));
    CHECK(fwa_config_offset(0x80000fffu, 4, 0x70, &offset));
    CHECK(fwa_config_offset(0xffffffffu, 4, 0x70, &offset));
    CHECK(fwa_config_offset(0x1000, 4, 0x70, &offset));
    CHECK(fwa_config_offset(0xfff, 2, 0x70, &offset));
    CHECK(fwa_config_offset(0xfff, 3, 0x70, &offset));
    CHECK(!fwa_config_offset(0xfff, 1, 0x70, &offset) && offset == 0xfff);
    uint32_t value;
    registers[1] = 0xffff0406;
    CHECK(!wla_cfg_write(4, 2, 0x402) && last_write == 0x402);
    registers[1] = 0xffff0406;
    CHECK(!wla_cfg_write(5, 1, 0x01) && last_write == 0x106);
    registers[0x78 / 4] = 0xffffabcd;
    CHECK(!wla_cfg_write((int)0x80000008u, 2, 0x1234) && last_write == 0x1234);
    registers[0x80 / 4] = 0xffffabcd;
    CHECK(!wla_cfg_write((int)0x80000010u, 1, 0x12) && last_write == 0xab12);
    registers[0x90 / 4] = 0x87654321;
    CHECK(!wla_cfg_write(0x91, 1, 0xaa) && last_write == 0x8765aa21);
    CHECK(!wla_cfg_read(0x90, 4, &value) && value == 0x8765aa21);
    CHECK(!wla_cfg_read(0x91, 1, &value) && value == 0xaa);
    unsigned before = writes;
    CHECK(wla_cfg_write(0xfff, 2, 0) && writes == before);
    fw_faults = 1;
    CHECK(wla_cfg_write(0x90, 4, 0) && writes == before);
    fw_faults = 0; wla_failed = 1; before = reads;
    CHECK(wla_cfg_read(0x90, 4, &value) && reads == before);
    wla_failed = 0;
}
int main(void) {
    const uint32_t total = 32u << 20;
    uint8_t *allocation = malloc(total + 64);
    CHECK(allocation != NULL);
    uint8_t *memory = (uint8_t *)(((uintptr_t)allocation + 63) & ~(uintptr_t)63);
    struct fwa_block blocks[4096];
    struct fwa_arena arena;
    CHECK(fwa_init(&arena, memory, 0xfffff000, total, blocks, 4096));
    CHECK(fwa_init(&arena, memory, 0xa0000001, total, blocks, 4096));
    CHECK(fwa_init(&arena, memory, 0xa0000000, total, blocks, 1));
    CHECK(!fwa_init(&arena, memory, 0xa0000000, total, blocks, 4096));
    CHECK(!fwa_map(&arena, memory, 4096));
    uint64_t bus = 99;
    CHECK(!fwa_alloc(&arena, 0, &bus) && !bus);
    CHECK(!fwa_alloc(&arena, (size_t)-1, &bus) && !bus);
    void *first = fwa_alloc(&arena, 7077888, &bus);
    CHECK(first && !(bus & 0x7fffff));
    CHECK(fwa_map(&arena, first, 7077888) == bus);
    CHECK(!fwa_map(&arena, first, 7077889));
    CHECK(fwa_free(&arena, 7077887, first, bus));
    CHECK(fwa_free(&arena, 7077888, (uint8_t *)first + 1, bus));
    CHECK(fwa_free(&arena, 7077888, first, bus + 1));
    CHECK(!fwa_free(&arena, 7077888, first, bus));
    CHECK(fwa_free(&arena, 7077888, first, bus));
    CHECK(!fwa_map(&arena, first, 1));
    first = fwa_alloc(&arena, 4097, &bus);
    CHECK(first && bus);
    CHECK(fwa_kfree(NULL, first));
    CHECK(fwa_kfree(&arena, NULL));
    CHECK(fwa_kfree(&arena, memory));
    CHECK(fwa_kfree(&arena, (uint8_t *)first + 1));
    CHECK(fwa_free(&arena, 0, first, bus));
    CHECK(fwa_free(&arena, 4097, first, 0));
    CHECK(!fwa_kfree(&arena, first));
    CHECK(!fwa_map(&arena, first, 1));
    CHECK(fwa_kfree(&arena, first));
    void *recycled = fwa_alloc(&arena, 4097, &bus);
    CHECK(recycled == first && fwa_map(&arena, recycled, 4097) == bus);
    CHECK(!fwa_free(&arena, 4097, recycled, bus));
    verify(&arena); CHECK(arena.count == 2);
    struct slot { void *memory; uint64_t bus; uint32_t bytes; } slots[256] = {0};
    uint32_t random = 0x19031c4d;
    for (unsigned round = 0; round < 30000; round++) {
        random = random * 1664525u + 1013904223u;
        unsigned index = (random >> 16) & 255;
        struct slot *slot = &slots[index];
        if (slot->memory) {
            CHECK(fwa_map(&arena, slot->memory, slot->bytes) == slot->bus);
                if (round & 1) CHECK(!fwa_kfree(&arena, slot->memory));
                else CHECK(!fwa_free(&arena, slot->bytes, slot->memory, slot->bus));
            CHECK(!fwa_map(&arena, slot->memory, 1));
            slot->memory = NULL;
        } else {
            uint32_t sizes[] = {1, 63, 64, 65, 128, 256, 512, 1024, 2048, 4095, 4096, 4097, 65535, 524289, 1048577};
            slot->bytes = sizes[random % (sizeof(sizes) / sizeof(sizes[0]))];
            slot->memory = fwa_alloc(&arena, slot->bytes, &slot->bus);
            if (slot->memory) {
                CHECK(fwa_map(&arena, slot->memory, slot->bytes) == slot->bus);
                CHECK(fwa_map(&arena, (uint8_t *)slot->memory + slot->bytes - 1, 1) == slot->bus + slot->bytes - 1);
                CHECK(!fwa_map(&arena, (uint8_t *)slot->memory + slot->bytes - 1, 2));
                if (slot->bytes <= 4096) {
                    uint32_t rounded = (slot->bytes + 63u) & ~63u, align = 64u;
                    while (align < rounded && align < 4096u) { align <<= 1; }
                    CHECK((slot->bus & (align - 1u)) == 0);
                    CHECK((slot->bus >> 12) == ((slot->bus + slot->bytes - 1u) >> 12));
                }
                const uint8_t *bytes = slot->memory;
                for (unsigned at = 0; at < slot->bytes; at++) {
                    if (bytes[at]) { CHECK(0); }
                }
                memset(slot->memory, 0xa5, slot->bytes);
            } else CHECK(!slot->bus);
        }
        if (!(round % 97)) verify(&arena);
    }
    for (unsigned index = 0; index < 256; index++) {
        if (slots[index].memory) CHECK(!fwa_free(&arena, slots[index].bytes, slots[index].memory, slots[index].bus));
    }
    verify(&arena); CHECK(arena.count == 2);
    /* Regression: place free space at offset 0xfc0 within a 4 KiB page and ensure sub-page
     * DMA pool classes (256/512/1024/2048 B) never straddle a 4 KiB boundary and stay 128 B aligned. */
    void *fillers[63];
    uint64_t filler_bus[63];
    for (unsigned index = 0; index < 63; index++) {
        fillers[index] = fwa_alloc(&arena, 64, &filler_bus[index]);
        CHECK(fillers[index] != NULL);
    }
    CHECK((filler_bus[62] & 4095u) == 0xf80u);
    uint32_t pool_classes[] = {256, 512, 1024, 2048};
    void *pool_ptrs[4];
    uint64_t pool_bus[4];
    for (unsigned index = 0; index < 4; index++) {
        uint32_t sz = pool_classes[index];
        pool_ptrs[index] = fwa_alloc(&arena, sz, &pool_bus[index]);
        CHECK(pool_ptrs[index] != NULL);
        CHECK((pool_bus[index] & (sz - 1u)) == 0);
        CHECK((pool_bus[index] & 127u) == 0);
        CHECK((pool_bus[index] >> 12) == ((pool_bus[index] + sz - 1u) >> 12));
    }
    for (unsigned index = 0; index < 4; index++) {
        CHECK(!fwa_free(&arena, pool_classes[index], pool_ptrs[index], pool_bus[index]));
    }
    for (unsigned index = 0; index < 63; index++) {
        CHECK(!fwa_free(&arena, 64, fillers[index], filler_bus[index]));
    }
    verify(&arena); CHECK(arena.count == 2);
    CHECK(!fwa_init(&arena, memory, 0xa0000000, 8192, blocks, 2));
    CHECK(!fwa_alloc(&arena, 64, &bus));
    CHECK(fwa_alloc(&arena, 4096, &bus) == memory + 4096);
    CHECK(!fwa_free(&arena, 4096, memory + 4096, bus));
    config_tests();
    wla_pool = arena;
    void *callback_memory = fwa_alloc(&wla_pool, 4096, &bus);
    CHECK(callback_memory);
    wla_dma_free(0, callback_memory, 0);
    CHECK(!wla_failed && !fwa_map(&wla_pool, callback_memory, 1));
    callback_memory = fwa_alloc(&wla_pool, 4096, &bus);
    CHECK(callback_memory);
    wla_dma_free(0, callback_memory, bus);
    CHECK(wla_failed && fwa_map(&wla_pool, callback_memory, 4096) == bus);
    wla_failed = 0;
    wla_dma_free(4096, callback_memory, bus);
    CHECK(!wla_failed && !fwa_map(&wla_pool, callback_memory, 1));
    wla_blocks = malloc(sizeof(*wla_blocks) * 16);
    CHECK(wla_blocks);
    for (unsigned index = 0; index < 4; index++) {
        wla_firmware[index].memory = malloc(17);
        wla_firmware[index].bytes = 17;
        CHECK(wla_firmware[index].memory);
    }
    wl_ath_arena.va = (uintptr_t)memory;
    wl_ath_release();
    CHECK(!normal_frees && wla_blocks && wla_pool.memory == memory);
    wl_ath_arena.va = 0;
    wl_ath_release();
    CHECK(normal_frees == 5 && !wla_blocks && !wla_pool.memory && !wla_pool.count);
    for (unsigned index = 0; index < 4; index++) {
        CHECK(!wla_firmware[index].memory && !wla_firmware[index].bytes);
    }
    wl_ath_release();
    CHECK(normal_frees == 5);
    free(allocation);
    printf("PASS ath12k board contracts: %u checks, 30000 allocation transitions\n", checks);
    return 0;
}
'''

ABI_FIELDS = {
    'lx_platform': ('now_ms', 'sleep_ms', 'poll', 'log', 'halt', 'alloc', 'free',
                    'dma_alloc', 'dma_free', 'dma_map', 'dma_sync', 'fw_get',
                    'now_ns', 'boottime_ns', 'busy_delay_us', 'preempt_disable', 'preempt_enable'),
    'nw_board': ('bar', 'bar_len', 'device_id', 'msi_cap', 'irq_base', 'cfg_read',
                 'cfg_write', 'msi_target', 'map_phys', 'diagnostics'),
    'nw_wifi_stats': ('stage', 'scans', 'frames', 'beacons', 'bss_seen', 'last_err'),
    'nw_bss_info': ('bssid', 'ssid_len', 'ssid', 'freq_mhz', 'signal_dbm', 'caps',
                    'beacon_int', 'age_ms', 'ie_len', 'ie'),
    'nw_control_transport': ('context', 'send', 'wake'),
    'nw_wifi_events': ('scan_done', 'connect_done', 'link_lost', 'rx', 'rx_flush',
                       'rx_packet', 'link_update'),
}
ABI_SYMBOLS = ('nw_wifi_start', 'nw_wifi_step', 'nw_wifi_stop', 'nw_wifi_poll',
               'nw_wifi_irq_mode', 'nw_wifi_scan', 'nw_wifi_bss_count', 'nw_wifi_bss_get',
               'nw_wifi_set_platform', 'nw_wifi_control_transport_set',
               'nw_wifi_control_attach', 'nw_wifi_control_receive', 'nw_wifi_power',
               'nw_wifi_set_events', 'nw_wifi_tx')


def abi_source(reference):
    symbols = tuple(ABI_FIELDS) + ABI_SYMBOLS
    lines = ['#include <stddef.h>', '#include <stdio.h>']
    lines += [f'#define {name} fuxi_{name}' for name in symbols]
    lines += ['#include "fuxi_wlan_ath12k.h"']
    lines += [f'#undef {name}' for name in symbols]
    lines += ['#include ' + json.dumps(reference.resolve().as_posix())]
    lines += ['#include ' + json.dumps((reference.parent / 'nw_control_endpoint.h').resolve().as_posix())]
    for name, fields in ABI_FIELDS.items():
        lines += [
            f'_Static_assert(sizeof(struct {name}) == sizeof(struct fuxi_{name}), "{name} size");',
            f'_Static_assert(_Alignof(struct {name}) == _Alignof(struct fuxi_{name}), "{name} alignment");',
        ]
        for field in fields:
            lines.append(f'_Static_assert(offsetof(struct {name}, {field}) == '
                         f'offsetof(struct fuxi_{name}, {field}), "{name}.{field} offset");')
    for name in ('lx_platform', 'nw_board', 'nw_control_transport'):
        lines += [f'static struct {name} upstream_{name};',
                  f'static struct fuxi_{name} local_{name};']
    lines += ['int main(void) {', 'unsigned offset;']
    for name in ('lx_platform', 'nw_board', 'nw_control_transport'):
        lines += [f'local_{name}.{field} = upstream_{name}.{field};' for field in ABI_FIELDS[name]]
    lines += ['if (fwa_config_offset(0, 4, 0, &offset)) return 1;',
              'puts("PASS pinned module ABI: six structure sizes/alignments, all field offsets and platform/control callbacks");',
              'return 0;', '}']
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--abi-header', type=Path)
    arguments = parser.parse_args()
    tools = runpy.run_path(str(ROOT / 'tools/test-fuxi-audio-prep.py'))
    compiler, environment = tools['compiler_environment']()
    msvc = Path(compiler[0].strip('"')).name.lower() in ('cl', 'cl.exe')
    adapter = (SOURCE / 'fuxi_wlan_ath12k.inc').read_text()
    config = 'static int\nwla_cfg_read(' + adapter.split('static int\nwla_cfg_read(', 1)[1].split(
        'static int\nwla_msi_target(', 1)[0]
    config = config.replace('__asm__ volatile("dsb sy" ::: "memory");', '')
    release = 'static void\nwl_ath_release(' + adapter.split('static void\nwl_ath_release(', 1)[1].split(
        'static void *\nwla_dma_alloc(', 1)[0]
    dma_free = 'static void\nwla_dma_free(' + adapter.split('static void\nwla_dma_free(', 1)[1].split(
        'static uint64_t\nwla_dma_map(', 1)[0]
    unlinked = (
        '#include <stdbool.h>\n#include <stdio.h>\n#include "fuxi_wlan_ath12k.h"\n' +
        'static void\nwla_poll(' + adapter.split('static void\nwla_poll(', 1)[1].split(
            'static void\nwla_log(', 1)[0] +
        'static bool\nwla_available(' + adapter.split('static bool\nwla_available(', 1)[1] +
        'int main(void) {\nunsigned offset;\n'
        'if (wla_available() || wl_ath_run() != 1 || fwa_config_offset(0, 4, 0, &offset)) return 1;\n'
        'wla_poll();\nputs("PASS unlinked ath12k adapter refuses start without module imports");\nreturn 0;\n}\n'
    )
    with tempfile.TemporaryDirectory(prefix='ath12k-contract-') as directory:
        temporary = Path(directory)
        source = temporary / 'contracts.c'
        source.write_text(HOST.replace('@CONFIG@', config).replace('@RELEASE@', release).replace('@DMA_FREE@', dma_free))
        output = temporary / ('contracts.exe' if os.name == 'nt' else 'contracts')
        if msvc:
            flags = ['/nologo', '/TC', '/std:c11', '/W4', '/WX', '/Od', '/Zi', '/MD']
            if arguments.sanitize:
                flags.append('/fsanitize=address')
            command = compiler + flags + [f'/I{SOURCE}', str(source), f'/Fe:{output}']
        else:
            flags = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-O1', '-g']
            if arguments.sanitize:
                flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
            command = compiler + flags + ['-I', str(SOURCE), str(source), '-o', str(output)]
        subprocess.run(command, cwd=directory, env=environment, check=True, timeout=120)
        subprocess.run([str(output)], cwd=directory, env=environment, check=True, timeout=120)
        source.write_text(unlinked, encoding='utf-8', newline='\n')
        subprocess.run(command, cwd=directory, env=environment, check=True, timeout=120)
        subprocess.run([str(output)], cwd=directory, env=environment, check=True, timeout=120)
        source.write_text((ROOT / 'tools/test-fuxi-wlan-control.c').read_text(), encoding='utf-8', newline='\n')
        subprocess.run(command, cwd=directory, env=environment, check=True, timeout=120)
        subprocess.run([str(output)], cwd=directory, env=environment, check=True, timeout=120)
        if arguments.abi_header:
            source.write_text(abi_source(arguments.abi_header), encoding='utf-8', newline='\n')
            subprocess.run(command, cwd=directory, env=environment, check=True, timeout=120)
            subprocess.run([str(output)], cwd=directory, env=environment, check=True, timeout=120)
            module_root = arguments.abi_header.resolve().parent.parent
            source.write_text(
                '#define FWC_REFERENCE_CODEC 1\n#include "nw_control.h"\n' +
                '#include ' + json.dumps((module_root / 'src/nw_control_wire.c').as_posix()) + '\n' +
                (ROOT / 'tools/test-fuxi-wlan-control.c').read_text(),
                encoding='utf-8', newline='\n')
            include_flags = [f'/I{module_root / "include"}'] if msvc else ['-I', str(module_root / 'include')]
            subprocess.run(command + include_flags, cwd=directory, env=environment, check=True, timeout=120)
            subprocess.run([str(output)], cwd=directory, env=environment, check=True, timeout=120)


if __name__ == '__main__':
    main()
