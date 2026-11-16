# CsvTC

Total Commander lister plugin (WLX) for viewing and editing CSV files.

- [Deutsch](README.de.md)
- [English](README.en.md)

## Overview

**CsvTC** shows CSV files as a table in Total Commander's lister (F3) and in
Quick View (Ctrl+Q), and lets you edit them right there. It is native C++ with
no dependencies besides the Windows system libraries, for 32 and 64 bit.

- Detects the separator automatically (`,` `;` Tab `|`); **S** switches it
- Uses the colors and dark mode of the Total Commander main window; **T**
  switches to the Markdown reader's color scheme
- Edit fields (**E**), insert rows (**i i**), delete rows (**d d**) and columns
  (**Shift+D Shift+D**), undo, save with **Ctrl+S** or when closing with **Esc**
- Saves byte-exact: unchanged rows keep their quotes, line endings, encoding
  and BOM
- English or German user interface, following Total Commander's language
  setting

See the full [Deutsch](README.de.md) / [English](README.en.md) docs for the
keyboard reference, installation, settings and build instructions.
