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
normal send opportunity to retry. That original change did not add timed retries;
the primary-server read path now has bounded retries as described below.
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
- PC1 (`XZ04`) was deployed on 2026-10-03 from merged commit `2b661ac`.
  The daemon remains registered at `C:\Retention_Artifact\inputleap-setup\patched-runtime\input-leapd.exe`;
  the active server remains in the original Downloads runtime directory.
  Both runtime directories were updated, with byte-for-byte deployment checks,
  service/core startup checks and TCP 24800 listener verification.
  The existing GUI was restarted; registration, certificates, networking and
  monitor configuration were preserved.
- PC1 rollback files and service/command exports:
  `C:\Retention_Artifact\inputleap-setup\deployment-XZ04-20261003-003028`.
  Production package and deploy script:
  `C:\Retention_Artifact\inputleap-setup\input-leap-production-2b661ac.zip`.
- The user reported completing PC2 deployment of the `2b661ac` package and
  subsequently confirmed PC1-to-PC2 copying worked. This was not independently
  verified through remote administration, which remains unavailable.
  Real PC1/PC2 application testing remains necessary; the historical intermittent
  clipboard loss was not captured, so these reproduced defects are not proof of
  its sole cause.

### Remaining review limits

This is not a complete clipboard-protocol/format-decoder hardening pass. The
chunk receiver's shared static state and the Windows DIB conversion bounds need
separate focused reproduction and tests. Also, native writes are not transactional:
a Windows `SetClipboardData` failure after `EmptyClipboard` cannot restore the old
contents. Failures are now detected and retried within a bounded budget; a
permanent native failure can still leave incomplete contents. No clipboard-history
access or clipboard-content logging was added.

## Intermittent clipboard delivery (2026-10-03, not deployed)

Two conditions reproduced the stale-paste symptom in isolation: a clipboard lock
covering incoming writes, and a primary copy notification delivered after the
screen switch. These reproduce possible failure paths, not the user's historical
incident or its frequency.

- Windows retains the latest pending incoming clipboard and retries unavailable
  writes up to 20 times at 50 ms intervals on the event queue. A sequence check
  under the native clipboard lock prevents a delayed retry from overwriting a
  newer local copy. New payloads replace pending data; remote grabs and screen
  shutdown cancel pending writes. Disabling clipboard sharing (including setting
  its size limit to zero) also cancels pending writes. Empty/unsupported payloads
  are not retried.
- Primary-client dirty state means a delivery has not yet been submitted; native
  completion and bounded retry belong to the platform. Keeping it dirty after
  asynchronous submission would replay completed, superseded or exhausted data
  on a later screen entry and could overwrite a newer local copy.
- A late primary ownership notification now reads and sends its data when a peer
  is already active. Failed primary reads use the same bounded retry cadence;
  ownership changes or disabled sharing prevent old pending reads from sending.
  Secondary-client outgoing reads still retry at the next normal send opportunity.
- Native format-write failures propagate to the retry path rather than counting
  an earlier successful clear as a successful write. The receive log reports
  receipt, not an unverified successful OS clipboard update.

Verification: seven new regression cases failed before implementation. After
implementation and the premerge follow-up fixes, all 51 clipboard tests passed
for 20 iterations in both Release and Debug. Two additional regression tests
failed before the follow-up fixes: async delivery replay (success, superseded,
and exhausted outcomes), and pending writes surviving disabled sharing (flag or
zero size). They also verify fresh delivery after a new dirty notification or
sharing re-enable. The original three premerge reproductions now pass 20 repeats.
The full Release suite passed 156/157 tests; its sole failure is the
same pre-existing daemon-argument test noted above. Release server, client, daemon,
and integration-test executables compile; the legacy integration executable was
not run because its clipboard tests use the interactive clipboard. New Windows
tests use a private non-interactive window station and manually delivered timers.

Follow-up evidence: `C:\Retention_Artifact\inputleap-setup\premerge-review-20261003`
(`permanent-before`, `fixed-release`, `fixed-debug`, `fixed-full`, and
`original-repro-fixed` logs and JSON results).
Initial evidence: `C:\Retention_Artifact\inputleap-setup\clipboard-review-20261003`
(`regression-before`, `regression-after`, `regression-debug`, `full-release` logs
and JSON results). Release binaries are in `build-clipboard\bin\Release` under
the same artifact root. They are development outputs from the modified working
tree, not the previously deployed `2b661ac` package. No service replacement,
remote commit, merge, or operational PC1/PC2 validation has been performed for
this follow-up change.
