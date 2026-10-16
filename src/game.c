/*
 * game.c - INVATAXI (Åkesoft, 17.10.1994) game logic, translated from the
 * Turbo Pascal 6.0 code in INVATAXI.EXE (main code segment, offsets 0000h-2A8Dh).
 *
 * Each function is named after what it does and tagged with the offset of
 * the original procedure. Global variables keep their original data segment
 * offsets in comments. Timing is expressed, as in the original, in VGA
 * vertical retraces (70 Hz).
 *
 * Text strings are the original Finnish ones (CP437 in the EXE, UTF-8 here).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "platform.h"
#include "modplayer.h"
#include "files.h"
#include "game.h"

/* ---------------------------------------------------------------- */
/* globals (DS offsets of the original)                               */

static int  key;                 /* 0044 */
static int  i46;                 /* 0046 general loop counter        */
static int  sound_dev;           /* 004A 0xFF = no sound              */
static int  sound_rate;          /* 004C                              */
static int  money_delta;         /* 0376 */
static int  lives;               /* 0378 */
static int  money;               /* 037A */
static int  destination;         /* 037C wanted place (= .SKP number) */
static int  choice;              /* 037E clicked place, 1 = nowhere   */
static int  rnd;                 /* 0380 */
static int  score;               /* 0388 */
static int  menu_sel;            /* 038C */
static int  mouse_x, mouse_y;    /* 038E / 0390 */
static int  game_over;           /* 0392 */
static int  quit_program;        /* 0393 */
static char player_name[256];    /* 0062 */
static char hs_lines[11][256];   /* 0494 (1-based) */
static int  tmp101c;             /* 101C */
static uint8_t taxi_buf[1460];   /* 101E word array, 1-based */
static int  taxi_x, taxi_y;      /* 2920 / 2928 */

/* the three pictures (ColoRIX "RIX3", 320x200, 6-bit palette) */
static const unsigned char *pic_map;    /* inside INVATAXI.EXE, segment 05BAh */
static const unsigned char *pic_title;  /* HJUVA_#1.EXE "alkukuvaa" */
static const unsigned char *pic_lefty;  /* HJUVA_#2.EXE "autoo"     */

/* ---------------------------------------------------------------- */
/* Turbo Pascal System.Random                                         */

static uint32_t rand_seed;

/* Seeds the random generator from the clock (or IT_SEED in test builds). */
static void tp_randomize(void)
{
    rand_seed = (uint32_t)time(NULL) ^ (plat_ticks_ms() * 2654435761u);
#ifdef INVATAXI_AUTOTEST
    if (getenv("IT_SEED")) rand_seed = (uint32_t)atol(getenv("IT_SEED"));
#endif
}

/* Returns a random number 0..range-1 using Turbo Pascal's Random algorithm. */
static int tp_random(int range)
{
    rand_seed = rand_seed * 0x08088405u + 1u;
    return (int)(((rand_seed >> 16) * (uint32_t)range) >> 16);
}

/* ---------------------------------------------------------------- */
/* small helpers corresponding to tiny routines in the EXE            */

/* 0036: wait for a complete vertical retrace */
static void vsync(void) { vga_wait_retrace(); }

/* Waits n vertical retraces. */
static void wait_frames(int n)
{
    /* "i46 := 1; repeat vsync; ... until i46 = n" style loops */
    for (i46 = 1; ; i46++) { vsync(); if (i46 == n) break; }
}

/* 0044 / 0068: INT 10h AH=01 cursor shape 2607h (hide) / 0607h (show) */
/* Hides the text cursor. */
static void hide_text_cursor(void) { vga_cursor_shape(0); }
/* Shows the text cursor. */
static void show_text_cursor(void) { vga_cursor_shape(1); }

/* 0694 / 06B7 */
/* Sets a pixel on the graphics screen. */
static void put_pixel(int x, int y, int c) { vga_putpixel(x, y, (uint8_t)c); }
/* Returns a pixel from the graphics screen. */
static int  get_pixel(int x, int y) { return vga_getpixel(x, y); }

/* 0000 + 0017: show a RIX picture (palette first, then 64000 bytes) */
static void show_picture(const unsigned char *rix)
{
    vga_set_palette(rix + 10);
    memcpy(vga_vram, rix + 0x30A, 64000);
}

/* Exec('hjuva_#n.exe', param): the helper programs just set mode 13h,
   load the palette and copy their picture to A000:0000, then exit. */
static void exec_hjuva(const unsigned char *rix)
{
    vga_set_mode(0x13);
    show_picture(rix);
}

/* PaxiMod "play sample file" (580D) followed by the game's
   "if sound then wait" loop. */
static void say(const char *skp, int period, int frames)
{
    pm_play_file(skp, period);
    if (sound_dev != 0xff) wait_frames(frames);
}

/* ---------------------------------------------------------------- */
/* 0267: sound device / mixing rate selection and music start         */

int game_sound_menu = 0;   /* (port) 1 = show the original sound card menu */

/* Picks the sound device and mixing rate (defaults unless the menu is enabled),
   opens audio and starts the music. */
static void sound_setup(void)
{
    if (!game_sound_menu) {
        /* (port) skip the menu: Sound Blaster, 22000 Hz */
        sound_dev = 7;
        sound_rate = 22000;
        pm_set_volumes(0xff, 0xff, 0xff, 0xff);
        if (!pm_open_audio(sound_rate))
            fprintf(stderr, "invataxi: could not open audio, playing without sound\n");
        else if (pm_load("harmonia.nva") != 0)
            fprintf(stderr, "invataxi: could not load harmonia.nva\n");
        return;
    }
    crt_clrscr();
    crt_writeln("");
    crt_writeln("Millä haluat hemmotella korviasi?");
    crt_writeln("");
    crt_writeln("1. Sound Blaster");
    crt_writeln("2. D/A LPT 1:ssä");
    crt_writeln("3. Testaa jos uskallat");
    crt_writeln("4. Ilman ääniä ei voi pelata!");
    crt_writeln("5. Fakiiritemppu: Piipperistä!");
    sound_dev = 1000;
    do {
        key = crt_readkey();
        if (key == '1') sound_dev = 7;     /* SoundBlaster      */
        if (key == '2') sound_dev = 1;     /* DAC on LPT1       */
        if (key == '3') sound_dev = 2;     /* DAC on LPT2       */
        if (key == '4') sound_dev = 0xff;  /* no sound          */
        if (key == '5') sound_dev = 0;     /* PC speaker        */
    } while (sound_dev > 999);

    sound_rate = 0;
    if (sound_dev != 0xff) {
        crt_clrscr();
        crt_writeln("");
        crt_writeln("");
        crt_writeln("Paljonkos pannaan?");
        crt_writeln("1. 8000  Hz");
        crt_writeln("2. 10000 Hz");
        crt_writeln("3. 12000 Hz");
        crt_writeln("4. 14000 Hz");
        crt_writeln("5. 18000 Hz");
        crt_writeln("6. 22000 Hz");
        do {
            key = crt_readkey();
            if (key == '1') sound_rate = 8000;
            if (key == '2') sound_rate = 10000;
            if (key == '3') sound_rate = 12000;
            if (key == '4') sound_rate = 14000;
            if (key == '5') sound_rate = 18000;
            if (key == '6') sound_rate = 22000;
        } while (sound_rate < 7001);
    }

    if (sound_dev == 0xff) {
        char dummy[128];
        crt_writeln("");
        crt_writeln("Ääliö ekkö sää nyt tajua, että et voi pelata ilman ääniä?!?");
        crt_writeln("Ja tää rupee bugittaankin sillon. Osta nyt itelles kortti!!!");
        hide_text_cursor();
        crt_readln(dummy, 127);
    }

    if (sound_dev != 0xff) {
        int err;
        pm_set_volumes(0xff, 0xff, 0xff, 0xff);
        if (!pm_open_audio(sound_rate)) err = 2;
        else err = pm_load("harmonia.nva");
        if (err == 1) crt_writeln("Mitäs menit sorkkiin niitä!");
        else if (err == 2) crt_writeln("Jaa häh? Yritäksää sekottaa koko koneen?");
        else if (err == 4) crt_writeln("Yritäpä saada lisää muistia.");
    }
}

/* ---------------------------------------------------------------- */
/* 0619: quit                                                         */

/* Returns to text mode, shows the farewell message and fades the music out. */
static void quit_to_dos(void)
{
    vga_set_mode(0x03);
    crt_clrscr();
    crt_textcolor(7);
    crt_writeln("Åkesoft on maailman paras!");
    crt_textcolor(7);
    for (i46 = 0xff; ; i46--) {
        pm_set_volumes(i46, i46, i46, i46);
        crt_delay(2);
        if (i46 == 0) break;
    }
    pm_stop();
    show_text_cursor();
    /* (port) keep the farewell message on screen for a moment */
    crt_delay(1500);
}

/* ---------------------------------------------------------------- */
/* 06E1 / 0739: grab and draw the 63x23 taxi from the "autoo" picture  */

/* Copies the 63x23 taxi image from the screen into taxi_buf. */
static void grab_taxi(void)
{
    int x, y, idx = 1;
    for (y = 0xb1; y <= 199; y++)
        for (x = 0xf7; x <= 0x135; x++) {
            idx++;
            taxi_buf[idx] = (uint8_t)get_pixel(x, y);
        }
}

/* Draws the taxi from taxi_buf at (taxi_x, taxi_y). */
static void draw_taxi(void)
{
    int x, y, idx = 1;
    for (y = taxi_y; y <= taxi_y + 0x16; y++)
        for (x = taxi_x; x <= taxi_x + 0x3e; x++) {
            idx++;
            put_pixel(x, y, taxi_buf[idx]);
        }
}

/* ---------------------------------------------------------------- */
/* 07B0: money bar shrinks (customer got bored and you paid)          */

/* Animates the money bar shrinking by money_delta and subtracts it from money. */
static void money_bar_decrease(void)
{
    int x, y, from;
    for (y = 0xc0; y <= 199; y++)
        for (x = 0x25; x <= money + 0x25; x++) put_pixel(x, y, 0x0c);

    tmp101c = money + 0x25 - money_delta;
    if (money - money_delta < 1) tmp101c = 0x25;
    from = money + 0x25;
    if (tmp101c <= from) {
        for (x = from; ; x--) {
            for (y = 0xc0; y <= 199; y++) put_pixel(x, y, 0x1f);
            pm_play_instrument(300, 12);
            wait_frames(10);
            if (x == tmp101c) break;
        }
    }
    money -= money_delta;
    money_delta = 0;
    if (money < 0) money = 0;
    wait_frames(20);
}

/* 08B5: money bar grows (fare paid) / plain redraw when delta = 0     */

/* Redraws the money bar, animates it growing by money_delta and adds it to money. */
static void money_bar_increase(void)
{
    int x, y;
    for (y = 0xc0; y <= 199; y++)
        for (x = 0x25; x <= money + 0x25; x++)
            if (x < 320) put_pixel(x, y, 0x0c);

    tmp101c = money + 0x25 + money_delta;
    if (money_delta > 0) {
        for (x = money + 0x25; x <= tmp101c; x++) {
            for (y = 0xc0; y <= 199; y++)
                if (x < 320) put_pixel(x, y, 0x0c);
            if (money_delta > 0) pm_play_instrument(200, 12);
            wait_frames(10);
        }
    }
    money += money_delta;
    money_delta = 0;
}

/* 09A4: erase lost "supermarios" (the three squares next to EMÄLÄT) */

/* Blanks out the life squares that have been lost. */
static void draw_lives(void)
{
    int x, y;
    if (lives < 3)
        for (y = 0xb1; y <= 0xba; y++) for (x = 0xf2; x <= 0xfb; x++) put_pixel(x, y, 0x1f);
    if (lives < 2)
        for (y = 0xb1; y <= 0xba; y++) for (x = 0xe8; x <= 0xf1; x++) put_pixel(x, y, 0x1f);
    if (lives < 1)
        for (y = 0xb1; y <= 0xba; y++) for (x = 0xde; x <= 0xe7; x++) put_pixel(x, y, 0x1f);
    wait_frames(10);
}

/* 0A7A: map screen */

/* Switches to graphics mode and shows the map with the remaining lives. */
static void show_map(void)
{
    vga_set_mode(0x13);
    show_picture(pic_map);
    draw_lives();
}

/* ---------------------------------------------------------------- */
/* 10B6: TAUSTATARINA - background story                              */

/* Shows the background story and waits for a key. */
static void story(void)
{
    crt_textcolor(9);
    crt_clrscr();
    crt_writeln("");
    crt_writeln("                               INVAKUSKI");
    crt_writeln(" Olipa kerran makkaratehtailija nimeltään Kauko Ilona Juutilainen. Hänellä oli");
    crt_writeln(" vaimo ja kaksi lasta. Lisäksi heidän perheeseensä kuului iloisen kirmakkaan  ");
    crt_writeln(" hehkeä koira - Musti. Eräänä aamuna heidän kotiinsa kannettiin Aamulehti.    ");
    crt_writeln(" Makkaratehtailija ei kuitenkaan liity tähän tarinaan millään tavalla, toisin ");
    crt_writeln(" kuin Pentti-Antero Sillanköykäys, entinen invaliidi, joka tätä nykyä toimii  ");
    crt_writeln(" rohkeasti yhteiskunnan etujen ajamiseksi. Hän on nimittäin invataxin kuljet- ");
    crt_writeln(" taja. Myös Olli oli ammatiltaan posliininmaalaaja. Tosin hänelläkään ei ole  ");
    crt_writeln(" osaa eikä arpaa tämän tarinan muotoutumiseen.                                ");
    crt_writeln(" Pentti-Antero oli ollut lapsesta asti sokea, kuuro, mykkä sekä kaulasta ylös-");
    crt_writeln(" päin halvaantunut. Mutta saavuttaessaan maagisen 27 vuoden iän hän alkoi ko- ");
    crt_writeln(" kea eriskummallisia enneunia. Seuraten totuuden henkeä hän koki parannuksen, ");
    crt_writeln(" hän tunsi voiman leviävän vartalossaan ja kuuli sanat \"The power is yours\".  ");
    crt_writeln(" Kuntouduttuaan hän säntäsi lähimpään Autosiwaan ja osti itselleen Marsu-merk-");
    crt_writeln(" kisen kilometrinielijän. Lapsuudestaan virikkeitä saaneena hän tunsi kutsu-  ");
    crt_writeln(" muksen siirtyä invaliidien palvelu-ammattiin: Hänestä tuli INVAKUSKI!        ");
    crt_writeln(" Mutta vaarat vaanivat Pentti-Anteroa, sillä invaliidit ovat sangen julmia ja ");
    crt_writeln(" säälimättömiä. Pienestäkin virheestä hän saa maksaa kalliisti. Auton ajelu on");
    crt_writeln(" siis tarkkaa hommaa.                                                         ");
    crt_writeln(" Nyt sinä saat olla Pentti-Anteron kengissä ja setviä invaliidien mystisiä sa-");
    crt_writeln(" laviestintöjä.");
    while (!crt_keypressed()) { }
    key = crt_readkey();
}

/* ---------------------------------------------------------------- */
/* HISCORE.NVA: alternating lines "name" / "score", 6 entries, CRLF   */

static const char *default_hiscores[12] = {
    "Zorro", "600", "Tom Of Svenland", "500", "Jorma Kaapeli", "400",
    "Karma Joopeli", "300", "Kekkonen", "200", "Jeesus", "100"
};

typedef struct { FILE *f; int line; } tfile;

/* ReadLn(f, s) */
/* Reads one line from the hiscore file, or from the built-in table if it's missing. */
static void t_readln(tfile *t, char *s)
{
    int c, n = 0;
    s[0] = 0;
    if (!t->f) {                       /* (port) missing file -> built-in table */
        if (t->line < 12) { strncpy(s, default_hiscores[t->line], 255); s[255] = 0; }
        t->line++;
        return;
    }
    while ((c = fgetc(t->f)) != EOF) {
        if (c == '\n') break;
        if (c == '\r') {
            int d = fgetc(t->f);
            if (d != '\n' && d != EOF) ungetc(d, t->f);
            break;
        }
        if (c == 26) { ungetc(c, t->f); break; }   /* ^Z = EOF in DOS text files */
        if (n < 255) s[n++] = (char)c;
    }
    s[n] = 0;
    t->line++;
}

/* Read(f, i); ReadLn(f) */
/* Reads one line and returns it as an integer. */
static int t_readint_ln(tfile *t)
{
    char s[256];
    t_readln(t, s);
    return atoi(s);
}

/* hiscore file strings are CP437; the Crt writer wants UTF-8 */
static void cp437_to_utf8(const char *in, char *out, size_t outsz);

/* 134E: SEE HISCORES */
/* Shows the hiscore list and waits for a key. */
static void show_hiscores(void)
{
    tfile t;
    char name[256], u8[1024];
    int row;
    hide_text_cursor();
    crt_clrscr();
    t.f = game_fopen("hiscore.nva", "rb"); t.line = 0;
    crt_textcolor(0x0c);
    crt_gotoxy(0x1b, 6);
    crt_write("Maailman parhaat invakuskit:");
    crt_textcolor(9);
    row = 9;
    for (i46 = 1; ; i46++) {
        t_readln(&t, name);
        score = t_readint_ln(&t);
        crt_gotoxy(0x1b, row);
        cp437_to_utf8(name, u8, sizeof u8);
        crt_write(u8);
        crt_gotoxy(0x35, row);
        crt_write_int(score);
        row++;
        if (i46 == 6) break;
    }
    if (t.f) fclose(t.f);
    while (!crt_keypressed()) { }
    key = crt_readkey();
}

/* 14CC: you made it to the list - enter name, insert at a random place */
static void enter_hiscore(void)
{
    tfile t;
    FILE *o;
    int pos, entry;
    show_text_cursor();
    crt_writeln("Onneksi olkoon! Olet päässyt parhaiden pelaajien joukkoon.");
    crt_write("Annapa nimesi! ");
    while (crt_keypressed()) crt_readkey();   /* (port) drop keys pressed before the prompt */
    crt_readln(player_name, 255);
    hide_text_cursor();

    t.f = game_fopen("hiscore.nva", "rb"); t.line = 0;
    for (i46 = 1; ; i46++) {
        t_readln(&t, hs_lines[i46]);
        if (i46 == 10) break;
    }
    if (t.f) fclose(t.f);

    crt_textcolor(7);
    pos = tp_random(6) + 1;            /* the "ranking" is a dice roll */

    o = game_fopen("hiscore.nva", "wb");
    if (!o) return;
    {
        int ln = 1;
        for (entry = 1; ; entry++) {
            if (entry == pos) fprintf(o, "%s\r\n%d\r\n", player_name, score);
            fprintf(o, "%s\r\n", hs_lines[ln]); ln++;
            fprintf(o, "%s\r\n", hs_lines[ln]); ln++;
            if (entry == 5) break;
        }
    }
    if (pos == 6) fprintf(o, "%s\r\n%d\r\n", player_name, score);
    fclose(o);
    game_file_commit("hiscore.nva");   /* (port) web version: keep it in the browser */
    show_hiscores();
}

/* 177E: end of game scoring */
/* Shows a (random) final score and randomly decides whether it makes the list. */
static void final_score(void)
{
    crt_textcolor(8);
    crt_writeln("");
    crt_writeln("Pisteesi suhteutettuna peliaikaasi ja rahankeruuseesi on...");
    score = tp_random(1000) + 200;     /* (sic) */
    wait_frames(0x28);
    crt_textcolor(10);
    crt_write_int(score);
    crt_writeln("");
    crt_writeln("");
    crt_textcolor(0x0d);
    tmp101c = tp_random(2);
    if (tmp101c == 1) enter_hiscore();
    if (tmp101c == 0) {
        crt_writeln("Valitettavasti et päässyt parhaiden pelaajien joukkoon.");
        crt_writeln("Sinusta ei ole invakuskiksi. Sinusta tulee taas invaliidi.");
        key = crt_readkey();
        show_hiscores();
    }
}

/* ---------------------------------------------------------------- */
/* 18B9: menu frame                                                   */

/* Draws the frame of the main menu. */
static void menu_box(void)
{
    crt_textcolor(0x0d);
    crt_gotoxy(0x1b, 6);    crt_write("┌────────────────────┐");
    crt_gotoxy(0x1b, 0x10); crt_write("└────────────────────┘");
    crt_gotoxy(0x1b, 8);    crt_write("│                    │");
    crt_gotoxy(0x1b, 9);    crt_write("│                    │");
    crt_gotoxy(0x1b, 10);   crt_write("│                    │");
    crt_gotoxy(0x1b, 0xb);  crt_write("│                    │");
    crt_gotoxy(0x1b, 0xc);  crt_write("│                    │");
    crt_gotoxy(0x1b, 0xd);  crt_write("│                    │");
    crt_gotoxy(0x1b, 0xe);  crt_write("│                    │");
    crt_gotoxy(0x1b, 7);    crt_write("│                    │");
    crt_gotoxy(0x1b, 0xf);  crt_write("│                    │");
}

/* 1A7B: title picture with the opening speech */
static void intro(void)
{
    exec_hjuva(pic_title);              /* Exec('hjuva_#1.exe', 'alkukuvaa') */
    pm_set_volumes(0x5a, 0x5a, 0x5a, 0x5a);
    say("1.skp", 0x17c, 200);
    say("3.skp", 0x186, 0xf4);
    pm_set_volumes(0xbe, 0xbe, 0xbe, 0xbe);
    while (!crt_keypressed()) { }
    key = crt_readkey();
    vga_set_mode(0x03);
}

/* ---------------------------------------------------------------- */
/* 1BD6: a new customer mumbles where he wants to go                   */

typedef struct { const char *file; int period; int frames; int id; } speech_t;

static const speech_t destinations[15] = {
    { "9.skp",  300,   0x6c, 9  },
    { "10.skp", 0x154, 0xa7, 10 },
    { "12.skp", 300,   0x4e, 12 },
    { "13.skp", 0x14a, 0x41, 13 },
    { "14.skp", 0x168, 0xa6, 14 },
    { "15.skp", 0x140, 0x5c, 15 },
    { "16.skp", 300,   0x71, 16 },
    { "17.skp", 0x14a, 0x9e, 17 },
    { "18.skp", 0x172, 0xd8, 18 },
    { "19.skp", 0x172, 0x91, 19 },
    { "20.skp", 0x140, 99,   20 },
    { "21.skp", 0x14a, 0xbb, 21 },
    { "22.skp", 300,   0x93, 22 },
    { "23.skp", 0x122, 0x55, 23 },
    { "24.skp", 0x140, 0x42, 24 },
};

/* Plays a greeting and a random destination speech, and sets destination. */
static void new_customer(void)
{
    wait_frames(0x32);
    pm_set_volumes(100, 100, 100, 100);
    rnd = tp_random(7);
    if (rnd == 0) say("6.skp", 0x140, 0x6b);
    if (rnd == 1) say("5.skp", 300, 0x47);
    if (rnd >= 2) say("2.skp", 0x118, 0x29);
    wait_frames(0x32);
    rnd = tp_random(15);
    say(destinations[rnd].file, destinations[rnd].period, destinations[rnd].frames);
    destination = destinations[rnd].id;
#ifdef INVATAXI_AUTOTEST
    fprintf(stderr, "destination %d\n", destination);
#endif
    pm_set_volumes(0xbe, 0xbe, 0xbe, 0xbe);
}

/* 21B6: which place on the map was clicked? (mouse x is 0..639) */

typedef struct { int x0, y0, x1, y1, id; } region_t;   /* exclusive bounds */

static const region_t regions[] = {
    { 0x1fe, 0x8b, 0x254, 0xb2, 12 },
    { 0x30,  0x89, 0x9a,  0xbf, 19 },
    { 0x0c,  0x92, 0x44,  0xbe, 19 },
    { 0x19e, 0x91, 0x1c8, 0xa8, 16 },
    { 0x104, 0xa2, 0x144, 0xb5, 22 },
    { 0xa4,  0x6c, 0xfa,  0x87, 10 },
    { 0x04,  0x6b, 0x26,  0x89, 18 },
    { 0x24,  0x6a, 0x40,  0x81, 18 },
    { 0x1fa, 0x59, 0x25c, 0x77, 9  },
    { 0x196, 0x61, 0x1d6, 0x7e, 15 },
    { 0x10e, 0x3c, 0x180, 0x51, 17 },
    { 0x12a, 0x4f, 0x16e, 0x65, 17 },
    { 0x9a,  0x42, 0xe2,  0x5e, 24 },
    { 0x1a,  0x2e, 0x6c,  0x5f, 14 },
    { 0x1fa, 0x08, 0x26e, 0x3e, 21 },
    { 0x140, 0x07, 0x1a2, 0x2c, 23 },
    { 0xc2,  0x03, 0x10e, 0x17, 20 },
    { 0x0c,  0x03, 0x60,  0x21, 13 },
};

/* Sets choice to the place under the mouse (1 = none); a hit ends the wait loop. */
static void map_click(void)
{
    size_t i;
    choice = 1;
    for (i = 0; i < sizeof regions / sizeof regions[0]; i++) {
        const region_t *r = &regions[i];
        if (mouse_x > r->x0 && mouse_x < r->x1 && mouse_y > r->y0 && mouse_y < r->y1)
            choice = r->id;
    }
    if (choice != 1) i46 = 400;      /* ends the waiting loop */
}

/* 247F: drive the customer - the taxi rolls along until a key is hit */
static void drive(void)
{
    exec_hjuva(pic_lefty);              /* Exec('hjuva_#2.exe', 'autoo') */
    taxi_x = 0x102;
    taxi_y = 0xb2;
    grab_taxi();
    taxi_x = 0x102;
    game_over = 0;
    do {
        pm_play_instrument(200, 4);     /* engine */
        for (i46 = 1; ; i46++) {
            draw_taxi();
            taxi_x -= 2;
            if (taxi_x < 0) taxi_x = 0x140;
            vsync();                    /* 002D: wait for retrace start */
            if (i46 == 0x31) break;
        }
    } while (!crt_keypressed());
    if (crt_keypressed()) key = crt_readkey();

    wait_frames(10);
    if (choice != destination) say("11.skp", 0x122, 0x74);
    if (choice == destination) say("8.skp", 0x122, 0x32);
    show_map();
    wait_frames(0x28);
    if (choice == destination) {
        money_delta = money_delta + tp_random(14) + 1;
        money_bar_increase();
    }
    if (choice != destination) {
        money_delta = 0;
        money_bar_increase();
        lives--;
        pm_play_instrument(300, 14);    /* trombone */
        draw_lives();
    }
    mouse_show();
}

/* 262B: the customer got tired of waiting */
static void impatient_customer(void)
{
    say("7.skp", 0x172, 0x15c);
    money_delta = money_delta + tp_random(20) + 6;
    money_bar_decrease();
}

/* 2697: ALOITA PELI */
/* Runs one game: customers keep coming until all lives are lost, then scoring. */
static void play_game(void)
{
    pm_jump(1, 0);
    lives = 3;
    money = 10;
    money_delta = 0;
    show_map();
    money_bar_increase();
    mouse_reset();
    mouse_show();
    mouse_setpos(0, 0);
    mouse_getpos(&mouse_x, &mouse_y);
    game_over = 0;
    do {
        new_customer();
        i46 = 1;
        choice = 0;
        while (i46 < 0x15e) {
            mouse_getpos(&mouse_x, &mouse_y);
            if (mouse_left_down()) map_click();
            vsync();
            i46++;
            if (crt_keypressed()) {
                key = crt_readkey();
                if (key == 27) i46 = 400;
                if (key == 27) lives = 0;
                key = 'a';
            }
        }
        if (choice != 0) drive();
        if (choice == 0 && lives > 0) impatient_customer();
        if (lives == 0) game_over = 1;
    } while (!game_over);

    pm_play_instrument(200, 4);
    for (i46 = 1; vsync(), i46 != 0x35; i46++) { }
    pm_play_instrument(200, 4);
    for (i46 = 1; vsync(), i46 != 0x35; i46++) { }
    vga_set_mode(0x03);
    final_score();
    pm_jump(0x20, 0);
}

/* 2808: the secret "9" key */
/* Shows the cheat key message and waits for Enter. */
static void cheat(void)
{
    char dummy[128];
    crt_clrscr();
    crt_writeln("");
    crt_writeln("Kiva! Jätkä löysi cheatti-nappulan. Onneks olkoon!");
    crt_readln(dummy, 127);
}

/* ---------------------------------------------------------------- */
/* asset loading                                                      */

#include "pklite.h"

/* Finds a 320x200 ColoRIX (RIX3) picture in a memory image; NULL if none. */
static const unsigned char *find_rix(const unsigned char *img, size_t len)
{
    size_t i;
    for (i = 0; i + 0x30A + 64000 <= len; i++) {
        if (img[i] == 'R' && img[i + 1] == 'I' && img[i + 2] == 'X' && img[i + 3] == '3' &&
            img[i + 4] == 0x40 && img[i + 5] == 0x01 && img[i + 6] == 0xC8 && img[i + 7] == 0x00)
            return img + i;
    }
    return NULL;
}

/* Loads an EXE, unpacks it and returns the RIX picture inside it (NULL if none). */
static const unsigned char *load_picture(const char *exe)
{
    size_t len, ulen;
    unsigned char *raw = game_load(exe, &len), *un;
    const unsigned char *r;
    if (!raw) { fprintf(stderr, "invataxi: cannot open %s\n", exe); return NULL; }
    un = pklite_unpack(raw, len, &ulen);
    if (un) {
        r = find_rix(un, ulen);
        if (r) { free(raw); return r; }
        free(un);
    }
    /* maybe someone already unpacked the EXE */
    r = find_rix(raw, len);
    if (r) return r;
    fprintf(stderr, "invataxi: no picture found in %s\n", exe);
    return NULL;
}

/* Loads the map, title and taxi pictures from the original EXEs; 1 if all found. */
int game_load_assets(void)
{
    pic_map   = load_picture("INVATAXI.EXE");
    pic_title = load_picture("HJUVA_#1.EXE");
    pic_lefty = load_picture("HJUVA_#2.EXE");
    return pic_map && pic_title && pic_lefty;
}

static void cp437_to_utf8(const char *in, char *out, size_t outsz);

#include "cp437.h"
/* Converts a CP437 string to UTF-8. */
static void cp437_to_utf8(const char *in, char *out, size_t outsz)
{
    size_t o = 0;
    const unsigned char *p = (const unsigned char *)in;
    for (; *p && o + 4 < outsz; p++) {
        unsigned u = (*p < 0x80) ? *p : cp437_uni[*p];
        if (u < 0x80) out[o++] = (char)u;
        else if (u < 0x800) { out[o++] = (char)(0xC0 | (u >> 6)); out[o++] = (char)(0x80 | (u & 0x3F)); }
        else { out[o++] = (char)(0xE0 | (u >> 12)); out[o++] = (char)(0x80 | ((u >> 6) & 0x3F)); out[o++] = (char)(0x80 | (u & 0x3F)); }
    }
    out[o] = 0;
}

#ifdef __EMSCRIPTEN__
/* (port) Web version: shows the title picture while the page waits for the
   Start click. game_main() then continues from the same picture. */
void game_show_title(void)
{
    exec_hjuva(pic_title);
}
#endif

/* ---------------------------------------------------------------- */
/* 28AF: main program                                                  */

/* Main program: sound setup and intro, then the main menu until the player quits. */
void game_main(void)
{
    tp_randomize();
    crt_clrscr();
    sound_setup();
    quit_program = 0;
    intro();
    do {
        pm_jump(0x20, 0);
        hide_text_cursor();
        crt_clrscr();
        menu_box();
        crt_textcolor(0x0e);
        crt_gotoxy(0x1e, 5);  crt_write("    INVATAXI    ");
        crt_gotoxy(0x32, 0x13); crt_write("    ÅKESOFT     ");
        crt_textcolor(0x0c);
        crt_gotoxy(0x1e, 8);  crt_write("1   ALOITA PELI");
        crt_gotoxy(0x1e, 10); crt_write("2   TAUSTATARINA");
        crt_gotoxy(0x1e, 12); crt_write("3   SEE HISCORES");
        crt_gotoxy(0x1e, 14); crt_write("4   QUIT");
        key = 'a';
        menu_sel = 0;
        do {
            key = crt_readkey();
            if (key == '1') menu_sel = 1;
            if (key == '2') menu_sel = 2;
            if (key == '4') menu_sel = 3;
            if (key == 'Q') menu_sel = 3;
            if (key == 'q') menu_sel = 3;
            if (key == '3') menu_sel = 4;
            if (key == '9') menu_sel = 5;
        } while (menu_sel == 0);
        if (menu_sel == 1) play_game();
        if (menu_sel == 2) story();
        if (menu_sel == 3) quit_program = 1;
        if (menu_sel == 4) show_hiscores();
        if (menu_sel == 5) cheat();
    } while (!quit_program);
    quit_to_dos();
}
