/*
 * platform.h - SDL2 replacement for the DOS / BIOS / Turbo Pascal Crt services
 * that INVATAXI used: VGA mode 13h and text mode 3, the VGA DAC, vertical
 * retrace, Crt (ClrScr, GotoXY, TextColor, Write, ReadKey, KeyPressed, Delay),
 * and the INT 33h mouse driver.
 */
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>

/* ---- lifecycle ---- */
int  plat_init(const char *title);
void plat_shutdown(void);

/* ---- video (VGA) ---- */
extern uint8_t vga_vram[65536];         /* segment A000h in mode 13h */
void vga_set_mode(int mode);            /* 0x03 = 80x25 text, 0x13 = 320x200x256 */
void vga_set_palette(const uint8_t *pal6); /* 768 bytes, 6-bit DAC values */
void vga_wait_retrace(void);            /* waits one 70 Hz frame and presents */
void vga_putpixel(int x, int y, uint8_t c);  /* mem[$A000:y*320+x] := c */
uint8_t vga_getpixel(int x, int y);
void vga_cursor_shape(int visible);     /* INT 10h AH=01h (0607h / 2607h) */

/* ---- Turbo Pascal Crt ---- */
void crt_clrscr(void);
void crt_gotoxy(int x, int y);          /* 1-based like Crt.GotoXY */
void crt_textcolor(int c);
void crt_write(const char *utf8);       /* Write(s)   - UTF-8 source text, shown as CP437 */
void crt_writeln(const char *utf8);     /* WriteLn(s) */
void crt_write_int(long v);             /* Write(v) */
int  crt_keypressed(void);
int  crt_readkey(void);                 /* returns CP437 code, 0 + scan for extended keys */
void crt_readln(char *buf, int maxlen); /* ReadLn from the keyboard, echoing; result is CP437 */
void crt_delay(int ms);

/* ---- INT 33h mouse ---- */
void mouse_reset(void);                 /* AX=21h */
void mouse_show(void);                  /* AX=01h */
void mouse_hide(void);                  /* AX=02h */
int  mouse_left_down(void);             /* AX=05h, returned status bit 0 */
void mouse_getpos(int *x, int *y);      /* AX=03h, x in 0..639, y in 0..199 */
void mouse_setpos(int x, int y);        /* AX=04h */

/* ---- audio device ---- */
typedef void (*plat_audio_cb)(int16_t *out, int frames, void *user);
int  plat_audio_open(int rate, plat_audio_cb cb, void *user);
void plat_audio_lock(void);
void plat_audio_unlock(void);

/* ---- misc ---- */
uint32_t plat_ticks_ms(void);

#endif
