/*
 * Timer_ReadTSC is inline assembly in the original too (cpuid; rdtsc).
 */
#include "timer.h"
#include "sdw_classes.h"

#include "../sdk/win32.h"
#include "../sdk/crt.h"

double g_cpuHz;

struct TimerCalibrationWork {
    s64 tscBefore, tscAfter, pcAfter;
    s64 samples[100];
    s64 frequency, pcBefore, sum;
};

/* on first construction, measures TSC ticks per QueryPerformanceCounter interval 100 times around a busy loop
 * of 10,000 sin(pow(i, 10)) calls, and sets g_cpuHz = sum * 1e6 / 100228400.0 (constant). */
Timer::Timer()
{
    TimerCalibrationWork work;
    struct {
        u16 inner, outer;
    } loop;
    if (g_cpuHz == 0.0) {
        work.sum = 0;
        QueryPerformanceFrequency(&work.frequency);
        for (loop.outer = 0; loop.outer < 100; loop.outer++) {
            QueryPerformanceCounter(&work.pcBefore);
            work.tscBefore = ReadTSC();
            for (loop.inner = 0; loop.inner < 10000; loop.inner++)
                sin(pow((double)loop.inner, 10.0));
            QueryPerformanceCounter(&work.pcAfter);
            work.tscAfter = ReadTSC();
            work.samples[loop.outer] =
                work.frequency * (work.tscAfter - work.tscBefore) / (work.pcAfter - work.pcBefore);
            work.sum += work.samples[loop.outer];
        }
        g_cpuHz = (double)(work.sum * 1000000) / 100228400.0;
    }
    baseTsc = 0;
    elapsedTsc = 0;
    deltaTsc = 0;
    running = 0;
}

Timer::~Timer() {}

void Timer::Start()
{
    running = 1;
    baseTsc = ReadTSC();
    elapsedTsc = 0;
    deltaTsc = 0;
}

/* takes a last sample, then stops. */
void Timer::Stop()
{
    s64 previous = elapsedTsc;
    elapsedTsc = ReadTSC() - baseTsc;
    deltaTsc = elapsedTsc - previous;
    running = 0;
}

/* Time_Resume passes the negated unconsumed delta saved by Time_Pause. */
void Timer::OffsetBase(double ticks)
{
    baseTsc += (s64)ticks;
}

/* samples (so it also advances deltaTsc), returns the elapsed time. */
double Timer::GetElapsed(int unit)
{
    struct {
        s64 previous;
        double result;
    } work;
    if (running == 1) {
        work.previous = elapsedTsc;
        elapsedTsc = ReadTSC() - baseTsc;
        deltaTsc = elapsedTsc - work.previous;
        work.result = Convert(elapsedTsc, unit);
    } else {
        work.result = Convert(elapsedTsc, unit);
    }
    return work.result;
}

/* samples, returns the time since the previous sample; 0.0 while stopped. */
double Timer::GetDelta(int unit)
{
    struct {
        s64 previous;
        double result;
    } work;
    if (running == 1) {
        work.previous = elapsedTsc;
        elapsedTsc = ReadTSC() - baseTsc;
        deltaTsc = elapsedTsc - work.previous;
        work.result = Convert(deltaTsc, unit);
    } else {
        work.result = 0.0;
    }
    return work.result;
}

double Timer::PeekElapsed(int unit)
{
    return Convert(elapsedTsc, unit);
}

double Timer::PeekDelta(int unit)
{
    double result;
    if (running == 1)
        result = Convert(deltaTsc, unit);
    else
        result = 0.0;
    return result;
}

/* The performance counter (Windows', or SDL's on the other systems: src/platform/win32_sdl_compat.cpp) in place of
 * the original's RDTSC: steady under frequency scaling and on every CPU. The calibration in the constructor then
 * measures it against itself, so g_cpuHz is the counter's frequency. */
s64 Timer::ReadTSC()
{
    s64 ticks = 0;
    QueryPerformanceCounter(&ticks);
    return ticks;
}

double Timer::Convert(s64 ticks, int unit)
{
    double result;
    switch (unit) {
        case TIMER_TICKS:
            result = (double)ticks;
            break;
        case TIMER_SECONDS:
            result = (double)ticks / g_cpuHz;
            break;
        case TIMER_MILLISECONDS:
            result = (double)(ticks * 1000) / g_cpuHz;
            break;
        case TIMER_MICROSECONDS:
            result = (double)(ticks * 1000000) / g_cpuHz;
            break;
        default:
            result = -1.0;
            break;
    }
    return result;
}
