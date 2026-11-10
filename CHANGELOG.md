# Changelog

All notable changes are also published on the
[GitHub releases](https://github.com/wyllianbs/SE/releases) page.

## 0.7.6 — 2026-11-10

- Python can now run a **secondary language server** alongside the main one
  (Settings ▸ Languages ▸ Python ▸ Secondary language server). The suggested
  pairing is **Pyright** as the main server (types, hover, completion) and
  **pylsp** as the secondary (lint and formatting, which Pyright does not do).
  Diagnostics and completions from both servers are merged; hover, go-to-
  definition and formatting use the main server and fall back to the secondary
  when it does not provide them. The status bar shows both (e.g. `pyright + pylsp`).
- The **Settings (JSON)** box now shows a real example for the selected server
  (gopls, pylsp, Pyright, Ruff, rust-analyzer, and others) instead of a single
  fixed sample, so each language suggests the options that actually apply to it.

## 0.7.5 — 2026-11-08

- New **View ▸ Inlay Hints** toggle (off by default): shows the language
  server's inline type and parameter-name hints (for example, gopls with
  `ui.inlayhint.hints` enabled shows `x int := …` and `fn(name: value)`). The
  hints are drawn in reserved inline space, so they never overlap the code and
  never change the file's text.
- The inlay-hint interface and the recent LSP options (Settings JSON, document
  formatting, auto-format on save) are now fully translated to Portuguese
  (pt_BR), and the Help page documents them.

## 0.7.4 — 2026-11-08

- Each language server can now take a **Settings (JSON)** configuration
  (Settings ▸ Languages ▸ Language server). Paste the server's own options in the
  editor-agnostic JSON form — for Go, for example, a `{"gopls": {…}}` object that
  turns on `gofumpt`, `staticcheck`, inlay hints and the analyses — and the editor
  passes them to the server (via `workspace/configuration` and
  `workspace/didChangeConfiguration`), so its features light up. The box reports
  whether the JSON is valid and stays editable even with the server turned off.
- **Format Document** and **Format Selection** (LSP) were added to the **Edit**
  menu and to the editor's right-click menu, with shortcuts `Shift+Alt+F` and
  `Ctrl+K, Ctrl+F`. They use the language server's formatter (for example
  `gofumpt`/`gofmt` through gopls).
- New **View ▸ Auto-Format on Save** toggle (off by default): formats the
  document with the language server before each save.

- Custom languages can now be marked **Compiled** (Settings ▸ Languages). Instead
  of just running `tool flags file`, the editor builds the file to
  `/tmp/slayer_out` and runs the resulting binary — like the built-in C/C++/Rust.
  Handy for compilers such as `clang`, `g++`, `rustc` or `clang` on LLVM IR.
- Every language now has an editable **Language name** (Settings ▸ Languages) —
  not just custom ones. Rename a built-in language to whatever you like (for
  example “LLVM” instead of `clang`); the name shows in the list, the status bar
  and the New File dialog. Leaving it at the catalog name keeps the default.
- All compiled languages (built-in C/C++/Rust and custom ones marked
  **Compiled**) gained a **Run after building** toggle, on by default. Turn it
  off to only build the binary without running it.
- Every language now has a customizable **Badge** (Settings ▸ Languages). Pick
  a ready-made badge from a built-in library of 60+ language logos — including
  ones outside the editor's own list such as LLVM, Zig, Kotlin, Swift, Dart,
  Elixir, Scala, TypeScript and more — or choose an image file of your own
  (PNG, SVG…). The badge shows in the language list, the file tabs, the status
  bar and the New File dialog; leaving it on **Default** follows the language.
  The ready-made list is sorted alphabetically.
- The language list in **Settings ▸ Languages** is now sorted alphabetically
  (built-in and custom languages together); a language added with **Add
  Language…** drops into its alphabetical place instead of at the bottom.

## 0.7.1 — 2026-11-05

- The “go to line” bar's up/down buttons now use chevrons (like Kate), matching
  the folding and other arrows in the editor, instead of filled arrow glyphs.

## 0.7.0 — 2026-11-05

- The terminal also comes up with a shell prompt ready when it is restored
  visible at startup (its state from the last session), not only on the toolbar
  toggle. Previously a terminal that was open when the app last closed could
  reopen empty until the first Run/Build started a shell.

## 0.6.9 — 2026-11-04

- The status bar is more interactive, as in Kate: clicking the cursor position
  (**line:column**) toggles the “go to line” bar (click to show, click again to
  hide), and clicking the **INSERT/OVERWRITE** indicator toggles the
  insert/overwrite mode (same as the Insert key).
- The editor font is now chosen like Kate: **Settings ▸ Editor** shows the
  current font as e.g. “Hack 11” with an edit button that opens the system font
  picker (family, style, size and a live sample; monospaced fonts), instead of a
  family dropdown plus a separate size box.
- The text-encoding menu (status bar) is now organized by region, like Kate —
  Unicode, Western European, Central European, Cyrillic, Greek, Arabic, Hebrew,
  Japanese, Korean, Chinese, Thai, … — instead of one long flat list. Only the
  encodings this build's Qt actually supports are shown, and UTF-8 stays at the
  top for quick access.
- Opening the terminal (the toolbar toggle, or `Ctrl+``) now always leaves a
  shell prompt ready to type, even before anything has been compiled or run.
  Previously the panel could open empty until the first Run/Build started a
  shell.
- Indentation now follows the Kate model, with three separate settings (in
  **Settings ▸ Editor** and on the status-bar button for the current file):
  **Tab width** (the visual width of a tab character), **Indentation width**
  (how far one level indents) and **Indentation mode** — **Spaces** (soft tabs),
  **Tabulators** (tab characters) or **Tabulators & Spaces** (tabs for whole tab
  stops, spaces for the rest). All default to 4 and Spaces, so existing files
  behave as before. The status bar shows e.g. “Soft Tabs: 4” (or “4 (8)” when
  the two widths differ). This replaces the old single tab-width plus
  “insert spaces” checkbox.

## 0.6.8 — 2026-11-04

- The language indicator in the bottom-right status bar is now a read-only label
  showing the active file's language, instead of a dropdown that let you pick a
  language from a list. A file's language follows its extension; to change it,
  use **Save As**.

## 0.6.7 — 2026-10-31

- **Open File** now opens its dialog in the folder of the current tab's file,
  instead of the folder of the last file that was opened. Untitled tabs (no file
  on disk) still fall back to the last-used folder.

## 0.6.6 — 2026-10-30

- Undo/redo no longer breaks when the wrapped-line hanging indent (wrap indent)
  is on. Reapplying that indent after an <kbd>Ctrl+Z</kbd> used to clear the
  redo history — the editor could get stuck partway through the history, unable
  to redo — because the margin reflow counted as a new edit. The reflow is now
  skipped while a redo is pending, so the full undo/redo history survives
  editing, saving (auto or manual) and turning word wrap on or off.

## 0.6.5 — 2026-10-27

- The terminal panel docked at the top or bottom can now be resized as far as
  when docked left or right. The welcome screen no longer forces a large minimum
  height on the editor area, so dragging the terminal's splitter lets it grow to
  most of the window (and shrink back) in every dock position.

## 0.6.4 — 2026-10-26

- Resizing the window no longer loses the cursor line. With word wrap on,
  reshaping the window reflows the text; the editor now keeps the cursor line
  in view afterwards (as Kate does) when it was visible before the resize,
  instead of scrolling away from it.

## 0.6.3 — 2026-10-24

- The in-app help (**Running code**, <kbd>F1</kbd>) now warns that, for C and
  C++, every sibling source file in the folder without its own `main()` is
  compiled together, and recommends keeping one program per folder — or opening
  the folder as a project — to avoid linking unrelated modules (which could
  cause “multiple definition” or compile errors).

## 0.6.2 — 2026-10-24

- Running a **C or C++** file now compiles the sibling source files in the same
  folder that do not define their own `main()`, so a program split across
  several files (a module plus its `.h` header, as in `main.c` + `calc.c`)
  links without a Makefile. Other programs in the folder (each with its own
  `main()`) are left out, so a folder of independent exercises still runs one
  at a time. Interpreted languages (Python, JavaScript, Ruby, Perl…) and Rust
  already resolve their sibling modules from the entry file's folder.
- Language servers now provide **Go to Definition** (Edit ▸ Go to Definition,
  `F12`, or Ctrl+click on a symbol) and **documentation on hover** (point at a
  symbol to see its signature/docs), in addition to the completion and
  diagnostics already offered. Both work for any language with a server
  installed (clangd, pylsp/Pyright, rust-analyzer, gopls, …).
- The debugger now supports **Rust**. `Ctrl+F5` on a `.rs` file compiles it
  with debug symbols and debugs it through `rust-gdb` (the gdb wrapper that
  loads the standard library's pretty-printers), falling back to plain `gdb`
  when `rust-gdb` is not installed. Breakpoints, stepping, the call stack and
  variable inspection work as they do for C/C++.

## 0.6.1 — 2026-10-24

- Highlights are now more visible and theme-customizable. Matching
  brackets/tags and selection occurrences are drawn with a stronger fill
  plus a crisp outline (VS Code style), so they stand out on any theme.
  The theme editor (Settings ▸ Appearance) gained a **Highlights** group
  with **Matching bracket/tag** and **Selection occurrences** colors, each
  with an adjustable opacity (alpha) in the color picker; **Selection**
  stays editable as before.

## 0.6.0 — 2026-10-22

- Code-quality fixes from a source audit (no behaviour change): renamed the
  `Language::requires` member (a C++20 keyword) to `requiredTools`, fixed a
  `-Wrange-loop-construct` warning in the Ruby indentation check and a
  `-Wreorder` warning in the `MainWindow` constructor.
- Packaging: the AppStream metainfo now lists the 0.5.x releases (it had
  stopped at 0.4.5), so software centers show the current version.
- Minor cleanups from a cppcheck pass (no behaviour change): several value
  getters now return by `const &` and two callback parameters take their
  list by `const &`, and `DapClient::start()` defensively disposes of any
  previous process before creating a new one.

## 0.5.12 — 2026-10-22

- Find: the Find action (magnifier icon / Ctrl+F) now toggles the search
  bar — triggering it again while the bar is open closes it, instead of
  only the bar's own close button doing so. The help entry documents this.
- Translations: completed the Brazilian Portuguese translation — the
  terminal, live-preview and find help topics, the shortcut table and the
  Edit menu (Cut/Copy/Paste/Select All) that were still falling back to
  English are now translated (`lupdate`, no `unfinished` entries left).

## 0.5.11 — 2026-10-22

- Minimap: dragging now matches VS Code/VSCodium. The visible-region
  indicator (slider) follows the cursor 1:1 during a drag (relative
  movement) instead of the previous absolute mapping, and clicking the
  minimap body snaps the slider's center to the cursor before dragging.
  The painted indicator and the drag math share a single geometry model,
  so the slider always stays under the pointer.

## 0.5.4 — 2026-10-19

## 0.5.8 — 2026-10-22

- Edit menu: Cut (Ctrl+X), Copy (Ctrl+C), Paste (Ctrl+V) and Select All
  (Ctrl+A) actions with icons (nf-cod-screen_cut, nf-cod-copy, nf-fa-paste,
  nf-md-select_all).

## 0.5.7 — 2026-10-22

- Edit menu: Cut (Ctrl+X), Copy (Ctrl+C), Paste (Ctrl+V) and Select All
  (Ctrl+A) actions with icons.
- Language servers (LSP) are now disabled by default; enable them in
  Settings ▸ Editor ▸ Language servers (LSP) or per-language in
  Settings ▸ Languages.
- Live preview now uses Server-Sent Events instead of polling: the browser
  keeps one persistent connection and reloads only when the editor sends a
  change, eliminating the constant stream of `/__slayer_live__` requests.
- Live preview debounce delay is now configurable in Settings ▸ Preview
  (default 500 ms, matching the VS Code Live Preview extension behaviour);
  the "Every N seconds" default interval changed from 2 s to 3 s.

## 0.5.2 — 2026-10-18

- Fix HTML syntax highlighting: tag names (`a`, `b`, `p`, …) are now colored
  only when they appear inside a tag (`<a href=…>`, `</p>`), not when used as
  plain words in text content. Attribute names are colored only when followed
  by `=`. The same context-aware rule applies to XML.
- Fix auto-save clearing undo/redo history: the document's modified state is
  now tracked without calling `QTextDocument::setModified(false)` (which
  cleared the undo stack on every auto-save).
- Fix Tab inserting wrong number of spaces when the line contains tab
  characters: column is now computed visually (each tab counts as the
  configured tab width), so the cursor always jumps to the next tab stop.
- Auto-dedent closing delimiters (`}`, `]`, `)`) when typed at the start of
  an otherwise-blank line: the line is unindented by one step automatically,
  matching the behaviour of Kate and VS Code.
- Data-driven indentation rules (VSCode-style `increaseIndentPattern` /
  `decreaseIndentPattern`) for C, C++, Java, JavaScript, TypeScript, Go,
  Rust, Python, Ruby, Lua, Shell, HTML, CSS, JSON, YAML, PHP, Pascal,
  Fortran, Haskell and CMake: auto-indent and auto-dedent now follow the
  same language-aware patterns used by VSCodium.

## 0.5.0 — 2026-10-18

- Project mode: File ▸ Open Folder… (Ctrl+Shift+O) opens a folder as a project,
  with a file tree panel (new file/folder, rename, move to trash, copy path),
  Open Recent Project and Close Folder. Each project keeps its own open tabs.
- Quick Open (Ctrl+P): find a project file by name with fuzzy matching.
- Find in Project (Ctrl+Shift+F): results grouped by file, with match case,
  whole word and regular expression options.
- Language servers use the open folder as their root.
- Build Project (Ctrl+Shift+B) and Run Project (F4), detected from the project
  root (CMake, Make, Cargo, Go, npm, Meson, Python) or set in Run ▸ Configure
  Project Commands…; F5 on a C/C++ file of such a project offers to run the
  whole project. New terminals open in the project folder.
- Syntax highlighting for CMake (CMakeLists.txt, *.cmake) and Makefiles; tab and
  language icons for CMake, Makefile, Markdown, JSON, HTML, CSS, XML, TOML and SQL.
- Performance mode (Settings ▸ Editor ▸ Performance; off by default): turns
  off syntax highlighting, folding, the minimap, indent guides and occurrence
  highlighting for large files — automatically when a file is opened with more
  than 50,000 lines, more than 5 MB or a line longer than 10,000 characters
  (all configurable), or always. Opening large files gets 2–4× faster and
  switching themes becomes instant; a "Performance" label shows in the status bar.
- Terminal right-click menu, like Konsole: open/copy link, select all, find,
  clear, save output, zoom, open the file manager here, rename tab and monitor
  for activity/silence; Configure sets the color scheme (Konsole schemes or the
  theme), cursor shape, history size and scrollbar, also in Settings ▸ Terminal.
- A folder given on the command line (or dropped on the window) opens as a project.
- Files with Windows line endings (CRLF) keep them when saved, instead of being
  converted to LF.
- Fixes:
  - Crash when stopping the debugger right after starting it.
  - Auto-save no longer overwrites a file that another program changed (git pull,
    another editor); the "modified by another program" bar asks first.
  - Auto-save failures show a status bar message instead of a dialog after every
    pause in typing.
  - The HTML preview server refused paths outside the site root only partly:
    sibling folders with the same name prefix were reachable.
  - Reopening the app with a folder open no longer erases the list of loose tabs.
  - Restarting a C/C++ debug session while compiling no longer aborts the new one.
  - Find in Project no longer hangs on FIFOs and other special files.
- bench_editor: a benchmark tool (not part of ctest) to time the editor with large files.
- Settings ▸ Editor ▸ Project: hidden files and folders (.gitignore-style
  patterns); Quick Open and Find in Project also honor the project's .gitignore.

## 0.4.6 — 2026-10-17

- Much faster theme switching and file opening on large files (no more
  multi-second freeze).
- View ▸ Dark Mode (Ctrl+Shift+D) switches the theme to its light/dark variant.
- Focus on run (View menu and Settings ▸ Terminal): terminal only when the
  program waits for input (default), always the terminal, or the editor.
- Selecting text highlights its other occurrences.
- Debugging uses the same terminal tab as Run; the step buttons and the
  debugger panel appear only during a session.
- Breakpoints only for languages the debugger supports (Python, C, C++).
- The terminal button is a plain show/hide toggle.
- Preview: cross-origin isolation (COOP/COEP) on by default.
- The automatic interface language follows LANGUAGE/LC_MESSAGES/LANG;
  complete pt_BR translation.
- Removed the leftover Flatpak support: the editor is distributed as a Debian
  package.

## 0.4.5 — 2026-10-16

- Find bar: persistent search history and Kate-style navigation arrows.
- Go to Line: inline bar at the bottom (jumps live as you type) instead of a
  modal dialog.
- Each open file's cursor position is remembered between sessions.
- Indentation of wrapped lines is now an optional setting (Settings ▸ Editor).
- Fixed a crash that could close the editor while searching in a word-wrapped
  document.

## 0.4.4 — 2026-10-15

- Syntax highlighting for HTML, CSS, JSON, XML, Markdown and other markup/config
  formats.
- Live HTML preview with automatic reload (built-in server or an external one
  such as PHP), with optional cross-origin isolation for in-browser WASM apps.
- Code minimap (configurable width), VS Code-style indentation guides and
  indentation of wrapped lines.
- Auto-close of HTML/XML tags, Ctrl+/ comment toggle for markup, an Open Recent
  menu and optional auto-save.

For earlier versions (0.1.0–0.4.3), see the
[GitHub releases](https://github.com/wyllianbs/SE/releases).

## Known issues

- **Indentation of wrapped lines on native Wayland.** The option
  "Indent wrapped lines" (Settings ▸ Editor, off by default) can crash the
  editor on some native Wayland sessions: Qt's text layout
  (`QTextDocumentLayout`) segfaults while drawing a block that carries the
  hanging indent. It is a Qt platform bug, not reproducible under X11/XWayland.
  Leave the option off on Wayland, or run the editor under XWayland
  (`QT_QPA_PLATFORM=xcb slayer-editor`) if you want it.
