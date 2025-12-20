import sys
from collections import deque
import threading

try:
    import serial
except ImportError:
    print("Missing dependency 'pyserial' (pip install pyserial)", file=sys.stderr)
    sys.exit(1)

# Import the minimal serial helpers
from ppg_serial import find_port, get_raw_ppg_value
from hr_filter import hr_filter_init, hr_filter_process
from ppg_plot import PPGPlotter
from ppg_peak_detector import PeakDetector

# Global configuration
BAUDRATE = 115200
BUFFER_SIZE = 200
SAMPLE_RATE = 100  # Hz (approximate, adjust based on your device)

# Global deque to store raw PPG values
raw_ppg_buffer = deque(maxlen=BUFFER_SIZE)


def store_ppg_value(raw_ppg_value: float) -> None:
    """Store a raw PPG value in the rolling buffer.
    
    Args:
        raw_ppg_value: The raw PPG value to store.
    """
    raw_ppg_buffer.append(raw_ppg_value)

def open_serial_port():
    """Open the serial port using auto-detected port and global BAUDRATE.
    
    Returns:
        serial.Serial: Opened serial port object, or None if failed.
    """

    port = find_port()
    if not port:
        print("No serial port found. Provide device or set PORT environment.", file=sys.stderr)
        return None

    try:
        ser = serial.Serial(port, BAUDRATE, timeout=1)
        print(f"Opened {port} @ {BAUDRATE}", file=sys.stderr)
        return ser
    except Exception as e:
        print(f"Failed to open serial port {port}: {e}", file=sys.stderr)
        return None


def read_serial(ser, raw_buffer, filtered_buffer, stop_flag):
    """Background thread function to read serial data and update buffers.
    
    Args:
        ser: Serial port object
        raw_buffer: Deque to store raw PPG values
        filtered_buffer: Deque to store filtered PPG values
        stop_flag: Threading event to signal when to stop
    """
    try:
        while not stop_flag.is_set():
            line = ser.readline()
            if not line:
                continue
            s = line.decode(errors="ignore").strip()
            if not s:
                continue
            raw_ppg_value = get_raw_ppg_value(s)
            if raw_ppg_value is not None:
                # Store raw value
                raw_buffer.append(raw_ppg_value)
                
                # Filter and store filtered value
                filtered_value = hr_filter_process(raw_ppg_value)
                filtered_buffer.append(filtered_value)
                
                # Optional: print values for debugging
                # print(f"Raw: {raw_ppg_value:.1f}, Filtered: {filtered_value:.1f}")
    except Exception as e:
        print(f"Serial read error: {e}", file=sys.stderr)
    finally:
        stop_flag.set()



def main() -> int:
    # Initialize the HR filter
    hr_filter_init()
    
    # Initialize peak detector
    peak_detector = PeakDetector(sample_rate=SAMPLE_RATE)
    
    # Buffer for signals
    raw_ppg_buffer = deque(maxlen=BUFFER_SIZE)
    filtered_ppg_buffer = deque(maxlen=BUFFER_SIZE)
    
    # Setup the plotter
    plotter = PPGPlotter(buffer_size=BUFFER_SIZE, sample_rate=SAMPLE_RATE)
    plotter.set_buffers(raw_ppg_buffer, filtered_ppg_buffer)
    
    # Scan serial port and get ser object to act on it
    ser = open_serial_port()
    if not ser:
        return 1
    
    # Start serial reading in a background thread
    stop_flag = threading.Event()
    
    def read_and_process():
        """Background thread to read serial and process."""
        while not stop_flag.is_set():
            line = ser.readline()
            if not line:
                continue
            s = line.decode(errors="ignore").strip()
            if not s:
                continue
            raw_ppg_value = get_raw_ppg_value(s)
            if raw_ppg_value is not None:
                # Store raw value
                raw_ppg_buffer.append(raw_ppg_value)
                
                # Filter and store filtered value
                filtered_value = hr_filter_process(raw_ppg_value)
                filtered_ppg_buffer.append(filtered_value)
                
                # Detect peaks and calculate BPM
                bpm = peak_detector.process(filtered_value)
                if bpm is not None:
                    print(f"Peak detected! BPM: {bpm:.1f}")
                    plotter.set_bpm(bpm)
                else:
                    # Update with current BPM even if no new peak
                    plotter.set_bpm(peak_detector.get_current_bpm())
    
    reader_thread = threading.Thread(target=read_and_process, daemon=True)
    reader_thread.start()
    
    try:
        # Start the live plot (blocks until window is closed)
        plotter.start()
    except KeyboardInterrupt:
        print("Interrupted, exiting...", file=sys.stderr)
    finally:
        stop_flag.set()
        ser.close()
        reader_thread.join(timeout=1)
    
    return 0


if __name__ == "__main__":
    sys.exit(main())