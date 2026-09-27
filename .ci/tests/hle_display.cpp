#include <cassert>
#include <cstdint>
#include <cstring>
#define RET_ERROR(code) (code)
constexpr int SCE_GXM_ERROR_INVALID_POINTER = -1;
constexpr int SCE_GXM_ERROR_OUT_OF_MEMORY = -2;
constexpr int SCE_GXM_ERROR_INVALID_VALUE = -3;
using Address = uint32_t;
struct MemState { char bytes[16] = {}; bool fail = false; int allocations = 0; };
struct Params { uint32_t displayQueueMaxPendingCount = 0, displayQueueCallbackDataSize = 0; };
struct Environment { struct { Params params; } gxm; MemState mem; };
template <typename T> struct Ptr {
    Address address;
    explicit Ptr(Address address) : address(address) {}
    explicit operator bool() const { return address != 0; }
    void *get(MemState &mem) const { assert(address); return mem.bytes + address; }
};
static Address alloc(MemState &mem, uint32_t size, const char *) {
    ++mem.allocations;
    assert(size == 4);
    return mem.fail ? 0 : 1;
}
static int prepare(Environment &emuenv, Ptr<const void> callbackData) {
    // INSERT_ALLOCATION
    return address;
}
static int validate(const Params *params) {
    // INSERT_VALIDATION
    return 0;
}
int main() {
    Params params{UINT32_MAX, 2};
    assert(validate(&params) == SCE_GXM_ERROR_INVALID_VALUE);
    params = {0x80000000, 2}; // Previously wrapped to zero and passed the check.
    assert(validate(&params) == SCE_GXM_ERROR_INVALID_VALUE);
    params = {2, 256};
    assert(validate(&params) == 0);
    params = {0, 0}; // A caller with no display callback remains legal here.
    assert(validate(&params) == 0);
    Environment env;
    assert(prepare(env, Ptr<const void>(0)) == 0 && env.mem.allocations == 0);
    env.gxm.params.displayQueueCallbackDataSize = 4;
    assert(prepare(env, Ptr<const void>(0)) == SCE_GXM_ERROR_INVALID_POINTER && env.mem.allocations == 0);
    env.mem.fail = true;
    assert(prepare(env, Ptr<const void>(8)) == SCE_GXM_ERROR_OUT_OF_MEMORY);
    env.mem.fail = false;
    std::memcpy(env.mem.bytes + 8, "VITA", 4);
    assert(prepare(env, Ptr<const void>(8)) == 1);
    assert(std::memcmp(env.mem.bytes + 1, "VITA", 4) == 0);
}
