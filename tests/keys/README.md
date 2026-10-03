# Test keys (public: never use them anywhere else)

Fixed ed25519 keys for GLOS's tests (PRD D29: Loop A carries a known test key).

- `hostkey`, `hostkey.pub`: the SSH host key a test GLOS uses (`KEYS\HOSTKEY`).
- `client`, `client.pub`: the key tests log in with; `AUTHKEYS` lists it (`KEYS\AUTHKEYS`).

Made with `ssh-keygen -t ed25519 -N ""`. Anyone can read them here, so a GLOS
using them is open to anyone who can reach it: test machines only.
