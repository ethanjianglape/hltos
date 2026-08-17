#pragma once

#include <cstdint>

namespace syscall {
int sys_sleep_ns(std::uint64_t ns);
}
