# Inline hook regression checks

Build with `make test-build`. Run `release/inline-hooks-test.exe` on Windows
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

Run `release/inline-hooks-test.exe --system` in a separate process to exercise
the runtime against the current Windows/Wine disk-space exports and D3D8
factory. This changes only that test process's loaded code. It checks factory
interception, not device rendering or gameplay.

Example on Linux (use a dedicated temporary prefix, not the game bottle):

```sh
test_dir=$(mktemp -d /tmp/mvp2005fix-tests-XXXXXX)
WINEPREFIX="$test_dir/prefix" wine release/inline-hooks-test.exe
WINEPREFIX="$test_dir/prefix" wine release/inline-hooks-test.exe --system
```

The tests are bounded regressions, not proof against arbitrary thread creation,
module unloading, or external code patchers. See the vendored library's
`README.mvp2005fix.md` for the supported lifecycle and synchronization limits.
