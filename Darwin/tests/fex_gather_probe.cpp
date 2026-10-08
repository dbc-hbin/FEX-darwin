// SPDX-License-Identifier: MIT
// Build with x86_64-w64-mingw32-clang++ -std=c++20 -O2 -Wall -Wextra -Werror -static.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cpuid.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

constexpr size_t Page = 0x10000;
static const char* case_name = "setup";
static unsigned completed, upper_snapshots;
[[noreturn]] static void fail(const char* condition, unsigned line) {
  std::fprintf(stderr, "Gather check failed: case=%s line=%u: %s\n", case_name, line, condition);
  std::fflush(stderr);
  ExitProcess(1);
}
#define CHECK(condition) do { if (!(condition)) fail(#condition, __LINE__); } while (0)

struct alignas(32) Vectors {
  unsigned char seed[32], index[32], mask[32];
  unsigned char dest[32], final_index[32], final_mask[32];
  uintptr_t original_gs, restored_gs;
};
static_assert(offsetof(Vectors, dest) == 96);
static_assert(offsetof(Vectors, original_gs) == 192);
static Vectors vectors;
struct Kind { const char* name; unsigned data, index, opcode, w; };
static constexpr Kind kinds[] = {
  {"vpgatherdd", 4, 4, 0x90, 0}, {"vpgatherdq", 8, 4, 0x90, 1},
  {"vpgatherqd", 4, 8, 0x91, 0}, {"vpgatherqq", 8, 8, 0x91, 1},
  {"vgatherdps", 4, 4, 0x92, 0}, {"vgatherdpd", 8, 4, 0x92, 1},
  {"vgatherqps", 4, 8, 0x93, 0}, {"vgatherqpd", 8, 8, 0x93, 1},
};

static uint64_t element(const unsigned char* bytes, unsigned width, unsigned lane) {
  uint64_t value = 0;
  std::memcpy(&value, bytes + lane * width, width);
  return value;
}
static void put(unsigned char* bytes, unsigned width, unsigned lane, uint64_t value) {
  std::memcpy(bytes + lane * width, &value, width);
}
static unsigned lanes(const Kind& kind, bool ymm) {
  return (ymm ? 32 : 16) / (kind.data > kind.index ? kind.data : kind.index);
}
static void initialize(const Kind& kind, bool ymm) {
  std::memset(&vectors, 0xcc, sizeof(vectors));
  for (unsigned i = 0; i < 32 / kind.data; ++i) {
    put(vectors.seed, kind.data, i, 0x7152433425160718ULL + i);
    put(vectors.mask, kind.data, i, 0x8122334455667788ULL);
  }
  // In particular, seed unused QD destination lanes and all upper XMM bits.
  CHECK(lanes(kind, ymm) * kind.data <= 32);
}

// Generated snippets use only volatile GPRs and vector registers 0, 1, 2.
// VEX gather encoding: mask=v2, dest=v0, VSIB index=v1, base=AX.
static unsigned char* code;
static size_t used;
static void byte(unsigned value) { CHECK(used < 256); code[used++] = static_cast<unsigned char>(value); }
static void imm(uint64_t value, unsigned width) {
  CHECK(used + width <= 256);
  std::memcpy(code + used, &value, width); used += width;
}
static void move(unsigned reg, uintptr_t value) {
  byte(reg >= 8 ? 0x49 : 0x48); byte(0xb8 + (reg & 7)); imm(value, 8);
}
static void vector_memory(bool store, unsigned reg, unsigned offset) {
  byte(0xc5); byte(0xfe); byte(store ? 0x7f : 0x6f);
  byte(0x82 + reg * 8); imm(offset, 4); // vmovdqu ymm, disp32(RDX)
}
static uintptr_t execute(const Kind& kind, bool ymm, unsigned scale, int displacement,
                         uintptr_t base, void* gs = nullptr, bool address32 = false);

struct Restart {
  bool active;
  uintptr_t pc;
  unsigned count, seen, lane[2], data, lane_count;
  unsigned char* page[2];
  unsigned char* source[8];
  uint64_t original[8];
  bool mutated[8];
};
static Restart restart;

// Intel SDM gather rules: faults are delivered low-to-high, but higher lanes
// may already be complete. Validate each supplied lane against its saved mask;
// never require a particular optional completion order above the fault.
static bool snapshot_lane(const unsigned char* dest, const unsigned char* mask,
                          unsigned local, unsigned lane, unsigned fault_lane) {
  const auto value = element(dest, restart.data, local);
  const auto saved_mask = element(mask, restart.data, local);
  // VEX gathers sign-extend each mask MSB before accessing any lane.
  const uint64_t pending_mask = restart.data == 4 ? 0xffffffffULL : 0xffffffffffffffffULL;
  if (lane < fault_lane) {
    CHECK(saved_mask == 0 && value == restart.original[lane]);
    return true;
  }
  if (lane == fault_lane || saved_mask != 0) {
    CHECK(saved_mask == pending_mask);
    CHECK(value == element(vectors.seed, restart.data, lane));
    return false;
  }
  CHECK(value == restart.original[lane]);
  return true;
}

static LONG CALLBACK handler(EXCEPTION_POINTERS* exception) {
  const auto& record = *exception->ExceptionRecord;
  auto* context = exception->ContextRecord;
  CHECK(restart.active && restart.seen < restart.count);
  const unsigned fault_lane = restart.lane[restart.seen];
  CHECK(record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record.NumberParameters == 2);
  CHECK(reinterpret_cast<uintptr_t>(record.ExceptionAddress) == restart.pc && context->Rip == restart.pc);
  CHECK(record.ExceptionInformation[0] == 0);
  CHECK(record.ExceptionInformation[1] == reinterpret_cast<uintptr_t>(restart.source[fault_lane]));
  CHECK((context->ContextFlags & CONTEXT_FLOATING_POINT) == CONTEXT_FLOATING_POINT);
  CHECK(!std::memcmp(&context->Xmm1, vectors.index, 16));
  bool gathered[8] {};
  const unsigned low_lanes = 16 / restart.data;
  for (unsigned i = 0; i < low_lanes && i < restart.lane_count; ++i)
    gathered[i] = snapshot_lane(reinterpret_cast<const unsigned char*>(&context->Xmm0),
                                reinterpret_cast<const unsigned char*>(&context->Xmm2), i, i, fault_lane);
  // An AVX exception must expose and restore its upper halves, not retain live handler state.
  CHECK((context->ContextFlags & CONTEXT_XSTATE) == CONTEXT_XSTATE);
  DWORD64 features = 0;
  CHECK(GetXStateFeaturesMask(context, &features));
  CHECK(features & XSTATE_MASK_AVX);
  DWORD length = 0;
  const auto* upper = static_cast<const unsigned char*>(LocateXStateFeature(context, XSTATE_AVX, &length));
  CHECK(upper && length >= 48);
  CHECK(!std::memcmp(upper + 16, vectors.index + 16, 16));
  for (unsigned i = low_lanes; i < restart.lane_count; ++i)
    gathered[i] = snapshot_lane(upper, upper + 32, i - low_lanes, i, fault_lane);
  ++upper_snapshots;
  // Changing completed source bytes proves that restart does not read them again.
  for (unsigned i = 0; i < restart.lane_count; ++i) {
    if ((i < fault_lane || gathered[i]) && !restart.mutated[i]) {
      for (unsigned j = 0; j < restart.data; ++j) restart.source[i][j] ^= 0xff;
      restart.mutated[i] = true;
    }
  }
  DWORD old = 0;
  CHECK(VirtualProtect(restart.page[restart.seen], Page, PAGE_READWRITE, &old));
  CHECK(old == PAGE_NOACCESS);
  ++restart.seen;
  // Deliberately destroy the live destination, index and mask registers. Only
  // a complete CONTEXT roundtrip can preserve progress across this handler.
  asm volatile("vpxor %%ymm0, %%ymm0, %%ymm0; vpxor %%ymm1, %%ymm1, %%ymm1; vpxor %%ymm2, %%ymm2, %%ymm2"
               ::: "ymm0", "ymm1", "ymm2");
  // No PC advance and no vector/context repair: retry the original gather.
  return EXCEPTION_CONTINUE_EXECUTION;
}

static uintptr_t execute(const Kind& kind, bool ymm, unsigned scale, int displacement,
                         uintptr_t base, void* gs, bool address32) {
  used = 0;
  move(0, base); move(2, reinterpret_cast<uintptr_t>(&vectors));
  vector_memory(false, 0, 0); vector_memory(false, 1, 32); vector_memory(false, 2, 64);
  if (gs) {
    move(8, reinterpret_cast<uintptr_t>(gs));
    byte(0xf3); byte(0x49); byte(0x0f); byte(0xae); byte(0xc9); // rdgsbase R9
    byte(0x4c); byte(0x89); byte(0x8a); imm(192, 4); // save original GS
    byte(0xf3); byte(0x49); byte(0x0f); byte(0xae); byte(0xd8); // wrgsbase R8
  }
  const auto pc = reinterpret_cast<uintptr_t>(code + used);
  if (gs) byte(0x65);
  if (address32) byte(0x67);
  byte(0xc4); byte(0xe2); byte(0x69 | (ymm ? 4 : 0) | (kind.w << 7));
  byte(kind.opcode); byte(0x84); byte(0x08 | (scale << 6)); imm(static_cast<uint32_t>(displacement), 4);
  if (gs) {
    byte(0xf3); byte(0x49); byte(0x0f); byte(0xae); byte(0xd9); // restore GS from R9
    byte(0xf3); byte(0x49); byte(0x0f); byte(0xae); byte(0xc8); // rdgsbase R8
    byte(0x4c); byte(0x89); byte(0x82); imm(200, 4);
  }
  vector_memory(true, 0, 96); vector_memory(true, 1, 128); vector_memory(true, 2, 160);
  byte(0xc5); byte(0xf8); byte(0x77); byte(0xc3); // vzeroupper; ret
  CHECK(FlushInstructionCache(GetCurrentProcess(), code, used));
  if (restart.active) restart.pc = pc;
  reinterpret_cast<void (*)()>(code)();
  if (gs) CHECK(vectors.original_gs == vectors.restored_gs);
  return pc;
}

static void verify(const Kind& kind, bool ymm, const unsigned char* expected) {
  CHECK(!std::memcmp(vectors.dest, expected, 32));
  CHECK(!std::memcmp(vectors.final_index, vectors.index, 32));
  const unsigned char zero[32] {};
  CHECK(!std::memcmp(vectors.final_mask, zero, 32));
  // Includes zeroing above VL and QD's unused destination lanes.
  for (unsigned i = lanes(kind, ymm) * kind.data; i < 32; ++i) CHECK(vectors.dest[i] == 0);
  ++completed;
}

static void ordinary_cases(unsigned char* arena) {
  DWORD old = 0;
  CHECK(VirtualProtect(arena + Page, Page, PAGE_NOACCESS, &old));
  CHECK(old == PAGE_READWRITE);
  char name[128];
  for (const auto& kind : kinds) for (unsigned length = 0; length < 2; ++length)
    for (unsigned scale = 0; scale < 4; ++scale) for (unsigned negative = 0; negative < 2; ++negative)
      for (unsigned masking = 0; masking < 3; ++masking) {
        const bool ymm = length != 0;
        const int displacement = negative ? -64 : 64;
        std::snprintf(name, sizeof(name), "%s-%s-scale%u-disp%d-mask%u", kind.name,
                      ymm ? "ymm" : "xmm", 1U << scale, displacement, masking);
        case_name = name;
        initialize(kind, ymm);
        unsigned char expected[32] {};
        auto* base = arena + Page / 2;
        for (unsigned i = 0; i < lanes(kind, ymm); ++i) {
          const bool active = masking == 0 || (masking == 1 && !(i & 1));
          const int64_t index = active ? (i & 1 ? -int64_t(32 * i + 16) : int64_t(32 * i + 16))
            : int64_t(Page / 2 - displacement) / (1U << scale);
          put(vectors.index, kind.index, i, static_cast<uint64_t>(index));
          put(vectors.mask, kind.data, i, active ? 0x8122334481223344ULL : 0x0122334401223344ULL);
          if (active) {
            auto* source = base + index * (1U << scale) + displacement;
            const uint64_t value = 0x3fe123453f234567ULL + i;
            std::memcpy(source, &value, kind.data);
            std::memcpy(expected + i * kind.data, source, kind.data);
          } else {
            std::memcpy(expected + i * kind.data, vectors.seed + i * kind.data, kind.data);
          }
        }
        execute(kind, ymm, scale, displacement, reinterpret_cast<uintptr_t>(base));
        verify(kind, ymm, expected);
      }
  CHECK(VirtualProtect(arena + Page, Page, PAGE_READWRITE, &old));
  CHECK(old == PAGE_NOACCESS);
}

static void segment_cases(unsigned char* window) {
  char name[128];
  for (const auto& kind : kinds) for (unsigned length = 0; length < 2; ++length)
    for (unsigned mode = 0; mode < 3; ++mode) {
      const bool ymm = length != 0, address32 = mode != 0;
      const int displacement = mode == 2 ? -64 : 64;
      const uintptr_t base = mode == 0 ? 0x200 : mode == 1 ? 0xfffffff0 : 0x10;
      std::snprintf(name, sizeof(name), "%s-%s-gs-%s-disp%d", kind.name, ymm ? "ymm" : "xmm",
                    address32 ? "addr32-wrap" : "addr64", displacement);
      case_name = name;
      initialize(kind, ymm);
      unsigned char expected[32] {};
      for (unsigned i = 0; i < lanes(kind, ymm); ++i) {
        int64_t index = int64_t(i * 4) - 8;
        // In addr32 mode, qword VSIB indices must also truncate before GS addition.
        if (address32 && kind.index == 8) index += (i & 1) ? -0x100000000LL : 0x100000000LL;
        put(vectors.index, kind.index, i, static_cast<uint64_t>(index));
        put(vectors.mask, kind.data, i, 0x8122334481223344ULL);
        const auto sum = base + static_cast<uint64_t>(index * 4) + displacement;
        const uintptr_t offset = address32 ? static_cast<uint32_t>(sum) : sum;
        const uint64_t value = 0x402abcdef1234567ULL + i;
        std::memcpy(window + offset, &value, kind.data);
        std::memcpy(expected + i * kind.data, window + offset, kind.data);
      }
      // No Windows calls, faults or callbacks occur while GS differs from the TEB.
      execute(kind, ymm, 2, displacement, base, window, address32);
      verify(kind, ymm, expected);
    }
}

static void restart_cases(unsigned char* arena) {
  char name[128];
  for (const auto& kind : kinds) for (unsigned mode = 0; mode < 3; ++mode) {
    std::snprintf(name, sizeof(name), "%s-ymm-restart-%s", kind.name,
                  mode == 0 ? "low" : mode == 1 ? "high" : "two-faults");
    case_name = name;
    initialize(kind, true);
    restart = {};
    restart.active = true; restart.data = kind.data;
    restart.lane_count = lanes(kind, true);
    const unsigned low = kind.data == 4 && kind.index == 4 ? 2 : 1;
    const unsigned high = restart.lane_count - (restart.lane_count == 8 ? 2 : 1);
    restart.count = mode == 2 ? 2 : 1;
    restart.lane[0] = mode == 1 ? high : low; restart.lane[1] = high;
    unsigned char expected[32] {};
    for (unsigned i = 0; i < restart.lane_count; ++i) {
      unsigned page = 0;
      if (i == restart.lane[0]) page = 1;
      if (mode == 2 && i == high) page = 2;
      restart.source[i] = arena + page * Page + i * 32;
      restart.original[i] = kind.data == 4 ? 0x3f456780ULL + i : 0x3ff123456789abc0ULL + i;
      std::memcpy(restart.source[i], &restart.original[i], kind.data);
      put(vectors.index, kind.index, i, static_cast<uint64_t>(restart.source[i] - arena));
      put(vectors.mask, kind.data, i, 0x8122334481223344ULL);
      put(expected, kind.data, i, restart.original[i]);
    }
    for (unsigned i = 0; i < restart.count; ++i) {
      restart.page[i] = arena + (i + 1) * Page;
      DWORD old = 0;
      CHECK(VirtualProtect(restart.page[i], Page, PAGE_NOACCESS, &old));
      CHECK(old == PAGE_READWRITE);
    }
    execute(kind, true, 0, 0, reinterpret_cast<uintptr_t>(arena));
    CHECK(restart.seen == restart.count);
    for (unsigned i = 0; i < restart.lane[restart.count - 1]; ++i) {
      CHECK(restart.mutated[i]);
      const uint64_t changed = kind.data == 4 ? uint32_t(~restart.original[i]) : ~restart.original[i];
      CHECK(element(restart.source[i], kind.data, 0) == changed);
    }
    verify(kind, true, expected);
    restart.active = false;
  }
}

int main() {
  std::printf("{\"guest_entry\":true,\"pointer_bits\":64}\n");
  std::fflush(stdout);
  static_assert(sizeof(void*) == 8);
  unsigned a, b, c, d;
  CHECK(__get_cpuid_max(0, nullptr) >= 7);
  __cpuid(1, a, b, c, d);
  CHECK((c & ((1U << 26) | (1U << 27) | (1U << 28))) == ((1U << 26) | (1U << 27) | (1U << 28)));
  uint32_t xcr_low, xcr_high;
  asm volatile("xgetbv" : "=a"(xcr_low), "=d"(xcr_high) : "c"(0));
  CHECK((xcr_low & 6) == 6);
  __cpuid_count(7, 0, a, b, c, d);
  CHECK(b & (1U << 5)); // AVX2 is required, never silently skipped.
  CHECK(b & 1U); // FSGSBASE is required for private-GS tests.
  code = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
  auto* arena = static_cast<unsigned char*>(VirtualAlloc(nullptr, 3 * Page, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
  CHECK(code && arena);
  constexpr size_t span = 0x100000000ULL;
  auto* window = static_cast<unsigned char*>(VirtualAlloc(nullptr, span + Page, MEM_RESERVE, PAGE_NOACCESS));
  CHECK(window);
  CHECK(VirtualAlloc(window, Page, MEM_COMMIT, PAGE_READWRITE) == window);
  CHECK(VirtualAlloc(window + span - Page, 2 * Page, MEM_COMMIT, PAGE_READWRITE) == window + span - Page);
  const auto veh = AddVectoredExceptionHandler(1, handler);
  CHECK(veh);
  ordinary_cases(arena);
  segment_cases(window);
  restart_cases(arena);
  CHECK(RemoveVectoredExceptionHandler(veh));
  CHECK(VirtualFree(window, 0, MEM_RELEASE));
  CHECK(VirtualFree(arena, 0, MEM_RELEASE));
  CHECK(VirtualFree(code, 0, MEM_RELEASE));
  std::printf("{\"gather_checks\":\"PASS\",\"cases\":%u,\"upper_xstate_snapshots\":%u}\n", completed, upper_snapshots);
}
