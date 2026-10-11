# INVATAXI – reverse-engineering notes

Åkesoft, *INVATAXI*, 17.10.1994. Code: "Kekkonen", music: "Tfosekå", graphics:
"Tom Of Svenland". These notes describe what the original DOS release contains
and how the portable C version maps onto it.

## Files

| File | What it is |
|---|---|
| `INVATAXI.EXE` | The game. Turbo Pascal 6.0 program, packed with PKLITE 1.12 (`-e`, "large + extra" compression). Contains the map picture. |
| `HJUVA_#1.EXE` | Tiny TP6 helper: if `ParamStr(1) = 'alkukuvaa'`, set mode 13h and show the title picture, then exit. |
| `HJUVA_#2.EXE` | Same helper with parameter `autoo`: shows the "driving" picture (a bar facade with a yellow taxi at the bottom). |
| `HARMONIA.NVA` | 4-channel ProTracker module (`M.K.`), title "Harmoniaa kansalle", 41 orders, 15 patterns. |
| `HISCORE.NVA` | Text file, CRLF: 6 × (name line, score line). |
| `1.SKP` … `24.SKP` | Raw signed 8-bit mono PCM speech, no header. |
| `INVATAXI.TXT`, `FILE_ID.DIZ` | Docs (Finnish). |

### Pictures
All three pictures are ColoRIX `RIX3` images linked into the EXEs as data:

```
+0   "RIX3"
+4   u16 width  (320)
+6   u16 height (200)
+8   u8  palette type 0xAF (VGA, 6-bit)
+9   u8  storage type 0 (raw)
+10  768 bytes palette (6-bit RGB)
+778 64000 bytes pixels
```
In the unpacked load module of `INVATAXI.EXE` the image is the whole segment
`05BAh`; in the HJUVA programs it is segment `0009h`. The game copies
`ptr+0x30A` to `A000:0000` with `rep movsb` (64000 bytes) and uploads `ptr+10`
through port 3C8h/3C9h.

The port unpacks the EXEs at runtime (`src/pklite.c`, output verified
byte-identical to Deark) and searches for the `RIX3` header, so no extracted
pictures need to be distributed.

## EXE layout (unpacked INVATAXI.EXE, load-module offsets)

| Segment | Size | Contents |
|---|---|---|
| 0000 | 5BA0h | main program (0000–2A8D) + PaxiMod v9.06 player (2A8E–5B9F, linked as .OBJ) |
| 05BA | FCEAh | the map picture |
| 158B | 1D0h | small mouse unit (INT 33h wrappers) |
| 15A8 | 140h | Dos unit (Intr, Exec, SwapVectors) |
| 15BC | 620h | Crt |
| 161E | B00h | System |
| 16CE | – | data segment |

### Runtime library entry points used
System `161E`: `027C` stack check, `0292` Assign, `0302` Reset, `0307` Rewrite,
`035C` Close, `05B7` ReadLn, `05E5` WriteLn, `0609` Write-flush, `0633` read string,
`066B` write string, `06A9` read integer, `0701` write integer, `0764` string
assign, `0951` Random, `09D8` Randomize, `00E9` Halt.
Crt `15BC`: `01C0` ClrScr, `0213` GotoXY, `0257` TextColor, `029C` Delay,
`02FB` KeyPressed, `030D` ReadKey.
Mouse `158B`: `0000` reset (AX=21h), `00A2` left button down (AX=05h, status
bit 0), `00DD` show, `0124` get position, `017F` set position.

## Game procedures (segment 0000)

| Offset | Port function | Notes |
|---|---|---|
| 0000/0017 | `show_picture` | copy pixels / upload palette |
| 002D/0036 | `vsync` | wait for retrace (3DAh bit 3) – all timing is in 70 Hz frames |
| 0044/0068 | hide/show text cursor | INT 10h AH=1, CX=2607h / 0607h |
| 0267 | `sound_setup` | device + rate menu, loads `harmonia.nva` |
| 0619 | `quit_to_dos` | "Åkesoft on maailman paras!", music fade |
| 0694/06B7 | put/get pixel | `mem[$A000:y*320+x]` |
| 06E1/0739 | `grab_taxi`/`draw_taxi` | 63×23 block from (247,177), drawn at y=178 moving 2 px/frame left |
| 07B0 | `money_bar_decrease` | |
| 08B5 | `money_bar_increase` | bar at y 192–199, x from 37 |
| 09A4 | `draw_lives` | three squares next to "EMÄLÄT" |
| 0A7A | `show_map` | |
| 10B6 | `story` | TAUSTATARINA |
| 134E | `show_hiscores` | |
| 14CC | `enter_hiscore` | |
| 177E | `final_score` | |
| 18B9 | `menu_box` | |
| 1A7B | `intro` | `Exec('hjuva_#1.exe','alkukuvaa')`, speech 1.skp + 3.skp |
| 1BD6 | `new_customer` | greeting (6/5/2.skp) + destination (9,10,12–24.skp) |
| 21B6 | `map_click` | 18 hit rectangles in mouse coordinates (x 0–639) |
| 247F | `drive` | `Exec('hjuva_#2.exe','autoo')`, taxi animation until a key, 8.skp right / 11.skp wrong |
| 262B | `impatient_customer` | 7.skp, lose random(20)+6 money |
| 2697 | `play_game` | |
| 2808 | `cheat` | key 9 in the main menu |
| 28AF | `game_main` | program entry |

### Game rules (as coded)
* 3 lives, 10 money. Each round a customer speaks a greeting and a destination.
* You have 350 frames (5 s) to click a place. Clicking empty map counts as a
  (wrong) choice but keeps the timer running; a later click can still change it.
* After a choice the taxi drives **until you press a key**. Right place:
  money += random(14)+1. Wrong: lose a life (trombone sound).
* No click: money −= random(20)+6.
* ESC sets lives to 0. Game ends when lives reach exactly 0 (pressing ESC and
  having also clicked a wrong place sends lives to −1 and the game goes on –
  an original bug, kept).
* Final score is `random(1000)+200`; whether you make the high-score list is
  `random(2)`, and your position in it is `random(6)+1`. Money and time are not
  used at all, despite the text.

### Destinations
| Speech | Id | Hit rectangle(s) (mouse x range, y range) |
|---|---|---|
| 9.skp | 9 | 507–603, 90–118 |
| 10.skp | 10 | 165–249, 109–134 |
| 12.skp | 12 | 511–595, 140–177 |
| 13.skp | 13 | 13–95, 4–32 |
| 14.skp | 14 | 27–107, 47–94 |
| 15.skp | 15 | 407–469, 98–125 |
| 16.skp | 16 | 415–455, 146–167 |
| 17.skp | 17 | 271–383, 61–80 and 299–365, 80–100 |
| 18.skp | 18 | 5–37, 108–136 and 37–63, 107–128 |
| 19.skp | 19 | 49–153, 138–190 and 13–67, 147–189 |
| 20.skp | 20 | 195–269, 4–22 |
| 21.skp | 21 | 507–621, 9–61 |
| 22.skp | 22 | 261–323, 163–180 |
| 23.skp | 23 | 321–417, 8–43 |
| 24.skp | 24 | 155–225, 67–93 |

(Map pixel = mouse x / 2.)

## PaxiMod v9.06

Far-call API (Pascal calling convention, `cs:5395` flag selects Pascal strings):

| Entry | Use |
|---|---|
| 55AE | detect hardware |
| 543C | `load(file, device, rate, mode=0, loop=4, var err)` |
| 53DF | set the four channel volumes (0–255) |
| 564A | jump to order/row (`AL`=order, `BX`=row×16) |
| 56E8 | play MOD instrument N once on the effect channel at "period" P |
| 580D | load a raw sample file (≤ 65535 bytes) and play it on the effect channel |
| 54E7 | stop |

Details that matter for sound:
* Pitch: `step = K / period` (8.8 fixed point) with `K = (1193182 / rate) * 768`,
  i.e. ≈ 3 579 546 / period Hz – the NTSC Amiga clock.
* Tick rate: `rate / (bpm*6/15 + 1)` samples (default divisor 51).
* Effective volume = note volume × channel volume >> 8, recomputed only on
  instrument set / volume effects.
* Mixer: each voice adds `(s × vol × 2) >> 8`; when the effect channel is
  active the music and effect sample are each halved before summing.
* Loops only when repeat length ≥ 4 words; voice end = length×2 − 3.
* Finetune supported (standard 16×36 ProTracker period table at 3D0Fh).

Game usage: menu music starts at order 32 (`564A(0x20)`), in-game music at
order 1. Music volume is ducked to 90 during the intro speech, 100 during the
customer speech and 190 otherwise. Instrument 4 (engine, period 200),
12 ("Kiva", money tick, 200/300) and 14 (trombone, 300).

Speech files and the playback periods used:

| File | Period | Wait (frames) | Use |
|---|---|---|---|
| 1.skp | 380 | 200 | intro |
| 3.skp | 390 | 244 | intro |
| 2/5/6.skp | 280/300/320 | 41/71/107 | greetings |
| 7.skp | 370 | 348 | impatient customer |
| 8.skp | 290 | 50 | right place |
| 11.skp | 290 | 116 | wrong place |
| 9–24.skp | 290–370 | 65–216 | destinations |
