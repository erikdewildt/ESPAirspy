# Streaming integrity and performance

The normal input rate is 768 kS/s. Each complex sample occupies four bytes. Decimation changes outbound payload bandwidth while USB acquisition remains at its native rate.

| Output (kS/s) | Payload (bytes/s) | Payload (Mbit/s) |
|---:|---:|---:|
| 768 | 3,072,000 | 24.576 |
| 384 | 1,536,000 | 12.288 |
| 192 | 768,000 | 6.144 |
| 96 | 384,000 | 3.072 |
| 48 | 192,000 | 1.536 |
| 24 | 96,000 | 0.768 |
| 12 | 48,000 | 0.384 |

These are calculations. The 20-byte frame header, FFT, TCP/IP and Ethernet add overhead. An empty 512 KiB ring represents about 171 ms of nominal USB input. Some of that margin may already be occupied. Historical results include 767.9 kS/s observed over 130 seconds with zero counted drops. See [hardware validation](validation.md) for configuration and limitations. Sustained streaming throughput and instrumentation cost for 0.4.0 are **NOT MEASURED**. The 0.4.0 hardware check confirmed startup, receiver detection and listener readiness after OTA.

## Reading the counters

Request `GET /api/status` before and after a measurement window. Record firmware version, uptime, rate, link speed, duplex, CPU per core, minimum internal heap and free PSRAM. Counters are cumulative since boot. A reboot resets them and invalidates cross-reboot deltas. The 32-bit event counters can wrap.

| Fields | Meaning |
|---|---|
| `sample_rate`, `iq_sample_rate` | Native rate and selected client rate in complex samples per second. The client rate is zero when disconnected. Use counter deltas to measure actual rates. |
| `spy_listening`, `spy_client`, `iq_streaming` | TCP listener readiness, client connection and active I/Q acquisition for streaming. |
| `input_iq_samples`, `iq_samples_sent` | Input processed during client streaming and samples in complete I/Q frames accepted by the socket. Divide deltas by elapsed wall time. Client reads are the independent measure of delivery. |
| `usb_completions`, `usb_empty`, `usb_blocks` | USB completion and nonempty/empty observations. Fatal transfer failures log a reason on serial before reboot. Use serial logs and uptime to detect error-driven resets. |
| `dropped_blocks`, `iq_ring_lost_samples` | Rejected I/Q blocks and estimated lost complex samples at the stream-buffer boundary. They do not count RF or remote-client losses. |
| `iq_ring_capacity_bytes`, `iq_ring_peak_estimate_bytes` | Actual configured capacity and maximum occupancy estimate. It uses pre-write occupancy plus accepted bytes. Concurrent reads can make this estimate higher than the real occupancy. |
| `iq_empty_reads`, `iq_send_failures`, `iq_max_send_us` | Empty reads while streaming, failed I/Q frame sends and worst wall time for an I/Q frame send. Empty reads alone are not evidence of a sample gap. Send duration includes scheduling. |
| `spy_bytes`, `spy_connections`, `/api/spectrum-stats` | Socket bytes across all lanes, connection count, and optional spectrum queue statistics. |

Packet sequences detect repeated or missing client frames within a session. They cannot detect duplicate raw samples and cannot identify gaps on the USB bus. To establish raw continuity on real hardware, inject a stable tagged or time-varying test pattern and compare client samples with an independent reference.

## Reproducible benchmark

1. Before each run, record board/silicon, firmware and IDF revision, clock, receiver model/firmware, USB speed, Ethernet duplex, gain, DC/IQ options and FFT setting. On a dedicated device, capture serial from boot through receiver enumeration and use the dashboard to confirm native rate and initial heap.
2. Reserve the server for a single test client. Run the rate and transition tests. They set frequency and gain during the test. The rate harness records client payload and frame sequence continuity, and reports newer transport counters when available.
3. Exercise connection, intentional stop, disconnect and reconnect. Check that sample rate, gain and tuning sync match the requested settings. Repeat with SDROxide and FFT enabled, since the rate script selects I/Q only.
4. With a dedicated client, pause application reads briefly, then resume. On Linux, a standalone test client's process can be paused and resumed with SIGSTOP/SIGCONT. Increase the pause from tens to a few hundred milliseconds with the remaining test conditions fixed. The OS receive buffer may absorb short pauses. Log counter deltas, recovery and the client exit reason. If the ring actually loses samples, check that the session ends and a fresh client can receive. No HTTP mutations or port forwarding are needed.
5. Test maximum native rate and deepest decimation. Use 60-second windows for comparisons and a 30-minute or longer soak for stability. Compare the same client, configuration and network under baseline and candidate builds. Check uptime, rate, loss, send time, CPU and heap. Repeat across nominal and high FFT load.

```sh
export AIRSPY_HOST='192.0.2.25' # Replace with your receiver address.
AIRSPY_TEST_SECONDS=60 python3 tests/hardware_rates_test.py
python3 tests/hardware_stream_transitions_test.py
python3 tests/hardware_sequences_test.py
AIRSPY_TEST_STAGES=0,6 AIRSPY_TEST_SECONDS=1800 python3 tests/hardware_rates_test.py
```

Run `./tests/run-host-tests.sh` for synthetic Q,I input order, signed I,Q output, little-endian framing, gain, filter and sample-count checks. The socketpair regression requires a bit-exact unity-gain frame followed by EOF after a simulated overrun. Test inputs involving DSP use bounded numerical tolerances in the C fixtures.

An unchanged RF noise floor is compatible with fewer dropped digital samples or better transport latency. For RF A/B testing use a stable reference signal, fixed receiver gain and tuning, equal sample settings, and the same FFT window and measurement bandwidth. Compare repeated tone SNR, image rejection or decoded messages under identical conditions. Uncontrolled HF propagation is unsuitable for claiming a small firmware sensitivity improvement.
