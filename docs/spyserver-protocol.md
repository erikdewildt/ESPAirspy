# SpyServer interoperability

ESPAirspy is a compatible receiver endpoint for a subset of SpyServer 2.x. It listens on TCP 5555 and serves one client at a time. [SDROxide](https://github.com/dividebysandwich/sdroxide) 1.7.2 was exercised in the [historical hardware validation](validation.md). Other clients and versions are unverified unless called out in that record.

The server accepts HELLO, settings and ping. It returns device information, client sync, pong, signed 16-bit little-endian interleaved I,Q, and optional unsigned 8-bit spectrum data. Control, I/Q and FFT sequences are independent. Input is nominally 768 kS/s. The advertised seven stages divide the native rate by powers of two. The server supports gain and frequency controls, plus independent I/Q and FFT centers within the practical captured bandwidth. The default FFT span is derived from the native 768 kS/s profile. Alternate USB sample profiles and out-of-band VFO tuning have limited validation.

Commands have 8-byte headers and a body limit of 4096 bytes. Responses have 20-byte headers. Settings validated by the implementation include I/Q format, stream enable, decimation, digital gain, and FFT display parameters when built with FFT. See [parser and serialization](../main/spy_protocol.c) and [supported commands](../main/spy_server.c). These notes document the implementation and observed compatibility. They are not an official SpyServer specification.

A stalled client can fill the producer buffer. The server closes the affected session when it observes an input overrun. Clients should reconnect and reapply settings. An incrementing packet sequence does not independently verify USB sample continuity. The data and control protocol uses plain TCP and has no authentication.
