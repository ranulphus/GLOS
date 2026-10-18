# Third-party code in GLOS

GLOS itself is MIT-licensed (PRD D10). Code from elsewhere lives under
`third_party/`, unchanged except where noted, with its own licence beside it.

| Directory | What | Version and source | Licence |
|---|---|---|---|
| `third_party/lwip` | lwIP TCP/IP stack: the IPv4 core (ARP, ICMP, IGMP, UDP, TCP, DHCP), Ethernet, the mDNS responder (`src/apps/mdns`, added in M5 from the same archive, its hash checked again), the SNTP client (`src/apps/sntp`, added in M10 the same way; the DNS client in `src/core/dns.c`, always vendored, is now turned on) and the headers | 2.2.0, `lwip-2.2.0.zip` from download.savannah.nongnu.org, sha256 `e12c769be5a1da9a1edf1b8f38d645c6c87d52a26a636172cea4b8c63ec04994`, signature by Simon Goldschmidt (key 746BEB8E56372EB695EA02E71D6B61C1B84299F5) checked | BSD-3-Clause (`third_party/lwip/COPYING`) |
| `third_party/tinyssh` | TinySSH's crypto for the SSH server: X25519, Ed25519 (with its field and group arithmetic), SHA-256, SHA-512, ChaCha20, Poly1305, constant-time compare, and D. J. Bernstein's `cryptoint` headers. Not sntrup761, and none of TinySSH's server. `has*.h` are GLOS's: no external libraries | 20260906, `github.com/janmojzis/tinyssh/archive/refs/tags/20260906.tar.gz`, sha256 `54c143281e3a7430e9db80847c3242bbd6bf859ceafb5a18562bc4ecbbb2806d`, signature by Jan Mojžíš (key D008B0C23D8479E46B9FCB9045DA517496939FF9) checked | CC0-1.0 OR 0BSD OR MIT-0 OR MIT (`third_party/tinyssh/LICENSE.md`) |
| `third_party/stb` | `stb_image.h`, the image viewer's decoder (M9; ring 3, as PRD D19 wants for parsers): PNG, JPEG, BMP, GIF, TGA, PNM. Unchanged | v2.30, `github.com/nothings/stb` commit `2c980bb59875b0d32144a71867fbdebb2f77cd20`, sha256 `594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3` | MIT OR public domain (Unlicense), at the file's end (`third_party/stb/LICENSE`) |
| `third_party/bearssl` | BearSSL, the TLS 1.2 client for apps (M10 step C; ring 3, D19): `inc/`, `src/` (with the T0 sources of its generated handshake and X.509 code), unchanged. Not its tools, samples, tests or the prebuilt `T0Comp.exe`. Built for DJGPP without SSE2, AES-NI, RDRAND, `/dev/urandom` or the system clock (Makefile `BEARSSL_FLAGS`); for the host as it is (`tools/certs/mkcerts`) | 0.6, `bearssl-0.6.tar.gz` from www.bearssl.org (no signature is published), sha256 `6705bba1714961b41a728dfc5debbe348d2966c117649392f8c8139efc83ff14`, 765,094 bytes, the same as FreeBSD's port has pinned since 2018 (`security/bearssl/distinfo`) | MIT (`third_party/bearssl/LICENSE.txt`) |
| Not in the tree: `C:\GLOS\CERTS.DAT` | HTTPS's trust anchors, made by `tools/certs/mkcerts` from curl's extract of Mozilla's root store (PRD D59): downloaded and checked at build time (`tools/setup/versions.mk`), not committed | `cacert-2026-09-25.pem` from curl.se/ca (121 roots), sha256 `a41b5d356aea97a529fe27e0f7316d2f9d946d75927476cf9cf1b90637d00505` (curl's own `.sha256` agrees) | MPL-2.0 (the data, as Mozilla's `certdata.txt`) |
| `kernel/gfx/fonts` | The 2D library's fonts (M6): 1-bit bitmaps of DejaVu Sans, Sans Bold and Sans Mono at 11 pixels, made by `tools/mkfont.py` (each file names its source's SHA-256). Provisional: the look's own fonts are chosen in M6's step G | DejaVu fonts 2.37 (`fonts-dejavu-core` 2.37-8 as Ubuntu packages it; https://dejavu-fonts.github.io/) | Bitstream Vera Fonts licence, DejaVu's changes in the public domain (`kernel/gfx/fonts/LICENSE`): free to use, copy, merge and distribute with the notice kept |

TinySSH signs Ed25519 "hedged" (random bytes mixed into the nonce); `tests/host/crypto_kat.c` checks
RFC 8032's signatures by verification for that reason.

GLOS's port of lwIP is its own code: `kernel/net/port/` (the options and
`arch/` headers) and `kernel/net/net.c` (the interface and the net thread).

## Behavioural references (no code taken)

CWSDPMI, HDPMI and DJGPP's runtime are used as behavioural baselines only
(PRD D26, docs/supervisor.md §1).

## Test inputs fetched at build time (not in the repository)

| Input | Use | Pin | Licence |
|---|---|---|---|
| HX DOS extender runtime 2.23 (`HXRT223.zip`) | HDPMI32i as a DPMI baseline in Loop A | `tools/setup/versions.mk` | Freeware ("may be used for any purpose") |
| DJGPP 2.05 test suite (`djtst205.zip`) | Its tests, compiled by `make djtst` and compared under CWSDPMI and GLOS (M4b) | `tools/setup/versions.mk` | DJGPP's licence (`COPYING.DJ`) |
| HDPMI's regression tests (HX repository, `Src/HDPMI/Regression/Regression.zip`) | Run under HDPMI32i and GLOS and compared (`make loopa-hdpmireg`, M4c) | `tools/setup/versions.mk` (commit f2276db9) | HX: freeware ("Copyright 1996-2026 Japheth", HXsrc.txt) |
| ecm's test builds (`pushbx.org/ecm/test/20210127.zip`: lDebugX, dpmimini) | lDebugX steps dpmimini into protected mode under HDPMI32i and GLOS (`make loopa-ecm`, M4c) | `tools/setup/versions.mk` | lDebug: Fair License (ecm) |

DJGPP's library sources (`djlsr205.zip`) were read as a behavioural reference for its exception and signal
paths (M4b); none of it is in GLOS.
