# XZ custom fixes

This repository is based on Input Leap and carries the Windows service/watchdog fixes used by the XZ multi-PC setup.

## Included fix

`MSWindowsWatchdog::shutdownExistingProcesses()` now recognizes the current Windows executable names:

- `input-leapc.exe`
- `input-leaps.exe`

This prevents a stale client/server process from surviving a daemon restart and creating duplicate screen-name connections. The service empty-command path also shuts down tracked and previously running Input Leap processes.

## Clipboard safety (2026-10-02)

Windows no longer empties its clipboard on a remote ownership announcement
(`grabClipboard`, including secondary-screen startup). Contents are replaced
only when a transfer supplies a Windows-supported format that can be converted.
An empty/unsupported transfer leaves local contents available for pasting; this
intentionally does not synchronize a remote empty clipboard as a local erase.
The in-memory protocol cache still supports empty clipboards.

Local copy detection now tracks the Windows clipboard sequence number rather
than relying on a previous destructive ownership tag. Repeated local copies are
detected, including when viewer-chain notifications are lost. Input Leap's own
received-data tag still prevents echoing remote data back. The sequence number
is scoped to a window station ([Microsoft API documentation](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getclipboardsequencenumber)).

Failed Windows reads propagate failure to the sender, which keeps its previous
send state rather than publishing an empty clipboard. This allows the next
normal send opportunity to retry; it is not a timed retry mechanism.
Incoming serialized clipboard data is fully length-checked before replacing
the cache; both receive paths discard invalid payloads. CF_HTML fragment offsets
are checked for overflow and bounds instead of throwing on malformed content.
No clipboard wire-format or configuration changes are required.

### Verification and rollout

- Windows x64 Release core binaries and Debug/Release tests built with VS 2022.
- 37 clipboard-related tests pass in both configurations; the seven sender/Windows
  safety tests also pass 20 consecutive iterations.
- Full Release suite: 142/143 pass. The unrelated
  `GenericArgsParsingTests.parseGenericArgs_deamonCmd_daemonTrue` failure was also
  reproduced from the unchanged HEAD source.
- Windows safety tests use a private non-interactive window station, not the
  user's clipboard. Run with `unittests.exe --gtest_filter=*Clipboard*`.
- Build/test outputs and the core replacement package are under
  `C:\Retention_Artifact\inputleap-setup`. GoogleTest v1.14.0 sources used for the
  build are under `deps\googletest-1.14.0`; this source snapshot needs those
  `googletest` and `googlemock` directories in `ext/gtest` to rebuild tests.
- Running PC1/PC2 services have **not** been replaced by this code-review change.
  Update both Windows endpoints for the preservation behavior in both directions.
  Real PC1/PC2 application testing remains necessary; the historical intermittent
  clipboard loss was not captured, so these reproduced defects are not proof of
  its sole cause.

### Remaining review limits

This is not a complete clipboard-protocol/format-decoder hardening pass. The
chunk receiver's shared static state and the Windows DIB conversion bounds need
separate focused reproduction and tests. Also, native writes are not transactional:
a Windows `SetClipboardData` failure after `EmptyClipboard` cannot restore the old
contents. No new background polling, arbitrary delay, clipboard-history access,
or clipboard-content logging was added.
