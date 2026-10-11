/*
 * platform.c - SDL2 implementation of the DOS-era services used by INVATAXI.
 */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"
#include "font_vga.h"
#include "cp437.h"

#define FRAME_HZ      70.086   /* VGA 320x200 / 80x25 vertical refresh */
#define TEXT_W        720
#define TEXT_H        400
#define LOGICAL_W     640
#define LOGICAL_H     480

uint8_t vga_vram[65536];

static SDL_Window   *win;
static SDL_Renderer *ren;
static SDL_Texture  *tex_gfx;   /* 320x200 */
static SDL_Texture  *tex_txt;   /* 720x400 */
static SDL_AudioDeviceID audio_dev;

static int      cur_mode = 0x03;
static uint32_t pal_rgb[256];
static uint32_t pix_gfx[320 * 200];
static uint32_t pix_txt[TEXT_W * TEXT_H];

/* text screen */
static uint16_t txt[25][80];    /* char | attr << 8 */
static int      cur_x, cur_y;   /* 0-based */
static int      text_attr = 7;
static int      cursor_on = 1;

/* timing */
static uint64_t perf_freq, next_frame;
static uint32_t frame_count;
static uint32_t frames_at_last_kbcheck = 0xffffffffu;

/* keyboard */
static uint8_t  kbuf[64];
static int      khead, ktail;

/* mouse */
static int mouse_visible_ctr = -1;
static int mouse_x, mouse_y;     /* driver coordinates 0..639, 0..199 */
static int mouse_btn;
static int mouse_click_latch;   /* a press that was released before the game polled */
static uint32_t mouse_click_frame;

static const uint32_t ega_rgb[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
};

/* Standard Microsoft mouse driver arrow (screen mask / cursor mask) */
static const uint16_t cur_screen[16] = {
    0x3FFF,0x1FFF,0x0FFF,0x07FF,0x03FF,0x01FF,0x00FF,0x007F,
    0x003F,0x001F,0x01FF,0x10FF,0x30FF,0xF87F,0xF87F,0xFC7F };
static const uint16_t cur_cursor[16] = {
    0x0000,0x4000,0x6000,0x7000,0x7800,0x7C00,0x7E00,0x7F00,
    0x7F80,0x7C00,0x6C00,0x4600,0x0600,0x0300,0x0300,0x0000 };

uint32_t plat_ticks_ms(void) { return SDL_GetTicks(); }

static void kpush(int c)
{
    int n = (khead + 1) % (int)sizeof kbuf;
    if (n == ktail) return;
    kbuf[khead] = (uint8_t)c;
    khead = n;
}

static int uni_to_cp437(uint32_t u)
{
    int i;
    if (u < 0x80) return (int)u;
    for (i = 128; i < 256; i++) if (cp437_uni[i] == u) return i;
    return -1;
}

static void toggle_fullscreen(void)
{
    uint32_t f = SDL_GetWindowFlags(win);
    SDL_SetWindowFullscreen(win, (f & SDL_WINDOW_FULLSCREEN_DESKTOP) ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
}

static void update_mouse_from_window(int wx, int wy)
{
    /* With a logical size set, SDL reports logical (640x480) coordinates. */
    int x = wx, y = wy;
    if (x < 0) x = 0;
    if (x > LOGICAL_W - 1) x = LOGICAL_W - 1;
    if (y < 0) y = 0;
    if (y > LOGICAL_H - 1) y = LOGICAL_H - 1;
    mouse_x = x * 640 / LOGICAL_W;
    mouse_y = y * 200 / LOGICAL_H;
}

static void pump_events(void)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            plat_shutdown();
            exit(0);
        case SDL_KEYDOWN: {
            SDL_Keycode k = e.key.keysym.sym;
            SDL_Keymod m = (SDL_Keymod)e.key.keysym.mod;
            if ((k == SDLK_RETURN && (m & KMOD_ALT)) || k == SDLK_F11) { toggle_fullscreen(); break; }
            if (m & KMOD_CTRL) {
                if (k >= SDLK_a && k <= SDLK_z) { kpush((int)(k - SDLK_a) + 1); break; }
            }
            switch (k) {
            case SDLK_RETURN: case SDLK_KP_ENTER: kpush(13); break;
            case SDLK_ESCAPE:    kpush(27); break;
            case SDLK_BACKSPACE: kpush(8); break;
            case SDLK_TAB:       kpush(9); break;
            case SDLK_UP:     kpush(0); kpush(0x48); break;
            case SDLK_DOWN:   kpush(0); kpush(0x50); break;
            case SDLK_LEFT:   kpush(0); kpush(0x4B); break;
            case SDLK_RIGHT:  kpush(0); kpush(0x4D); break;
            case SDLK_HOME:   kpush(0); kpush(0x47); break;
            case SDLK_END:    kpush(0); kpush(0x4F); break;
            case SDLK_PAGEUP: kpush(0); kpush(0x49); break;
            case SDLK_PAGEDOWN: kpush(0); kpush(0x51); break;
            case SDLK_INSERT: kpush(0); kpush(0x52); break;
            case SDLK_DELETE: kpush(0); kpush(0x53); break;
            default:
                if (k >= SDLK_F1 && k <= SDLK_F10) { kpush(0); kpush(0x3B + (int)(k - SDLK_F1)); }
                break;
            }
            break;
        }
        case SDL_TEXTINPUT: {
            const unsigned char *s = (const unsigned char *)e.text.text;
            while (*s) {
                uint32_t u; int c;
                if (*s < 0x80) u = *s++;
                else if ((*s & 0xE0) == 0xC0 && s[1]) { u = ((uint32_t)(s[0] & 0x1F) << 6) | (s[1] & 0x3F); s += 2; }
                else if ((*s & 0xF0) == 0xE0 && s[1] && s[2]) { u = ((uint32_t)(s[0] & 0x0F) << 12) | ((uint32_t)(s[1] & 0x3F) << 6) | (s[2] & 0x3F); s += 3; }
                else { s++; continue; }
                c = uni_to_cp437(u);
                if (c > 0) kpush(c);
            }
            break;
        }
        case SDL_MOUSEMOTION:
            update_mouse_from_window(e.motion.x, e.motion.y);
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (e.button.button == SDL_BUTTON_LEFT) { mouse_btn |= 1; mouse_click_latch = 1; mouse_click_frame = frame_count; }
            if (e.button.button == SDL_BUTTON_RIGHT) mouse_btn |= 2;
            update_mouse_from_window(e.button.x, e.button.y);
            break;
        case SDL_MOUSEBUTTONUP:
            if (e.button.button == SDL_BUTTON_LEFT) mouse_btn &= ~1;
            if (e.button.button == SDL_BUTTON_RIGHT) mouse_btn &= ~2;
            break;
        default: break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* rendering                                                           */

static void render_text(void)
{
    int row, col, line, bit;
    int blink_phase = (frame_count / 16) & 1;  /* char blink: 32-frame cycle */
    int cur_phase   = (frame_count / 8) & 1;   /* cursor blink: 16-frame cycle */
    for (row = 0; row < 25; row++) {
        for (col = 0; col < 80; col++) {
            uint16_t cell = txt[row][col];
            int ch = cell & 0xff, at = cell >> 8;
            uint32_t fg = ega_rgb[at & 15], bg = ega_rgb[(at >> 4) & 7];
            const uint8_t *g = &font_vga8x16[ch * 16];
            int hide = (at & 0x80) && blink_phase;
            for (line = 0; line < 16; line++) {
                uint32_t *p = &pix_txt[(row * 16 + line) * TEXT_W + col * 9];
                int bits = hide ? 0 : g[line];
                int ninth;
                if (cursor_on && !cur_phase && row == cur_y && col == cur_x && (line == 13 || line == 14))
                    bits = 0xff, ninth = 1;
                else
                    ninth = (ch >= 0xC0 && ch <= 0xDF) ? (bits & 1) : 0;
                for (bit = 0; bit < 8; bit++) p[bit] = (bits & (0x80 >> bit)) ? fg : bg;
                p[8] = ninth ? fg : bg;
                if (cursor_on && !cur_phase && row == cur_y && col == cur_x && (line == 13 || line == 14))
                    p[8] = fg;
            }
        }
    }
    SDL_UpdateTexture(tex_txt, NULL, pix_txt, TEXT_W * 4);
}

static void render_gfx(void)
{
    int i, r, c;
    for (i = 0; i < 64000; i++) pix_gfx[i] = pal_rgb[vga_vram[i]];
    if (mouse_visible_ctr >= 0) {
        int hx = mouse_x / 2, hy = mouse_y;   /* mode 13h: driver x is doubled */
        for (r = 0; r < 16; r++) {
            int y = hy + r;
            if (y < 0 || y >= 200) continue;
            for (c = 0; c < 16; c++) {
                int x = hx + c;
                uint16_t mbit = (uint16_t)(0x8000 >> c);
                if (x < 0 || x >= 320) continue;
                if (!(cur_screen[r] & mbit)) pix_gfx[y * 320 + x] = 0x000000;
                if (cur_cursor[r] & mbit) pix_gfx[y * 320 + x] ^= 0xFFFFFF;
            }
        }
    }
    SDL_UpdateTexture(tex_gfx, NULL, pix_gfx, 320 * 4);
}


#ifdef INVATAXI_AUTOTEST
/* Scripted input + screenshots for automated testing (not in normal builds).
   IT_SCRIPT file lines: "<frame> key <c|code>", "<frame> mouse <x> <y> <down 0/1>",
   "<frame> shot <file.ppm>", "<frame> quit" */
static FILE *it_f;
static long  it_next = -1;
static char  it_line[256];
static void it_load_next(void)
{
    it_next = -1;
    while (it_f && fgets(it_line, sizeof it_line, it_f)) {
        if (sscanf(it_line, "%ld", &it_next) == 1) return;
    }
}
static void it_shot(const char *fn)
{
    FILE *o = fopen(fn, "wb");
    int w = (cur_mode == 0x13) ? 320 : TEXT_W, h = (cur_mode == 0x13) ? 200 : TEXT_H, i;
    const uint32_t *p = (cur_mode == 0x13) ? pix_gfx : pix_txt;
    if (!o) return;
    fprintf(o, "P6\n%d %d\n255\n", w, h);
    for (i = 0; i < w * h; i++) { fputc((p[i] >> 16) & 255, o); fputc((p[i] >> 8) & 255, o); fputc(p[i] & 255, o); }
    fclose(o);
}
static void it_run(void)
{
    static int inited;
    if (!inited) { const char *s = getenv("IT_SCRIPT"); inited = 1; if (s) it_f = fopen(s, "r"); it_load_next(); }
    while (it_next >= 0 && it_next <= (long)frame_count) {
        char cmd[32], a[200]; int x, y, d;
        long fr;
        if (sscanf(it_line, "%ld %31s", &fr, cmd) == 2) {
            if (!strcmp(cmd, "key") && sscanf(it_line, "%ld %31s %199s", &fr, cmd, a) == 3) {
                if (a[0] == '#') kpush(atoi(a + 1)); else kpush((unsigned char)a[0]);
            } else if (!strcmp(cmd, "mouse") && sscanf(it_line, "%ld %31s %d %d %d", &fr, cmd, &x, &y, &d) == 5) {
                mouse_x = x; mouse_y = y; mouse_btn = d;
            } else if (!strcmp(cmd, "shot") && sscanf(it_line, "%ld %31s %199s", &fr, cmd, a) == 3) {
                it_shot(a);
            } else if (!strcmp(cmd, "quit")) { plat_shutdown(); exit(0); }
        }
        it_load_next();
    }
}
#endif

static void present(void)
{
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    if (cur_mode == 0x13) { render_gfx(); SDL_RenderCopy(ren, tex_gfx, NULL, NULL); }
    else                  { render_text(); SDL_RenderCopy(ren, tex_txt, NULL, NULL); }
    SDL_RenderPresent(ren);
#ifdef INVATAXI_AUTOTEST
    it_run();
#endif
}

static void frame_tick(void)
{
    uint64_t now;
    uint64_t step = (uint64_t)((double)perf_freq / FRAME_HZ);
    pump_events();
    present();
    now = SDL_GetPerformanceCounter();
    if (next_frame == 0 || now > next_frame + step * 8) next_frame = now;  /* resync after stalls */
    next_frame += step;
    for (;;) {
        now = SDL_GetPerformanceCounter();
        if (now >= next_frame) break;
        {
            uint64_t ms = (next_frame - now) * 1000 / perf_freq;
            if (ms > 1) SDL_Delay((uint32_t)(ms - 1));
        }
    }
    frame_count++;
}

void vga_wait_retrace(void) { frame_tick(); }

/* ------------------------------------------------------------------ */

int plat_init(const char *title)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 0;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           960, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 0; }
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
    if (!ren) { fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return 0; }
    SDL_RenderSetLogicalSize(ren, LOGICAL_W, LOGICAL_H);
    tex_gfx = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 320, 200);
    tex_txt = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, TEXT_W, TEXT_H);
    SDL_ShowCursor(SDL_DISABLE);
    SDL_StartTextInput();
    perf_freq = SDL_GetPerformanceFrequency();
    vga_set_mode(0x03);
    return 1;
}

void plat_shutdown(void)
{
    if (audio_dev) { SDL_CloseAudioDevice(audio_dev); audio_dev = 0; }
    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
    ren = NULL; win = NULL;
    SDL_Quit();
}

/* ------------------------------------------------------------------ */
/* VGA                                                                 */

void vga_set_mode(int mode)
{
    int i;
    cur_mode = mode;
    memset(vga_vram, 0, sizeof vga_vram);
    if (mode == 0x13) {
        /* default 256-colour DAC: we only need the first 16 to be sane */
        for (i = 0; i < 256; i++) pal_rgb[i] = (i < 16) ? ega_rgb[i] : 0;
    } else {
        for (i = 0; i < 25 * 80; i++) txt[i / 80][i % 80] = 0x0720;
        cur_x = cur_y = 0;
        cursor_on = 1;
    }
    mouse_visible_ctr = -1;   /* the mouse driver hides its cursor on a mode switch */
}

void vga_set_palette(const uint8_t *p)
{
    int i;
    for (i = 0; i < 256; i++) {
        uint32_t r = p[i * 3] & 63, g = p[i * 3 + 1] & 63, b = p[i * 3 + 2] & 63;
        r = (r << 2) | (r >> 4); g = (g << 2) | (g >> 4); b = (b << 2) | (b >> 4);
        pal_rgb[i] = (r << 16) | (g << 8) | b;
    }
}

void vga_putpixel(int x, int y, uint8_t c)
{
    vga_vram[(uint16_t)(y * 320 + x)] = c;   /* 16-bit offset arithmetic like the original */
}

uint8_t vga_getpixel(int x, int y)
{
    return vga_vram[(uint16_t)(y * 320 + x)];
}

void vga_cursor_shape(int visible) { cursor_on = visible; }

/* ------------------------------------------------------------------ */
/* Crt                                                                 */

static void scroll_up(void)
{
    int r, c;
    memmove(&txt[0][0], &txt[1][0], sizeof(txt[0]) * 24);
    for (c = 0; c < 80; c++) txt[24][c] = (uint16_t)(' ' | (text_attr << 8));
    (void)r;
}

static void put_cp437(int ch)
{
    switch (ch) {
    case 7: return;                               /* bell */
    case 8: if (cur_x > 0) cur_x--; return;
    case 10:
        if (++cur_y > 24) { cur_y = 24; scroll_up(); }
        return;
    case 13: cur_x = 0; return;
    default:
        txt[cur_y][cur_x] = (uint16_t)(ch | (text_attr << 8));
        if (++cur_x >= 80) {
            cur_x = 0;
            if (++cur_y > 24) { cur_y = 24; scroll_up(); }
        }
    }
}

static void write_utf8(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        uint32_t u; int c;
        if (*p < 0x80) u = *p++;
        else if ((*p & 0xE0) == 0xC0 && p[1]) { u = ((uint32_t)(p[0] & 0x1F) << 6) | (p[1] & 0x3F); p += 2; }
        else if ((*p & 0xF0) == 0xE0 && p[1] && p[2]) { u = ((uint32_t)(p[0] & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) | (p[2] & 0x3F); p += 3; }
        else { p++; continue; }
        c = uni_to_cp437(u);
        put_cp437(c < 0 ? '?' : c);
    }
}

void crt_clrscr(void)
{
    int r, c;
    for (r = 0; r < 25; r++) for (c = 0; c < 80; c++) txt[r][c] = (uint16_t)(' ' | (text_attr << 8));
    cur_x = cur_y = 0;
}

void crt_gotoxy(int x, int y)
{
    if (x < 1 || x > 80 || y < 1 || y > 25) return;
    cur_x = x - 1; cur_y = y - 1;
}

void crt_textcolor(int c)
{
    if (c & 0xf0) c = (c & 0x0f) | 0x80;
    text_attr = (text_attr & 0x70) | c;
}

void crt_write(const char *s) { write_utf8(s); }
void crt_writeln(const char *s) { if (s) write_utf8(s); put_cp437(13); put_cp437(10); }

void crt_write_int(long v)
{
    char b[32];
    snprintf(b, sizeof b, "%ld", v);
    write_utf8(b);
}

int crt_keypressed(void)
{
    pump_events();
    if (khead != ktail) { frames_at_last_kbcheck = frame_count; return 1; }
    /* A busy "repeat until KeyPressed" loop never waits for retrace; keep the
       screen alive and the CPU cool by spending one frame if none has passed
       since the last check. */
    if (frames_at_last_kbcheck == frame_count) frame_tick();
    frames_at_last_kbcheck = frame_count;
    return khead != ktail;
}

int crt_readkey(void)
{
    int c;
    while (khead == ktail) frame_tick();
    c = kbuf[ktail];
    ktail = (ktail + 1) % (int)sizeof kbuf;
    return c;
}

void crt_readln(char *buf, int maxlen)
{
    int n = 0, c;
    if (maxlen > 127) maxlen = 127;    /* Crt line buffer */
    for (;;) {
        c = crt_readkey();
        if (c == 0) { crt_readkey(); continue; }   /* ignore extended keys */
        if (c == 13) break;
        if (c == 8) {
            if (n > 0) { n--; put_cp437(8); put_cp437(' '); put_cp437(8); }
            continue;
        }
        if (c == 27) {
            while (n > 0) { n--; put_cp437(8); put_cp437(' '); put_cp437(8); }
            continue;
        }
        if (c < 32) continue;
        if (n < maxlen) { buf[n++] = (char)c; put_cp437(c); }
    }
    buf[n] = 0;
    put_cp437(13); put_cp437(10);
}

void crt_delay(int ms)
{
    /* Crt.Delay busy-waits; keep the window alive while doing so */
    uint32_t end = SDL_GetTicks() + (uint32_t)ms;
    while ((int32_t)(SDL_GetTicks() - end) < 0) {
        uint64_t now = SDL_GetPerformanceCounter();
        if (next_frame == 0 || now >= next_frame) frame_tick();
        else SDL_Delay(1);
    }
}

/* ------------------------------------------------------------------ */
/* mouse                                                               */

void mouse_reset(void) { mouse_visible_ctr = -1; mouse_click_latch = 0; }
void mouse_show(void)  { if (mouse_visible_ctr < 0) mouse_visible_ctr++; }
void mouse_hide(void)  { mouse_visible_ctr--; }
int  mouse_left_down(void)
{
    int r;
    pump_events();
    /* only remember very short clicks (pressed and released within the last frame) */
    r = (mouse_btn & 1) || (mouse_click_latch && frame_count - mouse_click_frame <= 1);
    mouse_click_latch = 0;
    return r;
}
void mouse_getpos(int *x, int *y) { pump_events(); *x = mouse_x; *y = mouse_y; }
void mouse_setpos(int x, int y)
{
    /* Only the driver's idea of the position changes; the host pointer is
       left alone and takes over again on the next motion event. */
    mouse_x = x; mouse_y = y;
}

/* ------------------------------------------------------------------ */
/* audio                                                               */

static plat_audio_cb a_cb;
static void *a_user;

static void SDLCALL sdl_audio_cb(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    a_cb((int16_t *)stream, len / 2, a_user);
}

int plat_audio_open(int rate, plat_audio_cb cb, void *user)
{
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = rate;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512;
    want.callback = sdl_audio_cb;
    a_cb = cb; a_user = user;
    audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!audio_dev) { fprintf(stderr, "SDL_OpenAudioDevice: %s\n", SDL_GetError()); return 0; }
    SDL_PauseAudioDevice(audio_dev, 0);
    return have.freq;
}

void plat_audio_lock(void)   { if (audio_dev) SDL_LockAudioDevice(audio_dev); }
void plat_audio_unlock(void) { if (audio_dev) SDL_UnlockAudioDevice(audio_dev); }
