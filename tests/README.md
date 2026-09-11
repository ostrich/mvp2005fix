# Inline hook regression checks

Build with `make release test-build`. Run `release/inline-hooks-test.exe` on Windows
or in an isolated Wine prefix. The executable includes the runtime source and
the same vendored MinHook sources as the DLL; it never launches or patches the
game. Assertions must remain enabled (do not set `-DNDEBUG`).

The default run checks:

- Installation while four threads call a function whose instructions cross
  the five-byte patch boundary; repeated installation is idempotent.
- Original-byte restoration and rejection of a conflicting patch.
- Injected suspension, context-read, and protection-change failures, successful
  retries, instruction-pointer relocation, and preservation of suspend counts.
- Runtime IAT and export callback paths, concurrent first-time factory vtable
  setup, and another caller entering while an original factory call is blocked.
- Relative calls and the hotpatch-above-entry layout, including restoration.
- Rejection of disable, queued disable, removal, and uninitialization while
  preserving active patches and published trampolines (including disabled hooks).

`release/startup-test.exe` uses the actual injection implementation and a small
fixture DLL. It creates suspended copies of itself and verifies delayed worker
readiness, DLL rejection/missing files, worker failure, missing signals, loader
timeout, process exit, event collisions, and injected wait/exit-code failures.
It checks that uncertain loader waits retain the remote argument allocation.
Its final case injects the production DLL and verifies that the main thread
remains suspended until the initialization handshake completes. It does not
launch the game or change the game bottle.

CI builds production and test targets, then runs both ordinary suites under
Wine/Xvfb before packaging. Both release publication jobs depend on that gate.
The graphics-dependent `--system` check remains a separate manual test.

Run `release/inline-hooks-test.exe --system` in a separate process to exercise
the runtime against the current Windows/Wine disk-space exports and D3D8
factory. This changes only that test process's loaded code. It checks factory
interception, not device rendering or gameplay.

Example on Linux (use a dedicated temporary prefix, not the game bottle):

```sh
test_dir=$(mktemp -d /tmp/mvp2005fix-tests-XXXXXX)
WINEPREFIX="$test_dir/prefix" wine release/inline-hooks-test.exe
WINEPREFIX="$test_dir/prefix" wine release/startup-test.exe
WINEPREFIX="$test_dir/prefix" wine release/inline-hooks-test.exe --system
```

The tests are bounded regressions, not proof against arbitrary thread creation,
module unloading, or external code patchers. See the vendored library's
`README.mvp2005fix.md` for the supported lifecycle and synchronization limits.
