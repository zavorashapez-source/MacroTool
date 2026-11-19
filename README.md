# Macro Tool — free & open source

A fast, native **C++/Direct2D** macro tool for Fortnite. Input macros only — bind
a key to a sequence of key/mouse presses (edit, build, movement, etc.). Free,
no key, full source included.

> **What this is NOT:** there is **no aimbot, no trigger bot, no ESP, no screen
> overlay reading players.** This tool only sends keyboard/mouse inputs you have
> configured. The only screen reading it does (optional, off by default) is the
> "Edit Fix" feature, which samples the centre of your own screen for the blue
> edit-overlay colour so it can tap your reset key if you get stuck in edit mode.

## Features

- **Native macro engine** — low-level keyboard/mouse hooks on a dedicated
  high-priority thread, so inputs stay tight even under load.
- **Frame-sync timing** — "Frame better" humanises step timing with tiny random
  jitter so taps always register; "Frame legacy" is exact frame-per-step.
- **Ready-made Fortnite macros** — Single Edit, Drag Edit, Edit Reset, Edit Spam,
  Wall + Edit Peek, and more, organised by category.
- **Macro editor** — name, category, trigger key, on-press / on-release steps,
  colour-coded syntax, quick-insert chips, step validation, in-game recorder,
  repeat / toggle / block-key options.
- **Edit Fix** — optional auto-recovery from a stuck edit: colour detection taps
  your reset key, plus a Key-check throttle that spaces rapid edit spam so it
  can't jam. (Needs borderless/windowed.)
- **Profiles & setup codes** — save/switch whole setups, and share all your
  macros with one `MTP1:…` code (or one macro with `MT1:…`).
- **Tweaks & Tester** — handy Windows tweaks and a live input tester.
- **Premium UI** — Direct2D, smooth spring animations, light/dark themes,
  command search (`/` or `Ctrl+F`), keyboard shortcuts.

## Running it

1. Run `MacroTool.exe`. It asks for **Administrator** — Windows requires elevation
   for an app to send input into a game running as admin.
2. Set your in-game keybinds in **Settings → Game keybinds** (so `{Edit}`, `{Wall}`
   resolve to your keys), then give each macro a trigger key.
3. **F8** turns all macros on/off. By default macros only fire while Fortnite is
   the focused window.

### Windows Defender / "virus" warning

The exe is **unsigned** and installs a global keyboard hook and runs as admin —
that trips heuristic flags (e.g. `Wacatac`) on some engines. It is a **false
positive**. You can read every line of source here and build it yourself. To run
it, click **More info → Run anyway** in SmartScreen, or allow it in Defender.
A code-signing certificate would remove the warning but costs money.

## Building from source

Requires **Visual Studio 2022** with the "Desktop development with C++" workload.

```
build.bat
```

This produces `bin\MacroTool.exe` (admin manifest) and `bin\MacroTool-dev.exe`
(no UAC, for testing). The build is `/O2 /MT /LTCG`, no PDB, static CRT —
a single self-contained ~1 MB exe, no DLLs.

## Keyboard shortcuts

| Key | Action |
|-----|--------|
| `F8` | Master on/off |
| `/` or `Ctrl+F` | Search |
| `Ctrl+N` | New macro |
| `Ctrl+S` | Save (in editor) |
| `Ctrl+Enter` | Save from the steps box |
| `Esc` | Close dialog / stop recording |

## Disclaimer

Provided **as-is, for educational purposes**, with no warranty. Input automation
may violate the terms of service of Fortnite / Epic Games and **could get your
account banned**. You are solely responsible for how you use it. The authors are
not liable for any consequences.

## License

Free to use, modify and share. No warranty.
