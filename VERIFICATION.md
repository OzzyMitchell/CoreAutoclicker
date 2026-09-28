# Verification - 2026-09-28

Local checkout: branch `codex/lexend-efficient-ui`, based on
`a6a32fc885217c5ca30850b344707d4fd6914ed7`.

## Builds

Clang 22.1.4, Visual Studio 18 C++ tools, Windows SDK 10.0.26100.0.
Both builds pass `/W4 /WX` with size optimization (`-Oz`).

| Artifact | Executable bytes |
| --- | ---: |
| Windows x64 | 33,792 |
| Windows x86 | 31,744 |
| Embedded Lexend subset (included above) | 14,020 |

The main C source is 603 lines, down from 876. It remains one translation unit.
Both executables import only Windows system DLLs: KERNEL32, USER32, GDI32,
COMCTL32, DWMAPI and MSIMG32. The x86 build links the needed 64-bit integer-division helper
statically; neither build requires a CRT DLL or a font installation.

## Native UI checks

Tested using the computer-use skill on Windows 11 Home x64, build 26200, at the
current monitor's scale. The x64 UI received the full interaction pass; the final
x86 build also launched and rendered correctly and passed invalid/empty input checks.

- Compact layout and embedded Lexend visibly render.
- Slider drag, Home/End endpoints, and 1-CPS arrow-key adjustment work.
- Numeric input and slider stay synchronized (including an exact 333 CPS entry).
- Enter commits 0 as 1; leaving 9999 commits 1000. Invalid and empty text restore
  the last valid rate instead of changing the active rate.
- Ctrl+Shift+F8 capture and applying a valid shortcut pair work.
- Duplicate shortcuts are rejected with an inline explanation.
- Tab navigation and checkbox mouse/Space behavior work.
- Maximize/restore preserves usable controls and restores the compact layout.
- Left and right hotkeys each start/stop at 1 CPS; status changes are visible.
  The cursor was positioned over the app's inert status area for actual input.
- F9 closes the app cleanly.

Found and fixed during this pass: stale slider paint after rate changes, focus
restoration on returning to the window, and checkbox tab order.

DPI changes across monitors, High Contrast, non-English keyboard layouts,
Windows 7/8/10, and native ARM64 hardware have not been runtime-tested. Their
code paths must not be described as universal hardware validation.

## Circle and scaling follow-up

- The form now scales uniformly and centers itself within the resized client area.
  The default is 420 x 298 logical pixels; the minimum is 315 x 224.
- Manual checks covered default size, maximized size, the minimum size and a
  narrow/tall window. Fonts and controls scaled together without clipping.
- Slider mouse input and Home/End stayed synchronized at both the large and small
  sizes. The checkbox tick also scales geometrically rather than staying one pixel.
- The thumb and focus ring now use cached premultiplied-alpha circular coverage,
  generated with 8x8 subpixel samples. No new rendering framework is used.
- `tests/check-ui.ps1` passed 626 focused/unfocused sprite checks over every
  integer scale from 72 to 384 (75%-400%). Checks cover horizontal, vertical and
  diagonal symmetry, exact equal-width/height bounds, antialiased edges, valid
  premultiplied alpha, cache reuse, and stable GDI object counts during resizing.
- Multi-monitor DPI transitions still require testing on appropriate hardware.

The timing measurements below were collected before the circle/scaling revision.
The click-worker source was compared with the retained previous revision and is
identical; this follow-up changes drawing and layout.

## Scheduler comparison

`tests/benchmark.ps1 -Long` compiles the original and current worker with the same
compiler options and runs each for 15 seconds. It intercepts `SendInput` and
records timestamps, worker CPU accounting and CPU cycle counts. No clicks are
sent to the desktop by this benchmark. The GUI, actual input-injection work and
the receiving application's processing are excluded. Other desktop activity was
not controlled, so this is a local comparison, not a hardware-wide guarantee.

| 1,000 CPS request | Original | Current |
| --- | ---: | ---: |
| Observed CPS | 999.055 | 996.941 |
| Worker CPU milliseconds / 15-second run | 500.000 | 62.500 |
| One logical core's CPU time | 3.3310% | 0.4163% |
| Worker CPU cycles / emitted click | 124732.57 | 20189.26 |
| Median inter-click gap (ms) | 1.0058 | 1.0073 |
| 99th-percentile inter-click gap (ms) | 1.4531 | 1.5058 |
| Worker exit latency (ms) | 0.0888 | 0.0685 |

Worker CPU time was 87.5% lower in this sample. Removing the spin tail
slightly reduced delivered CPS and widened the upper timing tail. This is the
intentional CPU/precision tradeoff. It does not imply that actual SendInput cost
or whole-application CPU use falls by the same percentage.

Short 3-second cases also exercise idle, 1, 60, 333 and 1,000 CPS, right-button
jitter, the normal-timer fallback, failed-input pausing and cancellation during a
1-CPS wait. Short cases can report zero CPU milliseconds due to accounting
granularity; zero is not a claim that the thread executes no work.

Cycle counts are reported directly, not converted to elapsed time, per
[Microsoft's QueryThreadCycleTime documentation](https://learn.microsoft.com/en-us/windows/win32/api/realtimeapiset/nf-realtimeapiset-querythreadcycletime).
The high-resolution timer availability and fallback follow
[CreateWaitableTimerExW](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createwaitabletimerexw).

Raw benchmark output is retained locally in `build/timing-*.jsonl`; run the script
to reproduce it. Release ZIPs include the executable, README, release notes,
this report, Apache license/NOTICE, and Lexend's OFL license.
