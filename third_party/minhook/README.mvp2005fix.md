# Vendored MinHook

Source: https://github.com/TsudaKageyu/minhook
Version: v1.3.4, commit c3fcafdc10146beb5919319d0683e44e3c30d537.
Only the files required for the x86 build are included. Line endings are LF.
The library is statically linked; its full license is in LICENSE.txt.

Local changes:

- `MH_EnsureHookEnabled` checks the actual patch bytes. It returns immediately
  if our jump is present, reapplies under thread suspension if the original
  bytes were restored, and refuses unknown modifications. Published trampolines
  are never replaced or freed by runtime code.
- Thread suspension retains handles through resume. Failure to suspend or
  inspect a live enumerated thread aborts the patch and resumes acquired threads.
- Single-hook disable uses the disable instruction-pointer relocation action.
- Silence the unused x86 `pOrigin` parameter warning.

The thread snapshot mechanism is inherited from MinHook: it coordinates with
enumerated threads, not arbitrary new thread creation or concurrent external
code writers. SafeDisc retry supports restoration of the original entry bytes;
it does not support replacing the function body or unloading its module. The
game process must retain this DLL, its target modules, and its trampolines for
the lifetime of the installed hooks. No unhooking is done in DllMain.
