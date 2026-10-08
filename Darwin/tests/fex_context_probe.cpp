// SPDX-License-Identifier: MIT
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#define CHECK(condition) do { if (!(condition)) { \
  std::fprintf(stderr, "Context check failed at line %d: %s (error=%lu)\n", __LINE__, #condition, GetLastError()); return 1; \
} } while (0)

struct WorkerArgs {
  const void* pattern;
  void* output;
  HANDLE ready;
  HANDLE gate;
  DWORD (WINAPI* wait)(HANDLE,HANDLE,DWORD,BOOL);
};
extern "C" DWORD WINAPI context_worker(void*);
extern "C" DWORD context_apc_run(const void*, void*, DWORD (WINAPI*)(HANDLE,DWORD,BOOL), HANDLE);
extern "C" void context_destroy_ymm();

// No compiler-generated vzeroupper may run between loading, callback, and observation.
asm(R"(
.text
.macro save_nonvolatile_xmm
  .irp reg,6,7,8,9,10,11,12,13,14,15
    movdqu %xmm\reg, (32 + 16 * (\reg - 6))(%rsp)
    .seh_savexmm %xmm\reg, (32 + 16 * (\reg - 6))
  .endr
.endm
.macro restore_nonvolatile_xmm
  .irp reg,6,7,8,9,10,11,12,13,14,15
    movdqu (32 + 16 * (\reg - 6))(%rsp), %xmm\reg
  .endr
.endm
.globl context_destroy_ymm
context_destroy_ymm:
  vzeroall
  ret
.globl context_apc_run
.seh_proc context_apc_run
context_apc_run:
  push %rbx
  .seh_pushreg %rbx
  push %r12
  .seh_pushreg %r12
  push %r13
  .seh_pushreg %r13
  sub $192, %rsp
  .seh_stackalloc 192
  save_nonvolatile_xmm
  .seh_endprologue
  mov %rdx, %rbx
  mov %r8, %r12
  mov %r9, %r13
  vmovdqu 0(%rcx), %ymm0
  vmovdqu 32(%rcx), %ymm1
  vmovdqu 64(%rcx), %ymm2
  vmovdqu 96(%rcx), %ymm3
  vmovdqu 128(%rcx), %ymm4
  vmovdqu 160(%rcx), %ymm5
  vmovdqu 192(%rcx), %ymm6
  vmovdqu 224(%rcx), %ymm7
  vmovdqu 256(%rcx), %ymm8
  vmovdqu 288(%rcx), %ymm9
  vmovdqu 320(%rcx), %ymm10
  vmovdqu 352(%rcx), %ymm11
  vmovdqu 384(%rcx), %ymm12
  vmovdqu 416(%rcx), %ymm13
  vmovdqu 448(%rcx), %ymm14
  vmovdqu 480(%rcx), %ymm15
  mov %r13, %rcx
  mov $10000, %edx
  mov $1, %r8d
  call *%r12
  vmovdqu %ymm0, 0(%rbx)
  vmovdqu %ymm1, 32(%rbx)
  vmovdqu %ymm2, 64(%rbx)
  vmovdqu %ymm3, 96(%rbx)
  vmovdqu %ymm4, 128(%rbx)
  vmovdqu %ymm5, 160(%rbx)
  vmovdqu %ymm6, 192(%rbx)
  vmovdqu %ymm7, 224(%rbx)
  vmovdqu %ymm8, 256(%rbx)
  vmovdqu %ymm9, 288(%rbx)
  vmovdqu %ymm10, 320(%rbx)
  vmovdqu %ymm11, 352(%rbx)
  vmovdqu %ymm12, 384(%rbx)
  vmovdqu %ymm13, 416(%rbx)
  vmovdqu %ymm14, 448(%rbx)
  vmovdqu %ymm15, 480(%rbx)
  restore_nonvolatile_xmm
  add $192, %rsp
  pop %r13
  pop %r12
  pop %rbx
  ret
.seh_endproc
.globl context_worker
.seh_proc context_worker
context_worker:
  push %rbx
  .seh_pushreg %rbx
  push %r12
  .seh_pushreg %r12
  push %r13
  .seh_pushreg %r13
  push %r14
  .seh_pushreg %r14
  sub $200, %rsp
  .seh_stackalloc 200
  save_nonvolatile_xmm
  .seh_endprologue
  mov 8(%rcx), %rbx
  mov 16(%rcx), %r13
  mov 24(%rcx), %r14
  mov 32(%rcx), %r12
  mov 0(%rcx), %rcx
  vmovdqu 0(%rcx), %ymm0
  vmovdqu 32(%rcx), %ymm1
  vmovdqu 64(%rcx), %ymm2
  vmovdqu 96(%rcx), %ymm3
  vmovdqu 128(%rcx), %ymm4
  vmovdqu 160(%rcx), %ymm5
  vmovdqu 192(%rcx), %ymm6
  vmovdqu 224(%rcx), %ymm7
  vmovdqu 256(%rcx), %ymm8
  vmovdqu 288(%rcx), %ymm9
  vmovdqu 320(%rcx), %ymm10
  vmovdqu 352(%rcx), %ymm11
  vmovdqu 384(%rcx), %ymm12
  vmovdqu 416(%rcx), %ymm13
  vmovdqu 448(%rcx), %ymm14
  vmovdqu 480(%rcx), %ymm15
  mov %r13, %rcx
  mov %r14, %rdx
  mov $10000, %r8d
  xor %r9d, %r9d
  call *%r12
  vmovdqu %ymm0, 0(%rbx)
  vmovdqu %ymm1, 32(%rbx)
  vmovdqu %ymm2, 64(%rbx)
  vmovdqu %ymm3, 96(%rbx)
  vmovdqu %ymm4, 128(%rbx)
  vmovdqu %ymm5, 160(%rbx)
  vmovdqu %ymm6, 192(%rbx)
  vmovdqu %ymm7, 224(%rbx)
  vmovdqu %ymm8, 256(%rbx)
  vmovdqu %ymm9, 288(%rbx)
  vmovdqu %ymm10, 320(%rbx)
  vmovdqu %ymm11, 352(%rbx)
  vmovdqu %ymm12, 384(%rbx)
  vmovdqu %ymm13, 416(%rbx)
  vmovdqu %ymm14, 448(%rbx)
  vmovdqu %ymm15, 480(%rbx)
  restore_nonvolatile_xmm
  add $200, %rsp
  pop %r14
  pop %r13
  pop %r12
  pop %rbx
  ret
.seh_endproc
)");

static unsigned apcs;
static void CALLBACK destroy_apc(ULONG_PTR) {
  context_destroy_ymm();
  ++apcs;
}

int main() {
  std::printf("{\"guest_entry\":true,\"pointer_bits\":%u}\n", unsigned(sizeof(void*) * 8));
  std::fflush(stdout);
  CHECK(sizeof(void*) == 8 && IsProcessorFeaturePresent(PF_AVX2_INSTRUCTIONS_AVAILABLE));
  alignas(32) uint64_t pattern[16][4], output[16][4] {}, changed[16][2];
  for (unsigned i = 0; i < 16; ++i) for (unsigned j = 0; j < 4; ++j)
    pattern[i][j] = 0x1234000000000000ULL | (uint64_t(i) << 32) | j;
  for (unsigned i = 0; i < 16; ++i) for (unsigned j = 0; j < 2; ++j)
    changed[i][j] = 0xabcd000000000000ULL | (uint64_t(i) << 32) | j;

  // The unsignaled event is subscribed before queueing; the pending APC is the only wakeup.
  HANDLE gate = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  CHECK(gate && QueueUserAPC(destroy_apc, GetCurrentThread(), 0));
  CHECK(context_apc_run(pattern, output, WaitForSingleObjectEx, gate) == WAIT_IO_COMPLETION);
  CHECK(apcs == 1);
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(!std::memcmp(pattern[i] + 2, output[i] + 2, 16));
    if (i >= 6) CHECK(!std::memcmp(pattern[i], output[i], 16));
  }
  CHECK(CloseHandle(gate));

  HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  gate = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  CHECK(ready && gate);
  // SignalObjectAndWait publishes readiness and blocks atomically, including the native-wait path.
  WorkerArgs args {pattern, output, ready, gate, SignalObjectAndWait};
  HANDLE thread = CreateThread(nullptr, 0, context_worker, &args, 0, nullptr);
  CHECK(thread && WaitForSingleObject(ready, 10000) == WAIT_OBJECT_0);
  CHECK(SuspendThread(thread) == 0);

  // Bare CONTEXT APIs must leave the adjacent bytes and unrequested registers alone.
  struct alignas(16) Bare { CONTEXT context; unsigned char guard[128]; } bare {};
  std::memset(bare.guard, 0xa5, sizeof(bare.guard));
  bare.context.ContextFlags = CONTEXT_CONTROL;
  bare.context.Rax = 0xcafebabe;
  CHECK(GetThreadContext(thread, &bare.context));
  CHECK(bare.context.Rax == 0xcafebabe);
  CHECK(SetThreadContext(thread, &bare.context));
  for (const auto byte : bare.guard) CHECK(byte == 0xa5);

  DWORD length = 0;
  PCONTEXT context = nullptr;
  CHECK(!InitializeContext(nullptr, CONTEXT_XSTATE, &context, &length) && length > sizeof(CONTEXT));
  std::vector<unsigned char> buffer(length);
  CHECK(InitializeContext(buffer.data(), CONTEXT_XSTATE, &context, &length));
  DWORD feature_length = 0;
  auto* high = static_cast<uint64_t*>(LocateXStateFeature(context, XSTATE_AVX, &feature_length));
  CHECK(high && feature_length == sizeof(changed));
  CHECK(SetXStateFeaturesMask(context, XSTATE_MASK_AVX));
  context->Rax = 0xfeedface;
  CHECK(GetThreadContext(thread, context));
  DWORD64 mask = 0;
  CHECK(GetXStateFeaturesMask(context, &mask) && (mask & XSTATE_MASK_AVX));
  CHECK(context->Rax == 0xfeedface);
  for (unsigned i = 0; i < 16; ++i) CHECK(!std::memcmp(high + i * 2, pattern[i] + 2, 16));

  std::memcpy(high, changed, sizeof(changed));
  CHECK(SetThreadContext(thread, context));
  std::memset(high, 0, sizeof(changed));
  CHECK(GetThreadContext(thread, context) && !std::memcmp(high, changed, sizeof(changed)));

  HANDLE read_context = nullptr, write_context = nullptr;
  CHECK(DuplicateHandle(GetCurrentProcess(), thread, GetCurrentProcess(), &read_context, THREAD_GET_CONTEXT, FALSE, 0));
  CHECK(DuplicateHandle(GetCurrentProcess(), thread, GetCurrentProcess(), &write_context, THREAD_SET_CONTEXT, FALSE, 0));
  CHECK(GetThreadContext(read_context, context) && !std::memcmp(high, changed, sizeof(changed)));
  CHECK(!SetThreadContext(read_context, context) && GetLastError() == ERROR_ACCESS_DENIED);
  CHECK(SetThreadContext(write_context, context));
  CHECK(!GetThreadContext(write_context, context) && GetLastError() == ERROR_ACCESS_DENIED);
  CHECK(CloseHandle(read_context) && CloseHandle(write_context));

  bare.context.ContextFlags = CONTEXT_FLOATING_POINT;
  CHECK(GetThreadContext(thread, &bare.context) && SetThreadContext(thread, &bare.context));
  CHECK(GetThreadContext(thread, context) && !std::memcmp(high, changed, sizeof(changed)));
  bare.context.ContextFlags = CONTEXT_CONTROL;
  CHECK(GetThreadContext(thread, &bare.context));
  for (const auto byte : bare.guard) CHECK(byte == 0xa5);

  // LocateExtendedFeature must reject a header-only tail without touching adjacent storage.
  struct ContextEx { struct Chunk { LONG offset; DWORD length; } all, legacy, xstate; };
  auto* context_ex = reinterpret_cast<ContextEx*>(context + 1);
  const DWORD saved_length = context_ex->xstate.length;
  context_ex->xstate.length = 64;
  CHECK(!GetThreadContext(thread, context) && GetLastError() == ERROR_INVALID_PARAMETER);
  CHECK(!SetThreadContext(thread, context) && GetLastError() == ERROR_INVALID_PARAMETER);
  context_ex->xstate.length = saved_length;

  // A Get without AVX requested must not overwrite its component bytes.
  CHECK(SetXStateFeaturesMask(context, 0));
  std::memset(high, 0xa5, sizeof(changed));
  CHECK(GetThreadContext(thread, context));
  for (unsigned i = 0; i < sizeof(changed); ++i) CHECK(reinterpret_cast<unsigned char*>(high)[i] == 0xa5);
  // On Set an absent component requests its architectural initial state.
  CHECK(SetThreadContext(thread, context));
  CHECK(SetXStateFeaturesMask(context, XSTATE_MASK_AVX));
  CHECK(GetThreadContext(thread, context));
  CHECK(GetXStateFeaturesMask(context, &mask) && (mask & XSTATE_MASK_AVX));
  for (unsigned i = 0; i < 32; ++i) CHECK(high[i] == 0);
  std::memcpy(high, changed, sizeof(changed));
  CHECK(SetThreadContext(thread, context));

  // A legacy control-only Set must not undo the separate upper-YMM Set.
  CHECK(SetThreadContext(thread, &bare.context));
  CHECK(ResumeThread(thread) == 1 && SetEvent(gate));
  CHECK(WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0);
  DWORD exit_code = 1;
  CHECK(GetExitCodeThread(thread, &exit_code) && exit_code == 0);
  for (unsigned i = 0; i < 16; ++i) {
    if (i >= 6) CHECK(!std::memcmp(output[i], pattern[i], 16));
    CHECK(!std::memcmp(output[i] + 2, changed[i], 16));
  }
  CHECK(CloseHandle(thread) && CloseHandle(ready) && CloseHandle(gate));
  std::printf("{\"context_checks\":\"PASS\",\"cases\":11,\"apc_ymm_preserved\":true,\"remote_xstate_roundtrip\":true,\"xstate_only\":true,\"legacy_context_bounded\":true,\"initial_avx_state\":true}\n");
  return 0;
}
