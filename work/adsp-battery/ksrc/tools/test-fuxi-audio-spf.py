import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'xnu/fuxi-adsp'
PREP = ROOT / 'xnu/fuxi-audio-prep'
AUDIO = ROOT / 'xnu/fuxi-audio'

HOST = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ShengAdsp.h"
#include "fuxi_audio_prep_gpr.h"
#include "fuxi_audio_smmu.h"
typedef void *DTEntry;
#define kSuccess 0
#define NSEC_PER_MSEC 1000000u
static uint64_t now;
static unsigned argument, adt_value, adt_bytes, registrations, sends, callback_depth, sent_length;
static bool have_argument, have_adt, register_failure;
static int send_error;
static unsigned dma_surveys, dma_opens, dma_closes;
static bool have_dma;
static const uint64_t test_dma_address = 0x1a9250000ull;
uint32_t fuxi_amp_tone;
static uint32_t dma_buffer[0x10000 / sizeof(uint32_t)];
static ShengAdspClient callbacks;
static uint8_t sent[SHENG_ADSP_MAX_MESSAGE];
static void (*on_send)(const uint8_t *data, uint32_t length);
struct ShengAdspChannel { unsigned marker; };
static struct ShengAdspChannel channel;
static uint64_t mach_absolute_time(void) { return now; }
static void clock_interval_to_deadline(unsigned interval,unsigned scale,uint64_t *deadline) {
    *deadline=now+(uint64_t)interval*scale;
}
static int PE_parse_boot_argn(const char *name,void *value,unsigned bytes) {
    assert(!strcmp(name,"fuxiaudiospf") && bytes==4);
    memcpy(value,&argument,bytes); return have_argument;
}
static int SecureDTLookupEntry(void *root,const char *name,DTEntry *entry) {
    assert(!root && !strcmp(name,"/chosen")); *entry=&adt_value; return have_adt?0:1;
}
static int SecureDTGetProperty(DTEntry entry,const char *name,const void **property,unsigned *bytes) {
    assert(entry==&adt_value && !strcmp(name,"fuxi,audio-spf"));
    *property=&adt_value; *bytes=adt_bytes; return 0;
}
ShengAdspChannel *sheng_adsp_channel(const char *name,uint32_t size,uint32_t count,int initiator,
    const ShengAdspClient *client,void *context) {
    assert(!strcmp(name,"adsp_apps") && size==512 && count==20 && !initiator && !context);
    assert(!callback_depth); callbacks=*client; registrations++;
    return register_failure?NULL:&channel;
}
int sheng_adsp_send(ShengAdspChannel *target,const void *data,uint32_t length) {
    assert(target==&channel && length>=24 && length<=512 && !callback_depth);
    memcpy(sent,data,length); sent_length=length; sends++;
    if (!send_error && on_send) on_send(data, length);
    return send_error;
}
void fuxi_audio_dma_survey(void) {
    assert(!callback_depth);
    dma_surveys++;
}
bool fuxi_audio_dma_open(uint64_t *address)
{
    assert(!callback_depth && address);
    dma_opens++;
    if (!have_dma) {
        return false;
    }
    *address = test_dma_address;
    return true;
}
void *fuxi_audio_dma_buffer(void)
{
    return have_dma && dma_opens && !dma_closes ? dma_buffer : NULL;
}
void fuxi_audio_dma_close(void)
{
    assert(!callback_depth && have_dma && dma_opens == 1 && !dma_closes);
    dma_closes++;
}
#include "fuxi_audio_spf.c"
static void reset(void) {
    memset(&gSpf,0,sizeof(gSpf)); memset(&callbacks,0,sizeof(callbacks));
    memset(&gPrm,0,sizeof(gPrm));
    memset(&gGraph,0,sizeof(gGraph));
    memset(&gTone,0,sizeof(gTone));
    memset(dma_buffer,0,sizeof(dma_buffer));
    on_send=NULL;
    fuxi_amp_tone=FUXI_TONE_IDLE;
    now=0; argument=0; adt_value=0; adt_bytes=4;
    registrations=0; sends=0; callback_depth=0; sent_length=0;
    dma_surveys=0; dma_opens=0; dma_closes=0; have_dma=false;
    have_argument=false; have_adt=false; register_failure=false; send_error=0;
}
static void state(int up) {
    callback_depth++; callbacks.state(NULL,up); callback_depth--;
}
static void deliver(uint32_t opcode,uint32_t token,uint32_t status) {
    uint8_t packet[512]={0}, payload[8]={0}; size_t length=0;
    FuxiAudioPrepGprFields fields={3,2,1,FUXI_SPF_PORT,token,opcode};
    unsigned bytes=4;
    if(opcode==FUXI_AUDIO_PREP_BASIC_RESULT) {
        uint32_t command=FUXI_AUDIO_PREP_GET_SPF_STATE;
        memcpy(payload,&command,4); memcpy(payload+4,&status,4); bytes=8;
    } else memcpy(payload,&status,4);
    assert(!fuxi_audio_prep_gpr_encode(packet,sizeof(packet),&fields,payload,bytes,&length));
    packet[6]=1;
    callback_depth++; callbacks.receive(NULL,packet,(uint32_t)length); callback_depth--;
}
static void begin(void) {
    reset(); have_adt=true; adt_value=1; fuxi_audio_spf_start();
    assert(registrations==1 && !sends); state(1); fuxi_audio_spf_poll();
    assert(sends==1 && gSpf.attempted && !gSpf.done);
    FuxiAudioPrepGprView query;
    assert(!fuxi_audio_prep_gpr_decode(sent,sent_length,&query));
    assert(query.fields.opcode==FUXI_AUDIO_PREP_GET_SPF_STATE &&
           query.fields.src_port==FUXI_SPF_PORT && query.fields.token==FUXI_SPF_TOKEN &&
           query.payload_bytes==16);
    for(unsigned index=0;index<16;index++) assert(!query.payload[index]);
}
static void begin_prm(void)
{
    reset();
    have_adt = true;
    adt_value = 3;
    fuxi_audio_spf_start();
    state(1);
    fuxi_audio_spf_poll();
    assert(sends == 1);
    deliver(FUXI_AUDIO_PREP_SPF_STATE_REPLY, FUXI_SPF_TOKEN, 1);
    fuxi_audio_spf_poll();
    assert(gSpf.done && sends == 2 && gPrm.phase == FUXI_PRM_VOTE_REPLY);
    FuxiAudioPrepGprView packet;
    assert(!fuxi_audio_prep_gpr_decode(sent, sent_length, &packet));
    assert(packet.fields.src_port == 2 && packet.fields.dst_port == 2 &&
        packet.fields.token == FUXI_PRM_TOKEN && packet.fields.opcode == FUXI_AR_OP_PRM_REQUEST);
    uint32_t data[9];
    assert(packet.payload_bytes == sizeof(data));
    memcpy(data, packet.payload, sizeof(data));
    const uint32_t expected[] = {0, 0, 0, 20, 2, 0x08001032, 4, 0, 1};
    assert(!memcmp(data, expected, sizeof(data)));
}

static void deliver_result(uint32_t src, uint32_t dst, uint32_t opcode, uint32_t command,
    uint32_t token, uint32_t status)
{
    uint8_t packet[32];
    const uint32_t payload[] = {command, status};
    const FuxiAudioPrepGprFields fields = {3, 2, src, dst, token, opcode};
    size_t length;
    assert(!fuxi_audio_prep_gpr_encode(packet, sizeof(packet), &fields,
        (const uint8_t *)payload, sizeof(payload), &length));
    callback_depth++;
    callbacks.receive(NULL, packet, (uint32_t)length);
    callback_depth--;
}

static void deliver_prm(uint32_t opcode, uint32_t command, uint32_t token, uint32_t status)
{
    deliver_result(2, 2, opcode, command, token, status);
}

static void test_prm(void)
{
    begin_prm();
    deliver_prm(FUXI_AR_OP_PRM_REQUEST_DONE, FUXI_AR_OP_PRM_REQUEST, FUXI_PRM_TOKEN + 1, 0);
    deliver_prm(FUXI_AUDIO_PREP_BASIC_RESULT, FUXI_AR_OP_PRM_REQUEST, FUXI_PRM_TOKEN, 0);
    fuxi_audio_spf_poll();
    assert(!gPrm.reply_ready && sends == 2);
    deliver_prm(FUXI_AR_OP_PRM_REQUEST_DONE, FUXI_AR_OP_PRM_REQUEST, FUXI_PRM_TOKEN, 0);
    fuxi_audio_spf_poll();
    assert(sends == 3 && gPrm.phase == FUXI_PRM_RELEASE_REPLY);
    FuxiAudioPrepGprView packet;
    assert(!fuxi_audio_prep_gpr_decode(sent, sent_length, &packet));
    assert(packet.fields.opcode == FUXI_AR_OP_PRM_RELEASE && packet.fields.token == FUXI_PRM_TOKEN + 1);
    deliver_prm(FUXI_AR_OP_PRM_RELEASE_DONE, FUXI_AR_OP_PRM_RELEASE, FUXI_PRM_TOKEN + 1, 0);
    fuxi_audio_spf_poll();
    assert(sends == 3 && gPrm.phase == FUXI_PRM_IDLE);

    begin_prm();
    deliver_prm(FUXI_AUDIO_PREP_BASIC_RESULT, FUXI_AR_OP_PRM_REQUEST, FUXI_PRM_TOKEN, 8);
    fuxi_audio_spf_poll();
    assert(sends == 2 && gPrm.phase == FUXI_PRM_IDLE);

    begin_prm();
    state(0);
    now += FUXI_SPF_REPLY_MS * (uint64_t)NSEC_PER_MSEC;
    fuxi_audio_spf_poll();
    assert(sends == 2 && gPrm.phase == FUXI_PRM_QUEUE_RELEASE);
    state(1);
    fuxi_audio_spf_poll();
    assert(sends == 3 && gPrm.phase == FUXI_PRM_RELEASE_REPLY);
    now += FUXI_SPF_REPLY_MS * (uint64_t)NSEC_PER_MSEC;
    fuxi_audio_spf_poll();
    assert(sends == 3 && gPrm.phase == FUXI_PRM_IDLE);
}

static void begin_graph_mode(unsigned mode, bool dma)
{
    reset();
    have_adt = true;
    adt_value = mode;
    have_dma = dma;
    fuxi_audio_spf_start();
    state(1);
    fuxi_audio_spf_poll();
    assert(sends == 1 && sent_length == 40 && gGraph.step == FUXI_GRAPH_IDLE && !dma_surveys);
    deliver(FUXI_AUDIO_PREP_SPF_STATE_REPLY, FUXI_SPF_TOKEN, 1);
    fuxi_audio_spf_poll();
    assert(gSpf.done && sends == 1);
}

static void begin_graph(void)
{
    begin_graph_mode(4, false);
    assert(gGraph.step == FUXI_GRAPH_OPEN_SPEAKER && dma_surveys == 1 && !dma_opens);
}

static void check_close(uint32_t subgraph)
{
    FuxiAudioPrepGprView packet;
    assert(!fuxi_audio_prep_gpr_decode(sent, sent_length, &packet));
    const uint32_t close[] = {0, 0, 0, 24, 1, 0x08001005, 8, 0, 1, subgraph};
    assert(packet.fields.opcode == FUXI_AR_OP_GRAPH_CLOSE &&
        packet.payload_bytes == sizeof(close) && !memcmp(packet.payload, close, sizeof(close)));
}

static void graph_reply(uint32_t status)
{
    const uint32_t port = graph_steps[gGraph.step].port;
    deliver_result(port, port == FUXI_AR_PRM_IID ? port : FUXI_SPF_PORT, FUXI_AUDIO_PREP_BASIC_RESULT,
        graph_steps[gGraph.step].opcode, FUXI_GRAPH_TOKEN + gGraph.step, status);
    fuxi_audio_spf_poll();
}

static void test_graph(void)
{
    begin_graph();
    fuxi_audio_spf_poll();
    deliver_result(2, FUXI_SPF_PORT, FUXI_AUDIO_PREP_BASIC_RESULT, FUXI_AR_OP_GRAPH_OPEN,
        FUXI_GRAPH_TOKEN + FUXI_GRAPH_OPEN_SPEAKER, 0);
    deliver_result(1, FUXI_SPF_PORT, FUXI_AUDIO_PREP_BASIC_RESULT, FUXI_AR_OP_GRAPH_OPEN,
        FUXI_GRAPH_TOKEN + FUXI_GRAPH_OPEN_PLAYBACK, 0);
    deliver_result(1, FUXI_SPF_PORT, FUXI_AUDIO_PREP_BASIC_RESULT, FUXI_AR_OP_SET_CFG,
        FUXI_GRAPH_TOKEN + FUXI_GRAPH_OPEN_SPEAKER, 0);
    fuxi_audio_spf_poll();
    assert(sends == 2 && gGraph.step == FUXI_GRAPH_OPEN_SPEAKER && gGraph.waiting);
    const enum fuxi_graph_step steps[] = {
        FUXI_GRAPH_OPEN_SPEAKER, FUXI_GRAPH_CONFIG_SPEAKER, FUXI_GRAPH_OPEN_PLAYBACK,
        FUXI_GRAPH_CONFIG_PLAYBACK, FUXI_GRAPH_CLOSE_PLAYBACK, FUXI_GRAPH_CLOSE_SPEAKER,
    };
    for (unsigned index = 0; index < sizeof(steps) / sizeof(*steps); ++index) {
        const enum fuxi_graph_step step = steps[index];
        FuxiAudioPrepGprView packet;
        assert(gGraph.step == step && gGraph.waiting && sends == index + 2);
        assert(!fuxi_audio_prep_gpr_decode(sent, sent_length, &packet));
        assert(packet.fields.src_port == FUXI_SPF_PORT && packet.fields.dst_port == FUXI_AR_APM_IID &&
            packet.fields.token == FUXI_GRAPH_TOKEN + step && packet.fields.opcode == graph_steps[step].opcode);
        if (step == FUXI_GRAPH_CONFIG_SPEAKER) {
            const uint32_t intf[] = {0x6052, 0x0800101b, 28, 0, 0, 1, 0x00010001, 0xf, 0x00200004, 1, 1, 0};
            const uint32_t tail[] = {0x6052, 0x0800106a, 4, 0, 2, 0, 0x6052, 0x0800113c, 16, 0, 0, 0, 4, 0};
            assert(packet.payload_bytes == 176 && !memcmp(packet.payload + 16, intf, sizeof(intf)) &&
                !memcmp(packet.payload + 120, tail, sizeof(tail)));
        } else if (step == FUXI_GRAPH_CLOSE_PLAYBACK) {
            check_close(FUXI_AR_SG_PLAY);
        } else if (step == FUXI_GRAPH_CLOSE_SPEAKER) {
            check_close(FUXI_AR_SG_SPEAKER);
        }
        graph_reply(0);
    }
    assert(gGraph.step == FUXI_GRAPH_IDLE && !gGraph.failed && sends == 7 && dma_surveys == 1);

    begin_graph();
    fuxi_audio_spf_poll();
    graph_reply(0);
    graph_reply(0);
    assert(gGraph.step == FUXI_GRAPH_OPEN_PLAYBACK);
    state(0);
    fuxi_audio_spf_poll();
    assert(gGraph.step == FUXI_GRAPH_CLOSE_PLAYBACK && sends == 4);
    state(1);
    fuxi_audio_spf_poll();
    check_close(FUXI_AR_SG_PLAY);
    now += FUXI_SPF_REPLY_MS * (uint64_t)NSEC_PER_MSEC;
    fuxi_audio_spf_poll();
    check_close(FUXI_AR_SG_SPEAKER);
    graph_reply(0);
    assert(gGraph.step == FUXI_GRAPH_IDLE && gGraph.failed && sends == 6);

    begin_graph();
    fuxi_audio_spf_poll();
    graph_reply(0);
    graph_reply(8);
    check_close(FUXI_AR_SG_SPEAKER);
    graph_reply(0);
    assert(gGraph.step == FUXI_GRAPH_IDLE && gGraph.failed && sends == 4);

    begin_graph();
    send_error = -1;
    fuxi_audio_spf_poll();
    now += FUXI_GRAPH_QUEUE_MS * (uint64_t)NSEC_PER_MSEC;
    fuxi_audio_spf_poll();
    assert(gGraph.step == FUXI_GRAPH_IDLE && gGraph.failed && sends == 2);
}

static void deliver_graph_completion(const uint32_t *words, size_t bytes)
{
    uint8_t packet[64];
    size_t length;
    const uint32_t port = graph_steps[gGraph.step].port;
    const FuxiAudioPrepGprFields fields = {
        FUXI_AUDIO_PREP_DOMAIN_APPS, FUXI_AUDIO_PREP_DOMAIN_ADSP, port,
        port == FUXI_AR_PRM_IID ? port : FUXI_SPF_PORT, FUXI_GRAPH_TOKEN + gGraph.step,
        graph_steps[gGraph.step].completion,
    };
    assert(fields.opcode && !fuxi_audio_prep_gpr_encode(packet, sizeof(packet), &fields,
        (const uint8_t *)words, bytes, &length));
    callback_depth++;
    callbacks.receive(NULL, packet, (uint32_t)length);
    callback_depth--;
    fuxi_audio_spf_poll();
}

static void test_pcm(bool write_timeout)
{
    begin_graph_mode(5, true);
    assert(gGraph.step == FUXI_GRAPH_VOTE_MACRO && dma_opens == 1 && dma_surveys == 0);
    fuxi_audio_spf_poll();
    for (enum fuxi_graph_step step = FUXI_GRAPH_VOTE_MACRO; step < FUXI_GRAPH_COMPLETE; step++) {
        assert(gGraph.step == step && gGraph.waiting);
        FuxiAudioPrepGprView packet;
        assert(!fuxi_audio_prep_gpr_decode(sent, sent_length, &packet));
        const uint32_t port = graph_steps[step].port;
        assert(packet.fields.src_port == (port == FUXI_AR_PRM_IID ? port : FUXI_SPF_PORT));
        assert(packet.fields.dst_port == port && packet.fields.opcode == graph_steps[step].opcode &&
            packet.fields.token == FUXI_GRAPH_TOKEN + step);
        if (step == FUXI_GRAPH_MAP) {
            const uint32_t mapping[] = {0x10003, 0, 0xa9250000, 1, 65536, 0, 0, 0, 0};
            assert(packet.payload_bytes == sizeof(mapping) && !memcmp(packet.payload, mapping, sizeof(mapping)));
            graph_reply(0);
            assert(gGraph.step == step && !gGraph.map_handle);
            const uint32_t handle = 0x1024;
            deliver_graph_completion(&handle, sizeof(handle));
        } else if (step == FUXI_GRAPH_WRITE) {
            const uint32_t command[] = {0xa9250000, 1, 0x1024, 61440, 0, 0, 0, 0, 0, 0, 0};
            assert(packet.payload_bytes == sizeof(command) && !memcmp(packet.payload, command, sizeof(command)));
            const uint32_t wrong[] = {0xa9250004, 1, 0x1024, 0};
            deliver_graph_completion(wrong, sizeof(wrong));
            assert(gGraph.step == step && gGraph.waiting);
            if (write_timeout) {
                now += FUXI_SPF_REPLY_MS * (uint64_t)NSEC_PER_MSEC;
                fuxi_audio_spf_poll();
            } else {
                const uint32_t done[] = {0xa9250000, 1, 0x1024, 0};
                deliver_graph_completion(done, sizeof(done));
            }
        } else if (port == FUXI_AR_PRM_IID) {
            if (step == FUXI_GRAPH_CLOCK_ON) {
                const uint32_t clock[] = {0, 0, 0, 36, 2, 0x0800102c, 20, 0, 1, 0x202, 6144000, 4, 0};
                assert(packet.payload_bytes == sizeof(clock) && !memcmp(packet.payload, clock, sizeof(clock)));
            }
            const uint32_t reply[] = {packet.fields.opcode, 0};
            deliver_graph_completion(reply, sizeof(reply));
        } else {
            graph_reply(0);
        }
    }
    assert(gGraph.step == FUXI_GRAPH_IDLE && gGraph.failed == write_timeout && dma_closes == 1);
}

static uint32_t tone_expect_sent, tone_expect_frame, tone_peak_seen;
static bool tone_expect_sine;

/* 每送出一筆 WRITE 就核對位址輪替與 DMA 緩衝區內容（靜音或精確正弦與淡入淡出）。 */
static void tone_verify_send(const uint8_t *data, uint32_t length)
{
    if (gGraph.step != FUXI_GRAPH_WRITE) {
        return;
    }
    FuxiAudioPrepGprView packet;
    assert(!fuxi_audio_prep_gpr_decode(data, length, &packet));
    assert(packet.fields.src_port == FUXI_SPF_PORT && packet.fields.dst_port == FUXI_AR_IID_PLAY_SHMEM &&
        packet.fields.opcode == FUXI_AR_OP_WRITE && packet.fields.token == FUXI_GRAPH_TOKEN + FUXI_GRAPH_WRITE);
    uint32_t words[11];
    assert(packet.payload_bytes == sizeof(words));
    memcpy(words, packet.payload, sizeof(words));
    const uint32_t index = tone_expect_sent % FUXI_TONE_PERIODS;
    const uint64_t expected_addr = test_dma_address + (uint64_t)index * FUXI_TONE_PERIOD_BYTES;
    assert(words[0] == (uint32_t)expected_addr && words[1] == (uint32_t)(expected_addr >> 32) &&
        words[2] == 0x1024u && words[3] == FUXI_TONE_PERIOD_BYTES);
    for (unsigned i = 4; i < 11; i++) {
        assert(words[i] == 0);
    }
    const uint32_t *period = dma_buffer + index * (FUXI_TONE_PERIOD_BYTES / sizeof(uint32_t));
    for (uint32_t frame = 0; frame < FUXI_TONE_PERIOD_FRAMES; frame++) {
        int32_t want = 0;
        if (tone_expect_sine && tone_expect_frame < FUXI_TONE_FRAMES) {
            want = fuxi_amp_tone_sample(tone_expect_frame++);
            if (want == 214748365) {
                tone_peak_seen++;
            }
        }
        for (uint32_t slot = 0; slot < 4; slot++) {
            assert(period[4 * frame + slot] == (uint32_t)want);
        }
    }
    tone_expect_sent++;
}

static void begin_tone_mode(uint32_t amp_state, bool dma)
{
    reset();
    have_adt = true;
    adt_value = 5;
    have_dma = dma;
    __atomic_store_n(&fuxi_amp_tone, amp_state, __ATOMIC_RELEASE);
    fuxi_audio_spf_start();
    state(1);
    fuxi_audio_spf_poll();
    assert(sends == 1 && gGraph.step == FUXI_GRAPH_IDLE);
    deliver(FUXI_AUDIO_PREP_SPF_STATE_REPLY, FUXI_SPF_TOKEN, 1);
    fuxi_audio_spf_poll();
    assert(gSpf.done && sends == 1);
}

static void graph_step_reply_ok(void)
{
    FuxiAudioPrepGprView packet;
    assert(gGraph.waiting && !fuxi_audio_prep_gpr_decode(sent, sent_length, &packet));
    const uint32_t port = graph_steps[gGraph.step].port;
    if (gGraph.step == FUXI_GRAPH_MAP) {
        graph_reply(0);
        const uint32_t handle = 0x1024;
        deliver_graph_completion(&handle, sizeof(handle));
    } else if (port == FUXI_AR_PRM_IID) {
        const uint32_t reply[] = {packet.fields.opcode, 0};
        deliver_graph_completion(reply, sizeof(reply));
    } else {
        graph_reply(0);
    }
}

static void tone_deliver_done(uint64_t addr, uint32_t handle, uint32_t status)
{
    const uint32_t done[] = {(uint32_t)addr, (uint32_t)(addr >> 32), handle, status};
    deliver_graph_completion(done, sizeof(done));
}

static void tone_deliver_next(uint32_t status)
{
    const uint64_t addr = test_dma_address + (uint64_t)(gTone.done % FUXI_TONE_PERIODS) * FUXI_TONE_PERIOD_BYTES;
    tone_deliver_done(addr, 0x1024u, status);
}

static void tone_reach_write(void)
{
    begin_tone_mode(FUXI_TONE_PENDING, true);
    assert(gGraph.step == FUXI_GRAPH_VOTE_MACRO && gTone.phase == FUXI_TONE_PCM_GATE && sends == 1);
    now += 1000ull * NSEC_PER_MSEC;
    fuxi_audio_spf_poll();
    assert(gTone.phase == FUXI_TONE_PCM_GATE && sends == 1);
    assert(fuxi_amp_tone_move(FUXI_TONE_PENDING, FUXI_TONE_READY));
    fuxi_audio_spf_poll();
    assert(gTone.phase == FUXI_TONE_PCM_GRAPH && gGraph.step == FUXI_GRAPH_VOTE_MACRO && gGraph.waiting);
    while (gGraph.step < FUXI_GRAPH_WRITE) {
        graph_step_reply_ok();
    }
    assert(gGraph.step == FUXI_GRAPH_WRITE && gTone.phase == FUXI_TONE_PCM_PREROLL &&
        gTone.sent == FUXI_TONE_AHEAD && gTone.done == 0);
}

static void tone_finish_teardown(bool expect_failed)
{
    while (gGraph.step != FUXI_GRAPH_IDLE) {
        assert(gGraph.step >= FUXI_GRAPH_STOP_PLAYBACK && gGraph.step < FUXI_GRAPH_COMPLETE);
        graph_step_reply_ok();
    }
    assert(gGraph.failed == expect_failed && dma_closes == 1 && gTone.phase == FUXI_TONE_PCM_OFF);
}

static void test_tone(void)
{
    /* 1. 完整成功路徑：GATE → 建圖 → PREROLL（5 筆靜音）→ CLOCKED → ON → PLAY（50 筆正弦 + 5 筆尾端靜音）→ DRAIN → OFF → FINISH → 關圖。 */
    tone_expect_sent = 0;
    tone_expect_frame = 0;
    tone_peak_seen = 0;
    tone_expect_sine = false;
    begin_tone_mode(FUXI_TONE_PENDING, true);
    on_send = tone_verify_send;
    assert(fuxi_amp_tone_move(FUXI_TONE_PENDING, FUXI_TONE_READY));
    fuxi_audio_spf_poll();
    while (gGraph.step < FUXI_GRAPH_WRITE) {
        graph_step_reply_ok();
    }
    assert(gTone.phase == FUXI_TONE_PCM_PREROLL && gTone.sent == 3 && tone_expect_sent == 3 && !gTone.clocked);
    for (unsigned i = 0; i < 4; i++) {
        now += 20ull * NSEC_PER_MSEC;
        tone_deliver_next(0);
        assert(fuxi_amp_tone_get() == FUXI_TONE_READY && !gTone.clocked);
    }
    now += 20ull * NSEC_PER_MSEC;
    tone_deliver_next(0);
    assert(fuxi_amp_tone_get() == FUXI_TONE_CLOCKED && gTone.clocked && gTone.done == 5 && gTone.sent == 8);
    /* 等功放上電期間持續送靜音。 */
    for (unsigned i = 0; i < 2; i++) {
        now += 20ull * NSEC_PER_MSEC;
        tone_deliver_next(0);
        assert(gTone.phase == FUXI_TONE_PCM_PREROLL && gTone.frame == 0);
    }
    /* 功放進入 ON：下一次 poll 切換 PLAY，下一個送出的週期開始填正弦。 */
    assert(fuxi_amp_tone_move(FUXI_TONE_CLOCKED, FUXI_TONE_ON));
    tone_expect_sine = true;
    fuxi_audio_spf_poll();
    assert(gTone.phase == FUXI_TONE_PCM_PLAY && gTone.tone_start == gTone.sent && gTone.frame == 0);
    /* 模擬暫時送出失敗一次：gTone.frame 必須回捲，下一輪重試同一筆正弦。 */
    now += 20ull * NSEC_PER_MSEC;
    send_error = -1;
    tone_deliver_next(0);
    assert(gTone.frame == 0 && gTone.sent == gTone.tone_start);
    send_error = 0;
    fuxi_audio_spf_poll();
    assert(gTone.frame == FUXI_TONE_PERIOD_FRAMES && gTone.sent == gTone.tone_start + 1);
    /* 繼續播完剩下的 49 個正弦週期，再播 5 個尾端靜音週期。 */
    while (gTone.phase == FUXI_TONE_PCM_PLAY) {
        now += 20ull * NSEC_PER_MSEC;
        if (gTone.frame >= FUXI_TONE_FRAMES && gTone.done + 1 < gTone.tone_end + FUXI_TONE_ZEROS) {
            assert(fuxi_amp_tone_get() == FUXI_TONE_ON);
        }
        tone_deliver_next(0);
    }
    assert(gTone.phase == FUXI_TONE_PCM_POSTROLL && fuxi_amp_tone_get() == FUXI_TONE_DRAIN);
    assert(gTone.frame == FUXI_TONE_FRAMES && tone_expect_frame == FUXI_TONE_FRAMES && tone_peak_seen > 1000);
    assert(gTone.tone_end == gTone.tone_start + 50u && gTone.done == gTone.tone_end + FUXI_TONE_ZEROS);
    tone_expect_sine = false;
    /* POSTROLL 期間持續送靜音直到功放 OFF。 */
    for (unsigned i = 0; i < 2; i++) {
        now += 20ull * NSEC_PER_MSEC;
        tone_deliver_next(0);
        assert(gTone.phase == FUXI_TONE_PCM_POSTROLL);
    }
    assert(fuxi_amp_tone_move(FUXI_TONE_DRAIN, FUXI_TONE_OFF));
    fuxi_audio_spf_poll();
    assert(gTone.phase == FUXI_TONE_PCM_FINISH);
    const uint32_t final_sent = gTone.sent;
    assert(final_sent == gTone.done + FUXI_TONE_AHEAD);
    while (gTone.done + 1 < final_sent) {
        now += 20ull * NSEC_PER_MSEC;
        tone_deliver_next(0);
        assert(gTone.sent == final_sent && gGraph.step == FUXI_GRAPH_WRITE && gTone.phase == FUXI_TONE_PCM_FINISH);
    }
    now += 20ull * NSEC_PER_MSEC;
    tone_deliver_next(0);
    assert(gTone.sent == final_sent && gTone.done == final_sent && gTone.stray == 0 && gTone.status == 0);
    assert(gTone.phase == FUXI_TONE_PCM_OFF && gGraph.step == FUXI_GRAPH_STOP_PLAYBACK);
    on_send = NULL;
    tone_finish_teardown(false);

    /* 2. DMA 開啟失敗時釋放功放（PENDING → FAILED）。 */
    begin_tone_mode(FUXI_TONE_PENDING, false);
    assert(gGraph.step == FUXI_GRAPH_IDLE && fuxi_amp_tone_get() == FUXI_TONE_FAILED);

    /* 3. GATE 期間功放已失敗：降回 mode 5 靜音探測。 */
    begin_tone_mode(FUXI_TONE_PENDING, true);
    assert(fuxi_amp_tone_move(FUXI_TONE_PENDING, FUXI_TONE_FAILED));
    fuxi_audio_spf_poll();
    assert(gTone.phase == FUXI_TONE_PCM_OFF && gGraph.step == FUXI_GRAPH_VOTE_MACRO && gGraph.waiting);
    while (gGraph.step < FUXI_GRAPH_WRITE) {
        graph_step_reply_ok();
    }
    {
        const uint32_t done[] = {0xa9250000, 1, 0x1024, 0};
        deliver_graph_completion(done, sizeof(done));
    }
    tone_finish_teardown(false);

    /* 4. GATE 逾時 120 秒：放棄功放並降回靜音探測。 */
    begin_tone_mode(FUXI_TONE_PENDING, true);
    now += FUXI_TONE_GATE_MS * (uint64_t)NSEC_PER_MSEC;
    fuxi_audio_spf_poll();
    assert(fuxi_amp_tone_get() == FUXI_TONE_FAILED && gTone.phase == FUXI_TONE_PCM_OFF && gGraph.waiting);

    /* 5. 建圖期間失敗（OPEN_SPEAKER 錯誤）：釋放功放（READY → FAILED）。 */
    begin_tone_mode(FUXI_TONE_READY, true);
    fuxi_audio_spf_poll();
    while (gGraph.step < FUXI_GRAPH_OPEN_SPEAKER) {
        graph_step_reply_ok();
    }
    graph_reply(8);
    assert(fuxi_amp_tone_get() == FUXI_TONE_FAILED && gTone.phase == FUXI_TONE_PCM_OFF);
    tone_finish_teardown(true);

    /* 6. PREROLL 還沒播完 5 筆靜音功放就放棄：第 5 筆完成時 CAS READY→CLOCKED 失敗並收尾。 */
    tone_reach_write();
    assert(fuxi_amp_tone_move(FUXI_TONE_READY, FUXI_TONE_FAILED));
    for (unsigned i = 0; i < 5; i++) {
        now += 20ull * NSEC_PER_MSEC;
        tone_deliver_next(0);
    }
    assert(gTone.phase == FUXI_TONE_PCM_FINISH && gTone.error && !gTone.clocked);
    while (gTone.done < gTone.sent) {
        tone_deliver_next(0);
    }
    tone_finish_teardown(true);

    /* 7. PREROLL 送出 CLOCKED 後功放上電失敗或逾時。 */
    tone_reach_write();
    for (unsigned i = 0; i < 5; i++) {
        now += 20ull * NSEC_PER_MSEC;
        tone_deliver_next(0);
    }
    assert(fuxi_amp_tone_get() == FUXI_TONE_CLOCKED);
    assert(fuxi_amp_tone_move(FUXI_TONE_CLOCKED, FUXI_TONE_FAILED));
    fuxi_audio_spf_poll();
    assert(gTone.phase == FUXI_TONE_PCM_FINISH && gTone.error);
    while (gTone.done < gTone.sent) {
        tone_deliver_next(0);
    }
    tone_finish_teardown(true);

    tone_reach_write();
    for (unsigned i = 0; i < 5; i++) {
        now += 20ull * NSEC_PER_MSEC;
        tone_deliver_next(0);
    }
    for (unsigned i = 0; i < 160 && gTone.phase == FUXI_TONE_PCM_PREROLL; i++) {
        now += 20ull * NSEC_PER_MSEC;
        tone_deliver_next(0);
    }
    assert(gTone.phase == FUXI_TONE_PCM_FINISH && gTone.error && fuxi_amp_tone_get() == FUXI_TONE_FAILED);
    while (gTone.done < gTone.sent) {
        tone_deliver_next(0);
    }
    tone_finish_teardown(true);

    /* 8. PLAY 期間收到亂序／錯 handle 的 WRITE_DONE（計入 stray）與非零狀態錯誤。 */
    tone_reach_write();
    for (unsigned i = 0; i < 5; i++) {
        now += 20ull * NSEC_PER_MSEC;
        tone_deliver_next(0);
    }
    assert(fuxi_amp_tone_move(FUXI_TONE_CLOCKED, FUXI_TONE_ON));
    fuxi_audio_spf_poll();
    assert(gTone.phase == FUXI_TONE_PCM_PLAY);
    tone_deliver_done(test_dma_address + ((gTone.done + 1) % FUXI_TONE_PERIODS) * FUXI_TONE_PERIOD_BYTES, 0x1024u, 0);
    tone_deliver_done(test_dma_address + (gTone.done % FUXI_TONE_PERIODS) * FUXI_TONE_PERIOD_BYTES, 0x9999u, 0);
    assert(gTone.stray == 2 && gTone.done == 5 && gTone.phase == FUXI_TONE_PCM_PLAY);
    tone_deliver_next(5);
    assert(gTone.error && gTone.status == 5 && gTone.phase == FUXI_TONE_PCM_FINISH &&
        fuxi_amp_tone_get() == FUXI_TONE_FAILED);
    while (gTone.done < gTone.sent) {
        tone_deliver_next(0);
    }
    tone_finish_teardown(true);

    /* 9. PLAY 期間 BASIC_RESULT 回非零錯誤，以及功放中途離開 ON。 */
    tone_reach_write();
    for (unsigned i = 0; i < 5; i++) {
        now += 20ull * NSEC_PER_MSEC;
        tone_deliver_next(0);
    }
    assert(fuxi_amp_tone_move(FUXI_TONE_CLOCKED, FUXI_TONE_ON));
    fuxi_audio_spf_poll();
    deliver_result(FUXI_AR_IID_PLAY_SHMEM, FUXI_SPF_PORT, FUXI_AUDIO_PREP_BASIC_RESULT, FUXI_AR_OP_WRITE,
        FUXI_GRAPH_TOKEN + FUXI_GRAPH_WRITE, 9);
    fuxi_audio_spf_poll();
    assert(gTone.error && gTone.status == 9 && gTone.phase == FUXI_TONE_PCM_FINISH &&
        fuxi_amp_tone_get() == FUXI_TONE_FAILED);
    while (gTone.done < gTone.sent) {
        tone_deliver_next(0);
    }
    tone_finish_teardown(true);

    tone_reach_write();
    for (unsigned i = 0; i < 5; i++) {
        now += 20ull * NSEC_PER_MSEC;
        tone_deliver_next(0);
    }
    assert(fuxi_amp_tone_move(FUXI_TONE_CLOCKED, FUXI_TONE_ON));
    fuxi_audio_spf_poll();
    assert(fuxi_amp_tone_move(FUXI_TONE_ON, FUXI_TONE_FAILED));
    fuxi_audio_spf_poll();
    assert(gTone.error && gTone.phase == FUXI_TONE_PCM_FINISH);
    while (gTone.done < gTone.sent) {
        tone_deliver_next(0);
    }
    tone_finish_teardown(true);

    /* 10. PLAY 期間寫入逾時與 GLINK 斷線。 */
    tone_reach_write();
    for (unsigned i = 0; i < 5; i++) {
        now += 20ull * NSEC_PER_MSEC;
        tone_deliver_next(0);
    }
    assert(fuxi_amp_tone_move(FUXI_TONE_CLOCKED, FUXI_TONE_ON));
    fuxi_audio_spf_poll();
    now += FUXI_SPF_REPLY_MS * (uint64_t)NSEC_PER_MSEC;
    fuxi_audio_spf_poll();
    assert(gTone.error && gTone.phase == FUXI_TONE_PCM_FINISH && fuxi_amp_tone_get() == FUXI_TONE_FAILED);
    now += FUXI_SPF_REPLY_MS * (uint64_t)NSEC_PER_MSEC;
    fuxi_audio_spf_poll();
    tone_finish_teardown(true);

    tone_reach_write();
    for (unsigned i = 0; i < 5; i++) {
        now += 20ull * NSEC_PER_MSEC;
        tone_deliver_next(0);
    }
    assert(fuxi_amp_tone_move(FUXI_TONE_CLOCKED, FUXI_TONE_ON));
    fuxi_audio_spf_poll();
    state(0);
    fuxi_audio_spf_poll();
    assert(gTone.phase == FUXI_TONE_PCM_OFF && fuxi_amp_tone_get() == FUXI_TONE_FAILED &&
        gGraph.step == FUXI_GRAPH_STOP_PLAYBACK);
    state(1);
    fuxi_audio_spf_poll();
    tone_finish_teardown(true);

    /* 11. POSTROLL 期間功放下電失敗。 */
    tone_reach_write();
    for (unsigned i = 0; i < 5; i++) {
        now += 20ull * NSEC_PER_MSEC;
        tone_deliver_next(0);
    }
    assert(fuxi_amp_tone_move(FUXI_TONE_CLOCKED, FUXI_TONE_ON));
    fuxi_audio_spf_poll();
    while (gTone.phase == FUXI_TONE_PCM_PLAY) {
        now += 20ull * NSEC_PER_MSEC;
        tone_deliver_next(0);
    }
    assert(gTone.phase == FUXI_TONE_PCM_POSTROLL && fuxi_amp_tone_get() == FUXI_TONE_DRAIN);
    assert(fuxi_amp_tone_move(FUXI_TONE_DRAIN, FUXI_TONE_FAILED));
    fuxi_audio_spf_poll();
    assert(gTone.error && gTone.phase == FUXI_TONE_PCM_FINISH);
    while (gTone.done < gTone.sent) {
        tone_deliver_next(0);
    }
    tone_finish_teardown(true);

    /* 12. POSTROLL 期間功放下電逾時：PCM 逾時收尾，DRAIN 仍留給功放端自行下電。 */
    tone_reach_write();
    for (unsigned i = 0; i < 5; i++) {
        now += 20ull * NSEC_PER_MSEC;
        tone_deliver_next(0);
    }
    assert(fuxi_amp_tone_move(FUXI_TONE_CLOCKED, FUXI_TONE_ON));
    fuxi_audio_spf_poll();
    while (gTone.phase == FUXI_TONE_PCM_PLAY) {
        now += 20ull * NSEC_PER_MSEC;
        tone_deliver_next(0);
    }
    assert(gTone.phase == FUXI_TONE_PCM_POSTROLL && fuxi_amp_tone_get() == FUXI_TONE_DRAIN);
    for (unsigned i = 0; i < 160 && gTone.phase == FUXI_TONE_PCM_POSTROLL; i++) {
        now += 20ull * NSEC_PER_MSEC;
        tone_deliver_next(0);
    }
    assert(gTone.error && gTone.phase == FUXI_TONE_PCM_FINISH && fuxi_amp_tone_get() == FUXI_TONE_DRAIN);
    while (gTone.done < gTone.sent) {
        tone_deliver_next(0);
    }
    tone_finish_teardown(true);
}

static uint32_t smmu_regs[FUXI_APPS_SMMU_BYTES / 4];
static uint32_t smmu_read_fault;
static unsigned smmu_writes, smmu_fail_write;

static bool smmu_read(void *context, uint32_t offset, uint32_t *value)
{
    assert(!context && !(offset & 3) && offset < FUXI_APPS_SMMU_BYTES);
    if (offset == smmu_read_fault) {
        return false;
    }
    *value = smmu_regs[offset / 4];
    return true;
}

static bool smmu_write(void *context, uint32_t offset, uint32_t value)
{
    assert(!context && (offset == 0x800 || offset == 0x804 || offset == 0xc00 || offset == 0xc04));
    ++smmu_writes;
    smmu_regs[offset / 4] = value; /* simulate a store reached MMIO before reporting failure */
    return smmu_writes != smmu_fail_write;
}

static void test_smmu(void)
{
    const unsigned smr = 0x800 / 4, s2cr = 0xc00 / 4, cb = 0x80000 / 4, stride = 0x1000 / 4;
    smmu_read_fault = UINT32_MAX;
    smmu_regs[0] = 0x290406;
    smmu_regs[0x20 / 4] = 0x4c017e69;
    smmu_regs[0x24 / 4] = 0x60020046;
    smmu_regs[0x28 / 4] = 0x5111;
    smmu_regs[0x48 / 4] = 4;
    for (unsigned i = 0; i < 105; ++i) {
        smmu_regs[s2cr + i] = 2u << 16;
    }
    for (unsigned i = 2; i <= 5; ++i) {
        smmu_regs[s2cr + i] = i; /* firmware-owned entries, including invalid SMRs */
    }
    smmu_regs[smr + 2] = 0x800000b6;
    smmu_regs[smr + 8] = 0x80001401;
    smmu_regs[s2cr + 8] = 6;
    smmu_regs[cb + stride * 6] = 1;
    smmu_regs[cb + stride * 6 + 0x24 / 4] = 7u << 16;

    struct fuxi_audio_smmu_state result;
    assert(fuxi_audio_smmu_read_state(smmu_read, NULL, &result) == FUXI_AUDIO_SMMU_OK);
    assert(result.slots == 105 && result.banks == 70 && result.gfsr == 4);
    assert(result.first_slots[0] == 0 && result.first_slots[1] == 1 && result.blank_slots == 100);
    assert(result.first_bank == 8 && result.unused_banks == 62);
    assert(result.wlan.matches == 1 && result.wlan.slot == 8 && result.wlan.s2cr == 6);
    assert(!result.audio[0].matches && !result.audio[1].matches && result.audio[0].slot == -1);

    smmu_regs[smr + 9] = 0x80001081; /* matches the masked audio SID despite a different ID */
    smmu_regs[s2cr + 9] = 9;
    smmu_regs[smr + 10] = 0x80201041; /* covers 0x1061 */
    smmu_regs[s2cr + 10] = 9;
    smmu_regs[smr + 12] = 0x80801001;
    smmu_regs[s2cr + 12] = 9;
    assert(fuxi_audio_smmu_read_state(smmu_read, NULL, &result) == FUXI_AUDIO_SMMU_OK);
    assert(result.audio[0].matches == 2 && result.audio[0].slot == 9 && result.audio[0].s2cr == 9);
    assert(result.audio[1].matches == 1 && result.audio[1].slot == 10);
    assert(result.blank_slots == 97 && result.unused_banks == 61);
    smmu_read_fault = 0x80000 + 6 * 0x1000 + 0x2c;
    assert(fuxi_audio_smmu_read_state(smmu_read, NULL, &result) == FUXI_AUDIO_SMMU_READ_ERROR);
    smmu_read_fault = UINT32_MAX;
    smmu_regs[0] |= 8;
    assert(fuxi_audio_smmu_read_state(smmu_read, NULL, &result) == FUXI_AUDIO_SMMU_UNSUPPORTED);
    smmu_regs[0] &= ~8u;
    smmu_regs[0x24 / 4] |= 1u << 31;
    assert(fuxi_audio_smmu_read_state(smmu_read, NULL, &result) == FUXI_AUDIO_SMMU_UNSUPPORTED);
}

static void test_passthrough(void)
{
    const unsigned smr = 0x800 / 4, s2cr = 0xc00 / 4;
    memset(smmu_regs, 0, sizeof(smmu_regs));
    smmu_regs[0] = 0x290406;
    smmu_regs[0x20 / 4] = 0x4c017e69;
    smmu_regs[0x24 / 4] = 0x60000046;
    smmu_regs[0x28 / 4] = 0x5111;
    smmu_regs[(0x1000 + 4 * FUXI_AUDIO_BYPASS_BANK) / 4] = 0x10000;
    for (unsigned slot = 0; slot < 105; ++slot) {
        smmu_regs[s2cr + slot] = FUXI_SMMU_FAULT_S2CR;
    }
    smmu_regs[smr + 4] = 0x80000040;
    smmu_regs[s2cr + 4] = FUXI_AUDIO_BYPASS_BANK;
    smmu_read_fault = UINT32_MAX;

    smmu_regs[smr + 7] = 0x80001081;
    assert(!fuxi_audio_smmu_attach(smmu_read, smmu_write, NULL) && !smmu_writes);
    smmu_regs[smr + 7] = 0;
    smmu_regs[0x84000 / 4] = 1;
    assert(!fuxi_audio_smmu_attach(smmu_read, smmu_write, NULL) && !smmu_writes);
    smmu_regs[0x84000 / 4] = 0;
    smmu_regs[smr + 1] = 0x42; /* restore the complete original invalid SMR */
    for (unsigned fail = 1; fail <= 4; ++fail) {
        smmu_writes = 0;
        smmu_fail_write = fail;
        assert(!fuxi_audio_smmu_attach(smmu_read, smmu_write, NULL));
        assert(!smmu_regs[smr] && smmu_regs[smr + 1] == 0x42 &&
            smmu_regs[s2cr] == FUXI_SMMU_FAULT_S2CR && smmu_regs[s2cr + 1] == FUXI_SMMU_FAULT_S2CR);
    }
    smmu_writes = smmu_fail_write = 0;
    assert(fuxi_audio_smmu_attach(smmu_read, smmu_write, NULL) && smmu_writes == 4);
    assert(fuxi_audio_smmu_attach(smmu_read, smmu_write, NULL) && smmu_writes == 4);
    assert(smmu_regs[smr] == FUXI_AUDIO_DMA_SMR0 && smmu_regs[smr + 1] == FUXI_AUDIO_DMA_SMR1 &&
        smmu_regs[s2cr] == 4 && smmu_regs[s2cr + 1] == 4);

    struct fuxi_audio_dma_handoff handoff = {FUXI_AUDIO_DMA_MAGIC, FUXI_AUDIO_DMA_BYTES, 0xa9250000, ~0xa9250000u};
    assert(fuxi_audio_dma_handoff_valid(&handoff));
    handoff.inverted_pa ^= 1;
    assert(!fuxi_audio_dma_handoff_valid(&handoff));
    handoff.pa = 0xffff4000;
    handoff.inverted_pa = ~handoff.pa;
    assert(!fuxi_audio_dma_handoff_valid(&handoff));
}

int main(void) {
    reset(); fuxi_audio_spf_start(); fuxi_audio_spf_poll(); assert(!registrations && !sends);
    for(unsigned value=0;value<7;value++) {
        reset(); have_adt=true; adt_value=value; fuxi_audio_spf_start();
        assert(registrations==(value>=1 && value<=5));
    }
    reset(); have_adt=true; adt_value=1; adt_bytes=8;
    fuxi_audio_spf_start(); assert(!registrations);
    reset(); have_adt=true; adt_value=1; have_argument=true; argument=0;
    fuxi_audio_spf_start(); assert(!registrations);
    reset(); have_argument=true; argument=1;
    fuxi_audio_spf_start(); fuxi_audio_spf_start(); assert(registrations==1);
    reset(); have_adt=true; adt_value=2;
    fuxi_audio_spf_start(); state(1); fuxi_audio_spf_poll();
    assert(sends==1 && sent_length==24);
    FuxiAudioPrepGprView bare;
    assert(!fuxi_audio_prep_gpr_decode(sent,sent_length,&bare) &&
           !bare.payload_bytes && bare.fields.opcode==FUXI_AUDIO_PREP_GET_SPF_STATE);
    deliver(FUXI_AUDIO_PREP_SPF_STATE_REPLY,FUXI_SPF_TOKEN,1);
    fuxi_audio_spf_poll(); assert(gSpf.done && gSpf.reply.state==1);
    reset(); have_adt=true; adt_value=1; register_failure=true;
    fuxi_audio_spf_start(); fuxi_audio_spf_start(); assert(registrations==1 && gSpf.done);
    reset(); have_adt=true; adt_value=1; fuxi_audio_spf_start();
    now=15000ull*NSEC_PER_MSEC; fuxi_audio_spf_poll(); assert(gSpf.done && !sends);
    begin(); deliver(FUXI_AUDIO_PREP_BASIC_RESULT,FUXI_SPF_TOKEN,0);
    fuxi_audio_spf_poll(); assert(gSpf.accepted && !gSpf.done && sends==1);
    deliver(FUXI_AUDIO_PREP_SPF_STATE_REPLY,FUXI_SPF_TOKEN,1);
    fuxi_audio_spf_poll(); assert(gSpf.done && gSpf.reply.state==1 && sends==1);
    state(0); state(1); fuxi_audio_spf_poll(); assert(sends==1);
    begin();
    for(unsigned index=0;index<10;index++)
        deliver(FUXI_AUDIO_PREP_SPF_STATE_REPLY,FUXI_SPF_TOKEN+1,1);
    assert(gSpf.invalid==4 && !gSpf.reply_ready);
    assert(gSpf.rejected[0].length==28 && gSpf.rejected[0].result==FuxiAudioPrepRoute &&
           gSpf.rejected[0].header[4]==FUXI_SPF_TOKEN+1 &&
           gSpf.rejected[0].header[5]==FUXI_AUDIO_PREP_SPF_STATE_REPLY);
    uint8_t oversized[513]={0};
    callbacks.receive(NULL,oversized,sizeof(oversized));
    assert(gSpf.invalid==4);
    /* 回覆期限 1 秒：逾時後等 100 ms 才重問，下一次用新 token。 */
    now=999ull*NSEC_PER_MSEC; fuxi_audio_spf_poll(); assert(!gSpf.timeouts && sends==1);
    now=1000ull*NSEC_PER_MSEC; fuxi_audio_spf_poll();
    assert(gSpf.timeouts==1 && !gSpf.waiting && !gSpf.done && sends==1);
    now=1099ull*NSEC_PER_MSEC; fuxi_audio_spf_poll(); assert(sends==1);
    now=1100ull*NSEC_PER_MSEC; fuxi_audio_spf_poll();
    assert(sends==2 && gSpf.waiting && gSpf.token==FUXI_SPF_TOKEN+1);
    FuxiAudioPrepGprView second;
    assert(!fuxi_audio_prep_gpr_decode(sent,sent_length,&second) && second.fields.token==FUXI_SPF_TOKEN+1);
    deliver(FUXI_AUDIO_PREP_SPF_STATE_REPLY,FUXI_SPF_TOKEN,1);
    assert(!gSpf.reply_ready);
    deliver(FUXI_AUDIO_PREP_SPF_STATE_REPLY,FUXI_SPF_TOKEN+1,1);
    fuxi_audio_spf_poll(); assert(gSpf.done && gSpf.queries==2 && gSpf.timeouts==1);
    /* 遠端錯誤與未就緒都重問，直到 state 1。 */
    begin(); deliver(FUXI_AUDIO_PREP_BASIC_RESULT,FUXI_SPF_TOKEN,5);
    fuxi_audio_spf_poll();
    assert(!gSpf.done && gSpf.errors==1 && gSpf.last_status==5 && gSpf.reported_first && sends==1);
    for(unsigned index=1;index<4;index++) {
        now+=100ull*NSEC_PER_MSEC; fuxi_audio_spf_poll();
        assert(sends==index+1 && gSpf.token==FUXI_SPF_TOKEN+index);
        deliver(FUXI_AUDIO_PREP_SPF_STATE_REPLY,FUXI_SPF_TOKEN+index,index==3?1:0);
        fuxi_audio_spf_poll();
    }
    assert(gSpf.done && gSpf.queries==4 && gSpf.not_ready==2 && gSpf.errors==1 && gSpf.last_state==1);
    now+=1000ull*NSEC_PER_MSEC; fuxi_audio_spf_poll(); assert(sends==4);
    /* 一律未就緒：第 100 次後停止，不再送。 */
    begin();
    for(unsigned index=0;index<FUXI_SPF_MAX_QUERIES;index++) {
        assert(sends==index+1);
        deliver(FUXI_AUDIO_PREP_SPF_STATE_REPLY,FUXI_SPF_TOKEN+index,0);
        fuxi_audio_spf_poll();
        now+=100ull*NSEC_PER_MSEC; fuxi_audio_spf_poll();
    }
    assert(gSpf.done && sends==FUXI_SPF_MAX_QUERIES && gSpf.not_ready==FUXI_SPF_MAX_QUERIES);
    now+=10000ull*NSEC_PER_MSEC; fuxi_audio_spf_poll(); assert(sends==FUXI_SPF_MAX_QUERIES);
    /* 一律逾時：同樣在第 100 次後停止。 */
    begin();
    for(unsigned index=0;index<2*FUXI_SPF_MAX_QUERIES+5;index++) {
        now+=1000ull*NSEC_PER_MSEC; fuxi_audio_spf_poll();
    }
    assert(gSpf.done && sends==FUXI_SPF_MAX_QUERIES && gSpf.timeouts==FUXI_SPF_MAX_QUERIES);
    /* 兩次查詢之間斷線就結束。 */
    begin(); deliver(FUXI_AUDIO_PREP_SPF_STATE_REPLY,FUXI_SPF_TOKEN,0); fuxi_audio_spf_poll();
    assert(!gSpf.done && !gSpf.waiting);
    state(0); fuxi_audio_spf_poll(); assert(gSpf.done);
    state(1); now+=1000ull*NSEC_PER_MSEC; fuxi_audio_spf_poll(); assert(sends==1);
    begin(); state(0); state(1); fuxi_audio_spf_poll(); assert(gSpf.done && sends==1);
    begin(); now=1000ull*NSEC_PER_MSEC;
    deliver(FUXI_AUDIO_PREP_SPF_STATE_REPLY,FUXI_SPF_TOKEN,1);
    assert(!gSpf.reply_ready);
    fuxi_audio_spf_poll(); assert(!gSpf.done && gSpf.timeouts==1 && sends==1);
    reset(); have_adt=true; adt_value=1; send_error=-1;
    fuxi_audio_spf_start(); state(1); fuxi_audio_spf_poll();
    assert(gSpf.done && sends==1); fuxi_audio_spf_poll(); assert(sends==1);
    /* 之後的重問送出失敗也結束。 */
    begin(); deliver(FUXI_AUDIO_PREP_SPF_STATE_REPLY,FUXI_SPF_TOKEN,0); fuxi_audio_spf_poll();
    send_error=-1; now+=100ull*NSEC_PER_MSEC; fuxi_audio_spf_poll();
    assert(gSpf.done && sends==2 && gSpf.queries==1);
    now+=1000ull*NSEC_PER_MSEC; fuxi_audio_spf_poll(); assert(sends==2);
    test_prm();
    test_graph();
    test_smmu();
    test_passthrough();
    begin_graph_mode(5, false);
    assert(gGraph.step == FUXI_GRAPH_IDLE && dma_opens == 1 && !dma_closes);
    test_pcm(false);
    test_pcm(true);
    test_tone();
    puts("PASS production audio SPF/PRM/TDM client: readiness, LPASS vote/release, graph configuration and cleanup (simulated DSP)");
    puts("PASS production audio SMMU survey: masked overlaps, occupied banks and ASIDs, access failure and layout bounds");
    puts("PASS production silent PCM: pass-through rollback, PRM, map/write confirmation and teardown");
    puts("PASS production tone PCM: mode 7 handshake, 2 kHz sine/fade/zeros, stray/error/timeout/disconnect teardown");
}
'''


def main():
    owner = (SOURCE / 'fuxi_adsp.c').read_text()
    assert owner.count('fuxi_audio_spf_start();') == 1
    assert owner.count('fuxi_audio_spf_poll();') == 1
    assert owner.index('gStarted = 1;') < owner.index('fuxi_audio_spf_start();')
    assert owner.index('fuxi_audio_spf_start();') < owner.index('kernel_thread_start(adspThread')
    assert owner.count('fa_map_prot(&window, VM_WIMG_IO, VM_PROT_READ)') == 1
    with tempfile.TemporaryDirectory(prefix='fuxi-audio-spf-') as temporary:
        directory = Path(temporary)
        for name in ('pexpert/arm64/board_config.h', 'sys/systm.h', 'kern/clock.h',
                     'pexpert/pexpert.h', 'pexpert/device_tree.h'):
            header = directory / name
            header.parent.mkdir(parents=True, exist_ok=True)
            header.write_text('')
        source = directory / 'host.c'
        source.write_text(HOST)
        executable = directory / 'host'
        subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-O1', '-g', '-DFUXI',
                        '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                        '-fno-omit-frame-pointer', '-I', str(directory), '-I', str(SOURCE),
                        '-I', str(PREP), '-I', str(AUDIO), str(source), str(PREP / 'fuxi_audio_prep_gpr.c'),
                        str(AUDIO / 'fuxi_audioreach.c'), str(AUDIO / 'fuxi_gpr.c'),
                        '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True, timeout=20)


if __name__ == '__main__':
    main()
