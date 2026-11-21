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

- Pictures inside a page are shown by their words in brackets; a link that leads to a picture shows it.
- Forms cannot be filled in yet, and files are not saved from here: use `FETCH address` at the
  [command line](COMMAND.MD) to fetch a file.
- Connections to https sites are checked as a browser checks them. The PC's clock must be right for that: see
  the Control Panel's Date and Time page.
