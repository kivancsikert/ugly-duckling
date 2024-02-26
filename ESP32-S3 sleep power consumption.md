# ESP32-S3 Power consumption in sleep modes

## Modem sleep

| Frequency (MHz) | Mode Description                                         | Typ1  (mW) | Typ2  (mW) |
| --------------- | -------------------------------------------------------- | ---------- | ---------- |
| **40 MHz**      | WAITI (Dual core in idle state)                          | 19.56      | 28.08      |
|                 | Single core running 32-bit data access, other core idle  | 25.04      | 33.36      |
|                 | Dual core running 32-bit data access                     | 27.88      | 36.96      |
|                 | Single core running 128-bit data access, other core idle | 29.88      | 38.10      |
|                 | Dual core running 128-bit data access                    | 34.20      | 43.08      |
| **80 MHz**      | WAITI (Dual core in idle state)                          | 43.56      | 62.04      |
|                 | Single core running 32-bit data access, other core idle  | 53.46      | 71.94      |
|                 | Dual core running 32-bit data access                     | 61.71      | 80.52      |
|                 | Single core running 128-bit data access, other core idle | 65.67      | 83.82      |
|                 | Dual core running 128-bit data access                    | 75.90      | 95.04      |
| **160 MHz**     | WAITI (Dual core in idle state)                          | 91.08      | 139.53     |
|                 | Single core running 32-bit data access, other core idle  | 107.64     | 156.18     |
|                 | Dual core running 32-bit data access                     | 131.13     | 179.79     |
|                 | Single core running 128-bit data access, other core idle | 137.73     | 186.48     |
|                 | Dual core running 128-bit data access                    | 164.94     | 213.84     |
| **240 MHz**     | WAITI (Dual core in idle state)                          | 108.57     | 157.08     |
|                 | Single core running 32-bit data access, other core idle  | 132.36     | 180.98     |
|                 | Dual core running 32-bit data access                     | 168.86     | 217.59     |
|                 | Single core running 128-bit data access, other core idle | 179.52     | 228.39     |
|                 | Dual core running 128-bit data access                    | 221.49     | 270.69     |
