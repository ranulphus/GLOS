# Chat

Chat is for IRC: channels of people talking, and private conversations. Each Chat window is on one network;
**Chat, Another Network** opens a window for another (give it a one-word name, then its settings), and its name
shows in that window's title.

## Connecting

**Chat, Settings** has the server (to begin with irc.libera.chat, with the connection locked by TLS), the
nick you want and a second one in case it is taken, an account and its password if you have one, and the
channels to join at once. **Connect** is Ctrl+K. The password is kept in GLOS's store of secrets, not in the
settings file.

If the connection is lost, Chat connects again by itself: first after five seconds, then waiting twice as long
each time, up to five minutes. It joins again the channels you were in. A server that has been silent for two
minutes is asked whether it is still there; with no answer in a minute, Chat counts the connection as lost.
**Disconnect** or /quit ends it for good.

The settings also choose whether to **hide comings and goings** (people joining and leaving), to **leave
colours out** of what people say, to **keep logs**, and whether Chat **tells you when you are named**: when a
line names you, or someone speaks to you privately, while Chat is not in front, its button in the task bar
flashes and a notification says who it was.

Logs are plain text, a file for each channel and conversation, in GLOS's folder under CHAT\LOGS and then the
network's name: #glos on Libera.Chat is `CHAT\LOGS\LIBERACH\GLOS.LOG`. Each line has its date and time. **Chat,
Search the Logs** (Ctrl+F) finds every line kept that has the words you give, in any letters, and opens the
log of the one you choose.

## Talking

The server, each channel and each private conversation are in the list at the left, with a count of what you
have not read. The channel's topic is above the talk, its people at the right.

| Keys | What they do |
|---|---|
| Enter | Say what you typed |
| Tab | Finish a nick you began typing |
| Up, Down | Lines you typed before |
| Alt+Up, Alt+Down | The conversation above, the one below |
| Ctrl+J | Join a channel |
| Ctrl+W | Leave the channel and close it |

## Commands

Typed where you talk, beginning with a stroke:

| Command | What it does |
|---|---|
| /join #channel | Join a channel |
| /part | Leave this one |
| /msg nick words | Say something to one person |
| /query nick | Open a private conversation |
| /me words | Say what you are doing |
| /nick name | Change your nick |
| /topic words | Set the channel's topic |
| /away words | Say you are away |
| /quit | Leave the server |

To say a line that begins with a stroke, begin it with two.
