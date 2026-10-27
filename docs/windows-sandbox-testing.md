# Windows Sandbox testing

Build SpaceWM and deploy its tests first, then run:

```powershell
.\scripts\test-in-windows-sandbox.ps1
```

Use `-PrepareOnly` to stage the payload and inspect the generated `.wsb` before
launching it. Every invocation creates a new directory under
`build/sandbox-runs/<timestamp>/`, preserving earlier reports.

The host script requires `ctest.exe` on PATH. Windows Sandbox must already be
enabled; `scripts/enable-windows-sandbox.ps1` is the separate administrator setup
step. The launcher does not enable features or reboot Windows.

The guest gets a read-only copy of the executables, Qt runtime, packaging scripts
and test manifest. It copies these onto its own disk and recreates
`D:\workspace\SpaceWM` using `subst`, preserving source paths compiled into the
tests. Only the dedicated `results` directory is mapped writable. Networking,
clipboard, audio/video input and printer redirection are disabled; vGPU is enabled.

The guest runs all registered CTest executables, copies their Qt Test reports,
then explicitly runs the real-app integration flow if Notepad is available.
Newer Sandbox images may omit Notepad. An unavailable integration flow is
reported separately and is not considered verified. Tests may also skip optional
dependencies such as Inno Setup; inspect the individual reports for coverage.

Results include `summary.json`, `guest.log`, `ctest.txt`, individual Qt Test
reports, integration output when run, a desktop image, and runtime TR/EH logs.
`summary.json` records exit codes and monitor geometry. `state: completed` means
the runner finished; check the exit codes to determine whether the tests passed.
No summary means that guest execution has not started. The sandbox remains open
after testing so its desktop can be inspected; closing it discards its local disk.

A sandbox session with one virtual monitor does not validate physical dual-screen
or mixed-DPI behavior. Keep real hardware acceptance for those cases, native
hotkeys, and actual taskbar clicks. A sandbox pass is not a replacement for them.

Configuration reference:
https://learn.microsoft.com/en-us/windows/security/application-security/application-isolation/windows-sandbox/windows-sandbox-configure-using-wsb-file
