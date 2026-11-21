# Chat

Chat is for IRC: channels of people talking, and private conversations, on one server at a time.

## Connecting

**Chat, Settings** has the server (to begin with irc.libera.chat, with the connection locked by TLS), the
nick you want and a second one in case it is taken, an account and its password if you have one, and the
channels to join at once. **Connect** is Ctrl+K. The password is kept in GLOS's store of secrets, not in the
settings file.

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
