/*
 * modplayer.h - re-implementation of the subset of "PaxiMod v9.06" (the
 * assembler MOD player linked into INVATAXI.EXE) that the game uses.
 *
 * Original far entry points in INVATAXI.EXE (segment 0) and their meaning:
 *   55AE  pm_detect()                 - hardware detection, no-op here
 *   543C  pm_load(file, dev, rate, ...) -> error code 0/1/2/4
 *   53DF  pm_set_volumes(a,b,c,d)     - per-channel music volume 0..255
 *   564A  pm_jump(order, row)         - continue playing from this position
 *   56E8  pm_play_instrument(period, instrument) - one-shot on channel 5
 *   580D  pm_play_file(file, period)  - load raw 8-bit signed sample, play on channel 5
 *   54E7  pm_stop()
 */
#ifndef MODPLAYER_H
#define MODPLAYER_H

int  pm_open_audio(int rate);           /* returns 1 on success */
int  pm_load(const char *path);         /* 0 = ok, 1 = file error, 2 = bad format */
void pm_set_volumes(int a, int b, int c, int d);
void pm_jump(int order, int row);
void pm_play_instrument(int period, int instrument);
int  pm_play_file(const char *path, int period);
void pm_stop(void);

#endif
