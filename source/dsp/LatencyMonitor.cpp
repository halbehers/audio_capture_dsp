#include "../../include/dsp/LatencyMonitor.h"

#include <algorithm>

namespace audiocapture::dsp
{

void LatencyMonitor::recordProduced(juce::int64 unitsProduced)
{
    if (unitsProduced <= 0)
        return;

    _cumulativeUnitsProduced += unitsProduced;

    const int next = (_writeIndex.load(std::memory_order_relaxed) + 1) & (ringCapacity - 1);
    _ring[(size_t) next] = { _cumulativeUnitsProduced, juce::Time::getMillisecondCounterHiRes() };
    _writeIndex.store(next, std::memory_order_release);
}

double LatencyMonitor::consumeLatencyMs(juce::int64 unitsConsumed)
{
    if (unitsConsumed > 0)
        _cumulativeUnitsConsumed += unitsConsumed;

    const int writeIdx = _writeIndex.load(std::memory_order_acquire);
    if (writeIdx < 0)
        return 0.0; // nothing produced yet

    if (unitsConsumed <= 0 && _cumulativeUnitsConsumed >= _ring[(size_t) writeIdx].cumulativeUnitsProduced)
        return 0.0;

    while (_readCursor != writeIdx &&
           _ring[(size_t) _readCursor].cumulativeUnitsProduced < _cumulativeUnitsConsumed)
    {
        _readCursor = (_readCursor + 1) & (ringCapacity - 1);
    }

    const double dwellMs = juce::Time::getMillisecondCounterHiRes() - _ring[(size_t) _readCursor].captureTimeMs;
    return std::max(0.0, dwellMs);
}

}
