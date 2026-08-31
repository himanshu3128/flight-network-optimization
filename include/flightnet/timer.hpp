// timer.hpp -- High-resolution wall-clock timer.
//
// std::chrono::steady_clock advertises a nanosecond period but several toolchains
// (notably MinGW on Windows) back it with a ~1 ms system tick. A single max-flow
// run on a 60-airport network takes tens of microseconds, so that granularity
// rounds almost every measurement to zero and makes the benchmark meaningless.
//
// On Windows we therefore read QueryPerformanceCounter directly (0.1 us ticks);
// everywhere else steady_clock is fine and is used as-is.
#ifndef FLIGHTNET_TIMER_HPP
#define FLIGHTNET_TIMER_HPP

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <chrono>
#endif

namespace flightnet {

class Timer {
public:
    Timer() { reset(); }

    void reset() { start_ = now(); }

    // Milliseconds since construction or the last reset().
    double elapsedMs() const {
#if defined(_WIN32)
        return 1000.0 * static_cast<double>(now() - start_) / static_cast<double>(frequency());
#else
        return std::chrono::duration<double, std::milli>(now() - start_).count();
#endif
    }

    // Smallest interval this clock can distinguish, in milliseconds. Reported by
    // the benchmark so a reader can tell measurement noise from real signal.
    static double resolutionMs() {
#if defined(_WIN32)
        return 1000.0 / static_cast<double>(frequency());
#else
        typedef std::chrono::steady_clock C;
        return 1000.0 * static_cast<double>(C::period::num) / static_cast<double>(C::period::den);
#endif
    }

private:
#if defined(_WIN32)
    typedef long long Tick;

    static Tick frequency() {
        static const Tick f = queryFrequency();
        return f;
    }

    // Typical QPC rate on modern Windows is 10 MHz (0.1 µs ticks), matching
    // the resolution documented at the top of this file. Using 1 as a fallback
    // would treat raw tick counts as seconds and inflate elapsedMs() by ~1e7.
    static const Tick kFallbackFrequency = 10000000;

    static Tick queryFrequency() {
        LARGE_INTEGER f;
        if (QueryPerformanceFrequency(&f) && f.QuadPart > 0)
            return static_cast<Tick>(f.QuadPart);
        return kFallbackFrequency;
    }

    static Tick now() {
        static Tick last = 0;
        LARGE_INTEGER c;
        if (QueryPerformanceCounter(&c)) {
            last = static_cast<Tick>(c.QuadPart);
            return last;
        }
        // QPC failed (uninitialized LARGE_INTEGER). Reuse the last successful
        // reading so elapsedMs() stays monotonic instead of jumping to garbage.
        // If no reading has succeeded yet, 0 yields a zero elapsed interval.
        return last;
    }
#else
    typedef std::chrono::steady_clock::time_point Tick;
    static Tick now() { return std::chrono::steady_clock::now(); }
#endif

    Tick start_;
};

} // namespace flightnet

#endif // FLIGHTNET_TIMER_HPP
