/*
 * winmm.c - win32hle WINMM: multimedia timers, joystick, waveOut audio, MCI.
 * libc timing; no waveOut device or joystick. Real output is provided by
 * the DirectSound/SDL bridge; unsupported devices report failure.
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include "win32hle.h"

#define MMSYSERR_NOERROR  0u
#define MMSYSERR_ERROR    1u
#define JOYERR_UNPLUGGED  167u
#define WHDR_DONE         0x00000001u
#define WHDR_INQUEUE      0x00000010u

/* --- multimedia timer --- */
static void m_timeGetTime(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    RET((uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u), 0);
}
static void m_timeBeginPeriod(void) { RET(MMSYSERR_NOERROR, 1); }
static void m_timeGetDevCaps(void) {           /* (&TIMECAPS, size) */
    if (A32(0) && A32(1) >= 8) MEM32(A32(0)) = 1, MEM32(A32(0) + 4) = 1000000;
    RET(MMSYSERR_NOERROR, 2);
}

/* --- multimedia timers ---
 * Each timer is a host thread that calls the guest's callback through
 * hle_call_guest, so the callback runs once it gets the machine, as on
 * Windows it runs on winmm's thread. timeKillEvent returns only once a
 * callback already under way has finished (giving the machine up while it
 * waits): a game frees what its callback uses right after the kill
 * (Tiberian Sun's movie audio, its bring-up log 8). */
#define MAX_MMTIMERS 32
static struct { volatile int used, killed, inflight; uint32_t id, delay, proc, user, flags; pthread_t thread; } g_mmt[MAX_MMTIMERS];
static uint32_t g_mmt_next = 1;
static void *mmtimer(void *arg) {
    int i = (int)(intptr_t)arg;
    uint32_t due = hle_ticks_ms() + g_mmt[i].delay;
    for (;;) {
        int32_t wait = (int32_t)(due - hle_ticks_ms());
        if (wait > 0) usleep((useconds_t)wait * 1000u);
        if (g_mmt[i].killed) break;
        __sync_fetch_and_add(&g_mmt[i].inflight, 1);
        if (!g_mmt[i].killed) {
            if (g_mmt[i].flags & 0x30u) {                /* TIME_CALLBACK_EVENT_SET / _PULSE: proc is an event */
                uint32_t h = g_mmt[i].proc;
                mach_enter();
                hle_set_event(h);
                mach_leave();
            } else {
                uint32_t a[5] = { g_mmt[i].id, 0, g_mmt[i].user, 0, 0 };
                hle_call_guest(g_mmt[i].proc, 5, a);
            }
        }
        __sync_fetch_and_sub(&g_mmt[i].inflight, 1);
        if (!(g_mmt[i].flags & 1u)) break;               /* TIME_ONESHOT */
        due += g_mmt[i].delay ? g_mmt[i].delay : 1;
        if ((int32_t)(hle_ticks_ms() - due) > 1000) due = hle_ticks_ms();   /* fell far behind: no catch-up storm */
    }
    g_mmt[i].used = 0;
    hle_drop_thread_state();
    return NULL;
}
static void m_timeSetEvent(void) {             /* (delay, resolution, proc, user, flags) */
    for (int i = 0; i < MAX_MMTIMERS; i++)
        if (!g_mmt[i].used) {
            g_mmt[i].used = 1, g_mmt[i].killed = 0, g_mmt[i].inflight = 0;
            g_mmt[i].id = g_mmt_next++, g_mmt[i].delay = A32(0), g_mmt[i].proc = A32(2), g_mmt[i].user = A32(3), g_mmt[i].flags = A32(4);
            if (pthread_create(&g_mmt[i].thread, NULL, mmtimer, (void *)(intptr_t)i) != 0) { g_mmt[i].used = 0; RET(0, 5); }
            pthread_detach(g_mmt[i].thread);
            RET(g_mmt[i].id, 5);
        }
    RET(0, 5);
}
static void m_timeKillEvent(void) {
    for (int i = 0; i < MAX_MMTIMERS; i++)
        if (g_mmt[i].used && g_mmt[i].id == A32(0) && !g_mmt[i].killed) {
            g_mmt[i].killed = 1;
            if (!pthread_equal(g_mmt[i].thread, pthread_self()))
                while (g_mmt[i].inflight) { hle_block_begin(); usleep(200); hle_block_end(); }
            RET(MMSYSERR_NOERROR, 1);
        }
    RET(97u /* MMSYSERR_INVALPARAM */, 1);
}
static void m_timeEndPeriod(void)   { RET(MMSYSERR_NOERROR, 1); }

/* --- joystick: none present, so the game takes its keyboard/mouse path --- */
static void m_joyGetNumDevs(void)   { RET(0, 0); }
static void m_joyGetDevCapsA(void)  { RET(JOYERR_UNPLUGGED, 3); }
static void m_joyGetPos(void)       { RET(JOYERR_UNPLUGGED, 2); }
static void m_joyGetPosEx(void)     { RET(JOYERR_UNPLUGGED, 2); }

/* waveOut is not connected yet. DirectSound below is the real output path;
 * do not advertise a silent device or retire unplayed PCM as successful. */
static void m_waveOutOpen(void){if(A32(0)&&gm_accessible(A32(0),4,1))MEM32(A32(0))=0;RET(6,6);}
static void m_waveOutWrite(void){RET(5,3);}
static void m_waveOutPrepareHeader(void){RET(5,3);}
static void m_waveOutUnprepareHeader(void){RET(5,3);}
static void m_waveOutReset(void){RET(5,1);}
static void m_waveOutClose(void){RET(5,1);}
static void m_waveOutGetNumDevs(void){RET(0,0);}
static void m_waveOutGetDevCapsA(void){RET(2,3);}
/* Capture and Windows mixer controls are unavailable in this host. */
static void m_no_devices(void){RET(0,0);}
static void m_no_device_caps(void){RET(2,3);}
static void m_waveInOpen(void){if(A32(0)&&gm_accessible(A32(0),4,1))MEM32(A32(0))=0;RET(6,6);}
static void m_mixerOpen(void){if(A32(0)&&gm_accessible(A32(0),4,1))MEM32(A32(0))=0;RET(6,5);}
static void m_invalid_audio_1(void){RET(5,1);}
static void m_invalid_audio_3(void){RET(5,3);}
static void m_waveInGetErrorTextA(void){uint32_t size=A32(2);if(!size||!gm_accessible(A32(1),size,1))RET(11,3);const char *text="No audio capture device is connected";snprintf(APTR(1),size,"%s",text);RET(0,3);}
/* --- MCI (CD audio / command strings): not available --- */
static void m_mciSendStringA(void)     { RET(274, 4); }
static void m_mciSendCommandA(void)    { RET(274, 4); }
static void m_mciGetDeviceIDA(void)    { RET(0,1); }
static void m_mciGetErrorStringA(void) { RET(0, 3); }          /* FALSE: no string */

const win32hle_shim win32hle_winmm[] = {
    { "timeGetTime",            m_timeGetTime },
    { "timeBeginPeriod",        m_timeBeginPeriod },
    { "timeEndPeriod",          m_timeEndPeriod },
    { "timeGetDevCaps",         m_timeGetDevCaps },
    { "timeSetEvent",           m_timeSetEvent },
    { "timeKillEvent",          m_timeKillEvent },
    { "joyGetNumDevs",          m_joyGetNumDevs },
    { "joyGetDevCapsA",         m_joyGetDevCapsA },
    { "joyGetPos",              m_joyGetPos },
    { "joyGetPosEx",            m_joyGetPosEx },
    { "waveOutOpen",            m_waveOutOpen },
    { "waveOutWrite",           m_waveOutWrite },
    { "waveOutPrepareHeader",   m_waveOutPrepareHeader },
    { "waveOutUnprepareHeader", m_waveOutUnprepareHeader },
    { "waveOutReset",           m_waveOutReset },
    { "waveOutClose",           m_waveOutClose },
    { "waveOutGetNumDevs",      m_waveOutGetNumDevs },
    { "waveOutGetDevCapsA",     m_waveOutGetDevCapsA },
    { "waveInOpen", m_waveInOpen },{ "waveInGetNumDevs", m_no_devices },{ "waveInGetDevCapsA", m_no_device_caps },
    { "waveInPrepareHeader", m_invalid_audio_3 },{ "waveInUnprepareHeader", m_invalid_audio_3 },{ "waveInAddBuffer", m_invalid_audio_3 },
    { "waveInStart", m_invalid_audio_1 },{ "waveInStop", m_invalid_audio_1 },{ "waveInReset", m_invalid_audio_1 },{ "waveInClose", m_invalid_audio_1 },{ "waveInGetErrorTextA", m_waveInGetErrorTextA },
    { "mixerOpen", m_mixerOpen },{ "mixerClose", m_invalid_audio_1 },{ "mixerGetNumDevs", m_no_devices },{ "mixerGetDevCapsA", m_no_device_caps },
    { "mixerGetLineInfoA", m_invalid_audio_3 },{ "mixerGetLineControlsA", m_invalid_audio_3 },{ "mixerGetControlDetailsA", m_invalid_audio_3 },{ "mixerSetControlDetails", m_invalid_audio_3 },
    { "mciSendCommandA",        m_mciSendCommandA },
    { "mciGetDeviceIDA",        m_mciGetDeviceIDA },
    { "mciSendStringA",         m_mciSendStringA },
    { "mciGetErrorStringA",     m_mciGetErrorStringA },
    { 0, 0 }
};
