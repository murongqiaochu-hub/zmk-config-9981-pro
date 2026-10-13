# P9981 A320 diagnostic capture

This temporary build observes the existing A320 motion-read path. It does not
enable tap-to-click and does not interpret GPIO or an idle log gap as finger
release.

## Firmware fields

- `DIAG gpio`: one-second heartbeat and GPIO state transitions; `gpio_err` is
  zero for a valid level and contains the negative GPIO error otherwise.
  `raw=none` means no I2C sample is associated with this heartbeat.
- `DIAG sample ... read=ok`: one existing successful seven-byte read, the
  monotonic firmware uptime at read start, raw bytes, signed unscaled `dx`/`dy`,
  scroll state, and the measured I2C call duration.
- `DIAG sample ... read=error`: the same attempt sequence and uptime, failing
  transaction phase and return code; `raw=none` avoids inventing a zero sample.

Each attempted read has a sequence number. The collector also reads the
heartbeat's `last_seq` so a lost final log can be noticed. A sequence gap means
an attempted-read diagnostic record was not captured; it is not a sensor
sample and does not identify whether Zephyr logging or USB transport dropped
the line.

The read schedule is unchanged: only the existing active-low motion GPIO path
reads I2C address `0x57`, writes register pointer `0x0A`, and reads seven bytes
from `0x0A`. No high-GPIO reads, unknown registers, or sensor configuration
writes are added. Logging is deferred through Zephyr's logger and the poll
worker never waits for a serial consumer. At the 10 ms fallback poll interval,
the upper bound is 100 read records per second while motion GPIO remains low;
a three-round capture is bounded to about six minutes plus setup and transport
probing. Logger/USB internal queue latency is not directly measured; the
firmware records I2C duration and the host records completed-line receive
timestamps and read-call duration.

## USB and collector

The board maps both `zephyr,console` and `zmk,studio-rpc-uart` to
`cdc_acm_uart`. The temporary diagnostic config disables ZMK Studio and enables
USB logging, leaving the CDC path for diagnostics. The rollback build keeps
Studio enabled. The collector filters USB VID/PID/product, probes matching CDC
ports for a `DIAG gpio` or `DIAG sample` record, and proceeds only if exactly
one endpoint matches. It sends zero data bytes and never sends Studio RPCs. It
records only `DIAG` firmware lines, exact raw line bytes (base64), Mac UTC and
monotonic receive time, separate firmware uptime, stage markers, and sequence
gaps. Host and firmware clocks are not synchronized. Host receive time is
when a complete line is returned; Zephyr queue and USB buffering are not
independently measurable in this setup.

Install the pinned collector dependency and run from the repository root only
after the diagnostic firmware is active and Studio is closed:

```sh
python3 -m pip install -r scripts/requirements-p9981-capture.txt
python3 scripts/p9981_capture.py --diagnostic-fw-active --rounds 3
```

The script closes the port on completion or interruption, refuses to overwrite
an existing output file, and stops if the endpoint is absent or ambiguous.
The default output directory is `captures/` and is git-ignored. The script is
prepared but must be validated against the actual diagnostic firmware before
it can be described as hardware-tested.
