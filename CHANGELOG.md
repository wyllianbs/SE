# Changelog

All notable changes are also published on the
[GitHub releases](https://github.com/wyllianbs/SE/releases) page.

## Unreleased

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
