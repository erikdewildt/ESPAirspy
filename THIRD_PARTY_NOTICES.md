# Third-party notices

| Component | Origin | License or terms |
|---|---|---|
| Frequency-domain I/Q balancer | [libairspyhf](https://github.com/airspy/airspyhf), `iqbalancer.c` and associated definitions | Airspy BSD-style terms below. The exact revision used for the port is unverified. |
| Ethernet example component | ESP-IDF Ethernet examples | Unlicense OR CC0-1.0. See [the retained upstream notice](components/ethernet_init/UPSTREAM-LICENSE). |
| SDK and SDK patches | [ESP-IDF v5.5.4](https://github.com/espressif/esp-idf/tree/v5.5.4) and the PSRAM clock backport from v5.5.6 | ESP-IDF's [distribution notices](https://github.com/espressif/esp-idf/blob/v5.5.4/LICENSE) and per-component files apply. Review enabled components before distributing linked binaries. |
| Airspy HF+ firmware | [Airspy](https://airspy.com/airspy-hf-discovery/) | User obtains the receiver image directly from Airspy. This project does not bundle it. |
| SDROxide harnesses | [SDROxide](https://github.com/dividebysandwich/sdroxide) | Rust files in `tests/` are standalone hardware harnesses for the external client, not linked into the ESP firmware. |

## libairspyhf I/Q balancer

Copyright (c) 2016-2023, Youssef Touil <youssef@airspy.com>  
Copyright (c) 2018, Leif Asbrink <leif@sm5bsz.com>

All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

- Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
- Neither the name of Airspy HF+ nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
