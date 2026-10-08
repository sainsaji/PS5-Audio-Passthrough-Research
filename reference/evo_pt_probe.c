/*
 * evo_pt_probe.c - can an app module put a Dolby/DTS bitstream on HDMI? (#47)
 *
 * A temporary diagnostic, driven by `evo-remote.sh ptprobe <cmd>`. The
 * September research (docs/research/audio-passthrough.md) stopped at "the
 * output-mode switch is gated" without ever logging a return code. Reading the
 * 12.70 libSceAudioOut showed where the switch really goes:
 *
 *   sceAudioOutExConfigureOutput(0, flags, mode, target, opt)
 *     -> builds a 0x28-byte HDMI audio descriptor from `mode`
 *     -> the same internal routine as sceAudioOutSysConfigureOutputMode
 *     -> libSceAvSetting sceAvControlChangeOutputMode(0x7000, ...)
 *
 * `mode` (decoded from the jump table at 0x60e8c):
 *   0 AC-3 5.1   1 AAC 5.1   2 DTS 5.1   3 E-AC-3 7.1   4 code 0xf0 7.1
 *   5 LPCM 2ch   6 LPCM 6ch  7,8 LPCM 8ch  9 code 0x16 5.1  10 code 0xf3 7.1
 *   0xff = every field "don't care", the routine's reset path
 * `target` is 1..3 or 0xff (all outputs).
 *
 * Commands (each runs on its own thread and logs to evo.log):
 *   info                      HDMI monitor info (read only)
 *   mode <n> [target]         switch the output mode, log the return code
 *   reset                     mode 0xff
 *   stream <pt|main> <secs> <path>
 *                             demux the first AC-3 track of <path>, wrap each
 *                             frame in an IEC 61937 burst, and feed it to the
 *                             passthrough port (ExPtOpen) or a plain S16
 *                             stereo MAIN port for <secs> seconds
 *   sweep <path>              AC-3 mode, then six passthrough-port variants
 *                             (type 0/1, param 14/12, byte order), 8 s each
 *                             with 4 s gaps, then reset
 *   sony <path>               the sequence citroncore.elf uses: ExOpen(0xFF,
 *                             0), then ExConfigureOutput(0, 0, 0, 1, 0), 15 s
 *                             of AC-3 bursts, ExClose, reset
 *
 * Run it from the home screen with nothing playing.
 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavformat/avformat.h>

#include "evo_boot_trace.h"

int sceKernelDlsym(int moduleHandle, const char *symbol, void **addrOut);
int sceKernelLoadStartModule(const char *name, size_t argc, const void *argv,
                             unsigned int flags, void *opt, int *res);
int sceAudioOutOpen(int32_t userId, int32_t type, int32_t index,
                    uint32_t length, uint32_t freq, uint32_t param);
int sceAudioOutClose(int32_t handle);
int sceAudioOutOutput(int32_t handle, const void *ptr);
int sceKernelUsleep(unsigned int microseconds);

typedef int (*fn_get_monitor_info)(int type, void *out, unsigned size);
typedef int (*fn_ex_configure_output)(int zero, unsigned flags, int mode,
                                      int target, uint64_t opt);
typedef int (*fn_pt_open)(int user, int type, int index, unsigned len,
                          unsigned freq, unsigned param);
typedef int (*fn_pt_close)(int handle);
/* sceAudioOutExOpen(user, mode): internal type-6 port, S16 stereo, with the
 * grain and rate taken from `mode` (AC-3/AAC/DTS 256 @ 48 kHz, E-AC-3 and
 * the 7.1 codes 1024 @ 192 kHz). */
typedef int (*fn_ex_open)(int user, int mode);

#define PT_GRAIN        256              /* frames per sceAudioOutOutput */
#define AC3_FRAMES      1536             /* PCM frames one AC-3 frame spans */
#define BURST_BYTES     (AC3_FRAMES * 4) /* S16 stereo */

static volatile int g_pt_running;

static void *resolve(const char *name, const char *nid)
{
    static int mod;
    void *fn = NULL;
    if (mod <= 0) {
        int res = 0;
        mod = sceKernelLoadStartModule("libSceAudioOut.sprx", 0, NULL, 0, NULL, &res);
        if (mod <= 0)
            mod = sceKernelLoadStartModule("/system/common/lib/libSceAudioOut.sprx",
                                           0, NULL, 0, NULL, &res);
        evo_bt("ptprobe: libSceAudioOut handle=%#x", (unsigned)mod);
        if (mod <= 0)
            return NULL;
    }
    if (sceKernelDlsym(mod, name, &fn) == 0 && fn)
        return fn;
    fn = NULL;
    if (sceKernelDlsym(mod, nid, &fn) == 0 && fn)
        return fn;
    evo_bt("ptprobe: %s (%s) not found", name, nid);
    return NULL;
}

static void hexdump(const char *tag, const uint8_t *p, unsigned n)
{
    char line[3 * 32 + 1];
    for (unsigned off = 0; off < n; off += 32) {
        unsigned k = n - off < 32 ? n - off : 32, w = 0;
        for (unsigned i = 0; i < k; ++i)
            w += (unsigned)snprintf(line + w, sizeof line - w, "%02x ", p[off + i]);
        evo_bt("ptprobe: %s +%03x %s", tag, off, line);
    }
}

static void do_info(void)
{
    fn_get_monitor_info get = (fn_get_monitor_info)
        resolve("sceAudioOutSysGetHdmiMonitorInfo", "Tf9-yOJwF-A");
    if (!get)
        return;
    uint8_t buf[0x180];
    memset(buf, 0, sizeof buf);
    int rc = get(1, buf, sizeof buf);
    evo_bt("ptprobe: GetHdmiMonitorInfo(1) rc=%#x", (unsigned)rc);
    if (rc >= 0)
        hexdump("monitor", buf, sizeof buf);
}

static void do_mode(int mode, int target)
{
    fn_ex_configure_output cfg = (fn_ex_configure_output)
        resolve("sceAudioOutExConfigureOutput", "VcE+gXSwFXI");
    if (!cfg)
        return;
    int rc = cfg(0, 0, mode, target, 0);
    evo_bt("ptprobe: ExConfigureOutput(mode=%#x target=%#x) rc=%#x",
           (unsigned)mode, (unsigned)target, (unsigned)rc);
}

/* One AC-3 frame -> one 6144-byte IEC 61937 burst, as S16LE stereo samples.
 * Pc = data type 1 (AC-3) | bitstream mode << 8; Pd = payload length in bits;
 * the payload is carried as big-endian 16-bit words. */
static int pack_ac3_burst(const uint8_t *frame, int size, int16_t *out)
{
    if (size < 6 || size + 8 > BURST_BYTES || frame[0] != 0x0B || frame[1] != 0x77)
        return -1;
    uint16_t *w = (uint16_t *)out;
    memset(out, 0, BURST_BYTES);
    w[0] = 0xF872;
    w[1] = 0x4E1F;
    w[2] = (uint16_t)(0x0001 | ((frame[5] & 7) << 8));
    w[3] = (uint16_t)(size * 8);
    for (int i = 0; i < size; i += 2) {
        uint8_t hi = frame[i], lo = i + 1 < size ? frame[i + 1] : 0;
        w[4 + i / 2] = (uint16_t)((hi << 8) | lo);
    }
    return 0;
}

/* How a stream run opens its port and lays out the data. */
struct pt_variant {
    int use_pt;      /* 1 = ExPtOpen, 2 = ExOpen + switch after open (Sony's
                        citroncore order), 0 = a plain MAIN port */
    int pt_type;     /* ExPtOpen type: 0 -> internal 6, 1 -> internal 7 */
    int pt_param;    /* 14 = stereo carrier, 12 = 8ch carrier */
    int swap;        /* byte-swap every 16-bit word of the burst */
};

static void do_stream(const struct pt_variant *v, int secs, const char *path)
{
    const int use_pt = v->use_pt;
    const int chans  = use_pt == 1 && v->pt_param == 12 ? 8 : 2;
    fn_pt_open  pt_open  = NULL;
    fn_pt_close pt_close = NULL;
    fn_ex_open  ex_open  = NULL;
    if (use_pt == 2) {
        ex_open  = (fn_ex_open)resolve("sceAudioOutExOpen", "6X6dp+07h4U");
        pt_close = (fn_pt_close)resolve("sceAudioOutExClose", "0TfjSulCV2A");
        if (!ex_open)
            return;
    } else if (use_pt) {
        pt_open  = (fn_pt_open)resolve("sceAudioOutExPtOpen", "4UlW3CSuCa4");
        pt_close = (fn_pt_close)resolve("sceAudioOutExPtClose", "xjjhT5uw08o");
        if (!pt_open)
            return;
    }

    AVFormatContext *fmt = NULL;
    int rc = avformat_open_input(&fmt, path, NULL, NULL);
    if (rc < 0) {
        evo_bt("ptprobe: open '%s' rc=%d", path, rc);
        return;
    }
    avformat_find_stream_info(fmt, NULL);
    int si = -1;
    for (unsigned i = 0; i < fmt->nb_streams; ++i)
        if (fmt->streams[i]->codecpar->codec_id == AV_CODEC_ID_AC3) {
            si = (int)i;
            break;
        }
    if (si < 0) {
        evo_bt("ptprobe: no AC-3 track in '%s'", path);
        avformat_close_input(&fmt);
        return;
    }

    int h;
    if (use_pt == 2)
        h = ex_open(0xFF, v->pt_type);   /* pt_type carries the mode here */
    else if (use_pt)
        h = pt_open(0xFF, v->pt_type, 0, PT_GRAIN, 48000, (unsigned)v->pt_param);
    else
        h = sceAudioOutOpen(0xFF, 0, 0, PT_GRAIN, 48000, 1 /* S16_STEREO */);
    evo_bt("ptprobe: stream %s type=%d param=%d swap=%d port handle=%#x track=%d",
           use_pt == 2 ? "ex" : use_pt ? "pt" : "main",
           v->pt_type, v->pt_param, v->swap, (unsigned)h, si);
    if (h < 0) {
        avformat_close_input(&fmt);
        return;
    }
    /* citroncore opens the port first and only then switches the output;
     * pt_param carries the ExConfigureOutput target here */
    if (use_pt == 2)
        do_mode(v->pt_type, v->pt_param);

    int16_t *burst = (int16_t *)malloc(BURST_BYTES);
    /* one grain as the port wants it: the burst in channels 0/1, rest silent */
    int16_t *grain = (int16_t *)malloc((size_t)PT_GRAIN * chans * 2);
    AVPacket *pkt = av_packet_alloc();
    long bursts = 0, bad = 0, out_err = 0;
    const long want = (long)secs * 48000 / AC3_FRAMES;
    while (burst && grain && pkt && bursts < want && av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == si) {
            if (pack_ac3_burst(pkt->data, pkt->size, burst) == 0) {
                if (v->swap) {
                    uint16_t *w = (uint16_t *)burst;
                    for (int i = 0; i < BURST_BYTES / 2; ++i)
                        w[i] = (uint16_t)((w[i] >> 8) | (w[i] << 8));
                }
                for (int g = 0; g < AC3_FRAMES / PT_GRAIN; ++g) {
                    const int16_t *src = burst + g * PT_GRAIN * 2;
                    memset(grain, 0, (size_t)PT_GRAIN * chans * 2);
                    for (int f = 0; f < PT_GRAIN; ++f) {
                        grain[f * chans]     = src[f * 2];
                        grain[f * chans + 1] = src[f * 2 + 1];
                    }
                    int orc = sceAudioOutOutput(h, grain);
                    if (orc < 0 && out_err++ < 3)
                        evo_bt("ptprobe: Output rc=%#x", (unsigned)orc);
                }
                if (bursts++ == 0)
                    hexdump("burst0", (const uint8_t *)burst, 32);
            } else if (bad++ < 3) {
                evo_bt("ptprobe: not an AC-3 frame (size=%d)", pkt->size);
            }
        }
        av_packet_unref(pkt);
    }
    sceAudioOutOutput(h, NULL);   /* wait for the last block to play out */
    int crc = use_pt && pt_close ? pt_close(h) : sceAudioOutClose(h);
    evo_bt("ptprobe: stream done bursts=%ld bad=%ld out_err=%ld close=%#x",
           bursts, bad, out_err, (unsigned)crc);
    av_packet_free(&pkt);
    free(grain);
    free(burst);
    avformat_close_input(&fmt);
}

/* Every passthrough-port variant in turn under AC-3 mode, 8 s each with 4 s of
 * quiet between, so whoever watches the soundbar can name the one that lit up. */
static void do_sweep(const char *path)
{
    static const struct pt_variant k_variants[] = {
        { 1, 0, 14, 0 }, { 1, 0, 14, 1 }, { 1, 1, 14, 0 },
        { 1, 1, 14, 1 }, { 1, 0, 12, 0 }, { 1, 1, 12, 0 },
    };
    do_mode(0, 0xff);
    for (unsigned i = 0; i < sizeof k_variants / sizeof k_variants[0]; ++i) {
        sceKernelUsleep(4 * 1000 * 1000);
        evo_bt("ptprobe: VARIANT %u", i + 1);
        do_stream(&k_variants[i], 8, path);
    }
    do_mode(0xff, 0xff);
}

static void *probe_thread(void *arg)
{
    char *spec = (char *)arg;
    evo_bt("ptprobe: BEGIN '%s'", spec);

    if (strcmp(spec, "info") == 0) {
        do_info();
    } else if (strcmp(spec, "reset") == 0) {
        do_mode(0xff, 0xff);
    } else if (strncmp(spec, "mode ", 5) == 0) {
        char *end = NULL;
        int mode = (int)strtol(spec + 5, &end, 0);
        int target = 0xff;
        if (end && *end)
            target = (int)strtol(end, NULL, 0);
        do_mode(mode, target);
    } else if (strncmp(spec, "stream ", 7) == 0) {
        char *p = spec + 7, *end = NULL;
        int use_pt = strncmp(p, "pt ", 3) == 0;
        if (!use_pt && strncmp(p, "main ", 5) != 0) {
            evo_bt("ptprobe: stream wants pt|main");
        } else {
            p += use_pt ? 3 : 5;
            int secs = (int)strtol(p, &end, 10);
            while (end && *end == ' ')
                ++end;
            if (secs <= 0 || !end || !*end) {
                evo_bt("ptprobe: usage stream <pt|main> <secs> <path>");
            } else {
                const struct pt_variant v = { use_pt, 0, 14, 0 };
                do_stream(&v, secs, end);
            }
        }
    } else if (strncmp(spec, "sweep ", 6) == 0) {
        do_sweep(spec + 6);
    } else if (strncmp(spec, "sony ", 5) == 0) {
        /* citroncore's AC-3 row: ExOpen(0xFF, 0), then ExConfigureOutput
         * (0, 0, mode 0, target 1, 0), then IEC 61937 bursts */
        const struct pt_variant v = { 2, 0, 1, 0 };
        do_stream(&v, 15, spec + 5);
        do_mode(0xff, 0xff);
    } else {
        evo_bt("ptprobe: unknown '%s'", spec);
    }

    evo_bt("ptprobe: END");
    free(spec);
    g_pt_running = 0;
    return NULL;
}

void evo_pt_probe_run(const char *spec)
{
    pthread_t th;
    if (g_pt_running) {
        evo_bt("ptprobe: already running");
        return;
    }
    size_t n = strlen(spec ? spec : "") + 1;
    char *copy = (char *)malloc(n);
    if (!copy)
        return;
    memcpy(copy, spec ? spec : "", n);
    g_pt_running = 1;
    if (pthread_create(&th, NULL, probe_thread, copy) != 0) {
        g_pt_running = 0;
        free(copy);
        evo_bt("ptprobe: pthread_create failed");
        return;
    }
    pthread_detach(th);
}
