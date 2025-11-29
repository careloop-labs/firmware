#!/usr/bin/env python3
"""Live plotting for PPG signals with time domain and frequency spectrum.

Provides real-time visualization of:
- Top plot: Time-domain signals (raw and filtered) with legend
- Bottom plot: Frequency spectrum (FFT) of the filtered signal
"""

import numpy as np
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
from collections import deque
from typing import Optional


class PPGPlotter:
    """Live plotter for PPG signals with dual subplot layout."""
    
    def __init__(self, buffer_size: int = 400, sample_rate: int = 25, 
                 update_interval_ms: int = 100):
        """Initialize the PPG plotter.
        
        Args:
            buffer_size: Number of samples to display
            sample_rate: Sampling rate in Hz (for FFT frequency axis)
            update_interval_ms: Animation update interval in milliseconds
        """
        self.buffer_size = buffer_size
        self.sample_rate = sample_rate
        self.update_interval_ms = update_interval_ms
        
        # Data buffers (external references will be set via set_buffers)
        self.raw_buffer = None
        self.filtered_buffer = None
        self.current_bpm = 0.0  # BPM value to display
        
        # Setup the figure with three subplots
        self.fig, (self.ax_raw, self.ax_filtered, self.ax_freq) = plt.subplots(3, 1, figsize=(12, 10))
        self.fig.suptitle('PPG Signal Analysis', fontsize=14, fontweight='bold')
        
        # Top subplot: Raw signal
        self.line_raw, = self.ax_raw.plot([], [], 'b-', label='Raw PPG', linewidth=1)
        self.ax_raw.set_xlabel('Sample Index')
        self.ax_raw.set_ylabel('Raw PPG Value')
        self.ax_raw.set_title('Raw PPG Signal')
        self.ax_raw.legend(loc='upper right')
        self.ax_raw.grid(True, alpha=0.3)
        
        # Middle subplot: Filtered signal
        self.line_filtered, = self.ax_filtered.plot([], [], 'r-', label='Filtered PPG', linewidth=1.5)
        self.ax_filtered.set_xlabel('Sample Index')
        self.ax_filtered.set_ylabel('Filtered PPG Value')
        self.ax_filtered.set_title('Filtered PPG Signal (Bandpass)')
        self.ax_filtered.legend(loc='upper right')
        self.ax_filtered.grid(True, alpha=0.3)
        
        # BPM text annotation on filtered plot
        self.bpm_text = self.ax_filtered.text(
            0.02, 0.95, 'BPM: --', 
            transform=self.ax_filtered.transAxes,
            fontsize=14, fontweight='bold',
            verticalalignment='top',
            bbox=dict(boxstyle='round', facecolor='wheat', alpha=0.8)
        )
        
        # Bottom subplot: Frequency spectrum
        self.line_spectrum, = self.ax_freq.plot([], [], 'g-', linewidth=1.5)
        self.ax_freq.set_xlabel('Frequency (Hz)')
        self.ax_freq.set_ylabel('Magnitude')
        self.ax_freq.set_title('Frequency Spectrum (Filtered Signal)')
        self.ax_freq.grid(True, alpha=0.3)
        self.ax_freq.set_xlim(0, 5)  # Focus on 0-5 Hz (typical HR range)
        
        plt.tight_layout()
        
        self.animation = None
    
    def set_buffers(self, raw_buffer: deque, filtered_buffer: deque):
        """Set the data buffers to plot.
        
        Args:
            raw_buffer: Deque containing raw PPG values
            filtered_buffer: Deque containing filtered PPG values
        """
        self.raw_buffer = raw_buffer
        self.filtered_buffer = filtered_buffer
    
    def set_bpm(self, bpm: float):
        """Update the current BPM value to display.
        
        Args:
            bpm: Current heart rate in beats per minute
        """
        self.current_bpm = bpm
    
    def _init_animation(self):
        """Initialize animation (called by FuncAnimation)."""
        self.line_raw.set_data([], [])
        self.line_filtered.set_data([], [])
        self.line_spectrum.set_data([], [])
        return self.line_raw, self.line_filtered, self.line_spectrum
    
    def _update_plot(self, frame):
        """Update plot data (called by FuncAnimation)."""
        if self.raw_buffer is None or self.filtered_buffer is None:
            return self.line_raw, self.line_filtered, self.line_spectrum
        
        # Get data from buffers
        raw_data = list(self.raw_buffer)
        filtered_data = list(self.filtered_buffer)
        
        n_raw = len(raw_data)
        n_filtered = len(filtered_data)
        
        # Update raw signal plot
        if n_raw > 0:
            x_raw = np.arange(n_raw)
            self.line_raw.set_data(x_raw, raw_data)
            
            # Auto-scale raw signal Y axis
            y_min = min(raw_data)
            y_max = max(raw_data)
            margin = 0.1 * (y_max - y_min) if y_max != y_min else 1.0
            self.ax_raw.set_ylim(y_min - margin, y_max + margin)
            self.ax_raw.set_xlim(0, n_raw)
        
        # Update filtered signal plot
        if n_filtered > 0:
            x_filtered = np.arange(n_filtered)
            self.line_filtered.set_data(x_filtered, filtered_data)
            
            # Auto-scale filtered signal Y axis
            y_min = min(filtered_data)
            y_max = max(filtered_data)
            margin = 0.1 * (y_max - y_min) if y_max != y_min else 1.0
            self.ax_filtered.set_ylim(y_min - margin, y_max + margin)
            self.ax_filtered.set_xlim(0, n_filtered)
        
        # Update BPM text
        if self.current_bpm > 0:
            self.bpm_text.set_text(f'BPM: {self.current_bpm:.1f}')
        else:
            self.bpm_text.set_text('BPM: --')
            
            # Compute FFT for frequency spectrum (only if we have enough samples)
            if n_filtered >= 32:  # Minimum samples for meaningful FFT
                # Apply window to reduce spectral leakage
                window = np.hanning(n_filtered)
                windowed_data = np.array(filtered_data) * window
                
                # Compute FFT
                fft_vals = np.fft.rfft(windowed_data)
                fft_mag = np.abs(fft_vals)
                fft_freqs = np.fft.rfftfreq(n_filtered, d=1.0/self.sample_rate)
                
                # Update frequency spectrum plot
                self.line_spectrum.set_data(fft_freqs, fft_mag)
                
                # Auto-scale frequency domain Y axis
                if len(fft_mag) > 0:
                    self.ax_freq.set_ylim(0, np.max(fft_mag) * 1.1)
        
        return self.line_raw, self.line_filtered, self.line_spectrum
    
    def start(self):
        """Start the live plot animation.
        
        This will block until the plot window is closed.
        """
        self.animation = FuncAnimation(
            self.fig,
            self._update_plot,
            init_func=self._init_animation,
            interval=self.update_interval_ms,
            blit=False,
            cache_frame_data=False
        )
        plt.show()


# Public API
__all__ = ["PPGPlotter"]
