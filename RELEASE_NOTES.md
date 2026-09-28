# Compact Lexend revision

- True antialiased circular slider thumb and focus ring, cached for redraws.
- Proportional resize scaling for the whole form, with centering and a 75% minimum.
  Text and controls grow together instead of stretching fields around empty space.

- Compact 420 x 298 logical-pixel form, padded borderless fields, and a bundled
  14,020-byte Lexend Regular subset. No animated background or idle redraw loop.
- Direct slider dragging/clicking, exact keyboard adjustment, complete track
  repaint, and synchronized numeric entry without interrupting partial edits.
- Keyboard focus restoration, consistent tab order, inline shortcut validation,
  and active/paused status. F9 remains reserved for closing.
- Event-driven worker with timer-only waits, coherent atomic settings, interruptible
  configuration changes, and no busy-wait tail or thread-priority boost.
- DPI-aware layout, high-contrast colors, timer fallback, and x86/x64 build options.
- Release archives include both code and font licenses.

See VERIFICATION.md for measured sizes, timing results, UI checks, and limits.
