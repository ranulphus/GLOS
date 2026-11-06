# Chat: plan (GLOS v1, phase A)

**New in v1.** An IRC client: live chat with today's networks (Libera.Chat, OFTC, the retro-computing channels) from
a 486, over TLS.

## What it does

- **Networks and servers:** a list of networks with their servers, nick, alternative nicks, real name, and
  authentication (SASL PLAIN with the password in SECRETS; NickServ as a fallback); connect on start (a setting).
- **TLS:** BearSSL (as Fetch and PKG), certificates checked against GLOS's roots once the clock is set; plain
  connections allowed where a network has no TLS.
- **Channels and private messages:**
  - a tab for each, under its network in a tree;
  - topics, user lists with modes, joins and parts shown quietly (a setting);
  - unread counts and highlights on the tree.
- **IRCv3 where the server has it:** server-time, message tags, away-notify, account-notify; chat history playback
  where offered.
- **Messages:**
  - colours and formatting (mIRC's codes) shown, and stripped by a setting;
  - URLs to click (the Web Reader);
  - nick completion with Tab;
  - commands (/join, /msg, /me, /topic, /nick, /quit and the rest).
- **Logs:** each channel's to a file (a setting), with a search over them.
- **Notifications:** for highlights and private messages when the window isn't in front (SHELL); the task bar's
  button flashes.
- **DCC file transfer:** not planned (security and NAT); maybe later.

## From toolkit 2

- **Panes:** the network and channel tree with unread badges, splitters.
- **The chat view:** a scrolling, coloured, wrapped text list that grows at the bottom; the input field with
  completion and history.
- **The user list.**

## From API 2.0

- **NET:** connections, DNS, TLS's randomness and time (today's NET, RANDOM, TIME, kept in 2.0).
- **SECRETS:** passwords for SASL and NickServ.
- **SHELL:** notifications; the task bar button's flash; the tray icon (optional: Chat in the background).
- **FS:** long file names for the logs.
- **THREAD:** connections and TLS off the window's thread.

## Libraries

- **TLS:** BearSSL (MIT), as GLOS's HTTPS client already uses (M10).
- **IRC:** GLOS's own protocol code.

## CPU floor

A 486DX2: a TLS handshake there takes about a second, as SSH's.

## Tests

- **Host:** the protocol parser and the formatting codes against recorded sessions; GUI scripts.
- **Loop A smoke:** a connection to a small IRC server on the host (an ircd run in the test, reached through
  SLiRP's forwarding) with TLS; join, send, receive.
- **The P4:** Libera.Chat for real, at the owner's say-so (no automated bots on public networks).
