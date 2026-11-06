# Web Reader: plan (GLOS v1, phase A)

**New in v1.** Reading the web and Gemini from a 486: pages with their text, links, images and simple layout,
without JavaScript, over HTTPS. Not a modern browser. A reader that makes today's documentation, articles, wikis
and the retro-computing web usable.

## What it does

- **Protocols:** HTTPS and HTTP (GLOS's client, M10), Gemini (TLS, its own simple format), Gopher.
- **HTML:**
  - a subset: headings, paragraphs, lists, links, images, tables (simple ones), preformatted text, block quotes,
    forms (text fields, buttons, check boxes, radio buttons, selects; GET and POST);
  - CSS: colours, fonts' sizes and weights, margins and padding, display (block, inline, none), width; enough for
    readable pages, not layout engines' full models;
  - character sets: UTF-8 and the Latin ones, shown in GLOS's fonts.
- **A reader view:** a page's main text extracted (headings, paragraphs, images) and shown cleanly, for the many
  pages whose layout needs more than the subset.
- **Images:** JPEG, PNG, GIF (first frame) through stb_image, loaded after the text, scaled to fit.
- **Navigation:**
  - tabs; back and forward; history; bookmarks (folders);
  - the address bar with search (a search engine that works without JavaScript, chosen by the user);
  - find in page.
- **Downloads:** to a folder, with progress, in the background (Fetch's code).
- **Cookies:** for logins on simple sites, kept per site, cleared from the menu.
- **Safety:**
  - certificates checked (GLOS's roots);
  - no scripts, no plugins;
  - pages can't run anything.

## From toolkit 2

- **The page view:** a rich-text view widget (styled runs, wrapped, with images and links inline, tables as simple
  grids), also useful to Help.
- **Forms:** made of the toolkit's fields.
- **Basics:** tabs, the address bar with completion, bookmarks (a tree), the download list.

## From API 2.0

- **NET:** connections, DNS, TLS's randomness and time.
- **THREAD:** fetching and parsing off the window's thread, images loaded as they come.
- **FS:** long file names for downloads; the cache's folder.
- **CLIP:** text, images, links.
- **SHELL:** "open with" for downloaded files; the default app for links (from Chat, the PDF Viewer).
- **SECRETS:** saved site passwords (optional).

## Libraries

- **TLS:** BearSSL (MIT).
- **Images:** stb_image.
- **HTML and CSS:** GLOS's own parser (an HTML5-style tokenizer, tolerant of broken pages) and its own layout of the
  subset.
- **Decompression:** miniz (gzip, deflate).

## CPU floor

- **A 486DX2:** text pages and Gemini.
- **A Pentium:** pages with many images. The TLS handshake there takes about a second.

## Tests

- **Host:** the HTML tokenizer and parser on a corpus (html5lib's test data, which is MIT); layout on test pages
  compared with stored results; Gemini and Gopher parsers; GUI scripts of tabs, bookmarks and the reader view.
- **Loop A smoke:** pages served by a small web server on the host (through SLiRP) with TLS: text, an image, a form
  posted, a download.
- **The P4:** real sites at the owner's say-so.

## Open points

- **Which search engine** to offer by default (one that works without JavaScript and allows it).
