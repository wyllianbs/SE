# Changelog

All notable changes are also published on the
[GitHub releases](https://github.com/wyllianbs/SE/releases) page.

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
