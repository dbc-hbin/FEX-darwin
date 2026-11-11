# Wine on macOS patch evidence

This queue maintains Windows ARM64EC/WoW64 translator DLLs and their native
ARM64 helpers for Wine on macOS. Linux runtime compatibility is outside its
scope; guest instruction regression assets usable with Wine remain in scope.

Apply every entry of [series](series) in order. [provenance.json](../provenance.json)
is the authority for the pinned upstream revision, patch SHA256 values and final
source tree; [prepare.py](../prepare.py) checks that replay. Directory order is
not application order, and arbitrary subsets/intermediate prefixes are not
supported feature switches. Hadron and Arknights retain their imported grouping
and licenses. The Wine companion is applied to Wine, outside the FEX series.

The retained inventory is **56 patches: 55 FEX entries (49 local + 5 Hadron +
1 Arknights) and 1 Wine companion**. IDs within unchanged categories stay stable. The gaps
are local bug-fixes 0014, 0017, 0018, 0029, 0030, 0032-0034, 0036,
optimizations 0002 and instruction-support 0001-0002. Removed IDs are not
reused. Upstream replacements below retain the supported behavior.

| Local category | Retained IDs | Count |
| --- | --- | --- |
| Defaults/policy | 0001-0003 | 3 |
| Optimizations | 0001, 0003-0004 | 3 |
| Memory lifetime | 0001-0005 | 5 |
| Correctness/safety | 0001-0013, 0015-0016, 0019-0028, 0031, 0035, 0037-0046 | 37 |
| Regression asset | 0001 | 1 |

## Evidence and retention criteria

The current upstream pin is
[`e3b30dda25342362588ed1096494bea019cde4cd`](https://github.com/FEX-Emu/FEX/commit/e3b30dda25342362588ed1096494bea019cde4cd),
39 commits after FEX-2611. Using the release tag alone would omit the SERIALIZE
reserved-encoding and CPUID/SYSCALL single-step corrections. Exact ancestry and
earlier overlap evidence is in the [pin review](../../build/local/upstream-pin-review-20261110/report.md).
The latest update also includes upstream FRED advertisement, CPU-topology
corrections, configuration-header cleanup and nonreturning JIT spill-stack resets.

| Removed local patch | Replacement in the upstream pin |
| --- | --- |
| optimizations/0002, instruction-support/0001 | [`465395de`](https://github.com/FEX-Emu/FEX/commit/465395dede08bbcff03f1ef15de4e96f8ea4d12e): private GDT removal and SLDT, including Split/Second SecuROM support. |
| bug-fixes/0014 | [`4dbb046d`](https://github.com/FEX-Emu/FEX/commit/4dbb046d62c57639fd93d433bb64896df7332ff0), [`3a2b1c0b`](https://github.com/FEX-Emu/FEX/commit/3a2b1c0b438fed5062bebff0c5f7c8e21879fe48): redirection-table width and exclusive upper bound. |
| bug-fixes/0032 | [`68f4c5fb`](https://github.com/FEX-Emu/FEX/commit/68f4c5fbc48d4dc5c26b781b9f8b7aff8f7ab1d9): mandatory-prefix selection for Order of the Sinking Star. |
| bug-fixes/0033 | [`8e606082`](https://github.com/FEX-Emu/FEX/commit/8e6060827ea698a6398fe2c3a5d436f9693b54e8): SWAPGS mode-specific fault; newer SERIALIZE tables reindex the same semantics. |
| bug-fixes/0034 | [`7b909d31`](https://github.com/FEX-Emu/FEX/commit/7b909d31e2ce3392fb8cbd9f50874f578eee8ff7): 32-bit opcode 0x82 aliases for Crysis 2/Split/Second. Keep upstream's later locked ADC/SBB test extensions. |
| bug-fixes/0036 | [`dbbda7b2`](https://github.com/FEX-Emu/FEX/commit/dbbda7b2b307ddb1374cd6fa9f0a460a88adfca0): restore the spill stack before nonreturning Syscall, ThreadRemoveCodeEntry and CPUID exits. |

Partial duplicates are removed as well. The upstream early shared BlobRef
construction replaces that part of memory-lifetime/0002; queued ownership,
lookup snapshots and eviction synchronization remain local. Upstream load
side-effect declarations replace that part of bug-fixes/0026; its register
allocation safeguards remain. The upstream VOLATILEMETADATA switch now gates
both PE and extended metadata; bug-fixes/0025 retains bounds and ordering fixes.
Bug-fixes/0001 adds effective hardware TSO to upstream's one-byte process hash and
uses constructor cache initialization, removing the later-init latch and calls.
Bug-fixes/0021 retains complete x87 addresses and the copy fast path while
removing the four fixed StoreStackMem addressing fields.

**A633** separates this upstream update from A632 and older caches because the
nonreturning JIT exit sequence changes. A632 introduced captured SMC checksums,
exact validated-code cache entries and corrected bit-string addresses.
A631 was the preceding upstream-pin/fault-packet boundary.
Both DLLs and native helpers must be rebuilt together because upstream adds a
CPU-index Unix operation. The paired Wine EFLAGS/XSTATE protocol remains required.

The new `bug-fixes/0045` addresses a separate reproduced boundary: POPF sets TF
then enters an already cached CPUID block without checking TF. The first trap
arrived after the successor store. Selecting the existing inline successor
form activates the existing CheckTF exit handling; the same unchanged
successor/RIP/continuation assertions then pass. This is exception correctness,
not evidence that it caused game jitter.

The [67-patch necessity audit][audit] is the detailed prior review, with exact
source ranges, public references and existing experiment paths. Its prepared
source snapshot and source-tree identity describe the pre-removal baseline.
The inventory below reorganizes that evidence for the retained set; it does not
claim that the audit or old receipts executed the reduced runtime.

Each retained patch appears once in the inventory. The patch link contains its
actual source changes; source-owner names locate the relevant code inside the
prepared tree. Receipt links resolve to ignored local evidence, not a portable
tracked test distribution. Evidence levels are:

- **G:** actual Windows game report upstream, including Linux/Proton runs; this
  is not a game run on this Mac.
- **WF:** actual Windows fixture through Wine on native ARM64, or production
  code with real Windows allocations. A diagnostic code-cache sink or native VM
  observer is identified where used. **A/B** means an existing before/after
  failure was recorded, not just that the patched runtime passed.
- **HF:** host fixture using production bodies or the actual pinned allocator;
  substituted hooks, I/O or POSIX termination do not prove Windows races.
- **C:** ISA/API, C++ lifetime/concurrency or production-code proof. Reachability
  is a runtime path, not proof that a named game reached its rare boundary.
- **P:** optional default/policy retained by the user's pre-PR feature constraint.
  Test-only assets are identified separately.

Retain the platform foundation and pre-PR disk-cache, hidden-hypervisor and
hardware-TSO defaults in `local/defaults/`. Pre-PR denotes ancestry, not a
directory category: effective TSO initialization and cache identity include
later corrections and live in `local/bug-fixes/0001`. Reclassification preserves
patch contents and application order. Retain the original REP/string and
gather acceleration already in the pinned base. No row establishes new game
FPS, jitter, map-stutter or memory-plateau improvement.

## Retained patch inventory

| Patch | Windows game/runtime reachability | Evidence and reason retained | Key source/evidence links |
| --- | --- | --- | --- |
| [hadron/0001](hadron/0001-Windows-UnixLib-Build-on-macOS.patch) | Every translated Windows process loads native ARM64 Unix helpers. | C: macOS build/API contract; removes unavailable prctl/librt dependencies. Required platform foundation. | UnixLib CMake/FEXUnixLib.cpp; [platform API][apple-jit]. |
| [hadron/0002](hadron/0002-Windows-Use-64K-guard-regions-for-the-call-ret-stack.patch) | Guest call/return prediction reaches stack guard faults. | C: imported reproduction and host-page contract; a 4 KiB guard sharing a 16 KiB host page can become writable. Retain 64 KiB guards. | Windows/Common/CallRetStack.h; call-ret WF in [R1]. |
| [hadron/0003](hadron/0003-FEXCore-Use-64K-guard-regions-for-JIT-buffers.patch) | Compilation fills temporary JIT buffers and reaches guard/restart handling. | C: emitter and handler must agree on host-page-safe guards. Call-ret fixture success does not isolate this JIT-buffer contract. | ThreadPoolAllocator.h, JIT.cpp, Windows/Common/JITGuardPage.h. |
| [hadron/0004](hadron/0004-Windows-UnixLib-Use-thread_set_x86_64_compat-for-har.patch) | Wine/FEX guest threads select hardware TSO through the native helper. | P + C: inherited pre-PR acceleration; retain Wine thread-policy integration. Software ordering remains available; no game FPS measurement. | UnixLib/FEXUnixLib.cpp; [build/TSO requirements](../README.md). |
| [hadron/0005](hadron/0005-Windows-Use-per-thread-W-X-JIT-code-buffers-on-macOS.patch) | JIT compilation, cache relocation and branch patching write executable buffers. | C: Apple MAP_JIT and per-thread W^X contract; pre-PR path avoids repeated page protection faults. Retain executable-generation ownership. | AllocatorHooks.h, Common/Allocator.cpp, UnixLib ABI, JIT.cpp; [Apple][apple-jit]. |
| [bug-fixes/0046](local/bug-fixes/0046-crc-byte-input-semantics.patch) | Writable-code validation and cache hashing call CRC over guest/code bytes. | C: avoids aliasing/alignment UB. Current callers already pass byte pointers; no observed game compiler failure or non-byte stepping trigger. | Utils/crc32.h, Core.cpp, CodeCache.cpp. Zero-length CRC evidence belongs to 0037. |
| [defaults/0001](local/defaults/0001-disk-cache-default-enabled.patch) | A game starts/restarts and translation entries are stored or reused on disk. | P: pre-PR disk-cache default. Optional policy, retained by the pre-PR preservation requirement; cold/warm fixtures do not measure a game benefit. | Config.json.in, Windows Module initialization, DiskCache.cpp; [R1]. |
| [defaults/0002](local/defaults/0002-hide-hypervisor-default-enabled.patch) | Guest CPUID feature discovery reads the hypervisor-present bit. | P: pre-PR compatibility default; hides CPUID.1:ECX[31]. Specific game necessity is unproven; no anti-cheat guarantee. | Config.json.in, CPUID.cpp; [Microsoft feature discovery][hypervisor]. |
| [bug-fixes/0002](local/bug-fixes/0002-nested-jit-write-mode-restoration.patch) | Nested executable allocations, code emission and fault-time patching enter JIT write scopes. | C + HF: restore the actual prior per-thread mode across all writers. Mode 2 also queries support; the obsolete separate support-query operation is removed. Scope fixture substitutes hooks; it does not directly validate hardware-register readback. | JitWriteScope.h, AllocatorHooks.h, UnixLib/FEXUnixLib.cpp; [Apple][apple-jit], [R6]. |
| [bug-fixes/0003](local/bug-fixes/0003-jit-buffer-guard-size.patch) | Shared JIT buffer allocation reserves a guarded executable generation. | C: reserve/exclude the entire configured guard. A passing call-ret guard test is not an isolated test of shared-code-buffer sizing. | SharedCodeBufferManager.cpp/.h; Hadron guard contract. |
| [bug-fixes/0004](local/bug-fixes/0004-shared-jit-buffer-publication.patch) | Concurrent compilation publishes and consumes the latest shared code-buffer owner. | C: lock publication of shared_ptr/generation state to prevent a C++ data race. WF rollover exercises operation, not a reproduced race baseline. | SharedCodeBufferManager.cpp/.h, JIT.cpp; [R1] rollover. |
| [bug-fixes/0005](local/bug-fixes/0005-darwin-code-cache-namespace.patch) | A game reloads compiled native bytes from a persistent disk cache. | C: separate Darwin code generation from upstream and incompatible prior generations. A namespace change is cache correctness, not an FPS optimization. | DiskCache.h FormatVersion, DiskCache.cpp identity; see removal cache gate below. |
| [bug-fixes/0001](local/bug-fixes/0001-effective-hardware-tso-cache-identity.patch) | Windows initialization selects effective hardware TSO before context/cache construction. | C: cache identity must reflect the TSO mode that actually succeeded. Retain later initialization corrections together with the pre-PR lineage. | Common/CPUFeatures.cpp, Context.h, DiskCache.cpp. |
| [defaults/0003](local/defaults/0003-hardware-tso-default-enabled.patch) | The native helper chooses the default TSO policy for Wine guest threads. | P: pre-PR opt-out acceleration default; requires the matching Wine policy and entitled loader. No game-speed observation. | UnixLib/FEXUnixLib.cpp; [build/TSO requirements](../README.md). |
| [bug-fixes/0006](local/bug-fixes/0006-packed-cache-relocation-bounds.patch) | A disk-cache hit relocates packed references into the native code buffer. | C: check the full relocation write width/type and destination bounds in release builds. Retain protection against malformed cache input. | CodeCache.cpp, DiskCache.cpp; assertions are disabled in the build recipe. |
| [bug-fixes/0007](local/bug-fixes/0007-disk-cache-file-and-blob-bounds.patch) | Default disk-cache mapping, file growth, append and blob parsing process cache files. | C: distinguish mapped/file lengths and bound count/size arithmetic. Mapping-off bypasses mapping accesses only; no claim that a game creates corrupt input. | DiskCache.cpp/.h; actual mapping lifetime fixture [R3] is not a per-bound A/B. |
| [bug-fixes/0008](local/bug-fixes/0008-windows-directory-utf8-paths.patch) | Cache traversal runs under a non-ASCII Windows user/cache path. | C + WF: keep UTF-8/UTF-16 conversion consistent. Actual café-root Wine coverage exists; no isolated failing pre-0008 baseline. | FileUtils.cpp, DiskCache.cpp; [R4] after-unix-links/prune-result.json. |
| [memory-lifetime/0001](local/memory-lifetime/0001-directory-enumeration-buffer.patch) | Windows directory enumeration grows its live buffer while traversing cache directories. | C: update the owning pointer to avoid UAF/double-free. One-token fix; actual growth failure was not observed (initial buffer is 16 KiB and normal enumeration returns partial batches). | FileUtils.cpp; [NtQueryDirectoryFile][directory]. |
| [bug-fixes/0009](local/bug-fixes/0009-prune-recognized-cache-files.patch) | Automatic cache pruning visits stale buckets during cache initialization. | C + WF: delete recognized valid FEX files only, without recursive bucket deletion. Retain with 0042; this patch alone did not fix symlink escape. | DiskCache.cpp, FileUtils.cpp, WinAPI/IO.cpp; [R4]. |
| [bug-fixes/0010](local/bug-fixes/0010-disk-cache-extent-pairs.patch) | Disk index parsing walks guest-code offset/length extent pairs. | C: reject odd counts and out-of-bounds extents before downstream pair reads. Payload byte length alone cannot establish pair validity. | DiskCache.cpp index parsing and entry lookup. |
| [bug-fixes/0011](local/bug-fixes/0011-memory-cache-blob-header.patch) | A disk-cache hit is promoted into the optional positive-budget memory LRU. | C, conditional: DiskCacheMemorySize defaults to 0, so this promotion is disabled by default. Retain the fixed header copy while positive memory caching is supported. | DiskCache.cpp StoreInMemory; Config.json.in DiskCacheMemorySize. |
| [bug-fixes/0012](local/bug-fixes/0012-disk-cache-footprint-identity.patch) | Cache insertion/lookup encounters one guest hash with different extent footprints. | C + HF: retain distinct identities and update the matching entry. Production-body host fixture substitutes I/O/queue; not actual Windows concurrency. | DiskCache.cpp; [R5] cache-cpu-after.log. |
| [memory-lifetime/0002](local/memory-lifetime/0002-disk-cache-owned-work-and-eviction.patch) | Asynchronous disk-cache writes and lookup snapshots overlap eviction/destruction. | C + HF/mutation: owned queued blobs, pinned readers and synchronized eviction avoid UAF/stale snapshots. Memory-cache=0 still has in-flight writes/failed entries. | DiskCache.cpp/.h writer/member ownership; [R5] cache-cpu-after.log and cache-cpu-mutant.log. |
| [optimizations/0001](local/optimizations/0001-lookup-l2-allocation.patch) | Per-thread lookup allocation precedes first L1 fault while L2 is disabled by default. | C: avoid unused L2 reservation/commit through Wine overcommit handling. Concrete allocation reduction; no game RSS/FPS/stutter number. | LookupCache.cpp, Config.json.in, Windows/Common/OvercommitTracker.h. |
| [bug-fixes/0013](local/bug-fixes/0013-lookup-tag-validity.patch) | Guest indirect calls, jumps and returns look up a target, including RIP zero. | C + WF: empty/invalid tag zero must not become a valid native null/stale branch. Null-target exception/continuation fixtures cover operation. | LookupCache.h, JIT/BranchOps.cpp; [R1] callret-guard. |
| [bug-fixes/0015](local/bug-fixes/0015-arm64ec-xstate-context-transfer.patch) | Windows context get/set, APC and exception continuation transfer FP/XSTATE. | C + WF: preserve requested legacy FP and upper YMM independently through paired Wine. Existing 15-case pass is not a patch-revert A/B or game observation. | ARM64EC/Module.cpp, Common/Exception.h; [XSTATE][xstate], [R1] context_probe. |
| [bug-fixes/0016](local/bug-fixes/0016-arm64ec-exception-carried-flags.patch) | ARM64EC exception/suspend contexts carry PF, AF and DF absent from NZCV. | C + WF: encode carried flags in the Wine exception packet. Retain with Arknights restoration and the Wine companion. | ARM64EC/Module.cpp; [CONTEXT][context], [R1] context_probe. |
| [bug-fixes/0019](local/bug-fixes/0019-wow64-address-wrapping.patch) | WoW64 syscall, stack, TEB and SMC boundaries pass guest addresses to 64-bit host APIs. | C: apply DWORD guest width before host use. WoW64 guest startup remains unverified; the x64 DWORD exception-helper fixture does not exercise these sites. | WOW64/Module.cpp; [R1] verification.json records the gap. |
| [bug-fixes/0020](local/bug-fixes/0020-effective-address-wrap-once.patch) | Guest address-size overrides, segmented operands and locked bit strings form memory addresses. | C / ISA: wrap effective addresses once, then perform linear subaccess offsets. Rare game boundary frequency is unknown; WoW64 execution is not proven. | Core/Addressing.cpp, OpcodeDispatcher.cpp; 32Bit_ASM BitTestMemoryAddress.asm. |
| [bug-fixes/0021](local/bug-fixes/0021-x87-store-complete-address.patch) | x87 guest stores, including an 80-bit operand tail, pass through stack optimization. | C: producer and consumer share a completed architectural address; tail is base+8. Retain with 0020 independently of removed environment/tail-load rewrites. | OpcodeDispatcher/X87.cpp, x87StackOptimizationPass.cpp, Passes.h/PassManager.cpp. |
| [bug-fixes/0022](local/bug-fixes/0022-wine-shared-data-reads.patch) | Windows runtime clock/version/feature loads read guest KUSER_SHARED_DATA at 0x7ffe0000. | C + WF: emulate guarded in-page reads using Wine native backing where the guest page cannot be mapped directly. Reject writes/unrecognized forms. | ARM64EC/UserSharedData.h, Module.cpp; [shared data][shared], [R1] shared-data runs. |
| [bug-fixes/0023](local/bug-fixes/0023-vector-tso-lowering.patch) | Vector guest memory operations select software/forced ordered IR and NEON lowering. | C: retain matching barriers and legal addressing even with hardware TSO available. Existing default/software WF passes are not a causal per-patch litmus failure. | JIT/MemoryOps.cpp, OpcodeDispatcher.h, json_ir_generator.py; [volatile contract][volatile]. |
| [bug-fixes/0024](local/bug-fixes/0024-pair-memory-complete-address.patch) | AVX loads/stores lower into paired 128-bit halves. | C: both halves use the same completed address. Current callers all use 16-byte elements; size-derived offset is small, with no game necessity for extra widths claimed. | OpcodeDispatcher.h and OpcodeDispatcher/AVX_128.cpp; [R1] pair fixture does not prove rare WoW64 boundaries. |
| [bug-fixes/0025](local/bug-fixes/0025-volatile-tso-range-contract.patch) | ImageTracker parses optional PE volatile metadata and compilation chooses ordering ranges. | C, conditional: metadata is an optional optimization. Retain bounds checks and forced-ordering precedence while supporting it; no real-game metadata or malformed-table WF. | ImageTracker.cpp, VolatileMetadata.h, Core.cpp; [Microsoft /volatile][volatile]. |
| [bug-fixes/0026](local/bug-fixes/0026-fault-visible-guest-state.patch) | Faulting guest loads and SEH/VEH register reconstruction pass through IR elimination/allocation. | C / ISA: retain fault side effects and correct guest registers/SP across transforms, including memory broadcasts. No isolated proof for every hunk. | IR/IR.json, RegisterAllocationPass.cpp, ARM64EC/Module.cpp. Windows frontends disable SVE. |
| [bug-fixes/0027](local/bug-fixes/0027-lse-cmpxchg-full-ordering.patch) | Game/runtime Interlocked compare-exchange lowers to the default Apple LSE path. | C + WF: full ordering must include failed CAS. Valve engine use establishes reachability; before-fix litmus also passed, so no reproduced ordering failure is claimed. | JIT/AtomicOps.cpp; [Interlocked][interlocked], [Valve atomics][valve], [R1] atomic-default. |
| [bug-fixes/0028](local/bug-fixes/0028-llsc-cmpxchg-fault-contract.patch) | Nondefault LL/SC fallback/disableatomics override emits CAS and handles unaligned faults. | C + WF, conditional: default Apple LSE bypasses it. Retain write-permission checking, NZCV and emitter/decoder agreement while fallback remains supported. | JIT/AtomicOps.cpp, Utils/ArchHelpers/Arm64.cpp; [R1] atomic-disableatomics. CMPXCHG16B misalignment is not emulated. |
| [regressions/0001](local/regressions/0001-fxrstor-top.patch) | Regression ASM executes FXRSTOR and checks x87 TOP/logical-stack restoration. | Test-only / C: 40-line test asset; adds no runtime implementation. Retain regression coverage independently of removed x87 rewrites; no new execution claimed. | unittests/ASM/X87/FXRSTOR_TOP.asm in the patch. |
| [bug-fixes/0031](local/bug-fixes/0031-xrstor-component-state.patch) | Guest XRSTOR requests individual state components and reads XSTATE headers/MXCSR. | C / ISA + WF: restore/initialize only requested components with correct fault-visible header accesses. The 93-case fixture is not exhaustive XRSTOR exception coverage. | OpcodeDispatcher/Vector.cpp, IR/IR.json; [Intel XRSTOR][xrstor], [R1] xstate_probe. |
| [bug-fixes/0035](local/bug-fixes/0035-alu-forwarded-constant-width.patch) | Ordinary byte/word ALU operations forward constants before promotion to full GPR operations. | C: mask upper garbage before promotion to preserve subregister results and avoid assertions. Retain while the inherited promotion remains; no isolated current WF A/B. | OpcodeDispatcher.cpp LoadGPRRegister/StoreGPRRegister and constant promotion. |
| [bug-fixes/0037](local/bug-fixes/0037-smc-crc-validation.patch) | Instruction CRC/full-SMC validation handles even a zero-length range. | C + WF A/B: compare the actual initialized accumulator and consistent CRC IR. Earlier zero-length failure is recorded; not evidence for 0046 aliasing. | JIT/BranchOps.cpp, ALUOps.cpp, IR.json; [R7] zero-crc-before/result.json. |
| [bug-fixes/0038](local/bug-fixes/0038-darwin-writable-code-smc-policy.patch) | Guest RWX JIT, Mono backpatches and self-modifying code run on 16 KiB host/4 KiB guest pages. | C + WF: instruction CRC covers writable code where host-page tracking is insufficient. Retain separate from CRC mechanics/retry metadata; runtime cost has no measured FPS figure. | Frontend.cpp, Core.cpp, InvalidationTracker.cpp; [R1] smc_probe and full-SMC. |
| [bug-fixes/0039](local/bug-fixes/0039-jit-recompile-debug-metadata.patch) | A large-block JIT compilation retries and later reconstructs a faulting guest RIP. | C: discard opcode/subblock offsets from the abandoned attempt. Metadata named debug is also consumed at runtime; no isolated forced-retry WF A/B. | JIT.cpp retry and RIP metadata; ARM64EC/Module.cpp fault reconstruction. |
| [bug-fixes/0040](local/bug-fixes/0040-callret-invalidation-safety.patch) | Cache worker threads or guest invalidation paths invalidate call/return caches. | C + WF coverage: skip absent internal-thread stacks and prevent decommit callbacks reentering the invalidation lock. Retain with 0041. | Core.cpp, Context.h, ARM64EC/Module.cpp; [R1] notifications. |
| [bug-fixes/0041](local/bug-fixes/0041-memory-notification-pairing.patch) | Windows Before/After protection/map notifications and unpaired cross-process updates change executable tracking. | C + WF: retain successful pairing, selective locking and race resync. Unconditional resync can reenter through stack decommit; broader WoW64 locks extend suspended-target mutex ownership. | InvalidationTracker.cpp/.h, both Windows Modules; [R1] notifications and unmap tracker fixtures; [R2] rejected simplification. |
| [memory-lifetime/0003](local/memory-lifetime/0003-block-link-arena-lifetime.patch) | Block-link arena resources die when translator resources/code-buffer generations retire. | WF A/B: existing release() must run on destruction; mappings have unique ownership. 128-cycle destruction retained 8 MiB before and zero after; not total game RSS. | fextl/memory_resource.h, LookupCache, SharedCodeBufferManager; [R8] baseline and [R9] aligned-regressions. |
| [arknights-macos-runtime/0001](arknights-macos-runtime/0001-arm64ec-restore-carried-eflags.patch) | Paired Wine returns a thread/exception continuation context to FEX. | C + WF: restore PF/AF/DF carried outside native NZCV. Maintain matching LMISXH_AKMOCL_EFLAGS opt-out on both sides; old native ntdll caused flag-roundtrip failure. | ARM64EC continuation code; [R1] 15-case context_probe; [build pairing](../README.md). |
| [bug-fixes/0042](local/bug-fixes/0042-cache-directory-reparse-points.patch) | Cache pruning enumerates directory/file links that can resolve outside the cache. | C + WF A/B: reject reparse points and verify resolved direct children; outside FOZ deletion failed before and passes after. Native-host concurrent filesystem replacement is outside tested protection. | FileUtils.cpp; WineFileUnixNameInformation contract; [R4] before-final and after-unix-links. |
| [bug-fixes/0043](local/bug-fixes/0043-failed-unmap-tracking.patch) | NtUnmapViewOfSection fails for still-committed private executable memory. | WF A/B: retain section ownership until After and remove tracking only on success. Prevents a reproduced next-execution fault; unpaired cross-process semantics stay separate. | InvalidationTracker.h and both Modules; [R6] failed-executable-unmap, [R3] after receipt. |
| [memory-lifetime/0004](local/memory-lifetime/0004-thread-heap-retirement.patch) | Wine externally terminates a translated thread without CRT allocator finalization. | C + HF A/B: retire the target heap after quiescence/state teardown, preserving live allocations, remote frees and the caller heap. Actual pinned rpmalloc diagnosis strands 8 heaps before, 0 after. | Windows thread state, parent-built rpmalloc adapter; [R3] heap-fixtures. POSIX fixture models absent Windows TLS destructor; not a game RSS test. |
| [bug-fixes/0044](local/bug-fixes/0044-dep-promotion-lifetime.patch) | WoW64 DEP compatibility records meet explicit executable protection, free or resync. | C + WF A/B: retire stale promotion metadata so DEP re-enable cannot remove genuine executable tracking. Fixture uses production tracker/real Windows allocations with a code-cache sink, not a WoW64 guest. | InvalidationTracker.cpp/.h; [DEP][dep], [R3] dep-baseline and dep-after-* receipts. |
| [memory-lifetime/0005](local/memory-lifetime/0005-disk-cache-mapping-lifetime.patch) | A disk-cache IndexedDB is destroyed after mapping a cache file through UnixLib. | WF/native A/B: capture the matching unmapper and exact length. 64 RO + 64 RW cycles release all native mappings. An 8 GiB reservation is virtual address space, not 8 GiB resident RAM. | DiskCache.cpp/.h and appended UnixLib unmap ABI; [R3] mapping-after.stdout.log/native VM observer. Rebuild both PE DLLs and both helpers together. |
| [optimizations/0003](local/optimizations/0003-full-cache-serialization.patch) | JIT cache submission sees a full disk cache with memory-cache budget zero. | C: skip serialization/copy/queue work for an entry that cannot be retained. Tiny avoided work, no change to guest semantics and no measured game FPS gain. | DiskCache.cpp, Core.cpp, Config.json.in cache budgets. |
| [optimizations/0004](local/optimizations/0004-duplicate-alu-ir.patch) | Ordinary guest ADD/SUB translation computes result and flags. | C: omit unused duplicate IR before CalculateFlags_ADD/SUB creates the actual result. Tiny translation-work reduction; no game FPS measurement. | OpcodeDispatcher.cpp ADD/SUB handlers. |
| [bug-fixes/0045](local/bug-fixes/0045-popf-cached-successor-single-step.patch) | Debuggers, protectors or runtime probes set TF with POPF before a previously translated successor. | C + WF A/B: use the inline known RIP so CheckTF cannot be bypassed by an L1 hit. The warm prefixed-CPUID probe fails one instruction late before this one-line change and passes after it. | OpcodeDispatcher.cpp POPFOp, JIT/BranchOps.cpp CheckTF; current `isa-smokes` and `popf-fixed` receipts. |
| [wine/0001](wine/0001-ntdll-preserve-carried-eflags-on-continuation.patch) | Native ntdll normalizes PSTATE for emulated continuation/thread contexts. | C + WF: preserve software PF/AF/DF packet bits for emulated PCs. Apply to Wine after paired transport; absent/old native ntdll reproduces flags failure. | Wine dlls/ntdll/unix/signal_arm64.c and thread.c; [R1] context_probe, [R9] pairing diagnosis. |

## Removal decisions and required checks

The current corrections add cooperative suspension protection to the private
XSTATE hook, query unmap bounds only after taking the frontend lock, and wrap
bit-string register displacements before segment addition. SMC validation uses
captured decoder input rather than a later live-memory read. Validated subblocks
discard patchable operand metadata and masks; insertion verifies and hashes one
owned guest-byte snapshot. Exact matching disk-cache entries remain supported.
The cost of losing masked reuse for those subblocks is unmeasured.

The [latest pin receipt](../../build/local/upstream-pin-20261111/verification.json)
records a clean 55-patch replay, both translator DLLs and both native helpers,
all seven preparation tests, 146 shared-data checks, nested JIT-write scopes
and the host cache ASan/UBSan fixture. Build logs retain ARM64EC register,
section-alignment and C++17-extension warnings.
Earlier correction checks recorded seven failing unmap-template replacement/
failure cases out of eight before the correction and all eight passing after
it; kernel section queries and executable-cache sinks are substituted in that
probe. The migrated mapping guest fixture compiled for x86_64 and i686.
Current Wine execution is blocked before guest entry by the existing loader's
restricted-entitlement signature failure. These checks do not establish a game
result, active hardware TSO, or working WoW64 guest startup.

Historical A631 pin-upgrade evidence is in
[upstream-pin-20261110/verification.json](../../build/local/upstream-pin-20261110/verification.json):

- That 56-patch queue replays to its recorded tree. Exact-SHA preparation
  passes seven local-Git behavioral tests, including a SHA after the release tag.
  Both translator DLLs and both native helpers build.
- The final isolated Wine run records 46 scenarios: 42 pass; four x87
  configurations retain byte-identical output with eight prior full-tag failures
  each. Full precision executes 120 checks and reduced precision 111, with the
  same nine pre-existing exclusions. No failing assertion was removed.
- After the one-line POPF correction, all seven affected context/DIV/default/
  full-SMC/L2/cache cold/warm runs pass. The context fixture has 16 cases,
  DIV has 84 including eight upstream long-division examples, SERIALIZE checks
  36 encodings, and the instruction-fetch probe checks nine positions with
  fault RIP/address and handler continuation.
- The pre-fix warm CPUID run traps after its successor store; the corrected
  run traps before it. Removing only upstream's post-CPUID TF guard also makes
  the same probe fail. Both failures emit the wrong-trap diagnostic before the
  debugger timeout; the runner records native ARM64, matching mapped DLL
  identities and successful cleanup. These are isolated mutation artifacts.
- Rebuilt arena/tracker/DWORD helper fixtures use the current production source.
  The native IndexedDB fixture releases all 64 RO and 64 RW mappings through the
  newly paired helper; observer image paths and helper hashes are recorded.
  The JIT scope hook fixture passes nesting/inactive/already-writable checks.
  The newly pinned rpmalloc also passes the production-adapter host fixture:
  eight target heaps remain stranded without retirement and zero with it,
  while live allocations, remote frees and caller ownership survive. This
  models absent Windows TLS finalization through controlled POSIX termination.
- The original REP/gather probes still fail at their recorded first cases
  under default and software TSO. Actual WoW64 startup still fails before guest
  entry with `c0000017`. Default Wine reports hardware TSO result 5/process
  policy 0. No installed runtime, game jitter/FPS or game RSS claim is made.
- Independent review found the new CPUID terminal exit omitted `ResetStack()`.
  At that pin, bug-fixes/0036 restored the frame before assigning the
  instruction-size argument. The latest pin supplies all three resets upstream,
  so 0036 has been removed from the current queue.
  The provider blocked the dedicated spill-pressure fixture request, so direct
  CPUID execution with confirmed nonzero spill slots remains unverified.

The earlier five removals omitted contracts that the necessity audit did not establish as
required for the supported runtime. This changes the queue/source, unlike the
previous source-identical feature split. Historical passes below are baseline
evidence; final removal A/B and reduced-runtime receipts must identify their
own applied tree and loaded artifacts before they can certify this reduction.

- **bug-fixes/0017:** the sole production caller selects the current thread via
  GetCPUArea(), so threading that same pointer through reconstruction was an
  equivalent refactoring. Keep the actual XSTATE and carried-flags transport.
  Recheck current-thread context/exception continuation after removal; this is
  not removal of a demonstrated remote-thread feature.
- **bug-fixes/0018:** current generated-fault producers do not emit the added
  GP(si_code=2, err=0) or SS conditions. Generated GP uses si_code=0x80;
  NoExec with si_code=2 is PF, and native exceptions without the generated flag
  pass through separately. Recheck ordinary AV, illegal instruction, breakpoint,
  stack/guard and context continuation. Exclude incompatible cached host bytes;
  the audit only inspected current/restored A62F producers, not every historical
  external cache or future embedding API.
- **instruction-support/0002 (SERIALIZE), historical FEX-2610 decision:** remove CPUID advertisement together
  with decoder, dispatch and barrier emission. It is a detectable optional ISA
  extension; a .NET intrinsic exists, but no required game execution was found.
  Check that CPUID reports unsupported and the unsupported opcode faults rather
  than decoding another system instruction. Programs using SERIALIZE without
  checking support can lose compatibility. The FEX-2611-based pin now provides
  this feature and its reserved-encoding fix upstream, so no local patch is needed.
  SLDT, prefix and 0x82 game fixes also now come from upstream.
- **bug-fixes/0029:** the audit found no concrete failure fixed by moving x87
  environment address construction before synchronization, which does not change
  the address GPRs. The old x87 fixture did not cover these environment commands.
  Direct before/after FNSTENV, FLDENV, FNSAVE and FRSTOR checks now cover operand
  sizes, state roundtrips, ordinary base/index/displacement operands and write
  extents. Segmented or faulting environment accesses were not directly exercised;
  the synchronization/address equivalence also relies on source analysis.
  Completed x87 store addresses and fault-visible state remain.
- **bug-fixes/0030:** both old lane and replacement scalar forms read exactly the
  final two bytes of m80, but their conditional ordering barriers differ. Removal
  requires FLD m80 value/classification and tail-boundary/fault comparisons under
  default and software/forced ordering modes, including no overread. Retain the
  lane lowering's ordering contract. Prior x87 passes alone did not prove this.

The historical **A630** cache namespace separated that reduction from A62F compiled
host bytes. The actual FormatVersion is in DiskCache.h in the
provenance-checked replay. Older cached fault packets and SERIALIZE translations
are not reused.

Historical removal receipts are under
[verified-removals-20261110](../../build/local/verified-removals-20261110/).
The 61-patch official replay and both translator DLL builds pass. Native helpers
and their ABI are unchanged; the existing paired helpers are reused.
The isolated Wine run records native ARM64 execution, matching mapped DLL
identities and server cleanup for all 43 scenarios: 39 pass, and four extended
x87 runs retain the same eight pre-existing assertion failures each.

The x87 comparisons cover default hardware policy, software TSO with vector
ordering on/off, and reduced x87 precision. Full precision runs 120 checks;
reduced precision runs 111, retaining the existing nine extended-class exclusions.
Both old and reduced runtimes have byte-identical diagnostic output in each
configuration. All original classification/FXRSTOR checks, restored values,
write extents and new m80 mapped/no-access/guard-boundary checks pass.
The failing legacy-environment checks expose GetX87FTW_Helper reconstructing
only valid/empty tags from AbridgedFTW: a zero register is written as valid,
producing 03ff instead of 13ff (and f03f instead of f13f after FRSTOR).
CW, SW and TOP match. The assertions remain failing and unchanged; this separate
tag-reconstruction defect was not fixed or hidden by removing 0029/0030.
The new boundary-fault evidence is for FLD m80, not environment-command faults.
See [x87-comparison.json](../../build/local/verified-removals-20261110/x87-comparison.json).

The SERIALIZE probe checks CPUID together with execution of E8 and four adjacent
unsupported encodings: the old build advertises/runs E8, the reduced build
advertises no support and raises illegal-instruction exceptions for all five.
Retained context/XSTATE, atomics (LSE and LL/SC), SMC, shared-data, cold/warm
cache, notifications/unmaps/DEP and lifetime scenarios pass. The DWORD exception
fixture was rebuilt against the reduced production header. These x64 fixtures
do not establish working WoW64 guest startup. The x87 fixture also compiles for
i686, but its guest execution remains unverified.

Independent mutant builds demonstrate that the new checks detect their owners:
loading FLDENV status from the tag offset increases failures from 8 to 10 and
breaks register preservation in both formats; shifting the m80 tail load from
base+8 to base+9 increases failures to 25, including value, exact-boundary and
guard checks. Both runs verify the intended mutant DLL identity and server
cleanup. The final source and runtime lane were not mutated. See
[mutation-env](../../build/local/verified-removals-20261110/mutation-env/result.json)
and [mutation-tail](../../build/local/verified-removals-20261110/mutation-tail/result.json).
The test-junk scanner reports no findings but does not inspect C++ files; the
fixture assertions were reviewed directly and exercised with these mutations.

## Contracts that must remain together

Nested JIT write restoration includes the allocator and every executable writer;
shared generation publication retains its owner lifetime. LL/SC emission stays
with both unaligned fault decoders. Cache eviction retains queued blob and reader
snapshot ownership even with memory-cache budget zero. The header promotion fix
is needed only when the default-disabled positive memory LRU is enabled.
Volatile metadata is optional, but supporting it requires bounds checks and
forced-ordering precedence. SMC policy, CRC validation and JIT retry metadata
have distinct roles and remain separate patches.

The support-query cleanup removes two unused native handlers, their enum and
dispatch entry, plus the incorrect post-construction TSO comment. The final
source loses 11 lines without removing W^X or hardware TSO. The removed enum
shifts JIT-write and cache-unmapping operation numbers, so both translator DLLs
and native helpers must be rebuilt together. The imported Hadron patch stays
intact; the cleanup belongs to local bug-fixes/0002, with the later mapping
patch's context updated. Patch count and guest code-cache namespace stay the same.
The [cleanup receipt](../../build/local/jev-cleanup-20261110/verification.json)
records the exact replay, all four module builds, nested-scope hook fixture,
six native ARM64 Wine JIT/cache scenarios and 64 RO + 64 RW IndexedDB
map/destruction cycles using the renumbered native unmap operation. Loaded
module paths and staged helper hashes identify the tested runtime. This does
not establish WoW64 guest execution or game performance.

Allocation, protection and free notifications retain selective locking and
cross-process resync. Paired unmaps hold the frontend lock through completion
and query their section bounds under that lock. The rejected unconditional
lock/resync simplification for the other notifications could reenter invalidation
through call/return stack decommit or extend termination-mutex ownership while a
WoW64 target is suspended. Failed unmaps retain executable tracking until success;
explicit executable protection/free/resync clears stale DEP promotions.

Memory claims refer to different resources: enumeration and queued snapshots
protect against **UAF**, block-link arenas release owned allocations,
thread-heap retirement returns a quiescent target's heap to allocator reuse, and
IndexedDB destruction releases **native virtual mappings**. Its writable 8 GiB
mapping is a virtual reservation, not an 8 GiB RSS measurement. Nonreturning JIT
spill exits protect **stack correctness**, not heap reclamation. None establishes
the cause of a game's memory growth, character jitter or map stutter, or a game
FPS gain. Dormant threads and signal pins can intentionally retain old executable
generations; monotonic arena allocations are not reclaimed while live code uses
them. Removing unused private GDT storage is a layout reduction, not a free fix.

## Pre-PR restoration and known limits

The historical reference is [Wine packaging PR #1][packaging-pr], packaging
commit b8271a78334fd2195bd4e347897727ab03db5421, FEX-2610 upstream
14c92681f4d62cf84d901460e0358de09c8847a7 plus its original seven patches.
Its reconstructed reference tree was 1954c0dcc0a30cf7a257537ea4003ae772767a92.
Original STOS/MOVS/CMPS/SCAS/LODS, both gather frontends, MemSet/MemCpy and
associated IR/dispatch/all-ones/LastDef support were restored. The compromise
RepString/GatherLoad operations and specialized register-allocation cases were
removed. [R9] original-equality.json compares the selected methods/files to the
pinned reference; it is not a whole-repository byte-equality claim.

The unchanged extended REP/gather tests retain original failures under both
default and software TSO: REP first fails at
`addr32-zero-count-opae-df0` and gather at
`vpgatherdd-xmm-gs-addr64-disp64`. Later partial-fault and REP suspension
cases are not reached. Original bulk contracts do not provide precise
partial-fault progress. Earlier compromise-versus-scalar benchmark gains do not
describe the restored paths. Ordinary exact-output benchmark passes do not erase
these extended failures.

WoW64 DLLs historically built, but actual guest startup failed low-address
allocation with c0000017. Its instruction behavior remains a verification gap;
x64 tracker/exception-helper passes are not WoW64 execution. SVE256 execution is
unavailable on this Apple host. The earlier context-flag failures were caused by
an old native Wine ntdll.so; the matching paired continuation fix is required,
not merely the Wine version. [Build and ABI requirements](../README.md) remain
binding: host-page PE section alignment, upstream's version-dependent MinGW
preserve_all/unwind safeguard,
matching Wine TSO policy and jointly rebuilt PE/native helper ABI.

## Historical receipt index

These receipts predate the five-patch removal. Counts and namespaces inside them
belong to their recorded trees and are not current-queue verification. Existing
ARM64EC X23/W23 and PE section-alignment warnings remain recorded; the unused
x87-field warning was removed by the preceding cleanup.

| Receipt | What it establishes and its limits |
| --- | --- |
| [Necessity audit][audit] | All 67 former entries reviewed read-only; exact source/public/evidence references, three removal recommendations and two x87 verification candidates. No new execution or game measurement. |
| [R1: slop-reduction-20261110][R1] | verification.json, prepare-final.log, regressions/result.json: both DLLs and 39 native ARM64 Wine scenarios passed for the pre-removal tree; helpers unchanged/reused, hashes and cleanup recorded. Three expected-failing mutations cover paired/unpaired executable interval removal and DWORD breakpoint underflow. The tracker uses real mappings with a code-cache sink; DWORD helper runs in x64. |
| [feature-contracts replay][feature-contracts] | prepare.log: former 66-entry FEX/60-local feature split replayed to the same baseline source. No rerun of the 39 runtime scenarios for that packaging-only split. |
| [R2: necessity-debloat-20261110][R2] | prepare-reviewed.log, source-reviewed.patch and regressions-reviewed/result.json are the accepted 39-entry/36-scenario cleanup. prepared-final, regressions and source-change.patch instead belong to the rejected notification-lock simplification. |
| [R3: runtime-fixes-20261110][R3] | Former 37-patch replay, both DLL/helper builds and 36 Wine scenarios passed. Failed executable unmap and DEP protection/resync have failing baselines. Actual pinned allocator host fixture strands 8 heaps before/0 after; actual IndexedDB + native VM observer releases all 64 RO and 64 RW mappings after destruction. No game RSS result. |
| [R6: wine-runtime-audit-20261110][R6] | findings.json records failed executable unmap reproduction and then-source findings for heap retirement, DEP and mapping ownership. Nested JIT scope hook fixture and five Wine SMC/rollover/cache runs passed. Later fixes/evidence are in R3. |
| [R4: cache-link-fix-20261110][R4] | before-final/prune-result.json reproduces outside-FOZ deletion; after-unix-links/prune-result.json passes disabled/enabled/repeated pruning (1,536 checks each), directory and file links, Unicode root, foreign/malformed/nested/current-cache preservation. Held-directory sharing protects Wine replacement; direct concurrent native filesystem changes are not covered. |
| [macos-wine-only-20261110][wine-only] | Scoped inactive Windows/POSIX/ELF cleanup receipts; former 32-patch replay and DLL builds, three 1,536-check pruning runs. Its symlink failure predates/fed R4. Linux ELFParser consumers are excluded from the MinGW dependency graph. |
| [R9: prepr-memory-restore-20261110][R9] | prepare.log, original-equality.json, aligned-regressions (33 ordinary scenarios: 27 regressions, 2 exact-output benchmarks, 4 arena modes), original-limits (the known REP/gather failures). before.tar.gz preserves the prior queue. Host-page-aligned PE relink fixes startup; 4 KiB alignment failed before translator initialization. |
| [R8: arena-leak-fix-20261110][R8] | Before-fix destruction/reuse/growth retain 8/16/24 MiB over 128 cycles; explicit release leaves zero. R9 after-fix destruction/release/reuse/growth leave zero mappings/committed bytes. Not a game memory-plateau result. |
| [R5: evidence-debloat-20261109][R5] | Cache CPU production-body host fixtures with ASan/UBSan and expected-failing snapshot mutation; before.tar.gz preserves the earlier queue. Atomic-before also passed: ordering necessity is architectural, not a fabricated litmus failure. |
| [R7: minimal-local-20261109][R7] | Earlier baseline, zero-crc-before failure and post-review guests. Atomic fixtures cover 35 cases/mode, 4,096 publication, 8,192 failed-CAS ordering and 8,192 returned-observation rounds; shared-data fixtures cover scalar/SSE/AVX, clocks, write faults and consecutive cache-enabled runs (not independent cache-hit proof). |
| [restore-prepr-20261109][compromise] | Discarded compromise implementation and historical evidence; benchmark comparisons there are not current restored-path performance claims. |
| [feature-split-20261109][feature-split] | Earlier feature-split backup and prepare receipts. |
| [category-split log][category-split], [category-renumber log][category-renumber] | Historical category moves and old numbering only; the current removal leaves stable ID gaps. |

Earlier reductions removed inline DIV/dead-remainder lookahead, native/callsite
inline caches, direct branch patching, discontiguous growth, custom lookup clocks,
L1 hashing/window growth, cached IR/forwarding/POP coalescing, FPCR/AFP elision,
PAUSE throttling and specialized peepholes. Later reduction removed shared extent
vector ownership, compile-time shared-page remapping, data-only invalidation
preflight, synthetic split-address helpers, extra segmented pairing, CarryInvert
epoch exceptions and forced LL/SC on LSE hosts. Correctness/lifetime dependencies
and inherited host feature selection remain. Receipts retain the full historical
scope; restoring dead storage or optional policies is not part of this reduction.
No game runtime was installed or replaced by this documentation work.

[audit]: ../../build/local/necessity-audit-20261110/report.md
[R1]: ../../build/local/slop-reduction-20261110/
[R2]: ../../build/local/necessity-debloat-20261110/
[R3]: ../../build/local/runtime-fixes-20261110/
[R4]: ../../build/local/cache-link-fix-20261110/
[R5]: ../../build/local/evidence-debloat-20261109/
[R6]: ../../build/local/wine-runtime-audit-20261110/
[R7]: ../../build/local/minimal-local-20261109/
[R8]: ../../build/local/arena-leak-fix-20261110/
[R9]: ../../build/local/prepr-memory-restore-20261110/
[feature-contracts]: ../../build/local/feature-contracts-20261110/prepare.log
[wine-only]: ../../build/local/macos-wine-only-20261110/
[compromise]: ../../build/local/restore-prepr-20261109/
[feature-split]: ../../build/local/feature-split-20261109/
[category-split]: ../../build/local/category-split-20261110.log
[category-renumber]: ../../build/local/category-renumber-20261110.log
[packaging-pr]: https://github.com/riverfog7/wine-build-new/pull/1
[apple-jit]: https://developer.apple.com/documentation/apple-silicon/porting-just-in-time-compilers-to-apple-silicon
[hypervisor]: https://learn.microsoft.com/en-us/virtualization/hyper-v-on-windows/tlfs/feature-discovery
[arm64ec]: https://learn.microsoft.com/en-us/windows/arm/arm64ec-abi
[xstate]: https://learn.microsoft.com/en-us/windows/win32/debug/working-with-xstate-context
[context]: https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-context
[shared]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntddk/ns-ntddk-kuser_shared_data
[volatile]: https://learn.microsoft.com/en-us/cpp/build/reference/volatile
[interlocked]: https://learn.microsoft.com/en-us/windows/win32/api/winnt/nf-winnt-interlockedcompareexchange
[valve]: https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/public/tier0/threadtools.h#L422-L490
[xrstor]: https://cdrdv2-public.intel.com/929356/334569-093-sdm-vol-2d.pdf
[prefix-game]: https://github.com/FEX-Emu/FEX/issues/6065
[prefix-fix]: https://github.com/FEX-Emu/FEX/commit/68f4c5fbc48d4dc5c26b781b9f8b7aff8f7ab1d9
[swapgs]: https://www.felixcloutier.com/x86/swapgs
[swapgs-pr]: https://github.com/FEX-Emu/FEX/pull/6056
[release]: https://fex-emu.com/FEX-2611/
[split-game]: https://github.com/FEX-Emu/FEX/issues/6054
[alias-pr]: https://github.com/FEX-Emu/FEX/pull/6053
[dep]: https://learn.microsoft.com/en-us/windows/win32/memory/data-execution-prevention
[directory]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntquerydirectoryfile
[sldt-pr]: https://github.com/FEX-Emu/FEX/pull/6055
[sldt]: https://www.felixcloutier.com/x86/sldt
