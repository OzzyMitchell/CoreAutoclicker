# CoreAutoclicker

A compact native Windows autoclicker written in C. The executable includes a
small private Lexend Regular font; no installation, GPU framework, or runtime
redistributable is required.

## Controls

- `F6`: toggle left clicking. `F7`: toggle right clicking.
- `F9`: close immediately, including while editing a shortcut.
- Select a shortcut field, press a key combination, then **Apply hotkeys**.
  Shortcut capture pauses clicking. Duplicate or occupied shortcuts are rejected.
- Set **1-1,000 clicks/sec** by dragging or clicking the slider, or typing a number.
  Arrow keys adjust by 1; Page Up/Down by 10; Home/End select the endpoints.
  Valid rate edits apply immediately. Empty or invalid text is restored on commit;
  numeric values outside the range are clamped on Enter or focus loss.
- **Use SendInput** is enabled by default. Turning it off posts mouse messages to
  the window under the cursor; many applications ignore these messages.
- **Cursor jitter** adds a small random movement before each click.

Settings last for the current session. The status label shows which button is
active. Windows can block input to an app running at a higher integrity level;
failed input pauses clicking instead of silently continuing.

## Window size

Resize the window to scale the entire form: Lexend text, fields, buttons,
checkboxes, spacing and slider all grow or shrink together. The proportions stay
fixed, and the form is centered when the window has extra width or height.
The default stays compact; the minimum size is 75% of the default. Maximizing
fits the form to the available screen area. Monitor DPI changes are also handled.

The slider thumb and focus ring are antialiased circles. Their small bitmap is
cached between scale/theme changes; dragging reuses it.

## Timing and compatibility

The worker sleeps on an event while paused. While active it uses QPC deadlines
and a cancellable, one-shot waitable timer. It does not busy-spin, change the
system timer resolution, or raise thread priority. Fractional periods carry their
remainder forward. Missed deadlines are discarded; no backlog is replayed.

A high-resolution timer is requested on systems that support it; a normal
waitable timer is the fallback. This favors low CPU use over sub-millisecond
precision. The target CPS is a request: actual timing depends on Windows,
hardware, system load, and the receiving application. The fallback can be much
less accurate at high rates. See [verification](VERIFICATION.md) for measurements.

The interface uses GDI and native keyboard-accessible controls, handles DPI
changes, and uses system colors in High Contrast mode. x64 and x86 builds are
provided. Windows 7 API fallbacks are retained, but runtime testing so far is on
one Windows 11 x64 machine. Older Windows, ARM64, other DPI settings, and other
hardware have not been runtime-tested.

## Build

Install the Visual Studio C++ desktop build tools, a Windows SDK, and LLVM
clang-cl (standalone or the Visual Studio LLVM component). Then run:

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Architecture x86
```

Outputs go to `build/<architecture>` and `dist`. `-NoPackage` skips the ZIP.
`-Architecture arm64` also requires the ARM64 C++ tools; this target is unverified.
The x86 link takes only the required integer-division support from `libcmt.lib`;
there is no CRT startup or runtime DLL dependency.

The included font is ready to build. Font regeneration instructions are in
[assets/README.md](assets/README.md). Timing checks intercept input so that they
never click on the desktop:

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\benchmark.ps1
powershell -ExecutionPolicy Bypass -File .\tests\check-ui.ps1
```

## License

Copyright 2026 Ozzy M. Code: Apache License 2.0, see [LICENSE](LICENSE).
Bundled Lexend subset: SIL Open Font License 1.1, see
[assets/OFL-Lexend.txt](assets/OFL-Lexend.txt).
