MACOS
-----
Requires macOS 10.13 or newer. invataxi is a universal binary that runs
natively on both Apple silicon and Intel Macs. SDL2 is built into it, so
no other files are needed.

The program is not signed with an Apple Developer ID, so macOS blocks it
the first time. Either remove the quarantine flag in Terminal:

    xattr -d com.apple.quarantine invataxi

or try to open it once, then go to System Settings > Privacy & Security
and click "Open Anyway".

Start the game by double-clicking invataxi in Finder, or from Terminal:

    ./invataxi
