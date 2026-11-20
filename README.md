# Macro Tool

Free macro tool for Fortnite, written in C++ (Win32 + Direct2D). You bind a key to a
list of key/mouse presses and it plays them back. Edits, builds, movement, whatever.
No key needed.

It only sends the inputs you set up yourself. No aimbot, no triggerbot, no ESP. The
only thing that looks at the screen is Edit Fix (off by default): it checks a few
pixels in the middle of your screen for the blue edit colour and taps your reset key
if you got stuck in edit mode.

## Features

- Main page: your keyboard, mouse and controller with which macro sits on which key,
  live as you press them
- Controller support: controller buttons can trigger macros (Xbox pads right away,
  PlayStation and Switch pads through Steam Input or DS4Windows)
- Macro engine on low-level hooks, running on its own high priority thread so timing
  stays tight even when the PC is busy
- Frame timing. "Frame better" adds a tiny random jitter so taps register reliably,
  "Frame legacy" is one frame per step, exact
- Built-in Fortnite macros: Single Edit, Drag Edit, Edit Reset, Edit Spam, Wall + Edit
  Peek and more, sorted by category
- Macro editor with syntax colouring, quick insert chips, a recorder, repeat/toggle and
  on-press / on-release steps
- In-game menu: press Insert in game and you get a menu over Fortnite with a column per
  category. Click a macro to turn it on or off. Insert or Esc closes it and you're back
  in the game. It's a normal window, nothing gets injected. Needs borderless/windowed
- Edit / reset on release: if you have those turned on in Fortnite, switch this on too
  and the edit macros stop pressing the extra confirm key
- Edit Fix: resets a stuck edit automatically, plus "Key check" which spaces out edit
  spam so it doesn't jam. Needs borderless/windowed
- Profiles and share codes. `MTP1:...` is your whole setup, `MT1:...` is one macro
- Windows tweaks page and an input tester
- Light/dark theme, search (`/` or Ctrl+F), keyboard shortcuts

## How to use

1. Run `MacroTool.exe`. It wants admin, because Windows doesn't let a normal app send
   input into a game that runs as admin.
2. Settings > Game keybinds: put in your Fortnite binds so `{Edit}`, `{Wall}` etc. are
   your keys. Then give the macros you want a trigger key.
3. F8 turns everything on/off. By default macros only fire when Fortnite is the active
   window.
4. Insert opens the in-game menu (Settings > General > In-game menu). If you use Edit /
   Reset on release in Fortnite, turn on "Edit / reset on release" there as well.

### Defender says it's a virus

It's an unsigned exe that runs as admin and hooks the keyboard, so some antivirus
engines flag it (usually as `Wacatac`). That's a false positive. To run it click More
info > Run anyway in SmartScreen, or allow it in Defender. A signing certificate would
fix this but it costs money.

## Shortcuts

| Key | What it does |
|-----|--------|
| F8 | Macros on/off |
| Insert | In-game menu |
| `/` or Ctrl+F | Search |
| Ctrl+N | New macro |
| Ctrl+S | Save (editor) |
| Ctrl+Enter | Save from the steps box |
| Esc | Close dialog / stop recording |

## Disclaimer

Use at your own risk, no warranty. Macros can break Fortnite / Epic Games terms of
service and you can get banned for it. That's on you, not on whoever wrote this.

## License

Free to use, change and share. No warranty.
