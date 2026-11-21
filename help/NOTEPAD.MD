# Notepad

Notepad is for text files: notes, batch files, settings files, programs' sources. Up to twelve files can be
open at once, each in a tab.

## Files

- **New** (Ctrl+N), **Open** (Ctrl+O), **Save** (Ctrl+S), **Save As**, **Close** the tab (Ctrl+W).
- Ctrl+Tab goes round the tabs. A star before a file's name in the title means it has changes not yet saved.
- A name saved with no ending gets .TXT.
- **View, Keep Session** opens the same files again the next time Notepad starts.

## Editing

| Keys | What they do |
|---|---|
| Ctrl+Z | Undo |
| Ctrl+X, Ctrl+C, Ctrl+V | Cut, copy, paste |
| Ctrl+A | Choose everything |
| Ctrl+F, F3 | Find, and find the next |
| Ctrl+H | Replace |
| Ctrl+Shift+F | Find in the files of a folder |
| Ctrl+G | Go to a line |
| Ctrl+F2, F2, Shift+F2 | Mark a line, go to the next mark, and to the one before |

Find and Replace can match capitals, whole words, and regular expressions, and can replace every place at
once. Find in Files lists the lines found in a pane under the text; choose one to go there.

## How the text looks

- **Format, Word Wrap** breaks long lines at the window's edge.
- **View, Line Numbers** shows them at the left.
- **View, Highlighting** colours C, batch files, INI files, assembly, Pascal, HTML and Markdown, by the file's
  name or as you choose under Highlight As.
- The bar at the bottom shows the line and column, and how the file is written.

## Letters beyond English

**Format, Encoding** says how the file's bytes are letters: UTF-8, or one of DOS's code pages (437, 850, 852,
866, 737). A file written by an old DOS program in another language will want its code page.
