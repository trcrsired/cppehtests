# Why C++-style EH on JIT'd (wasm) code is ~25-100x slower than native, and can't be fixed

Measured on WAVM running `performance/flat/ehslow.wasm`
(1,000,000 iterations of `try { throw } catch {}`, i.e. a flat wasm exception
throw/catch loop — a 2-3 frame unwind per throw).

| platform | unwinder | ehslow time | vs native |
|---|---|---|---|
| x86_64-linux-gnu | LLVM libunwind | ~12.8 s | ~25x slower |
| aarch64-apple-darwin24 | Apple libunwind (system) | ~97 s | ~100x slower |

The native equivalent on the same machines runs in well under a second.

## How a wasm `throw` works in WAVM

`wasm throw` compiles to `wavm_throw_wasm_ehtag`, which performs a real Itanium
ABI unwind: `_Unwind_RaiseException` walks the stack **twice** (phase 1 to find a
handler, phase 2 to run it), invoking each frame's personality routine
(`__gxx_personality_v0`) against the DWARF FDEs that WAVM registered for its
JIT'd code via `__register_frame` / `__unw_add_find_dynamic_unwind_sections`.

Every frame the cursor steps through — and every `_Unwind_SetIP` to a landing
pad — must first answer: *"which unwind-info section contains this PC?"*
That is where all the time goes.

## The root cause: JIT PCs have no image, so every lookup scans every image

Libunwind resolves PC → unwind info in this fixed order:

1. **Ask the dynamic loader** whether the PC belongs to a loaded image:
   - Linux: `dl_iterate_phdr` — calls back once per loaded ELF object, walking
     each object's program headers.
   - macOS: `_dyld_find_unwind_sections` → `findImageMappedAt` — walks every
     loaded image's Mach-O load commands.
2. **Ask JIT-registered sources** (`findDynamicUnwindSections` callbacks, then
   `DwarfFDECache`, the list `__register_frame` fills).

A JIT PC is in anonymous mmap memory. Step 1 therefore **always fails**, but it
is always executed — there is no negative caching. Step 1's cost is
O(number of loaded objects × work per object), paid for every frame of every
unwind phase, plus once more for the landing pad's `_Unwind_SetIP`.

Measured on x86_64-linux-gnu (interposing `dl_iterate_phdr`):

- **7 `dl_iterate_phdr` calls per wasm throw**
- **~179 objects visited per call** (169 of them are `libLLVM*.so` component
  dylibs — WAVM's own dependencies make this worse)
- ~460k throws in 8 s → ~2.4M phdr scans, ~427M object visits ≈ ~17 µs/throw
  ≈ essentially 100% of the runtime.

The macOS profile is the same shape: ~60% of unwind time sits inside dyld's
`findImageMappedAt`/`forEachLoadCommand` scanning ~500 loaded images per lookup;
the rest is the FDE lookup itself plus arm64e pointer authentication
(`pacia`/`autda`) on every `unw_proc_info_t` field.

The registered-FDE fallback (`DwarfFDECache::findFDE`) that finally locates our
FDE is itself an unsorted **linear scan** over all registered FDEs — cheap at
~120 entries for this test, but another O(N) term.

Native C++ EH is fast because its PCs live in real images: the lookup hits the
`.eh_frame_hdr` binary search (or `_dl_find_object`) immediately and never
touches the scan-all-objects path.

## Why it can't be fixed from the JIT side

- **The lookup order is baked into libunwind.** Image lookup is consulted
  before every JIT registration mechanism (`__register_frame`,
  `__unw_add_dynamic_eh_frame_section`, `__unw_add_find_dynamic_unwind_sections`).
  There is no API to skip it or to cache "this PC range is JIT".
- **There is no public API to put JIT code into the fast image index.** On
  glibc, `__register_frame_info_bases` does *not* feed `_dl_find_object`
  (verified on glibc 2.44 — registration succeeds, `_dl_find_object` still
  returns -1). On macOS, dyld's `JustInTimeLoader` is a private debugger SPI.
- **On macOS the unwinder is the OS.** `/usr/lib/system/libunwind.dylib` can't
  be patched, and a userland `libunwind.dylib` only takes over unwind bindings
  if it's actually loaded into the process.
- **The two-phase walk plus `_Unwind_SetIP` re-resolution** are required by the
  Itanium ABI — you cannot unwind once and reuse the result.
- Interposing `dl_iterate_phdr`/`_dyld_find_unwind_sections` is not viable:
  a library can't outrank libc/dyld in symbol resolution.

## What actually helps

- **Fewer loaded objects.** The cost is linear in the number of images.
  Building the toolchain as a monolithic `libLLVM`/`libLLVM.dylib` (or static
  LLVM) takes Linux from ~179 objects to ~10 and macOS from ~500 to ~10 —
  roughly an order of magnitude off every lookup. This is the single biggest
  lever and requires no WAVM changes.
- **A libunwind that checks registered FDEs first** (or keeps a sorted
  registered-FDE index): a small patch, but only effective where *your*
  libunwind is the one doing the unwinding — fine on Linux, not deployable
  against the system unwinder on macOS.
- **Not using the Itanium unwinder for wasm-to-wasm throws at all** (engine-side
  frame tracking, as wasmtime/v8 do) — the only real fix, and a redesign.
- **Contrast: Windows.** `RtlAddFunctionTable` inserts JIT function tables into
  a proper indexed structure consulted directly by the kernel unwinder — no
  image scan — which is why the same tests are not slow on windows-gnu.

## TL;DR

Slow wasm EH is not a WAVM codegen problem. It is the Itanium unwinder's
PC→unwind-info lookup: JIT code isn't a loaded image, so every single frame
lookup on every single unwind pays a linear scan of all loaded objects before
reaching the registered-FDE list — and no public API lets a JIT change that.
