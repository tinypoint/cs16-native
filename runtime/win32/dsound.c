/*
 * dsound.c - DirectSound on SDL2 audio: IDirectSound and IDirectSoundBuffer,
 * with a software mixer.
 *
 * A secondary buffer is guest memory the game Locks and writes, as it would
 * be on Windows; the mixer (SDL's audio thread) reads it at the buffer's
 * frequency, volume and pan and moves its play cursor in real time, so a
 * game that streams into a looping buffer by watching GetCurrentPosition
 * keeps doing so. The primary buffer is only its format: everything is mixed
 * at the device's rate. One mutex covers the buffers' state, between the
 * mixer and the shims.
 *
 * With no audio device (SDL_AUDIODRIVER=dummy, a headless box) SDL's dummy
 * driver still runs the callback in real time, so the cursors still move.
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <SDL2/SDL.h>
#include "win32hle.h"

#define DS_OK               0u
#define DSERR_INVALIDPARAM  0x80070057u
#define DSERR_NODRIVER      0x88780078u
#define DSERR_INVALIDCALL   0x88780032u
#define DSERR_PRIOLEVELNEEDED 0x88780046u
#define DSERR_BUFFERLOST    0x88780096u
#define DSBCAPS_PRIMARYBUFFER 0x00000001u
#define DSBPLAY_LOOPING     0x00000001u
#define DSBSTATUS_PLAYING   0x00000001u
#define DSBSTATUS_LOOPING   0x00000004u

#define OUT_RATE 44100

typedef struct {
    uint8_t *data;              /* the samples, guest-visible */
    uint8_t *shadow;            /* audio-thread snapshot, shared by duplicates */
    int *data_refs;             /* shared with duplicates */
    uint32_t bytes, flags;
    int channels, bits, rate, original_rate, block;
    uint32_t lock_offset,lock_bytes;int locked;
    int playing, looping, primary;
    double pos;                 /* the play cursor, in frames */
    int volume, pan;            /* hundredths of a dB; -10000..10000 */
    float gl, gr;
} dsbuf;

static pthread_mutex_t g_mix = PTHREAD_MUTEX_INITIALIZER;
#define MAX_BUFS 256
static dsbuf *g_buf[MAX_BUFS];
static SDL_AudioDeviceID g_dev;
static int g_audio_ok = -1;
static float g_master = 1.0f;
void hle_dsound_set_master(float gain) { pthread_mutex_lock(&g_mix);g_master=gain;pthread_mutex_unlock(&g_mix); }

static void gains(dsbuf *b) {
    float v = b->volume <= -10000 ? 0.0f : powf(10.0f, (float)b->volume / 2000.0f);
    float l = 1.0f, r = 1.0f;
    if (b->pan < 0) r = b->pan <= -10000 ? 0.0f : powf(10.0f, (float)b->pan / 2000.0f);
    if (b->pan > 0) l = b->pan >= 10000 ? 0.0f : powf(10.0f, (float)-b->pan / 2000.0f);
    b->gl = v * l, b->gr = v * r;
}

static inline float sample(const dsbuf *b, uint32_t frame, int ch) {
    const uint8_t *p = b->shadow + (size_t)frame * (uint32_t)b->block + (b->channels > 1 ? (size_t)ch * (uint32_t)(b->bits / 8) : 0);
    return b->bits == 16 ? (float)*(const int16_t *)p / 32768.0f : ((float)*p - 128.0f) / 128.0f;
}

static int audio_open(void);
static int audio_open_once(void) { return audio_open(); }

/* Host streams: PCM a host pushes (a movie's sound), mixed with the
 * buffers. A ring of interleaved s16 at the stream's rate, played at that
 * rate. */
#define MAX_STREAMS 4
#define STREAM_FRAMES (1 << 17)
static struct { int used, channels, rate; float gain; int16_t *ring; uint32_t rd, wr; double pos; } g_stream[MAX_STREAMS];
int hle_audio_stream_open(int rate, int channels) {
    if (!audio_open_once()) return -1;
    pthread_mutex_lock(&g_mix);
    int id = -1;
    for (int i = 0; i < MAX_STREAMS && id < 0; i++)
        if (!g_stream[i].used) {
            id = i;
            g_stream[i].ring = (int16_t *)calloc(STREAM_FRAMES * 2, 2);
            g_stream[i].used = 1, g_stream[i].channels = channels, g_stream[i].rate = rate, g_stream[i].gain = 1.0f;
            g_stream[i].rd = g_stream[i].wr = 0, g_stream[i].pos = 0;
        }
    pthread_mutex_unlock(&g_mix);
    return id;
}
void hle_audio_stream_push(int id, const int16_t *pcm, int frames) {
    if (id < 0 || id >= MAX_STREAMS) return;
    pthread_mutex_lock(&g_mix);
    for (int f = 0; f < frames && g_stream[id].wr - g_stream[id].rd < STREAM_FRAMES - 1; f++, g_stream[id].wr++) {
        uint32_t k = g_stream[id].wr % STREAM_FRAMES;
        g_stream[id].ring[2 * k] = pcm[f * g_stream[id].channels];
        g_stream[id].ring[2 * k + 1] = pcm[f * g_stream[id].channels + (g_stream[id].channels > 1)];
    }
    pthread_mutex_unlock(&g_mix);
}
int hle_audio_stream_queued(int id) {                    /* frames not yet played */
    if (id < 0 || id >= MAX_STREAMS) return 0;
    pthread_mutex_lock(&g_mix);
    int n = (int)(g_stream[id].wr - g_stream[id].rd);
    pthread_mutex_unlock(&g_mix);
    return n;
}
void hle_audio_stream_gain(int id, float gain) { if (id >= 0 && id < MAX_STREAMS) g_stream[id].gain = gain; }
void hle_audio_stream_close(int id) {
    if (id < 0 || id >= MAX_STREAMS) return;
    pthread_mutex_lock(&g_mix);
    free(g_stream[id].ring);
    memset(&g_stream[id], 0, sizeof g_stream[id]);
    pthread_mutex_unlock(&g_mix);
}

static void mix(void *ud, Uint8 *stream, int len) {
    (void)ud;
    memset(stream,0,(size_t)len);
    int frames = len / 4;
    static float acc[2 * 8192];
    if (frames > 8192) frames = 8192;
    memset(acc, 0, sizeof(float) * 2 * (size_t)frames);
    pthread_mutex_lock(&g_mix);
    for (int i = 0; i < MAX_BUFS; i++) {
        dsbuf *b = g_buf[i];
        if (!b || !b->playing || b->primary || !b->block) continue;
        uint32_t nframes = b->bytes / (uint32_t)b->block;
        double step = (double)b->rate / OUT_RATE;
        for (int f = 0; f < frames && b->playing; f++) {
            uint32_t at = (uint32_t)b->pos;
            if (at >= nframes) {
                if (b->looping) { b->pos=fmod(b->pos,nframes); at=(uint32_t)b->pos; }
                else { b->playing = 0; b->pos = 0; break; }
            }
            float l = sample(b, at, 0), r = b->channels > 1 ? sample(b, at, 1) : l;
            acc[2 * f] += l * b->gl;
            acc[2 * f + 1] += r * b->gr;
            b->pos += step;
        }
    }
    for (int i = 0; i < MAX_STREAMS; i++) {
        if (!g_stream[i].used) continue;
        double step = (double)g_stream[i].rate / OUT_RATE;
        for (int f = 0; f < frames && g_stream[i].rd < g_stream[i].wr; f++) {
            uint32_t k = g_stream[i].rd % STREAM_FRAMES;
            acc[2 * f] += g_stream[i].ring[2 * k] / 32768.0f * g_stream[i].gain;
            acc[2 * f + 1] += g_stream[i].ring[2 * k + 1] / 32768.0f * g_stream[i].gain;
            g_stream[i].pos += step;
            while (g_stream[i].pos >= 1.0 && g_stream[i].rd < g_stream[i].wr) g_stream[i].pos -= 1.0, g_stream[i].rd++;
        }
    }
    float master=g_master;
    pthread_mutex_unlock(&g_mix);
    int16_t *out = (int16_t *)stream;
    for (int k = 0; k < 2 * frames; k++) {
        float v = acc[k] * master * 32767.0f;
        out[k] = (int16_t)(v > 32767.0f ? 32767 : v < -32768.0f ? -32768 : v);
    }
}

static int audio_open(void) {
    if (g_audio_ok >= 0) return g_audio_ok;
    g_audio_ok = 0;
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");      /* SIGINT and SIGTERM end the process, not WM_CLOSE */
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) { fprintf(stderr, "[dsound] no audio: %s\n", SDL_GetError()); return 0; }
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = OUT_RATE, want.format = AUDIO_S16SYS, want.channels = 2, want.samples = 1024, want.callback = mix;
    g_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!g_dev) { fprintf(stderr, "[dsound] no audio device: %s\n", SDL_GetError()); return 0; }
    SDL_PauseAudioDevice(g_dev, 0);
    fprintf(stderr, "[dsound] %s, %d Hz\n", SDL_GetCurrentAudioDriver(), have.freq);
    return g_audio_ok = 1;
}

/* the object: vtable, refs, then the buffer slot */
#define SLOT(self) MEM32((self) + 8)
static dsbuf *buf_of(uint32_t self) { if(!gm_accessible(self,12,0))return NULL;uint32_t i = SLOT(self); return i < MAX_BUFS ? g_buf[i] : NULL; }

static uint32_t g_vt_ds, g_vt_buf;
static uint32_t new_buffer(dsbuf *b) {
    pthread_mutex_lock(&g_mix);
    int slot = -1;
    for (int i = 0; i < MAX_BUFS && slot < 0; i++) if (!g_buf[i]) slot = i;
    if (slot >= 0) g_buf[slot] = b;
    pthread_mutex_unlock(&g_mix);
    if (slot < 0) return 0;
    uint32_t o = hle_com_new(g_vt_buf, 12);
    if(!o){pthread_mutex_lock(&g_mix);g_buf[slot]=NULL;pthread_mutex_unlock(&g_mix);return 0;}
    SLOT(o) = (uint32_t)slot;
    return o;
}

static void sound_query(int buffer){
 uint32_t self=A32(0),out=A32(2);if(!gm_accessible(out,4,1))RET(DSERR_INVALIDPARAM,3);MEM32(out)=0;
 if(!gm_accessible(A32(1),16,0))RET(DSERR_INVALIDPARAM,3);
 static const uint8_t unknown[16]={0,0,0,0,0,0,0,0,0xc0,0,0,0,0,0,0,0x46};
 static const uint8_t sound[16]={0x83,0xfa,0x9a,0x27,0x81,0x49,0xce,0x11,0xa5,0x21,0,0x20,0xaf,0x0b,0xe5,0x60};
 uint8_t wanted[16];memcpy(wanted,sound,16);if(buffer)wanted[0]=0x85;
 if(memcmp(APTR(1),unknown,16)&&memcmp(APTR(1),wanted,16))RET(0x80004002u,3);
 if(!gm_accessible(self,12,1)||(buffer&&!buf_of(self)))RET(DSERR_INVALIDPARAM,3);MEM32(self+4)++;MEM32(out)=self;RET(DS_OK,3);
}
static void sound_query_device(void){sound_query(0);}
static void sound_query_buffer(void){sound_query(1);}
static int format_valid(uint32_t w){
 if(!gm_accessible(w,16,0))return 0;uint32_t ch=MEM16(w+2),bits=MEM16(w+14),rate=MEM32(w+4),block=MEM16(w+12);
 return MEM16(w)==1&&(ch==1||ch==2)&&(bits==8||bits==16)&&rate>=100&&rate<=100000&&block==ch*(bits/8)&&MEM32(w+8)==rate*block;
}
/* ---- IDirectSoundBuffer ---- */
static void b_Release(void) {
    uint32_t self = A32(0), n = --MEM32(self + 4);
    if (!n) {
        pthread_mutex_lock(&g_mix);
        dsbuf *b = buf_of(self);
        g_buf[SLOT(self)] = NULL;
        pthread_mutex_unlock(&g_mix);
        if (b) {
            if (--*b->data_refs == 0) { hle_free(b->data); free(b->shadow); free(b->data_refs); }
            free(b);
        }
        gm_free(gm_ptr(self));
    }
    RET(n, 1);
}
static void b_GetCaps(void) {                            /* (this, &DSBCAPS) */
    dsbuf *b = buf_of(A32(0));
    uint32_t c = A32(1);
    if (!b || !gm_accessible(c,20,1)||MEM32(c)<20) RET(DSERR_INVALIDPARAM, 2);
    MEM32(c + 4) = b->flags | 0x8u /* LOCSOFTWARE */, MEM32(c + 8) = b->bytes, MEM32(c + 12) = 0, MEM32(c + 16) = 0;
    RET(DS_OK, 2);
}
static void b_GetCurrentPosition(void) {                 /* (this, &play, &write) */
    dsbuf *b = buf_of(A32(0));
    if (!b) RET(DSERR_INVALIDPARAM, 3);
    if((A32(1)&&!gm_accessible(A32(1),4,1))||(A32(2)&&!gm_accessible(A32(2),4,1)))RET(DSERR_INVALIDPARAM,3);
    pthread_mutex_lock(&g_mix);
    memcpy(b->shadow,b->data,b->bytes);
    int playing=b->playing;
    uint32_t play = b->block ? (uint32_t)b->pos * (uint32_t)b->block : 0;
    uint32_t lead = b->block ? (uint32_t)(b->rate / 100) * (uint32_t)b->block : 0;   /* 10 ms ahead */
    pthread_mutex_unlock(&g_mix);
    if (b->bytes) play %= b->bytes;
    if (A32(1)) MEM32(A32(1)) = play;
    if (A32(2)) MEM32(A32(2)) = b->bytes ? (play + (playing ? lead : 0)) % b->bytes : 0;
    RET(DS_OK, 3);
}
static void b_GetFormat(void) {                          /* (this, &wfx, size, &written) */
    dsbuf *b = buf_of(A32(0));
    uint32_t w = A32(1);
    if (!b) RET(DSERR_INVALIDPARAM, 4);
    if((w&&(A32(2)<18||!gm_accessible(w,18,1)))||(A32(3)&&!gm_accessible(A32(3),4,1)))RET(DSERR_INVALIDPARAM,4);
    if (w && A32(2) >= 18) {
        MEM16(w) = 1, MEM16(w + 2) = (uint16_t)b->channels, MEM32(w + 4) = (uint32_t)b->rate;
        MEM32(w + 8) = (uint32_t)(b->rate * b->block), MEM16(w + 12) = (uint16_t)b->block, MEM16(w + 14) = (uint16_t)b->bits;
        if (A32(2) >= 18) MEM16(w + 16) = 0;
    }
    if (A32(3)) MEM32(A32(3)) = 18;
    RET(DS_OK, 4);
}
static void b_GetVolume(void)    { dsbuf *b = buf_of(A32(0)); if (A32(1) && b) MEM32(A32(1)) = (uint32_t)b->volume; RET(b ? DS_OK : DSERR_INVALIDPARAM, 2); }
static void b_GetPan(void)       { dsbuf *b = buf_of(A32(0)); if (A32(1) && b) MEM32(A32(1)) = (uint32_t)b->pan; RET(b ? DS_OK : DSERR_INVALIDPARAM, 2); }
static void b_GetFrequency(void) { dsbuf *b = buf_of(A32(0)); if (A32(1) && b) MEM32(A32(1)) = (uint32_t)b->rate; RET(b ? DS_OK : DSERR_INVALIDPARAM, 2); }
static void b_GetStatus(void){dsbuf *b=buf_of(A32(0));if(!b||!gm_accessible(A32(1),4,1))RET(DSERR_INVALIDPARAM,2);pthread_mutex_lock(&g_mix);MEM32(A32(1))=(b->playing?DSBSTATUS_PLAYING:0)|(b->playing&&b->looping?DSBSTATUS_LOOPING:0);pthread_mutex_unlock(&g_mix);RET(DS_OK,2);}
static void b_Initialize(void) { RET(DSERR_INVALIDCALL, 3); }   /* already initialized */
static void b_Lock(void) {                               /* (this, offset, bytes, &p1, &n1, &p2, &n2, flags) */
    dsbuf *b = buf_of(A32(0));
    if (!b || b->primary) RET(b ? DSERR_PRIOLEVELNEEDED : DSERR_INVALIDPARAM, 8);
    uint32_t off = A32(1), n = A32(2), fl = A32(7);
    if (fl & 2u) off = 0, n = b->bytes;                  /* DSBLOCK_ENTIREBUFFER */
    else if (fl & 1u) {                                  /* DSBLOCK_FROMWRITECURSOR */
        pthread_mutex_lock(&g_mix);
        off = b->block ? ((uint32_t)b->pos * (uint32_t)b->block + (uint32_t)(b->rate / 100) * (uint32_t)b->block) % b->bytes : 0;
        pthread_mutex_unlock(&g_mix);
    }
    if ((fl&~3u)||b->locked||!n||off>=b->bytes||n>b->bytes||!gm_accessible(A32(3),4,1)||!gm_accessible(A32(4),4,1)||(A32(5)&&!gm_accessible(A32(5),4,1))||(A32(6)&&!gm_accessible(A32(6),4,1)))RET(DSERR_INVALIDPARAM,8);
    uint32_t n1 = off + n <= b->bytes ? n : b->bytes - off;
    if(n1<n&&(!A32(5)||!A32(6)))RET(DSERR_INVALIDPARAM,8);
    b->locked=1;b->lock_offset=off;b->lock_bytes=n;
    if (A32(3)) MEM32(A32(3)) = gm_va(b->data+off);
    if (A32(4)) MEM32(A32(4)) = n1;
    if (A32(5)) MEM32(A32(5)) = n1 < n ? gm_va(b->data) : 0;
    if (A32(6)) MEM32(A32(6)) = n - n1;
    RET(DS_OK, 8);
}
static void b_Unlock(void) {
 dsbuf *b=buf_of(A32(0));if(!b||!b->locked)RET(DSERR_INVALIDPARAM,5);uint32_t n1=b->lock_bytes;if(n1>b->bytes-b->lock_offset)n1=b->bytes-b->lock_offset;
 if(A32(1)!=gm_va(b->data+b->lock_offset)||A32(2)!=n1||A32(4)!=b->lock_bytes-n1||(A32(4)&&A32(3)!=gm_va(b->data)))RET(DSERR_INVALIDPARAM,5);
 pthread_mutex_lock(&g_mix);memcpy(b->shadow,b->data,b->bytes);b->locked=0;pthread_mutex_unlock(&g_mix);RET(DS_OK,5);
}            /* (this, p1, n1, p2, n2): the memory is the buffer */
static void b_Play(void) {                               /* (this, reserved, priority, flags) */
    dsbuf *b = buf_of(A32(0));
    if (!b) RET(DSERR_INVALIDPARAM, 4);
    pthread_mutex_lock(&g_mix);
    memcpy(b->shadow,b->data,b->bytes);
    b->playing = 1, b->looping = (A32(3) & DSBPLAY_LOOPING) != 0;
    pthread_mutex_unlock(&g_mix);
    RET(DS_OK, 4);
}
static void b_SetCurrentPosition(void) {
    dsbuf *b = buf_of(A32(0));
    if (!b || !b->block) RET(DSERR_INVALIDPARAM, 2);
    pthread_mutex_lock(&g_mix);
    b->pos = (double)((A32(1) % (b->bytes ? b->bytes : 1)) / (uint32_t)b->block);
    pthread_mutex_unlock(&g_mix);
    RET(DS_OK, 2);
}
static void b_SetFormat(void) {                          /* the primary's: the mix is at the device rate anyway */
    dsbuf *b = buf_of(A32(0));
    uint32_t w = A32(1);
    if(!b||!b->primary||!format_valid(w))RET(DSERR_INVALIDPARAM,2);
    pthread_mutex_lock(&g_mix);b->channels=MEM16(w+2);b->rate=b->original_rate=(int)MEM32(w+4);b->bits=MEM16(w+14);b->block=MEM16(w+12);pthread_mutex_unlock(&g_mix);
    RET(DS_OK, 2);
}
static void b_SetVolume(void) {
    dsbuf *b = buf_of(A32(0));
    if (!b) RET(DSERR_INVALIDPARAM, 2);
    pthread_mutex_lock(&g_mix);
    if((int32_t)A32(1)>0||(int32_t)A32(1)<-10000){pthread_mutex_unlock(&g_mix);RET(DSERR_INVALIDPARAM,2);}
    b->volume = (int)A32(1);
    gains(b);
    pthread_mutex_unlock(&g_mix);
    RET(DS_OK, 2);
}
static void b_SetPan(void) {
    dsbuf *b = buf_of(A32(0));
    if (!b) RET(DSERR_INVALIDPARAM, 2);
    pthread_mutex_lock(&g_mix);
    if((int32_t)A32(1)<-10000||(int32_t)A32(1)>10000){pthread_mutex_unlock(&g_mix);RET(DSERR_INVALIDPARAM,2);}
    b->pan = (int)A32(1);
    gains(b);
    pthread_mutex_unlock(&g_mix);
    RET(DS_OK, 2);
}
static void b_SetFrequency(void) {
    dsbuf *b = buf_of(A32(0));
    if (!b) RET(DSERR_INVALIDPARAM, 2);
    pthread_mutex_lock(&g_mix);
    if(A32(1)&&(A32(1)<100||A32(1)>100000)){pthread_mutex_unlock(&g_mix);RET(DSERR_INVALIDPARAM,2);}
    b->rate=A32(1)?(int)A32(1):b->original_rate;
    pthread_mutex_unlock(&g_mix);
    RET(DS_OK, 2);
}
static void b_Stop(void) {
    dsbuf *b = buf_of(A32(0));
    if (!b) RET(DSERR_INVALIDPARAM, 1);
    pthread_mutex_lock(&g_mix);
    b->playing = 0;
    pthread_mutex_unlock(&g_mix);
    RET(DS_OK, 1);
}
static void b_Restore(void) { RET(DS_OK, 1); }

/* ---- IDirectSound ---- */
static void d_Release(void) {
    uint32_t self = A32(0), n = --MEM32(self + 4);
    if (!n) gm_free(gm_ptr(self));
    RET(n, 1);
}
static void d_CreateSoundBuffer(void) {                  /* (this, &DSBUFFERDESC, &out, outer) */
    uint32_t d = A32(1), out = A32(2);
    if(!gm_accessible(out,4,1))RET(DSERR_INVALIDPARAM,4);MEM32(out)=0;
    if(!gm_accessible(d,20,0)||MEM32(d)<20||A32(3))RET(DSERR_INVALIDPARAM,4);
    uint32_t flags = MEM32(d + 4), bytes = MEM32(d + 8), wfx = MEM32(d + 16);
    if((flags&DSBCAPS_PRIMARYBUFFER)&&wfx)RET(DSERR_INVALIDPARAM,4);
    if((flags&0x10u)||(!(flags&DSBCAPS_PRIMARYBUFFER)&&(!bytes||bytes>(16u<<20)||!format_valid(wfx))))RET(DSERR_INVALIDPARAM,4);
    dsbuf *b = (dsbuf *)calloc(1, sizeof *b);if(!b)RET(0x8007000e,4);
    b->flags = flags, b->primary = (flags & DSBCAPS_PRIMARYBUFFER) != 0;
    b->channels = 2, b->bits = 16, b->rate = 22050, b->block = 4;
    if (wfx) {
        b->channels = MEM16(wfx + 2), b->rate = (int)MEM32(wfx + 4), b->block = MEM16(wfx + 12), b->bits = MEM16(wfx + 14);
        if (MEM16(wfx) != 1 || (b->bits != 8 && b->bits != 16) || b->channels < 1 || b->channels > 2) {
            fprintf(stderr, "[dsound] buffer format tag %u, %d bits, %d channels: not PCM this mixer plays\n", MEM16(wfx), b->bits, b->channels);
            free(b);
            RET(DSERR_INVALIDPARAM, 4);
        }
    }
    if (b->primary) bytes = 4096 * 4;
    if(bytes%(uint32_t)b->block){free(b);RET(DSERR_INVALIDPARAM,4);}
    b->original_rate=b->rate;b->bytes = bytes;
    b->data = (uint8_t *)hle_alloc(bytes ? bytes : 4);
    b->shadow=calloc(1,bytes);if(!b->data||!b->shadow){if(b->data)hle_free(b->data);free(b->shadow);free(b);RET(0x8007000e,4);}
    if (b->bits == 8) memset(b->data, 128, bytes);memcpy(b->shadow,b->data,bytes);
    b->data_refs = (int *)malloc(sizeof(int));
    if(!b->data_refs){hle_free(b->data);free(b->shadow);free(b);RET(0x8007000e,4);}
    *b->data_refs = 1;
    gains(b);
    uint32_t o = new_buffer(b);
    if (!o) { hle_free(b->data); free(b->shadow); free(b->data_refs); free(b); RET(DSERR_INVALIDPARAM, 4); }
    MEM32(out) = o;
    RET(DS_OK, 4);
}
static void d_GetCaps(void){uint32_t c=A32(1);if(!gm_accessible(c,96,1)||MEM32(c)<96)RET(DSERR_INVALIDPARAM,2);memset(gm_ptr(c+4),0,92);MEM32(c+4)=0xf3f;MEM32(c+8)=100;MEM32(c+12)=100000;MEM32(c+16)=1;RET(DS_OK,2);}
static void d_DuplicateSoundBuffer(void) {               /* (this, original, &out) */
    dsbuf *src = buf_of(A32(1));
    if(!gm_accessible(A32(2),4,1))RET(DSERR_INVALIDPARAM,3);MEM32(A32(2))=0;if(!src||src->primary)RET(DSERR_INVALIDPARAM,3);
    dsbuf *b = (dsbuf *)malloc(sizeof *b);if(!b)RET(0x8007000e,3);
    pthread_mutex_lock(&g_mix);
    *b = *src;
    (*b->data_refs)++;
    pthread_mutex_unlock(&g_mix);
    b->playing = 0, b->pos = 0;b->locked=0;
    uint32_t o = new_buffer(b);
    MEM32(A32(2)) = o;
    if(!o){--*b->data_refs;free(b);}
    RET(o ? DS_OK : DSERR_INVALIDPARAM, 3);
}
static void d_SetCooperativeLevel(void){int l,t,r,b;if(!hle_window_screen_rect(A32(1),&l,&t,&r,&b)||A32(2)<1||A32(2)>2)RET(DSERR_INVALIDPARAM,3);MEM32(A32(0)+8)=A32(2);RET(DS_OK,3);}
static void d_Compact(void) { RET(DS_OK, 1); }
static void d_GetSpeakerConfig(void){if(!gm_accessible(A32(1),4,1))RET(DSERR_INVALIDPARAM,2);MEM32(A32(1))=4;RET(DS_OK,2);}   /* DSSPEAKER_STEREO */
static void d_SetSpeakerConfig(void){RET(A32(1)==4?DS_OK:DSERR_INVALIDPARAM,2);}
static void d_Initialize(void) { RET(DSERR_INVALIDCALL, 2); }

static void vtables(void) {
    if (g_vt_ds) return;
    static const win32hle_shim ds[] = {
        { "IDirectSound::QueryInterface", sound_query_device }, { "IDirectSound::AddRef", hle_com_AddRef },
        { "IDirectSound::Release", d_Release }, { "IDirectSound::CreateSoundBuffer", d_CreateSoundBuffer },
        { "IDirectSound::GetCaps", d_GetCaps }, { "IDirectSound::DuplicateSoundBuffer", d_DuplicateSoundBuffer },
        { "IDirectSound::SetCooperativeLevel", d_SetCooperativeLevel }, { "IDirectSound::Compact", d_Compact },
        { "IDirectSound::GetSpeakerConfig", d_GetSpeakerConfig }, { "IDirectSound::SetSpeakerConfig", d_SetSpeakerConfig },
        { "IDirectSound::Initialize", d_Initialize }, { 0, 0 } };
    static const win32hle_shim buf[] = {
        { "IDirectSoundBuffer::QueryInterface", sound_query_buffer }, { "IDirectSoundBuffer::AddRef", hle_com_AddRef },
        { "IDirectSoundBuffer::Release", b_Release }, { "IDirectSoundBuffer::GetCaps", b_GetCaps },
        { "IDirectSoundBuffer::GetCurrentPosition", b_GetCurrentPosition }, { "IDirectSoundBuffer::GetFormat", b_GetFormat },
        { "IDirectSoundBuffer::GetVolume", b_GetVolume }, { "IDirectSoundBuffer::GetPan", b_GetPan },
        { "IDirectSoundBuffer::GetFrequency", b_GetFrequency }, { "IDirectSoundBuffer::GetStatus", b_GetStatus },
        { "IDirectSoundBuffer::Initialize", b_Initialize }, { "IDirectSoundBuffer::Lock", b_Lock },
        { "IDirectSoundBuffer::Play", b_Play }, { "IDirectSoundBuffer::SetCurrentPosition", b_SetCurrentPosition },
        { "IDirectSoundBuffer::SetFormat", b_SetFormat }, { "IDirectSoundBuffer::SetVolume", b_SetVolume },
        { "IDirectSoundBuffer::SetPan", b_SetPan }, { "IDirectSoundBuffer::SetFrequency", b_SetFrequency },
        { "IDirectSoundBuffer::Stop", b_Stop }, { "IDirectSoundBuffer::Unlock", b_Unlock },
        { "IDirectSoundBuffer::Restore", b_Restore }, { 0, 0 } };
    g_vt_ds = hle_com_vtable(ds);
    g_vt_buf = hle_com_vtable(buf);
}

static void s_DirectSoundCreate(void) {                  /* (guid, &out, outer) */
    vtables();
    if (!gm_accessible(A32(1),4,1)) RET(DSERR_INVALIDPARAM, 3);MEM32(A32(1))=0;
    if(A32(2)||A32(0))RET(DSERR_INVALIDPARAM,3);
    if (!audio_open()) { MEM32(A32(1)) = 0; RET(DSERR_NODRIVER, 3); }
    MEM32(A32(1)) = hle_com_new(g_vt_ds, 12);if(!MEM32(A32(1)))RET(0x8007000e,3);
    RET(DS_OK, 3);
}
static void s_DirectSoundEnumerateA(void) {              /* (callback, context): the one device */
    char *desc=gm_strdup("Primary Sound Driver"),*mod=gm_strdup("");
    uint32_t a[4] = { 0, gm_va(desc), gm_va(mod), A32(1) };
    if (A32(0)) hle_call_guest(A32(0), 4, a);
    gm_free(desc);gm_free(mod);
    RET(DS_OK, 2);
}

const win32hle_shim win32hle_dsound[] = {
    { "dsound.dll#1", s_DirectSoundCreate }, { "DirectSoundCreate", s_DirectSoundCreate },
    { "dsound.dll#2", s_DirectSoundEnumerateA }, { "DirectSoundEnumerateA", s_DirectSoundEnumerateA },
    { 0, 0 }
};

/* Deterministic PCM checks plus a live callback/cursor check on the real driver.
 * The manually rendered fixture is never sent to the physical output device. */
#include <unistd.h>
#define A_CHECK(c) do {if(!(c)){fprintf(stderr,"[audio-probe] failed line %d: %s\n",__LINE__,#c);return 14;}}while(0)
static uint32_t audio_vcall(uint32_t object,unsigned slot,unsigned count,const uint32_t *args){uint32_t a[10]={object};for(unsigned i=0;i<count;i++)a[i+1]=args[i];return hle_call_guest(MEM32(MEM32(object)+slot*4),(int)count+1,a);}
int cs16_probe_audio(void){
 uint32_t out=gm_va(gm_calloc(1,4)),a[9]={0,out,0};A_CHECK(!hle_call_guest(hle_resolve("DirectSoundCreate"),3,a));uint32_t ds=MEM32(out);A_CHECK(ds);
 const char *driver=SDL_GetCurrentAudioDriver();A_CHECK(driver&&!strcmp(driver,"coreaudio"));SDL_PauseAudioDevice(g_dev,1);
 uint32_t caps=gm_va(gm_calloc(1,104));MEM32(caps)=MEM32(caps+100)=0xcafebabe;MEM32(caps+4)=96;uint32_t caparg=caps+4;A_CHECK(!audio_vcall(ds,4,1,&caparg));A_CHECK(MEM32(caps)==0xcafebabe&&MEM32(caps+100)==0xcafebabe&&MEM32(caps+4+20)==0);
 uint32_t iid=gm_va(gm_calloc(1,16));MEM32(iid)=0xdeadbeef;uint32_t qi[]={iid,out};A_CHECK(audio_vcall(ds,0,2,qi)==0x80004002u&&!MEM32(out));
 uint32_t fmt=gm_va(gm_calloc(1,18)),desc=gm_va(gm_calloc(1,20));MEM16(fmt)=1;MEM16(fmt+2)=2;MEM32(fmt+4)=44100;MEM32(fmt+8)=176400;MEM16(fmt+12)=4;MEM16(fmt+14)=16;MEM32(desc)=20;MEM32(desc+4)=0x80;MEM32(desc+8)=1000;MEM32(desc+16)=fmt;
 uint32_t make[]={desc,out,0};A_CHECK(!audio_vcall(ds,3,3,make));uint32_t buffer=MEM32(out);A_CHECK(buffer);uint32_t result=gm_va(gm_calloc(4,4));
 uint32_t lock[]={0,1000,result,result+4,result+8,result+12,0};A_CHECK(!audio_vcall(buffer,11,7,lock));uint32_t data=MEM32(result);A_CHECK(data&&MEM32(result+4)==1000&&!MEM32(result+8)&&!MEM32(result+12));
 for(unsigned i=0;i<250;i++){MEM16(data+i*4)=1000;MEM16(data+i*4+2)=(uint16_t)-1000;}uint32_t unlock[]={data,1000,0,0};A_CHECK(!audio_vcall(buffer,19,4,unlock));
 lock[0]=992;lock[1]=16;A_CHECK(!audio_vcall(buffer,11,7,lock));A_CHECK(MEM32(result)==data+992&&MEM32(result+4)==8&&MEM32(result+8)==data&&MEM32(result+12)==8);
 uint32_t split[]={data+992,8,data,8};A_CHECK(!audio_vcall(buffer,19,4,split));lock[0]=1000;A_CHECK(audio_vcall(buffer,11,7,lock)==DSERR_INVALIDPARAM);
 uint32_t play[]={0,0,0};A_CHECK(!audio_vcall(buffer,12,3,play));int16_t pcm[512]={0};mix(NULL,(Uint8 *)pcm,128*4);A_CHECK(pcm[0]==999&&pcm[1]==-999);
 uint32_t zero=0;A_CHECK(!audio_vcall(buffer,13,1,&zero));uint32_t volume=(uint32_t)-600;A_CHECK(!audio_vcall(buffer,15,1,&volume));mix(NULL,(Uint8 *)pcm,4);A_CHECK(pcm[0]==501&&pcm[1]==-501);
 A_CHECK(!audio_vcall(buffer,15,1,&zero));uint32_t pan=(uint32_t)-10000;A_CHECK(!audio_vcall(buffer,16,1,&pan));mix(NULL,(Uint8 *)pcm,4);A_CHECK(pcm[0]==999&&pcm[1]==0);A_CHECK(!audio_vcall(buffer,16,1,&zero));
 uint32_t bad=10001;A_CHECK(audio_vcall(buffer,16,1,&bad)==DSERR_INVALIDPARAM);bad=1;A_CHECK(audio_vcall(buffer,15,1,&bad)==DSERR_INVALIDPARAM);
 uint32_t hz=22050;A_CHECK(!audio_vcall(buffer,17,1,&hz));A_CHECK(!audio_vcall(buffer,17,1,&zero));uint32_t output_arg=out;A_CHECK(!audio_vcall(buffer,8,1,&output_arg)&&MEM32(out)==44100);
 uint32_t duplicate[]={buffer,out};A_CHECK(!audio_vcall(ds,5,2,duplicate));uint32_t second=MEM32(out);A_CHECK(second&&second!=buffer);A_CHECK(audio_vcall(buffer,2,0,NULL)==0);buffer=second;A_CHECK(!audio_vcall(buffer,12,3,play));
 A_CHECK(!audio_vcall(buffer,13,1,&zero));mix(NULL,(Uint8 *)pcm,4);A_CHECK(pcm[0]==999&&pcm[1]==-999);
 /* No guest writes race the audio callback: live samples refresh on the
  * interface boundary, then the mixer reads only a synchronized host copy. */
 MEM16(data)=2000;A_CHECK(!audio_vcall(buffer,13,1,&zero));mix(NULL,(Uint8 *)pcm,4);A_CHECK(pcm[0]==999);uint32_t position[]={out,0};A_CHECK(!audio_vcall(buffer,4,2,position));A_CHECK(!audio_vcall(buffer,13,1,&zero));mix(NULL,(Uint8 *)pcm,4);A_CHECK(pcm[0]==1999);
 A_CHECK(!audio_vcall(buffer,18,0,NULL));
 /* 8-bit mono, half-rate: each source sample must supply two output frames. */
 MEM16(fmt+2)=1;MEM32(fmt+4)=22050;MEM32(fmt+8)=22050;MEM16(fmt+12)=1;MEM16(fmt+14)=8;MEM32(desc+8)=16;A_CHECK(!audio_vcall(ds,3,3,make));uint32_t mono=MEM32(out);
 lock[0]=0;lock[1]=16;A_CHECK(!audio_vcall(mono,11,7,lock));uint32_t mdata=MEM32(result);MEM8(mdata)=255;MEM8(mdata+1)=0;unlock[0]=mdata;unlock[1]=16;A_CHECK(!audio_vcall(mono,19,4,unlock));A_CHECK(!audio_vcall(mono,12,3,play));mix(NULL,(Uint8 *)pcm,16);A_CHECK(pcm[0]==32511&&pcm[2]==32511&&pcm[4]==-32767&&pcm[6]==-32767);A_CHECK(audio_vcall(mono,2,0,NULL)==0);
 /* The physical driver gets silence; its callback must advance a real cursor. */
 memset(gm_ptr(data),0,1000);A_CHECK(!audio_vcall(buffer,13,1,&zero));play[2]=1;A_CHECK(!audio_vcall(buffer,12,3,play));SDL_PauseAudioDevice(g_dev,0);
 uint32_t advanced=0;for(unsigned i=0;i<30&&!advanced;i++){usleep(10000);A_CHECK(!audio_vcall(buffer,4,2,position));advanced=MEM32(out);}A_CHECK(advanced>0&&advanced<1000);SDL_PauseAudioDevice(g_dev,1);A_CHECK(!audio_vcall(buffer,18,0,NULL));
 A_CHECK(audio_vcall(buffer,2,0,NULL)==0);A_CHECK(audio_vcall(ds,2,0,NULL)==0);for(unsigned i=0;i<MAX_BUFS;i++)A_CHECK(!g_buf[i]);SDL_CloseAudioDevice(g_dev);g_dev=0;g_audio_ok=-1;
 printf("{\"audioProbe\":true,\"nativeDriver\":\"coreaudio\",\"pcm16Stereo\":true,\"pcm8MonoResampling\":true,\"volumeAndPan\":true,\"frequencyReset\":true,\"splitLock\":true,\"sharedDuplicateLifetime\":true,\"hostSampleSnapshot\":true,\"unsupportedInterfaceRejected\":true,\"softwareCapabilities\":true,\"liveCallbackCursorBytes\":%u,\"buffersReleased\":true}\n",advanced);return 0;
}
