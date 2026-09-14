# Why C++-style EH on JIT'd (wasm) code is ~25-100x slower than native, and can't be fixed

Measured on WAVM running `performance/flat/ehslow.wasm`
(1,000,000 iterations of `try { throw } catch {}`, i.e. a flat wasm exception
throw/catch loop — a 2-3 frame unwind per throw).

| platform | unwinder | ehslow time | vs native |
|---|---|---|---|
| x86_64-linux-gnu | LLVM libunwind | ~12.8 s | ~25x slower |
| aarch64-apple-darwin24 | Apple libunwind (system) | ~97 s | ~100x slower |

The native equivalent on the same machines runs in well under a second —
but "native speed" is itself still slow, see below.

## Native C++ EH: faster than JIT, still unacceptably slow (~100x slower than a syscall; compared to herbceptions, nearly infinitely slower)

Even with the image lookup hitting the fast path, a native `throw` still pays
for: the exception object allocation, the **two-phase** stack walk, DWARF CFI
interpretation per frame, LSDA parsing, and personality dispatch — all to
transfer control a few frames up. That is microsecond-scale per throw versus
the nanosecond-scale cost of a normal return.

A stark reference point is `syscall.cc` — 1,000,000 `close(-1)` calls, i.e.
a full kernel round-trip per iteration, on x86_64-linux-gnu:

| test | mechanism | time (1M iters) |
|---|---|---|
| `herbgood` | native herbceptions (`{T,i1}` return + branch) | **0.000000351 s** (~0.35 ps/iter — the loop optimizes to nothing) |
| `syscall` | kernel round-trip (`close(-1)`) | **~0.13 s** (~130 ns) |
| `ehslow` | native C++ `throw`/`catch` | ~1-2.5 s (~1-2.5 µs) |
| `ehslow` (wasm, macOS) | C++ `throw`/`catch` under WAVM | ~97 s (~97 µs) |

A language-level error return costs **more than crossing into the kernel and
back** — ~7-19x slower than a syscall natively, and ~750x slower under a JIT
(~100x or worse in realistic workloads). **~100x slower than a syscall is
totally unacceptable**: a syscall is already considered an expensive,
to-be-avoided operation — code goes out of its way to batch I/O and use
`vDSO`/io_uring precisely to dodge that ~130ns. If reporting one error to the
caller costs more than an entire trip into the kernel and back, the
mechanism has failed at its only job. Meanwhile the same 1M herbception
throws take 351 ns *total* — ~7,000,000x faster than native C++ EH and
~370,000x faster than a single syscall, because the whole thing is just a
discriminant in a register. Throwing a C++ exception is more expensive than
asking the kernel to do work for you.

Herbceptions (`throw throws` / `catch throws`, documented in
`llvm_herbceptions/llvm-project/clang/docs/CIR/Herbceptions.md`) avoid the
unwinder entirely: the error travels **in the return value** as `{T, i1}`.
A throw is a store + return; a catch is a `test i1` + branch. No stack walk,
no FDEs, no personality routine — nothing for the OS unwinder to do at all.

Same 1,000,000-throw benchmark, wasm builds on the Mac:

| test | mechanism | time |
|---|---|---|
| `flat_herbgood` | herbceptions (`{T,i1}` return + branch) | **~0.004 s** (~4 ns/throw) |
| `flat_ehslow` | C++ EH (`_Unwind_RaiseException` × 2 phases) | ~97 s (~97 µs/throw) |

~26,000x — and the gap is structural, not a tuning problem. Compared to
herbceptions, C++ EH is **nearly infinitely slower**: a herbception throw is
just a return with the discriminant set, so on the happy path its overhead
over a plain call is *zero* — there is no floor the ratio can settle at.
A wasm herbception throw (~4 ns) is even cheaper than a native syscall
(~130 ns); a wasm C++ throw (~97 µs) is ~750x *slower* than one. Fixing every
issue in this document only brings wasm EH back to *native C++ EH* speed —
which is still slower than a syscall and arbitrarily far behind
value-propagated errors. This is the core argument for herbceptions: not
"exceptions but a bit faster" but "error propagation that costs what it
should".

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

## Corollary: EH cost scales with the number of loaded shared libraries

The same mechanism means a *native* program's EH speed depends on how many
dylibs it links:

- **glibc/Linux**: native PCs are covered by `_dl_find_object`, an indexed
  address-range database the loader maintains (plus `eh_frame_hdr` binary
  search inside the found object), so DSO count mostly doesn't hurt native
  lookups. The O(#objects) `dl_iterate_phdr` scan is only paid by PCs outside
  all images — JIT code. That's exactly why WAVM, whose own `wavm` binary
  drags in 169 `libLLVM*.so` component dylibs, is hit so hard: the fallback
  scan it runs ~7 times per throw is sized by the process's object count.
- **macOS**: `_dyld_find_unwind_sections` → `findImageMappedAt` walks the
  image list even for *native* PCs (early exit at the containing image), so
  every unwind lookup is O(position in the image list). A program linking
  170 dylibs pays ~250 image checks per lookup per frame — throw-heavy code
  in a many-dylib program genuinely gets slower the more libraries it links.
- **Windows**: `RtlLookupFunctionEntry` uses the kernel's indexed function
  tables — insensitive to module count. Not a problem.

This is consistent with why large, many-DSO codebases (LLVM itself bans
exceptions) avoid EH on the hot path: its per-throw cost isn't a fixed
constant — it's proportional to the environment the process happens to
load. (LLVM's official reasons are binary size, portability, and
`Error`/`Expected` determinism, but the image-count-proportional lookup cost
makes EH an even worse fit for a 170-dylib build.)

## Implementing C++ EH for wasm is extremely hard

Getting `throw`/`catch` working at all on JIT'd wasm required all of the
following in WAVM — most of it invisible, per-platform, and fragile:

- **Semantics**: the wasm exception-handling proposal gives `try`/`catch`/
  `throw`/`rethrow` with *tags*. C++ needs LSDA type matching, cleanup landing
  pads, `catch(...)`, rethrowing the in-flight exception, destructor ordering,
  and `noexcept` → `std::terminate`. WAVM layers a personality routine +
  tag/type-dispatch scheme on top to reconstruct all of it.
- **Structured control flow**: wasm has no arbitrary jumps, so "unwind to a
  landing pad N frames up" can't be expressed in wasm itself — the engine
  emits IR-level `catchswitch`/`cleanuppad`/`invoke` and lowers real unwinding
  into machine code around every call.
- **Per-format unwind metadata**: DWARF FDEs on ELF/Mach-O, `.pdata`/`.xdata`
  on Windows — emitted by the JIT, relocated correctly (LLVM's RuntimeDyld
  `processFDE` actively *corrupts* arm64 Mach-O FDEs by double-applying
  `SUBTRACTOR`-pair deltas — WAVM must repair them before registration), and
  registered with the OS unwinder.
- **Per-OS registration**: `__register_frame` on ELF; `RtlAddFunctionTable` +
  SEH landing-pad trampolines on Windows; on macOS, `__register_frame` *plus*
  the `__unw_add_find_dynamic_unwind_sections` SPI returning a valid
  `dso_base` — without it, macOS 15's `unw_set_reg` null-dereferences
  `unw_proc_info_t.extra` while checking `cpusubtype` for arm64e.
- **Unwinder identity**: on macOS, dyld binds unwind symbols to whichever
  libunwind is loaded; LLVM and Apple libunwind have different `UnwindCursor`
  layouts, so a single unwind crossing both segfaults.
- **Traps are not EH**: div-by-zero/OOB/stack-overflow arrive as signals (or
  SEH exceptions) and need a separate translation layer (`catchSignals`) that
  must coexist with the unwind machinery.

And all of it must hold simultaneously across architectures
(x86_64/arm64/arm64e), object formats (ELF/Mach-O/PE), and unwinders
(LLVM/Apple/Windows) — which is why porting this subsystem means debugging
segfaults inside the unwinder itself. By contrast, the entire herbception
lowering is "return `{T, i1}` and branch on the discriminant" — no ABI
contract with the OS unwinder exists at all.

## TL;DR

Slow wasm EH is not a WAVM codegen problem. It is the Itanium unwinder's
PC→unwind-info lookup: JIT code isn't a loaded image, so every single frame
lookup on every single unwind pays a linear scan of all loaded objects before
reaching the registered-FDE list — and no public API lets a JIT change that.

And even if it could, table-driven unwinding is still ~µs per throw — slower
than a kernel syscall round-trip — while herbceptions propagate errors in the
return value at ~ns per throw. C++ EH is hard to implement on wasm, slow even
when implemented perfectly, and slower still under a JIT.
