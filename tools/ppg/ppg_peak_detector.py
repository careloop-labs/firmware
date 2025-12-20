#!/usr/bin/env python3
"""Adaptive threshold peak detection for PPG signals.

Implements a simple but robust real-time peak detection algorithm
suitable for embedded implementation.
"""

import numpy as np
from typing import Optional


class PeakDetector:
    """Adaptive threshold peak detector for PPG heart rate estimation."""
    
    def __init__(self, sample_rate: int = 100, min_bpm: int = 40, max_bpm: int = 200):
        """Initialize the peak detector.
        
        Args:
            sample_rate: Sampling rate in Hz
            min_bpm: Minimum expected heart rate (for max refractory period)
            max_bpm: Maximum expected heart rate (for min refractory period)
        """
        self.sample_rate = sample_rate
        
        # Refractory period: minimum samples between peaks
        # At max_bpm, this is the shortest valid peak-to-peak interval
        self.min_peak_distance = int(60 * sample_rate / max_bpm)
        
        # Running statistics (exponential moving average)
        self.alpha = 0.05  # Smoothing factor for mean/variance
        self.mean = 0.0
        self.var = 0.0
        
        # Peak detection state
        self.last_peak_idx = -999999  # Initialize far in the past
        self.prev_value = 0.0
        self.prev2_value = 0.0
        self.sample_idx = 0
        
        # BPM calculation from inter-peak intervals
        self.peak_times = []  # Store last N peak times (in seconds)
        self.bpm_window = 5   # Use last 5 peaks for BPM averaging
        
        self.current_bpm = 0.0
        
    def reset(self):
        """Reset detector state."""
        self.mean = 0.0
        self.var = 0.0
        self.last_peak_idx = -999999
        self.prev_value = 0.0
        self.prev2_value = 0.0
        self.sample_idx = 0
        self.peak_times = []
        self.current_bpm = 0.0
    
    def process(self, value: float) -> Optional[float]:
        """Process one sample from the filtered PPG signal.
        
        Args:
            value: Single sample from the filtered (bandpass) PPG signal
            
        Returns:
            BPM value if a peak was detected and BPM can be calculated, else None
        """
        # Update running statistics using exponential moving average
        delta = value - self.mean
        self.mean += self.alpha * delta
        self.var += self.alpha * (delta * delta - self.var)
        std = np.sqrt(max(self.var, 1e-10))  # Avoid division by zero
        
        # Adaptive threshold: mean + k*std
        # k=0.4 works well for filtered PPG (tune if needed)
        threshold = self.mean + 0.4 * std
        
        # Detect peak using three criteria:
        # 1. Current value above adaptive threshold
        # 2. Local maximum: prev2 < prev < current and current > future
        #    (we check current > prev > prev2, assuming next will be lower)
        # 3. Refractory period: enough time since last peak
        is_peak = False
        if (value > threshold and 
            value > self.prev_value and 
            self.prev_value > self.prev2_value and
            (self.sample_idx - self.last_peak_idx) > self.min_peak_distance):
            
            is_peak = True
            self.last_peak_idx = self.sample_idx
            
            # Store peak time in seconds
            peak_time = self.sample_idx / self.sample_rate
            self.peak_times.append(peak_time)
            
            # Keep only recent peaks for BPM calculation
            if len(self.peak_times) > self.bpm_window:
                self.peak_times.pop(0)
        
        # Update sample history
        self.prev2_value = self.prev_value
        self.prev_value = value
        self.sample_idx += 1
        
        # Calculate BPM from recent inter-peak intervals
        if len(self.peak_times) >= 2:
            # Calculate time differences between consecutive peaks
            time_diffs = np.diff(self.peak_times)
            # Average period (peak-to-peak time)
            avg_period = np.mean(time_diffs)
            # Convert to BPM (60 seconds / period)
            self.current_bpm = 60.0 / avg_period
            
            # Return BPM only when a new peak is detected
            if is_peak:
                return self.current_bpm
        
        return None
    
    def get_current_bpm(self) -> float:
        """Get the most recent BPM estimate.
        
        Returns:
            Current BPM estimate (0.0 if not enough data yet)
        """
        return self.current_bpm


# Public API
__all__ = ["PeakDetector"]
