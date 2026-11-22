# Web Reader

Reads web pages as text: headings, paragraphs, lists, tables and links. It is a reader, not a browser: it runs
no scripts and follows no page's own design, which is what lets an old PC read today's web. Many pages read
well this way; pages that are programs do not work.

## Going somewhere

Type in **Address** (Ctrl+L) and press Enter:

- a whole address, or a site's name;
- words to search for (the search is DuckDuckGo's plain pages);
- a file's path on this PC.

| Keys | What they do |
|---|---|
| Tab, Shift+Tab | The next link, the one before |
| Enter | Follow the link |
| Alt+Left, Alt+Right | Back, forward |
| Alt+Home | The start page |
| F5 | Fetch the page again |
| Esc | Stop fetching |
| Ctrl+O | Open a file from a disk |
| Ctrl+F | Find words in the page |
| F3, Shift+F3 | Find them again further on, or further back |
| Ctrl+D | Keep the page among your bookmarks |
| Ctrl+B | The bookmarks: a page of links, each with **take away** |
| F9 | Reader View: the page's own text alone, without its menus, lists of links and colours |

- Pictures inside a page are shown in it (PNG, JPEG, GIF and BMP), made smaller where they are wider than
  the page; those from the web come once the page is there, a moment later. A picture that can't be had is
  shown by its words in brackets. A link that leads to a picture shows it alone.
- Forms cannot be filled in yet, and files are not saved from here: use `FETCH address` at the
  [command line](COMMAND.MD) to fetch a file.
- Connections to https sites are checked as a browser checks them. The PC's clock must be right for that: see
  the Control Panel's Date and Time page.
