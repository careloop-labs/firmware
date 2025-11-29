#!/usr/bin/env python3
"""Heart rate bandpass IIR filter (Python port of hr_filter.cpp)

This module implements a 3-stage biquad cascade (Direct Form II Transposed):
- 2 high-pass stages (cutoff ~0.5 Hz) to reject DC and drift
- 1 low-pass stage (cutoff ~10 Hz) to reject high-frequency noise

The result is a bandpass filter suitable for PPG heart rate detection.
"""

from typing import List


class BiquadDF2T:
    """Single biquad section using Direct Form II Transposed structure.
    
    y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
    """
    
    def __init__(self, b0: float = 0.0, b1: float = 0.0, b2: float = 0.0,
                 a1: float = 0.0, a2: float = 0.0):
        self.b0 = b0
        self.b1 = b1
        self.b2 = b2
        self.a1 = a1
        self.a2 = a2
        # Delay states (transposed form)
        self.s1 = 0.0
        self.s2 = 0.0
    
    def process(self, x: float) -> float:
        """Process one sample through the biquad."""
        y = self.b0 * x + self.s1
        self.s1 = self.b1 * x - self.a1 * y + self.s2
        self.s2 = self.b2 * x - self.a2 * y
        return y
    
    def reset(self) -> None:
        """Reset the filter state."""
        self.s1 = 0.0
        self.s2 = 0.0


class BiquadCascadeDF2T:
    """Cascade of biquad sections for multi-stage filtering."""
    
    def __init__(self, sections: List[BiquadDF2T]):
        self.sections = sections
    
    def process(self, x: float) -> float:
        """Process one sample through all cascaded sections."""
        y = x
        for section in self.sections:
            y = section.process(y)
        return y
    
    def process_buffer(self, input_buffer: List[float]) -> List[float]:
        """Process a buffer of samples.
        
        Args:
            input_buffer: List of input samples
            
        Returns:
            List of filtered output samples
        """
        output = []
        for x in input_buffer:
            y = self.process(x)
            output.append(y)
        return output
    
    def reset(self) -> None:
        """Reset all filter states."""
        for section in self.sections:
            section.reset()


# SOS coefficients from design_sos.py (same as C++ version)
# Each row: [b0, b1, b2, a1, a2] with a0=1
SOS = [
    # HP stage 1: b0, b1, b2, a1, a2
    [0.967694809, -1.935389618, 0.967694809, -1.954001962, 0.954619251],
    # HP stage 2: b0, b1, b2, a1, a2
    [1.0, -2.0, 1.0, -1.980323859, 0.980949464],
    # LP stage 1: b0, b1, b2, a1, a2
    [0.036574836, 0.073149672, 0.036574836, -1.390895281, 0.537194625]
]

# Global filter instance
_hr_filter = None


def hr_filter_init() -> None:
    """Initialize the heart rate bandpass filter.
    
    Must be called once before using hr_filter_process().
    """
    global _hr_filter
    sections = []
    for sos_row in SOS:
        b0, b1, b2, a1, a2 = sos_row
        sections.append(BiquadDF2T(b0, b1, b2, a1, a2))
    _hr_filter = BiquadCascadeDF2T(sections)


def hr_filter_process(raw_value: float) -> float:
    """Process a single raw PPG sample through the bandpass filter.
    
    Args:
        raw_value: Raw PPG sample value
        
    Returns:
        Filtered PPG value
        
    Raises:
        RuntimeError: If hr_filter_init() has not been called
    """
    global _hr_filter
    if _hr_filter is None:
        raise RuntimeError("hr_filter_init() must be called before hr_filter_process()")
    return _hr_filter.process(raw_value)


def hr_filter_reset() -> None:
    """Reset the filter state (clear internal delays).
    
    Useful when starting a new signal acquisition session.
    """
    global _hr_filter
    if _hr_filter is not None:
        _hr_filter.reset()


# Public API
__all__ = ["hr_filter_init", "hr_filter_process", "hr_filter_reset"]
