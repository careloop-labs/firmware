import numpy as np
from scipy import signal

fs = 100.0
sos_hp = signal.butter(4, 0.40, btype='highpass', fs=fs, output='sos')  # 2 rows
sos_lp = signal.butter(2, 7.0,  btype='lowpass',  fs=fs, output='sos')  # 1 row

sos = np.vstack([sos_hp, sos_lp])  # 3x6: [b0,b1,b2,a0,a1,a2]
sos[:, :3] /= sos[:, 3:4]          # normalize a0->1 (usually already 1)
sos[:, 4:] /= sos[:, 3:4]
sos[:, 3] = 1.0
# For our C++: take [b0,b1,b2,a1,a2]
print(np.array2string(sos[:, [0,1,2,4,5]], precision=9, separator=', '))