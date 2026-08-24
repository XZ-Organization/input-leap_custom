# XZ custom fixes

This repository is based on Input Leap and carries the Windows service/watchdog fixes used by the XZ multi-PC setup.

## Included fix

`MSWindowsWatchdog::shutdownExistingProcesses()` now recognizes the current Windows executable names:

- `input-leapc.exe`
- `input-leaps.exe`

This prevents a stale client/server process from surviving a daemon restart and creating duplicate screen-name connections. The service empty-command path also shuts down tracked and previously running Input Leap processes.

## Not included

Clipboard loss has not been reproduced or fixed yet. No clipboard behavior was changed speculatively.
