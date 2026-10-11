INVATAXI - portable C / SDL2 port
Version @VERSION@
https://github.com/klassikkopelit/invataxi

A reverse-engineered port of INVATAXI (Åkesoft, 1994), the Finnish DOS game
where you drive an "invataxi" and have to work out where your mumbling
passenger wants to go.


YOU NEED THE ORIGINAL GAME FILES
--------------------------------
This package contains only the game program. It reads the original game
files at runtime, so you need your own copy of the original game:

    INVATAXI.EXE   HJUVA_#1.EXE   HJUVA_#2.EXE
    HARMONIA.NVA   HISCORE.NVA    1.SKP ... 24.SKP

The easiest way: copy everything from this package into the folder that
holds the original game files (or the original files into this folder) and
start the game there.

The game folder can also be given on the command line:

    invataxi /path/to/original/game
    invataxi --sound-menu /path/to/original/game

--sound-menu shows the original sound card / mixing rate menu.
HISCORE.NVA is updated in place, as in the original.

@PLATFORM_NOTES@

CONTROLS
--------
Menus               number keys
Pick a destination  left mouse button on the map
Stop the taxi       any key
Give up             Esc
Fullscreen          Alt+Enter or F11


CREDITS
-------
INVATAXI (c) Åkesoft 1994.
SDL2 (https://libsdl.org) - zlib license, see LICENSE-SDL2.txt.
IBM VGA 8x16 font from VileR's "VGA text mode fonts"
(https://github.com/viler-int10h/vga-text-mode-fonts).
PKLITE decompression follows Jason Summers' Deark.
