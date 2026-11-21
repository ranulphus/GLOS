# Terminal

The Terminal is a window on GLOS's [command line](COMMAND.MD), and on other computers. Up to eight tabs, each
with its own command line.

| Keys | What they do |
|---|---|
| Ctrl+Shift+T | A new tab |
| Ctrl+Shift+W | Close the tab |
| Ctrl+Shift+C | Copy what the mouse has chosen |
| Shift+Ins, Ctrl+Shift+V | Paste |
| Shift+PgUp, Shift+PgDn | Back and forward through the lines that have scrolled away |
| Ctrl+Shift+F | Find in the tab's lines |
| Ctrl+Shift+G, Ctrl+Shift+H | Find the next, and the one before |
| Ctrl+Shift+O | Saved sessions |

Typing EXIT closes a tab; closing the last tab closes the Terminal. Other keys, F1 among them, go to the
program running in the tab.

## Programs in a tab

A DOS program that only prints text runs in the tab. One that wants the whole screen takes it until it ends:
see [DOS programs and games](DOSPROGS.MD).

## Other computers: saved sessions

**File, Sessions** (Ctrl+Shift+O) keeps the computers you connect to, each under a name, and opens one in a
new tab. A session is one of:

- **SSH**: a computer's name or address, the user, and if you like a key file, a command to run there, and
  tunnels (a port of this PC that leads to a port over there).
- **Telnet** and **Raw** (TCP alone): a host and a port.
- **Serial**: a COM port (COM1 to COM4) with its speed, data bits, parity, stop bits and flow control.

Each session can have its own colours (black on white, white on black, green or amber screen, blue), text
size, number of lines kept, the code page of the far end's letters, and a file its output is logged to.
Passwords are never kept.

At a prompt, `ssh name` opens a saved session too, and `ssh user@computer` connects without one. Ctrl+] ends
a Telnet or serial session. More in [The network](NETWORK.MD).
