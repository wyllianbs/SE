# Slayer Editor

A lightweight **Qt 6** code editor with a tabbed interface and an **embedded
terminal**. Write code and run it side by side — press <kbd>F5</kbd> to compile
and run the current file, with language-server diagnostics, a visual debugger,
a Kate-style split view and a choice of themes.

This is the Linux port of the Android Slayer Editor.

![Code and run in one place](screenshots/meta-01-run.png)

## Features

- **Tabbed editor** with syntax highlighting, auto-indentation, auto-closing
  pairs, find/replace and code folding.
- **Embedded terminal** (QTermWidget) dockable on any edge of the window, or
  detached.
- **Run with <kbd>F5</kbd>** — C, C++, Python, Rust, Go, JavaScript, Java and
  shell scripts, straight into the built-in terminal.
- **Language Server Protocol** — diagnostics and completion (clangd, pylsp,
  rust-analyzer, gopls, typescript-language-server, and more), configurable per
  language.
- **Visual debugger** over the Debug Adapter Protocol — breakpoints, stepping,
  variables and the call stack (`gdb` for C/C++, `debugpy` for Python).
- **Kate-style split view** — one document across several panes — and a status
  bar with git branch, cursor position, encoding and more.
- **Themes** — dozens of light and dark color schemes that can follow the
  system, plus an optional symbol display.

## Install

### Debian / Ubuntu (amd64)

Download the latest `.deb` from the [Releases](https://github.com/wyllianbs/SE/releases)
page and install it with `apt`, which pulls in the required Qt 6 and QTermWidget
libraries:

```bash
sudo apt install ./slayer-editor_0.4.0-1_amd64.deb
```

To run programs from the editor, install the relevant toolchains (for example
`build-essential` and `python3`). Language servers and debuggers are optional
and detected automatically when present (`clangd`, `pylsp`, `gdb`, `debugpy`, …).

## Build from source

Build dependencies:

```bash
sudo apt install cmake qt6-base-dev libqtermwidget-dev qt6-l10n-tools libutf8proc-dev
```

Configure, build and run:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/slayer-editor
```

To build the Debian package instead:

```bash
dpkg-buildpackage -us -uc -b
```

## License

Slayer Editor is released under the **GPL-3.0-or-later** license; see
[LICENSE](LICENSE). The bundled fonts (Hack, JetBrains Mono and Nerd Fonts
symbols) keep their own licenses, documented in
[debian/copyright](debian/copyright).

Copyright © Wyllian Bezerra da Silva
