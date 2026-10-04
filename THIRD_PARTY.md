# Third-party code in GLOS

GLOS itself is MIT-licensed (PRD D10). Code from elsewhere lives under
`third_party/`, unchanged except where noted, with its own licence beside it.

| Directory | What | Version and source | Licence |
|---|---|---|---|
| `third_party/lwip` | lwIP TCP/IP stack: the IPv4 core (ARP, ICMP, UDP, TCP, DHCP), Ethernet and the headers | 2.2.0, `lwip-2.2.0.zip` from download.savannah.nongnu.org, sha256 `e12c769be5a1da9a1edf1b8f38d645c6c87d52a26a636172cea4b8c63ec04994`, signature by Simon Goldschmidt (key 746BEB8E56372EB695EA02E71D6B61C1B84299F5) checked | BSD-3-Clause (`third_party/lwip/COPYING`) |
| `third_party/tinyssh` | TinySSH's crypto for the SSH server: X25519, Ed25519 (with its field and group arithmetic), SHA-256, SHA-512, ChaCha20, Poly1305, constant-time compare, and D. J. Bernstein's `cryptoint` headers. Not sntrup761, and none of TinySSH's server. `has*.h` are GLOS's: no external libraries | 20260906, `github.com/janmojzis/tinyssh/archive/refs/tags/20260906.tar.gz`, sha256 `54c143281e3a7430e9db80847c3242bbd6bf859ceafb5a18562bc4ecbbb2806d`, signature by Jan Mojžíš (key D008B0C23D8479E46B9FCB9045DA517496939FF9) checked | CC0-1.0 OR 0BSD OR MIT-0 OR MIT (`third_party/tinyssh/LICENSE.md`) |

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

DJGPP's library sources (`djlsr205.zip`) were read as a behavioural reference for its exception and signal
paths (M4b); none of it is in GLOS.
