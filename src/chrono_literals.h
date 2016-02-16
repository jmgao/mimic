#pragma once
#pragma GCC system_header

// Chrono literals aren't widely available yet, so backport these.
#if 0
using namespace std::chrono;
#else
static constexpr std::chrono::milliseconds operator""ms(unsigned long long ms) {
  return std::chrono::milliseconds(ms);
}

static constexpr std::chrono::seconds operator""s(unsigned long long s) {
  return std::chrono::seconds(s);
}
#endif

