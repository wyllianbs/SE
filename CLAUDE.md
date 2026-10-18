# Slayer Editor — instructions for Claude

## Authorship (mandatory)

- Every commit must have the maintainer as author and committer:
  `Wyllian Bezerra da Silva <wyllianbs@gmail.com>`.
  Before the first commit of a session, run:
  `git config user.name "Wyllian Bezerra da Silva"` and
  `git config user.email "wyllianbs@gmail.com"`.
- Never add `Co-Authored-By`, `Claude-Session` or any other attribution
  trailer to commit messages, and no "Generated with Claude Code" lines in
  pull requests, comments or files.
- Do not open pull requests; push the branch and let the maintainer open
  and merge it (Rebase and merge).

## Project

- Qt 6 (Widgets) + qtermwidget6, C++17, CMake. Debian package in `debian/`.
- Build and test: `cmake -S . -B build && cmake --build build -j$(nproc)`
  then `(cd build && QT_QPA_PLATFORM=offscreen ctest)`.
- Every user-visible string uses `tr()`; after changing strings, run the
  `slayer-editor_lupdate` target and translate the new entries in
  `translations/slayer-editor_pt_BR.ts` (no `unfinished` left).
- New features and fixes go under `## Unreleased` in `CHANGELOG.md`; the
  help (`src/HelpDialog.cpp`) and its shortcut table must stay in sync with
  the menus.
- Code comments are in Portuguese, matching the existing code.
