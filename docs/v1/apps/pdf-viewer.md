# PDF Viewer: plan (GLOS v1, phase A)

**New in v1.** Reading PDFs on a Pentium-class PC: manuals, datasheets, papers, books. The owner's choice is GLOS's
own renderer, scoped: the PDFs people read most drawn properly, the exotic ones best effort.

## What it does

- **Reading:**
  - pages one at a time or continuous, fit width, fit page, zoom;
  - two pages side by side (books);
  - the outline (bookmarks) in a pane, thumbnails in another;
  - go to a page; links inside the document followed (and web links offered to the Web Reader).
- **Text:**
  - select and copy;
  - search with every hit highlighted and listed;
  - the document's text as plain text (export).
- **Drawing:**
  - **Text:** the standard 14 fonts (replaced by metric-compatible free fonts: the Liberation or URW families,
    permissive); embedded TrueType fonts (stb_truetype); embedded Type 1 and CFF fonts (GLOS's own Type 1/CFF
    reader, since stb_truetype reads TrueType only: the hardest part, see risks).
  - **Paths:** lines, curves, fills (even-odd, nonzero), strokes with widths, joins, caps, dashes, clipping.
  - **Images:** JPEG (DCT, stb_image), Flate (miniz), raw, with their colour spaces (gray, RGB, CMYK roughly, indexed).
  - **Anti-aliased** at 16 and 32 bits, plain at 8.
- **Best effort, not promised:** Type 3 fonts, shadings, transparency groups and soft masks (drawn without the
  effect), forms (shown, not filled), encrypted PDFs (the standard RC4 and AES-128 handlers with an empty password,
  which most "protected" PDFs use; others refused).
- **Speed:**
  - pages drawn in the background, the visible one first, the next ones after;
  - a cache of drawn pages;
  - big documents opened without reading them whole (the cross-reference table, then pages as needed).
- **Printing:** not planned for v1 (see Notepad's open point on printers).

## From toolkit 2

- **The view:** a canvas with scrolling, zoom and selection.
- **Panes:** the outline (a tree) and thumbnails (an icon view that loads as it goes), splitters.
- **Basics:** search with a results list; the toolbar.
- **A vector renderer:** the 2D library's own, or the PDF Viewer's (settled in C; the toolkit's antialiased fonts
  may share it).

## From API 2.0

- **THREAD:** parsing and drawing pages off the window's thread.
- **FS:** long file names; reading a file in pieces (random access) without loading it whole.
- **CLIP:** text and images.
- **SHELL:** links to the Web Reader ("open with").

## Libraries

- **Parsing:** a permissive PDF parser. PDFio (Apache-2.0) is the first candidate (its objects, streams, xref and
  encryption), or GLOS's own if PDFio proves too big or too slow for DJGPP.
- **Fonts and images:** stb_truetype (public domain / MIT) for TrueType; stb_image for JPEG; miniz for Flate.
- **Free fonts:** for the standard 14 (Liberation, SIL OFL; or URW base 35 under its permissive licence, to be
  checked).
- **Not used:** MuPDF (AGPL), Poppler and xpdf (GPL): not even as reference code.

## CPU floor

- **A Pentium.** Simple pages on a Pentium 100.
- **A Pentium II** for comfortable reading of complex ones.

It refuses no CPU: on a 486 it works, slowly.

## Risks

- **Type 1 and CFF fonts:** many PDFs embed these; a reader and rasteriser for them is real work (hinting is
  skipped; outlines are enough at screen sizes).
- **Odd PDFs:** broken files are common; the parser repairs a broken cross-reference by scanning, as readers do.
- **Size and speed:** a page of a big scanned book is a big JPEG decoded each time it's drawn; the cache matters.

## Tests

- **Host:**
  - a corpus of PDFs (public-domain manuals, papers, generated test files for each feature), each page drawn and
    compared with a reference drawn by a reference renderer on the host (Poppler's `pdftoppm`, run as a separate tool
    to make references, never linked), with a tolerance;
  - text extraction compared;
  - the parser on broken files.
- **Loop A smoke:** a three-page PDF opened and its first page shown; search finds a word.
- **The P4:** a 300-page manual: page turn times.
