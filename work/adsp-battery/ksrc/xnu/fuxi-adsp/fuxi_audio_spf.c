#include <pexpert/arm64/board_config.h>
#if defined(FUXI)
#include <sys/systm.h>
#include <kern/clock.h>
#include <pexpert/pexpert.h>
#include <pexpert/device_tree.h>
#include "ShengAdsp.h"
#include "fuxi_audio_prep_gpr.h"
#include "fuxi_audio_smmu.h"
#include "fuxi_audioreach.h"
#include "fuxi_amp_tone.h"

#define FUXI_SPF_PORT 0x1000u
#define FUXI_SPF_TOKEN 0x53504601u
#define FUXI_SPF_INTENT_BYTES 512u
/* 與 Sheng 參考實作相同：DSP 剛起來時常回「未就緒」，最多問 100 次，每次等 1 秒回覆、間隔至少 100 ms。 */
#define FUXI_SPF_MAX_QUERIES 100u
#define FUXI_SPF_REPLY_MS 1000u
#define FUXI_SPF_GAP_MS 100u
#define FUXI_PRM_TOKEN 0x50524d01u
#define FUXI_PRM_LPASS_MACRO 1u
#define FUXI_PRM_CLEANUP_MS 2500u
#define FUXI_GRAPH_TOKEN 0x54444d01u
#define FUXI_GRAPH_QUEUE_MS 2500u
#define FUXI_TDM_BIT_CLOCK 0x202u
#define FUXI_TDM_BIT_CLOCK_HZ (48000u * 4u * 32u)
#define FUXI_TDM_CLOCK_INVERT 4u
#define FUXI_PCM_BYTES (3840u * 4u * sizeof(uint32_t)) /* 80 ms, four 32-bit slots */
_Static_assert(FUXI_PCM_BYTES <= FUXI_AUDIO_DMA_BYTES, "PCM must fit in the loader buffer");
/* 出聲模式（功放 mode 7）：早期 au_tone_pump 的 20 ms 週期、四個緩衝輪替；這裡最多三個在 DSP 手上，第四個供下一次填寫。 */
#define FUXI_TONE_PERIOD_FRAMES 960u
#define FUXI_TONE_PERIOD_BYTES (FUXI_TONE_PERIOD_FRAMES * 4u * sizeof(uint32_t))
#define FUXI_TONE_PERIODS 4u
#define FUXI_TONE_AHEAD 3u
#define FUXI_TONE_ZEROS 5u        /* 開功放前、以及正弦之後各播 100 ms 靜音（早期先確認 write_done ≥ 5） */
#define FUXI_TONE_GATE_MS 120000u /* 功放載入與試跑保護韌體的上限 */
#define FUXI_TONE_WAIT_MS 3000u   /* 靜音播放、功放上電、功放下電各自的上限 */
_Static_assert(FUXI_TONE_PERIODS * FUXI_TONE_PERIOD_BYTES <= FUXI_AUDIO_DMA_BYTES, "tone periods must fit in the loader buffer");
_Static_assert(FUXI_TONE_AHEAD < FUXI_TONE_PERIODS, "the period being filled must not be in the DSP's hands");
_Static_assert(FUXI_TONE_FRAMES % FUXI_TONE_PERIOD_FRAMES == 0, "the tone ends on a period boundary");
#if defined(__aarch64__)
#define FUXI_PCM_BARRIER() __asm__ volatile("dsb sy" ::: "memory")
#else
#define FUXI_PCM_BARRIER() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#endif

enum fuxi_prm_phase {
    FUXI_PRM_IDLE, FUXI_PRM_VOTE_REPLY, FUXI_PRM_QUEUE_RELEASE, FUXI_PRM_RELEASE_REPLY
};

/* q6prm.c 的 HW core vote/unvote，封包沿用 fuxi_audioreach。
 * 只短暫取得 LPASS macro 資源，用來確認 PRM 可用。沒有 APM graph、TDM 或功放設定。
 */
static struct {
    enum fuxi_prm_phase phase;
    uint64_t deadline;
    uint32_t status;
    bool reply_ready;
} gPrm;

enum fuxi_graph_step {
    FUXI_GRAPH_IDLE, FUXI_GRAPH_VOTE_MACRO, FUXI_GRAPH_VOTE_CODEC, FUXI_GRAPH_CLOCK_ON,
    FUXI_GRAPH_OPEN_SPEAKER, FUXI_GRAPH_CONFIG_SPEAKER, FUXI_GRAPH_OPEN_PLAYBACK,
    FUXI_GRAPH_CONFIG_PLAYBACK, FUXI_GRAPH_CONFIG_INPUT, FUXI_GRAPH_MAP,
    FUXI_GRAPH_PREPARE_SPEAKER, FUXI_GRAPH_PREPARE_PLAYBACK, FUXI_GRAPH_START_SPEAKER,
    FUXI_GRAPH_START_PLAYBACK, FUXI_GRAPH_WRITE, FUXI_GRAPH_STOP_PLAYBACK,
    FUXI_GRAPH_STOP_SPEAKER, FUXI_GRAPH_CLOSE_PLAYBACK, FUXI_GRAPH_CLOSE_SPEAKER,
    FUXI_GRAPH_UNMAP, FUXI_GRAPH_CLOCK_OFF, FUXI_GRAPH_UNVOTE_CODEC,
    FUXI_GRAPH_UNVOTE_MACRO, FUXI_GRAPH_COMPLETE
};

_Static_assert(FUXI_GRAPH_COMPLETE < 32, "graph command mask");
#define GRAPH_SENT(step) (1u << (step))
#define AP FUXI_AR_APM_IID
#define PRM FUXI_AR_PRM_IID
#define SHMEM FUXI_AR_IID_PLAY_SHMEM
static const struct {
    const char *name;
    uint32_t opcode, port, completion, prerequisite;
} graph_steps[] = {
    [FUXI_GRAPH_VOTE_MACRO] = {"enable LPASS macro", FUXI_AR_OP_PRM_REQUEST, PRM, FUXI_AR_OP_PRM_REQUEST_DONE, 0},
    [FUXI_GRAPH_VOTE_CODEC] = {"enable digital codec", FUXI_AR_OP_PRM_REQUEST, PRM, FUXI_AR_OP_PRM_REQUEST_DONE, 0},
    [FUXI_GRAPH_CLOCK_ON] = {"enable TDM clock", FUXI_AR_OP_PRM_REQUEST, PRM, FUXI_AR_OP_PRM_REQUEST_DONE, 0},
    [FUXI_GRAPH_OPEN_SPEAKER] = {"open speaker", FUXI_AR_OP_GRAPH_OPEN, AP, 0, 0},
    [FUXI_GRAPH_CONFIG_SPEAKER] = {"configure speaker", FUXI_AR_OP_SET_CFG, AP, 0, 0},
    [FUXI_GRAPH_OPEN_PLAYBACK] = {"open playback", FUXI_AR_OP_GRAPH_OPEN, AP, 0, 0},
    [FUXI_GRAPH_CONFIG_PLAYBACK] = {"configure playback", FUXI_AR_OP_SET_CFG, AP, 0, 0},
    [FUXI_GRAPH_CONFIG_INPUT] = {"configure shared memory input", FUXI_AR_OP_SET_CFG, SHMEM, 0, 0},
    [FUXI_GRAPH_MAP] = {"map PCM buffer", FUXI_AR_OP_MEM_MAP, AP, FUXI_AR_OP_MEM_MAP_DONE, 0},
    [FUXI_GRAPH_PREPARE_SPEAKER] = {"prepare speaker", FUXI_AR_OP_GRAPH_PREPARE, AP, 0, 0},
    [FUXI_GRAPH_PREPARE_PLAYBACK] = {"prepare playback", FUXI_AR_OP_GRAPH_PREPARE, AP, 0, 0},
    [FUXI_GRAPH_START_SPEAKER] = {"start speaker", FUXI_AR_OP_GRAPH_START, AP, 0, 0},
    [FUXI_GRAPH_START_PLAYBACK] = {"start playback", FUXI_AR_OP_GRAPH_START, AP, 0, 0},
    [FUXI_GRAPH_WRITE] = {"write 80 ms of silent PCM", FUXI_AR_OP_WRITE, SHMEM, FUXI_AR_OP_WRITE_DONE, 0},
    [FUXI_GRAPH_STOP_PLAYBACK] = {"stop playback", FUXI_AR_OP_GRAPH_STOP, AP, 0, GRAPH_SENT(FUXI_GRAPH_START_PLAYBACK)},
    [FUXI_GRAPH_STOP_SPEAKER] = {"stop speaker", FUXI_AR_OP_GRAPH_STOP, AP, 0, GRAPH_SENT(FUXI_GRAPH_START_SPEAKER)},
    [FUXI_GRAPH_CLOSE_PLAYBACK] = {"close playback", FUXI_AR_OP_GRAPH_CLOSE, AP, 0, GRAPH_SENT(FUXI_GRAPH_OPEN_PLAYBACK)},
    [FUXI_GRAPH_CLOSE_SPEAKER] = {"close speaker", FUXI_AR_OP_GRAPH_CLOSE, AP, 0, GRAPH_SENT(FUXI_GRAPH_OPEN_SPEAKER)},
    [FUXI_GRAPH_UNMAP] = {"unmap PCM buffer", FUXI_AR_OP_MEM_UNMAP, AP, 0, 0},
    [FUXI_GRAPH_CLOCK_OFF] = {"release TDM clock", FUXI_AR_OP_PRM_RELEASE, PRM, FUXI_AR_OP_PRM_RELEASE_DONE, GRAPH_SENT(FUXI_GRAPH_CLOCK_ON)},
    [FUXI_GRAPH_UNVOTE_CODEC] = {"release digital codec", FUXI_AR_OP_PRM_RELEASE, PRM, FUXI_AR_OP_PRM_RELEASE_DONE, GRAPH_SENT(FUXI_GRAPH_VOTE_CODEC)},
    [FUXI_GRAPH_UNVOTE_MACRO] = {"release LPASS macro", FUXI_AR_OP_PRM_RELEASE, PRM, FUXI_AR_OP_PRM_RELEASE_DONE, GRAPH_SENT(FUXI_GRAPH_VOTE_MACRO)},
};
#undef AP
#undef PRM
#undef SHMEM

static struct {
    enum fuxi_graph_step step;
    uint64_t deadline, dma_address;
    uint32_t status, queued, map_handle;
    bool waiting, reply_ready, link_lost, failed;
} gGraph;

static struct {
    ShengAdspChannel *channel;
    uint64_t deadline;   /* 第一次查詢前是 channel 期限，之後是目前這次查詢的回覆期限 */
    uint64_t next_query; /* 未就緒、錯誤或逾時後，下一次查詢最早的時間 */
    FuxiAudioPrepSpfReply reply;
    struct {
        uint32_t length, header[6];
        int result;
    } rejected[4];
    unsigned invalid, reported_invalid, format;
    unsigned queries, not_ready, errors, timeouts;
    uint32_t token, last_state, last_status;
    int up, attempted, waiting, done, accepted, reported_accepted, reported_first, reply_ready, disconnected;
} gSpf;

/* 出聲模式的 PCM 端階段。GATE：圖還沒開，等功放 READY；GRAPH：照 mode 5 建圖；PREROLL：WRITE 步驟送靜音，播滿後交出
 * CLOCKED，等功放 ON；PLAY：送一秒正弦；POSTROLL：正弦全部回報後再送靜音，交出 DRAIN，等功放 OFF；FINISH：停送，等手上的緩衝回來。 */
enum fuxi_tone_phase {
    FUXI_TONE_PCM_OFF, FUXI_TONE_PCM_GATE, FUXI_TONE_PCM_GRAPH, FUXI_TONE_PCM_PREROLL,
    FUXI_TONE_PCM_PLAY, FUXI_TONE_PCM_POSTROLL, FUXI_TONE_PCM_FINISH
};

static struct {
    enum fuxi_tone_phase phase;
    uint64_t deadline;
    uint32_t sent, done, frame, tone_start, tone_end, stray, status;
    bool clocked, error;
    const char *why;
} gTone;

void fuxi_audio_spf_start(void);
void fuxi_audio_spf_poll(void);
extern void fuxi_audio_dma_survey(void);
extern bool fuxi_audio_dma_open(uint64_t *address);
extern void fuxi_audio_dma_close(void);
extern void *fuxi_audio_dma_buffer(void);

static unsigned format(void)
{
    uint32_t value = 0;
    DTEntry chosen;
    const void *property;
    unsigned bytes;
    if (!PE_parse_boot_argn("fuxiaudiospf", &value, sizeof(value)) &&
        SecureDTLookupEntry(NULL, "/chosen", &chosen) == kSuccess &&
        SecureDTGetProperty(chosen, "fuxi,audio-spf", &property, &bytes) == kSuccess &&
        bytes == sizeof(value))
        memcpy(&value, property, sizeof(value));
    return value <= 5 ? value : 0;
}

static void graph_set_step(enum fuxi_graph_step step)
{
    gGraph.step = step;
    gGraph.waiting = false;
    gGraph.reply_ready = false;
    gGraph.link_lost = false;
    clock_interval_to_deadline(FUXI_GRAPH_QUEUE_MS, NSEC_PER_MSEC, &gGraph.deadline);
}

static uint32_t graph_packet(uint8_t *packet, uint32_t capacity)
{
    static const FuxiArFormat pcm = {48000, 32, 4};
    const uint32_t token = FUXI_GRAPH_TOKEN + gGraph.step;
    const uint32_t opcode = graph_steps[gGraph.step].opcode;
    switch (gGraph.step) {
    case FUXI_GRAPH_VOTE_MACRO:
    case FUXI_GRAPH_UNVOTE_MACRO:
        return fuxi_ar_prm_vote(packet, capacity, FUXI_AR_PRM_IID, token, FUXI_AR_HW_MACRO,
            gGraph.step == FUXI_GRAPH_VOTE_MACRO);
    case FUXI_GRAPH_VOTE_CODEC:
    case FUXI_GRAPH_UNVOTE_CODEC:
        return fuxi_ar_prm_vote(packet, capacity, FUXI_AR_PRM_IID, token, FUXI_AR_HW_DCODEC,
            gGraph.step == FUXI_GRAPH_VOTE_CODEC);
    case FUXI_GRAPH_CLOCK_ON:
    case FUXI_GRAPH_CLOCK_OFF:
        return fuxi_ar_prm_clock_attr(packet, capacity, FUXI_AR_PRM_IID, token, FUXI_TDM_BIT_CLOCK,
            gGraph.step == FUXI_GRAPH_CLOCK_ON ? FUXI_TDM_BIT_CLOCK_HZ : 0, FUXI_TDM_CLOCK_INVERT);
    case FUXI_GRAPH_OPEN_SPEAKER:
        return fuxi_ar_open_speaker_tdm(packet, capacity, FUXI_SPF_PORT, token);
    case FUXI_GRAPH_CONFIG_SPEAKER:
        return fuxi_ar_config_speaker_tdm(packet, capacity, FUXI_SPF_PORT, token, &pcm);
    case FUXI_GRAPH_OPEN_PLAYBACK:
        return fuxi_ar_open_playback_stream_tdm(packet, capacity, FUXI_SPF_PORT, token);
    case FUXI_GRAPH_CONFIG_PLAYBACK:
        return fuxi_ar_config_playback_stream(packet, capacity, FUXI_SPF_PORT, token, &pcm);
    case FUXI_GRAPH_CONFIG_INPUT:
        return fuxi_ar_config_playback_shmem(packet, capacity, FUXI_SPF_PORT, token, &pcm);
    case FUXI_GRAPH_MAP:
        return fuxi_ar_map(packet, capacity, FUXI_SPF_PORT, token, gGraph.dma_address, FUXI_AUDIO_DMA_BYTES);
    case FUXI_GRAPH_UNMAP:
        return fuxi_ar_unmap(packet, capacity, FUXI_SPF_PORT, token, gGraph.map_handle);
    case FUXI_GRAPH_WRITE:
        return fuxi_ar_write(packet, capacity, FUXI_SPF_PORT, token, gGraph.dma_address, gGraph.map_handle,
            FUXI_PCM_BYTES);
    case FUXI_GRAPH_PREPARE_PLAYBACK:
    case FUXI_GRAPH_START_PLAYBACK:
    case FUXI_GRAPH_STOP_PLAYBACK:
    case FUXI_GRAPH_CLOSE_PLAYBACK:
        return fuxi_ar_graph_command(packet, capacity, FUXI_SPF_PORT, token, opcode, FUXI_AR_SG_PLAY);
    case FUXI_GRAPH_PREPARE_SPEAKER:
    case FUXI_GRAPH_START_SPEAKER:
    case FUXI_GRAPH_STOP_SPEAKER:
    case FUXI_GRAPH_CLOSE_SPEAKER:
        return fuxi_ar_graph_command(packet, capacity, FUXI_SPF_PORT, token, opcode, FUXI_AR_SG_SPEAKER);
    default:
        return 0;
    }
}

/* ---- 出聲模式 ---- */
static bool tone_owns_write(void)
{
    return gGraph.step == FUXI_GRAPH_WRITE && gTone.phase >= FUXI_TONE_PCM_GRAPH;
}

/* 出錯：把功放狀態改成 FAILED（DRAIN 交給功放自己收尾），不再送新的緩衝。 */
static void tone_stop(const char *why)
{
    if (!gTone.why) {
        gTone.why = why;
    }
    gTone.error = true;
    fuxi_amp_tone_abandon();
    gTone.phase = FUXI_TONE_PCM_FINISH;
    clock_interval_to_deadline(FUXI_SPF_REPLY_MS, NSEC_PER_MSEC, &gTone.deadline);
}

/* GATE：PCM 圖還沒送任何命令。功放還在 PENDING 就繼續等；READY 才建圖，否則照 mode 5 只送靜音。回傳 true 表示可以往下走。 */
static bool tone_gate(void)
{
    const uint32_t state = fuxi_amp_tone_get();
    clock_interval_to_deadline(FUXI_GRAPH_QUEUE_MS, NSEC_PER_MSEC, &gGraph.deadline);
    if (state == FUXI_TONE_PENDING && !gGraph.link_lost && mach_absolute_time() < gTone.deadline) {
        return false;
    }
    if (state == FUXI_TONE_READY && !gGraph.link_lost) {
        gTone.phase = FUXI_TONE_PCM_GRAPH;
        printf("FUXI AUDIO TONE: bottom amplifier ready, opening the speaker graph\n");
    } else {
        const uint32_t was = fuxi_amp_tone_abandon();
        gTone.phase = FUXI_TONE_PCM_OFF;
        printf("FUXI AUDIO TONE: amplifier state %u%s, silent PCM only\n", was,
            gGraph.link_lost ? " and GLINK lost" : state == FUXI_TONE_PENDING ? " after the wait" : "");
    }
    return true;
}

/* 第 index 個週期：靜音，或剩下的正弦（不足一個週期的部分補零）。裝置記憶體只用 32 位元 volatile 寫入，不讓編譯器換成 DC ZVA。 */
static bool tone_fill(uint32_t index, bool tone)
{
    uint8_t *base = fuxi_audio_dma_buffer();
    if (!base) {
        return false;
    }
    volatile uint32_t *out = (volatile uint32_t *)(base + index * FUXI_TONE_PERIOD_BYTES);
    for (uint32_t i = 0; i < FUXI_TONE_PERIOD_FRAMES; i++) {
        uint32_t value = 0;
        if (tone && gTone.frame < FUXI_TONE_FRAMES) {
            value = (uint32_t)fuxi_amp_tone_sample(gTone.frame++);
        }
        for (uint32_t slot = 0; slot < 4; slot++) {
            out[4 * i + slot] = value;
        }
    }
    FUXI_PCM_BARRIER();
    return true;
}

/* WRITE_DONE 必須照送出的順序回來（位址與 map handle 都對）；不認得的只計數。在擁有者執行緒的接收回呼裡，不送任何東西。 */
static void tone_receive(const uint8_t *data, uint32_t length)
{
    FuxiAudioPrepGprView view;
    if (!gSpf.up || length > FUXI_SPF_INTENT_BYTES || fuxi_audio_prep_gpr_decode(data, length, &view) ||
        view.fields.src_domain != FUXI_AUDIO_PREP_DOMAIN_ADSP ||
        view.fields.dst_domain != FUXI_AUDIO_PREP_DOMAIN_APPS ||
        view.fields.src_port != FUXI_AR_IID_PLAY_SHMEM || view.fields.dst_port != FUXI_SPF_PORT ||
        view.fields.token != FUXI_GRAPH_TOKEN + FUXI_GRAPH_WRITE) {
        return;
    }
    if (view.fields.opcode == FUXI_AUDIO_PREP_BASIC_RESULT) {
        uint32_t result[2];
        if (view.payload_bytes == sizeof(result)) {
            memcpy(result, view.payload, sizeof(result));
            if (result[0] == FUXI_AR_OP_WRITE && result[1] != 0) {
                gTone.error = true;
                gTone.status = result[1];
            }
        }
        return;
    }
    if (view.fields.opcode != FUXI_AR_OP_WRITE_DONE || view.payload_bytes < 16) {
        return;
    }
    uint32_t result[4];
    memcpy(result, view.payload, sizeof(result));
    const uint64_t returned = (uint64_t)result[1] << 32 | result[0];
    const uint64_t expected = gGraph.dma_address + (uint64_t)(gTone.done % FUXI_TONE_PERIODS) * FUXI_TONE_PERIOD_BYTES;
    if (gTone.done >= gTone.sent || returned != expected || result[2] != gGraph.map_handle) {
        gTone.stray++;
        return;
    }
    if (result[3] != 0) {
        gTone.error = true;
        gTone.status = result[3];
    }
    gTone.done++;
    clock_interval_to_deadline(FUXI_SPF_REPLY_MS, NSEC_PER_MSEC, &gGraph.deadline);
}

/* 出聲模式下 WRITE 步驟由這裡接手。回傳 true 表示結束，呼叫端再 graph_advance(gTone.error) 進入停圖。 */
static bool tone_pump(void)
{
    const uint64_t now = mach_absolute_time();
    if (gTone.phase == FUXI_TONE_PCM_GRAPH) {
        gTone.phase = FUXI_TONE_PCM_PREROLL;
        clock_interval_to_deadline(FUXI_TONE_WAIT_MS, NSEC_PER_MSEC, &gTone.deadline);
        clock_interval_to_deadline(FUXI_SPF_REPLY_MS, NSEC_PER_MSEC, &gGraph.deadline);
        printf("FUXI AUDIO TONE: speaker graph started, %u ms of zeros before the amplifier\n", FUXI_TONE_ZEROS * 20u);
    }
    if (gTone.phase < FUXI_TONE_PCM_FINISH) {
        const uint32_t state = fuxi_amp_tone_get();
        if (gGraph.link_lost) {
            tone_stop("GLINK disconnect");
        } else if (gTone.error) {
            tone_stop("write error");
        } else if (now >= gGraph.deadline) {
            tone_stop("write reply timeout");
        } else if (gTone.phase == FUXI_TONE_PCM_PREROLL && !gTone.clocked) {
            if (gTone.done >= FUXI_TONE_ZEROS) {
                if (fuxi_amp_tone_move(FUXI_TONE_READY, FUXI_TONE_CLOCKED)) {
                    gTone.clocked = true;
                    clock_interval_to_deadline(FUXI_TONE_WAIT_MS, NSEC_PER_MSEC, &gTone.deadline);
                    printf("FUXI AUDIO TONE: TDM clock running, %u periods of zeros played\n", gTone.done);
                } else {
                    tone_stop("amplifier gave up before the clock");
                }
            } else if (now >= gTone.deadline) {
                tone_stop("zeros did not play");
            }
        } else if (gTone.phase == FUXI_TONE_PCM_PREROLL) {
            if (state == FUXI_TONE_ON) {
                gTone.phase = FUXI_TONE_PCM_PLAY;
                gTone.tone_start = gTone.sent;
                printf("FUXI AUDIO TONE: amplifier on, tone starts at period %u\n", gTone.sent);
            } else if (state != FUXI_TONE_CLOCKED) {
                tone_stop("amplifier did not power up");
            } else if (now >= gTone.deadline) {
                tone_stop("amplifier power-up timeout");
            }
        } else if (gTone.phase == FUXI_TONE_PCM_PLAY) {
            if (state != FUXI_TONE_ON) {
                tone_stop("amplifier left ON during the tone");
            } else if (gTone.frame >= FUXI_TONE_FRAMES && gTone.done >= gTone.tone_end + FUXI_TONE_ZEROS) {
                if (fuxi_amp_tone_move(FUXI_TONE_ON, FUXI_TONE_DRAIN)) {
                    gTone.phase = FUXI_TONE_PCM_POSTROLL;
                    clock_interval_to_deadline(FUXI_TONE_WAIT_MS, NSEC_PER_MSEC, &gTone.deadline);
                    printf("FUXI AUDIO TONE: tone played, %u frames in periods %u-%u, amplifier powering down\n",
                        gTone.frame, gTone.tone_start, gTone.tone_end - 1);
                } else {
                    tone_stop("amplifier left ON during the tone");
                }
            }
        } else if (gTone.phase == FUXI_TONE_PCM_POSTROLL) {
            if (state == FUXI_TONE_OFF) {
                gTone.phase = FUXI_TONE_PCM_FINISH;
                clock_interval_to_deadline(FUXI_SPF_REPLY_MS, NSEC_PER_MSEC, &gTone.deadline);
            } else if (state != FUXI_TONE_DRAIN) {
                tone_stop("amplifier failed while powering down");
            } else if (now >= gTone.deadline) {
                tone_stop("amplifier power-down timeout");
            }
        }
    }
    if (gTone.phase == FUXI_TONE_PCM_FINISH) {
        if (gTone.done < gTone.sent && gSpf.up && !gGraph.link_lost && now < gTone.deadline) {
            return false;
        }
        printf("FUXI AUDIO TONE: %s%s%s periods sent=%u done=%u tone_frames=%u stray=%u status=%u amp_state=%u\n",
            gTone.error ? "failed" : "complete", gTone.why ? ": " : "", gTone.why ? gTone.why : "", gTone.sent, gTone.done,
            gTone.frame, gTone.stray, gTone.status, fuxi_amp_tone_get());
        gTone.phase = FUXI_TONE_PCM_OFF;
        return true;
    }
    while (gSpf.up && gTone.sent < gTone.done + FUXI_TONE_AHEAD) {
        const uint32_t index = gTone.sent % FUXI_TONE_PERIODS, frame = gTone.frame;
        const bool tone = gTone.phase == FUXI_TONE_PCM_PLAY && gTone.frame < FUXI_TONE_FRAMES;
        uint8_t packet[FUXI_SPF_INTENT_BYTES];
        const uint32_t length = fuxi_ar_write(packet, sizeof(packet), FUXI_SPF_PORT, FUXI_GRAPH_TOKEN + FUXI_GRAPH_WRITE,
            gGraph.dma_address + (uint64_t)index * FUXI_TONE_PERIOD_BYTES, gGraph.map_handle, FUXI_TONE_PERIOD_BYTES);
        if (!length || !tone_fill(index, tone) || sheng_adsp_send(gSpf.channel, packet, length) != 0) {
            gTone.frame = frame;
            break;
        }
        gGraph.queued |= GRAPH_SENT(FUXI_GRAPH_WRITE);
        gTone.sent++;
        if (tone && gTone.frame >= FUXI_TONE_FRAMES) {
            gTone.tone_end = gTone.sent;
        }
        clock_interval_to_deadline(FUXI_SPF_REPLY_MS, NSEC_PER_MSEC, &gGraph.deadline);
    }
    return false;
}

static void graph_receive(const uint8_t *data, uint32_t length)
{
    if (gGraph.step == FUXI_GRAPH_WRITE && gTone.phase >= FUXI_TONE_PCM_PREROLL) {
        tone_receive(data, length);
        return;
    }
    if (!gGraph.waiting || gGraph.reply_ready || gGraph.link_lost || !gSpf.up ||
        mach_absolute_time() >= gGraph.deadline) {
        return;
    }

    const uint32_t port = graph_steps[gGraph.step].port;
    const uint32_t completion = graph_steps[gGraph.step].completion;
    FuxiAudioPrepGprView view;
    if (length > FUXI_SPF_INTENT_BYTES || fuxi_audio_prep_gpr_decode(data, length, &view) ||
        view.fields.src_domain != FUXI_AUDIO_PREP_DOMAIN_ADSP ||
        view.fields.dst_domain != FUXI_AUDIO_PREP_DOMAIN_APPS ||
        view.fields.src_port != port || view.fields.dst_port != (port == FUXI_AR_PRM_IID ? port : FUXI_SPF_PORT) ||
        view.fields.token != FUXI_GRAPH_TOKEN + gGraph.step) {
        return;
    }
    if (view.fields.opcode == FUXI_AUDIO_PREP_BASIC_RESULT) {
        uint32_t result[2];
        if (view.payload_bytes != sizeof(result)) {
            return;
        }
        memcpy(result, view.payload, sizeof(result));
        if (result[0] != graph_steps[gGraph.step].opcode || (completion && result[1] == 0)) {
            return;
        }
        gGraph.status = result[1];
    } else if (!completion || view.fields.opcode != completion) {
        return;
    } else if (completion == FUXI_AR_OP_MEM_MAP_DONE && view.payload_bytes == sizeof(gGraph.map_handle)) {
        memcpy(&gGraph.map_handle, view.payload, sizeof(gGraph.map_handle));
        gGraph.status = gGraph.map_handle == 0;
    } else if (completion == FUXI_AR_OP_WRITE_DONE && view.payload_bytes >= 16) {
        uint32_t result[4];
        memcpy(result, view.payload, sizeof(result));
        const uint64_t returned_address = (uint64_t)result[1] << 32 | result[0];
        if (returned_address != gGraph.dma_address || result[2] != gGraph.map_handle) {
            return;
        }
        gGraph.status = result[3];
    } else if (port == FUXI_AR_PRM_IID && (view.payload_bytes == 4 || view.payload_bytes == 8)) {
        /* Earlier Fuxi GLINK accepts either status alone or the two-word reply. */
        memcpy(&gGraph.status, view.payload + view.payload_bytes - 4, sizeof(gGraph.status));
    } else {
        return;
    }
    gGraph.reply_ready = true;
}

static void graph_advance(bool failed)
{
    gGraph.failed |= failed;
    if (failed && gTone.phase == FUXI_TONE_PCM_GRAPH) {
        fuxi_amp_tone_abandon();
        gTone.phase = FUXI_TONE_PCM_OFF;
        printf("FUXI AUDIO TONE: speaker graph failed before the tone, amplifier released\n");
    }
    if (failed && gGraph.step < FUXI_GRAPH_STOP_PLAYBACK) {
        graph_set_step(FUXI_GRAPH_STOP_PLAYBACK);
    } else if (gGraph.step == FUXI_GRAPH_CONFIG_PLAYBACK && !gGraph.dma_address) {
        graph_set_step(FUXI_GRAPH_CLOSE_PLAYBACK);
    } else {
        graph_set_step(gGraph.step + 1);
    }
}

/* Runs on the ADSP owner thread. Clean up any resource that might have
 * started or opened after a command was queued, even after a lost reply.
 * The optional PCM transfer uses a boot-owned buffer of zeros.
 */
static void graph_poll(void)
{
    if (gGraph.step == FUXI_GRAPH_IDLE) {
        return;
    }
    if (gTone.phase == FUXI_TONE_PCM_GATE && !tone_gate()) {
        return;
    }
    const char *name = graph_steps[gGraph.step].name;
    if (tone_owns_write()) {
        if (!tone_pump()) {
            return;
        }
        graph_advance(gTone.error);
    } else if (gGraph.link_lost) {
        printf("FUXI AUDIO TDM: %s interrupted by GLINK disconnect\n", name);
        graph_advance(true);
    } else if (gGraph.reply_ready) {
        printf("FUXI AUDIO TDM: %s status=%u\n", name, gGraph.status);
        graph_advance(gGraph.status != 0);
    } else if (mach_absolute_time() >= gGraph.deadline) {
        printf("FUXI AUDIO TDM: %s %s timeout\n", name, gGraph.waiting ? "reply" : "queue");
        graph_advance(true);
    }

    while (gGraph.step >= FUXI_GRAPH_STOP_PLAYBACK && gGraph.step < FUXI_GRAPH_COMPLETE) {
        const uint32_t prerequisite = graph_steps[gGraph.step].prerequisite;
        if (gGraph.step == FUXI_GRAPH_UNMAP ? gGraph.map_handle != 0 : (gGraph.queued & prerequisite) != 0) {
            break;
        }
        graph_set_step(gGraph.step + 1);
    }
    if (gGraph.step == FUXI_GRAPH_COMPLETE) {
        if (gTone.phase != FUXI_TONE_PCM_OFF) {
            fuxi_amp_tone_abandon();
            gTone.phase = FUXI_TONE_PCM_OFF;
        }
        if (gGraph.dma_address) {
            fuxi_audio_dma_close();
            printf("FUXI AUDIO PCM: probe complete result=%s\n", gGraph.failed ? "failed" : "ok");
        }
        printf("FUXI AUDIO TDM: probe complete result=%s\n", gGraph.failed ? "failed" : "ok");
        gGraph.step = FUXI_GRAPH_IDLE;
        return;
    }
    if (!gSpf.up || gGraph.waiting) {
        return;
    }
    if (tone_owns_write()) {
        /* 剛從 START_PLAYBACK 進到 WRITE：同一輪就開始送靜音。 */
        if (tone_pump()) {
            graph_advance(gTone.error);
        }
        return;
    }

    uint8_t packet[FUXI_SPF_INTENT_BYTES];
    const uint32_t length = graph_packet(packet, sizeof(packet));
    if (length && sheng_adsp_send(gSpf.channel, packet, length) == 0) {
        gGraph.queued |= GRAPH_SENT(gGraph.step);
        gGraph.waiting = true;
        clock_interval_to_deadline(FUXI_SPF_REPLY_MS, NSEC_PER_MSEC, &gGraph.deadline);
    }
}

static int prm_send(bool enable)
{
    uint8_t packet[64];
    uint32_t token = FUXI_PRM_TOKEN + (enable ? 0u : 1u);
    uint32_t length = fuxi_ar_prm_vote(packet, sizeof(packet), FUXI_AR_PRM_IID,
        token, FUXI_PRM_LPASS_MACRO, enable);
    return length ? sheng_adsp_send(gSpf.channel, packet, length) : FuxiAudioPrepLength;
}

static void prm_receive(const uint8_t *data, uint32_t length)
{
    bool vote = gPrm.phase == FUXI_PRM_VOTE_REPLY;
    if ((!vote && gPrm.phase != FUXI_PRM_RELEASE_REPLY) || !gSpf.up ||
        gPrm.reply_ready || mach_absolute_time() >= gPrm.deadline) {
        return;
    }
    FuxiAudioPrepGprView view;
    if (length > FUXI_SPF_INTENT_BYTES || fuxi_audio_prep_gpr_decode(data, length, &view) ||
        view.fields.src_domain != FUXI_AUDIO_PREP_DOMAIN_ADSP ||
        view.fields.dst_domain != FUXI_AUDIO_PREP_DOMAIN_APPS ||
        view.fields.src_port != FUXI_AR_PRM_IID || view.fields.dst_port != FUXI_AR_PRM_IID ||
        view.fields.token != FUXI_PRM_TOKEN + (vote ? 0u : 1u)) {
        return;
    }
    uint32_t command = vote ? FUXI_AR_OP_PRM_REQUEST : FUXI_AR_OP_PRM_RELEASE;
    uint32_t response = vote ? FUXI_AR_OP_PRM_REQUEST_DONE : FUXI_AR_OP_PRM_RELEASE_DONE;
    if (view.fields.opcode == response && view.payload_bytes == 8) {
        memcpy(&gPrm.status, view.payload + sizeof(uint32_t), sizeof(gPrm.status));
        gPrm.reply_ready = true;
    } else if (view.fields.opcode == FUXI_AUDIO_PREP_BASIC_RESULT && view.payload_bytes == 8) {
        uint32_t words[2];
        memcpy(words, view.payload, sizeof(words));
        /* Basic result 的成功只表示 DSP 收到，仍等專用回覆。 */
        if (words[0] == command && words[1] != 0) {
            gPrm.status = words[1];
            gPrm.reply_ready = true;
        }
    }
}

static void prm_poll(void)
{
    if (gPrm.phase == FUXI_PRM_IDLE) {
        return;
    }
    bool expired = mach_absolute_time() >= gPrm.deadline;
    if (gPrm.phase == FUXI_PRM_VOTE_REPLY && (gPrm.reply_ready || expired)) {
        if (gPrm.reply_ready) {
            printf("FUXI AUDIO PRM: LPASS macro request status=%u\n", gPrm.status);
            if (gPrm.status) {
                gPrm.phase = FUXI_PRM_IDLE;
                return;
            }
        } else {
            printf("FUXI AUDIO PRM: LPASS macro request timeout, releasing possible vote\n");
        }
        gPrm.reply_ready = false;
        gPrm.phase = FUXI_PRM_QUEUE_RELEASE;
        clock_interval_to_deadline(FUXI_PRM_CLEANUP_MS, NSEC_PER_MSEC, &gPrm.deadline);
        expired = false;
    }
    if (gPrm.phase == FUXI_PRM_QUEUE_RELEASE) {
        if (expired) {
            printf("FUXI AUDIO PRM: release could not be queued\n");
            gPrm.phase = FUXI_PRM_IDLE;
        } else if (gSpf.up && !prm_send(false)) {
            gPrm.phase = FUXI_PRM_RELEASE_REPLY;
            clock_interval_to_deadline(FUXI_SPF_REPLY_MS, NSEC_PER_MSEC, &gPrm.deadline);
        }
    } else if (gPrm.phase == FUXI_PRM_RELEASE_REPLY) {
        if (gPrm.reply_ready) {
            printf("FUXI AUDIO PRM: LPASS macro release status=%u\n", gPrm.status);
            gPrm.phase = FUXI_PRM_IDLE;
        } else if (expired) {
            printf("FUXI AUDIO PRM: LPASS macro release unconfirmed\n");
            gPrm.phase = FUXI_PRM_IDLE;
        }
    }
}

static void channel_state(void *context, int up)
{
    (void)context;
    gSpf.up = up != 0;
    if (!up && gSpf.attempted && !gSpf.done)
        gSpf.disconnected = 1;
    if (!up && gGraph.step != FUXI_GRAPH_IDLE && gGraph.step < FUXI_GRAPH_STOP_PLAYBACK)
        gGraph.link_lost = true;
}

static void channel_receive(void *context, const uint8_t *data, uint32_t length)
{
    FuxiAudioPrepSpfReply reply;
    int result;
    (void)context;
    if (gPrm.phase != FUXI_PRM_IDLE) {
        prm_receive(data, length);
        return;
    }
    if (gGraph.step != FUXI_GRAPH_IDLE) {
        graph_receive(data, length);
        return;
    }
    if (!gSpf.waiting || gSpf.done || !gSpf.up || gSpf.disconnected || gSpf.reply_ready ||
        mach_absolute_time() >= gSpf.deadline)
        return;
    result = length > FUXI_SPF_INTENT_BYTES ? FuxiAudioPrepLength :
        fuxi_audio_prep_spf_reply(data, length, FUXI_SPF_PORT, gSpf.token, &reply);
    if (result) {
        if (gSpf.invalid < 4) {
            gSpf.rejected[gSpf.invalid].length = length;
            gSpf.rejected[gSpf.invalid].result = result;
            if (data && length >= 24 && length <= FUXI_SPF_INTENT_BYTES)
                memcpy(gSpf.rejected[gSpf.invalid].header, data, 24);
            gSpf.invalid++;
        }
        return;
    }
    if (reply.kind == FuxiAudioPrepSpfAccepted) {
        gSpf.accepted = 1;
    } else {
        gSpf.reply = reply;
        gSpf.reply_ready = 1;
    }
}

void fuxi_audio_spf_start(void)
{
    const ShengAdspClient client = {channel_state, channel_receive};
    if (gSpf.channel || gSpf.done)
        return;
    gSpf.format = format();
    if (!gSpf.format)
        return;
    gSpf.channel = sheng_adsp_channel("adsp_apps", FUXI_SPF_INTENT_BYTES, 20, 0, &client, NULL);
    if (!gSpf.channel) {
        gSpf.done = 1;
        printf("FUXI AUDIO SPF: channel registration failed\n");
        return;
    }
    clock_interval_to_deadline(15000, NSEC_PER_MSEC, &gSpf.deadline);
    printf("FUXI AUDIO SPF: registered adsp_apps %s client\n",
        gSpf.format >= 4 ? "with TDM probe" : gSpf.format == 3 ? "with PRM probe" : "query-only");
}

static void retry_or_stop(void)
{
    gSpf.waiting = 0;
    if (gSpf.queries >= FUXI_SPF_MAX_QUERIES) {
        gSpf.done = 1;
        printf("FUXI AUDIO SPF: not ready after %u queries (not ready %u, errors %u, timeouts %u, last state %u status %u)\n",
            gSpf.queries, gSpf.not_ready, gSpf.errors, gSpf.timeouts, gSpf.last_state, gSpf.last_status);
        return;
    }
    clock_interval_to_deadline(FUXI_SPF_GAP_MS, NSEC_PER_MSEC, &gSpf.next_query);
}

void fuxi_audio_spf_poll(void)
{
    uint8_t packet[40];
    size_t length = 0;
    int result;
    prm_poll();
    graph_poll();
    if (!gSpf.channel || gSpf.done)
        return;
    if (gSpf.disconnected) {
        gSpf.done = 1;
        printf("FUXI AUDIO SPF: channel closed before state reply\n");
        return;
    }
    while (gSpf.reported_invalid < gSpf.invalid) {
        unsigned index = gSpf.reported_invalid++;
        const uint32_t *header = gSpf.rejected[index].header;
        printf("FUXI AUDIO SPF: rejected reply %u error %d bytes %u header %#x route %#x ports %#x/%#x token %#x opcode %#x\n",
            index + 1, gSpf.rejected[index].result, gSpf.rejected[index].length,
            header[0], header[1], header[2], header[3], header[4], header[5]);
    }
    if (gSpf.accepted && !gSpf.reported_accepted) {
        gSpf.reported_accepted = 1;
        printf("FUXI AUDIO SPF: query accepted, awaiting state\n");
    }
    if (gSpf.reply_ready) {
        gSpf.reply_ready = 0;
        if (gSpf.reply.kind == FuxiAudioPrepSpfRemoteError) {
            gSpf.errors++;
            gSpf.last_status = gSpf.reply.status;
        } else {
            gSpf.last_state = gSpf.reply.state;
            if (gSpf.reply.state == 1) {
                gSpf.done = 1;
                gSpf.waiting = 0;
                printf("FUXI AUDIO SPF: state 1 after %u queries (not ready %u, errors %u, timeouts %u), playback untested\n",
                    gSpf.queries, gSpf.not_ready, gSpf.errors, gSpf.timeouts);
                if (gSpf.format == 3) {
                    result = prm_send(true);
                    if (result) {
                        printf("FUXI AUDIO PRM: vote queue failed %d\n", result);
                    } else {
                        gPrm.phase = FUXI_PRM_VOTE_REPLY;
                        clock_interval_to_deadline(FUXI_SPF_REPLY_MS, NSEC_PER_MSEC, &gPrm.deadline);
                    }
                } else if (gSpf.format == 4) {
                    fuxi_audio_dma_survey();
                    graph_set_step(FUXI_GRAPH_OPEN_SPEAKER);
                } else if (gSpf.format == 5) {
                    if (fuxi_audio_dma_open(&gGraph.dma_address)) {
                        graph_set_step(FUXI_GRAPH_VOTE_MACRO);
                        if (fuxi_amp_tone_get() != FUXI_TONE_IDLE) {
                            /* 功放 mode 7：圖先不開，等功放載入並核對保護韌體。 */
                            gTone.phase = FUXI_TONE_PCM_GATE;
                            clock_interval_to_deadline(FUXI_TONE_GATE_MS, NSEC_PER_MSEC, &gTone.deadline);
                            printf("FUXI AUDIO TONE: waiting for the bottom amplifier\n");
                        }
                    } else {
                        fuxi_amp_tone_abandon();
                        printf("FUXI AUDIO PCM: loader DMA handoff or live SMMU routes unavailable\n");
                    }
                }
                return;
            }
            gSpf.not_ready++;
        }
        if (!gSpf.reported_first) {
            gSpf.reported_first = 1;
            if (gSpf.reply.kind == FuxiAudioPrepSpfRemoteError)
                printf("FUXI AUDIO SPF: first answer remote error %u, retrying\n", gSpf.reply.status);
            else
                printf("FUXI AUDIO SPF: first answer state %u, retrying\n", gSpf.reply.state);
        }
        retry_or_stop();
        return;
    }
    if (gSpf.waiting) {
        if (!gSpf.up) {
            gSpf.done = 1;
            printf("FUXI AUDIO SPF: channel closed before state reply\n");
        } else if (mach_absolute_time() >= gSpf.deadline) {
            gSpf.timeouts++;
            retry_or_stop();
        }
        return;
    }
    if (!gSpf.attempted && mach_absolute_time() >= gSpf.deadline) {
        gSpf.done = 1;
        printf("FUXI AUDIO SPF: channel timeout\n");
        return;
    }
    if (!gSpf.up) {
        if (gSpf.attempted) {
            gSpf.done = 1;
            printf("FUXI AUDIO SPF: channel closed before state reply\n");
        }
        return;
    }
    if (gSpf.attempted && mach_absolute_time() < gSpf.next_query)
        return;
    gSpf.token = FUXI_SPF_TOKEN + gSpf.queries;
    if (gSpf.format == 2) {
        const FuxiAudioPrepGprFields fields = {
            FUXI_AUDIO_PREP_DOMAIN_ADSP, FUXI_AUDIO_PREP_DOMAIN_APPS,
            FUXI_SPF_PORT, FUXI_AUDIO_PREP_APM_PORT, gSpf.token, FUXI_AUDIO_PREP_GET_SPF_STATE
        };
        result = fuxi_audio_prep_gpr_encode(packet, sizeof(packet), &fields, NULL, 0, &length);
    } else {
        result = fuxi_audio_prep_spf_query(packet, sizeof(packet), FUXI_SPF_PORT, gSpf.token, &length);
    }
    if (!result && length == (gSpf.format == 2 ? 24u : 40u))
        result = sheng_adsp_send(gSpf.channel, packet, (uint32_t)length);
    else if (!result)
        result = FuxiAudioPrepLength;
    gSpf.attempted = 1;
    if (result) {
        gSpf.done = 1;
        printf("FUXI AUDIO SPF: query queue failed %d after %u queries\n", result, gSpf.queries);
        return;
    }
    gSpf.queries++;
    gSpf.waiting = 1;
    clock_interval_to_deadline(FUXI_SPF_REPLY_MS, NSEC_PER_MSEC, &gSpf.deadline);
    if (gSpf.queries == 1)
        printf("FUXI AUDIO SPF: state query queued bytes %u\n", (unsigned)length);
}
#endif
