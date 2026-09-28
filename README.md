# CoreAutoclicker

Small Windows autoclicker with Lexend.

[Download](https://github.com/OzzyMitchell/CoreAutoclicker/releases/latest)

## Use

- F6: left clicks. F7: right clicks. F9: quit.
- Set 1-1,000 clicks/sec. Resize the window to scale the UI.
- To change a hotkey: select it, press keys, then **Apply**.
- **Cursor jitter** adds small random movements.
- Turn **SendInput** off to use window messages; some apps ignore them.

Settings reset on exit. Actual click rate varies. Tested on Windows 11.

## Build

Requires Visual Studio C++ tools, Windows SDK and LLVM clang-cl.

```powershell
.\build.ps1
.\build.ps1 -Architecture x86
```

Files go to `build` and `dist`.

Code: [Apache 2.0](LICENSE). Font: [SIL OFL 1.1](assets/OFL-Lexend.txt).
