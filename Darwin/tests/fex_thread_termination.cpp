// SPDX-License-Identifier: MIT
// Build as x64 or x86 PE and run through native ARM64 Wine/FEX.
#include <windows.h>
#include <cassert>
#include <cstdint>
#include <cstdio>

struct WorkerState {
  HANDLE ready;
  HANDLE proceed;
  HANDLE running;
  volatile LONG value;
};

static DWORD WINAPI Worker(void* argument) {
  auto* state = static_cast<WorkerState*>(argument);
  assert(SetEvent(state->ready));
  assert(WaitForSingleObject(state->proceed, 10000) == WAIT_OBJECT_0);
  assert(SetEvent(state->running));
  // Keep executing guest instructions without holding CRT or Windows locks.
  for (;;) {
    state->value = static_cast<LONG>((static_cast<uint32_t>(state->value) * 1664525U + 1013904223U) & 0x7fffffffU);
  }
}

static DWORD WINAPI ExitNormally(void*) {
  return 0x1234;
}

int main() {
  std::printf("{\"guest_entry\":true,\"pointer_bits\":%zu}\n", sizeof(void*) * 8);
  char path[MAX_PATH];
  assert(GetModuleFileNameA(nullptr, path, sizeof(path)));
  std::printf("FEX_PROBE_IMAGE:%s\n", path);
  std::fflush(stdout);
  constexpr unsigned cases = 32;
  for (unsigned i = 0; i < cases; ++i) {
    WorkerState state {CreateEventW(nullptr, TRUE, FALSE, nullptr),
                       CreateEventW(nullptr, TRUE, FALSE, nullptr),
                       CreateEventW(nullptr, TRUE, FALSE, nullptr), 0};
    assert(state.ready && state.proceed && state.running);
    HANDLE thread = CreateThread(nullptr, 0, Worker, &state, 0, nullptr);
    assert(thread);
    assert(WaitForSingleObject(state.ready, 10000) == WAIT_OBJECT_0);
    assert(SuspendThread(thread) == 0);
    CONTEXT context {};
    context.ContextFlags = CONTEXT_CONTROL;
    assert(GetThreadContext(thread, &context));
    assert(SetEvent(state.proceed));
    assert(ResumeThread(thread) == 1);
    assert(WaitForSingleObject(state.running, 10000) == WAIT_OBJECT_0);
    assert(TerminateThread(thread, 0x5678));
    assert(WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0);
    DWORD code = 0;
    assert(GetExitCodeThread(thread, &code) && code == 0x5678);
    assert(CloseHandle(thread));
    assert(CloseHandle(state.ready));
    assert(CloseHandle(state.proceed));
    assert(CloseHandle(state.running));
    thread = CreateThread(nullptr, 0, ExitNormally, nullptr, 0, nullptr);
    assert(thread && WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0);
    assert(GetExitCodeThread(thread, &code) && code == 0x1234);
    assert(CloseHandle(thread));
  }
  std::printf("{\"thread_termination\":\"PASS\",\"cases\":%u}\n", cases);
}
