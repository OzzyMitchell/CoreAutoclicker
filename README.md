# CoreAutoclicker

A native Windows autoclicker written in C.

[Download the latest release](https://github.com/OzzyMitchell/CoreAutoclicker/releases/latest)

## Controls

- Toggle left click: `F6`
- Toggle right click: `F7`
- Emergency close: `F9`
- Click rate: `1–1,000 CPS`

`F6` and `F7` can be changed in the app. `F9` is fixed.

## Input modes

`Universal SendInput` is enabled by default and works with most applications.

When disabled, clicks are posted directly to the window under the cursor. This
is useful for testing, but many applications ignore posted messages.

## Timing

Missed clicks are discarded rather than replayed in a burst.

CoreAutoclicker uses QPC period timing, a high-resolution waitable timer when
available, and a bounded final spin. Low-CPU timing is always enabled.

## Build

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

## License

Copyright 2026 Ozzy M.

Licensed under the Apache License, Version 2.0.
