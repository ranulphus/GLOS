/* The mixer's arithmetic: kernel/include/mixgain.h. */
#include "mixgain.h"

void mix_gains(u32 volume, s32 balance, int mute, u32 *left, u32 *right)
{
    u32 v = volume > MIX_UNITY ? MIX_UNITY : volume;
    if (balance < -256)
        balance = -256;
    if (balance > 256)
        balance = 256;
    if (mute)
        v = 0;
    *left = balance > 0 ? v * (u32)(256 - balance) / 256 : v;
    *right = balance < 0 ? v * (u32)(256 + balance) / 256 : v;
}

void mix_master(const s32 *acc, s16 *out, u32 frames, u32 volume, int mute, u16 *peak)
{
    u32 i, g = mute ? 0 : volume > MIX_UNITY ? MIX_UNITY : volume;
    for (i = 0; i < frames * 2; i++) {
        s32 v = acc[i];
        u32 mag;
        if (g != MIX_UNITY)
            v = (s32)(((long long)v * (s32)g) / MIX_UNITY);
        if (v > 32767)
            v = 32767;
        else if (v < -32768)
            v = -32768;
        out[i] = (s16)v;
        mag = v < 0 ? (u32)(-v) : (u32)v;
        if (mag > 32767)
            mag = 32767;
        if (mag > peak[i & 1])
            peak[i & 1] = (u16)mag;
    }
}

u32 mix_percent(u32 peak) { return peak >= 32767 ? 100 : (peak * 100 + 16383) / 32767; }

u32 mix_from_percent(u32 percent) { return percent >= 100 ? MIX_UNITY : (percent * MIX_UNITY + 50) / 100; }

u32 mix_to_percent(u32 gain) { return gain >= MIX_UNITY ? 100 : (gain * 100 + MIX_UNITY / 2) / MIX_UNITY; }
