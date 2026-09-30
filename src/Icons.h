#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

/** Ícones desenhados a partir dos glifos da Symbols Nerd Font embutida. */
namespace Icons {

enum Glyph : char32_t {
    NewFile = 0xEA7F,
    Open = 0xEAF7,
    Save = 0xEB4B,
    SaveAs = 0xEB4A,
    Undo = 0xF0E2,
    Redo = 0xF01E,
    Search = 0xEA6D,
    Run = 0xEB2C,
    Terminal = 0xEA85,
    Palette = 0xF1FC,
    Settings = 0xEB51,
    Info = 0xEA74,
    Close = 0xEA76,
    Layout = 0xF0DB,
    Clear = 0xEABF,
    Float = 0xF2D2,
    File = 0xEA7B,
    LangC = 0xE61E,
    LangCpp = 0xE61D,
    LangPython = 0xE606,
    LangRust = 0xE7A8,
    LangGo = 0xE626,
    LangJs = 0xE60C,
    LangJava = 0xE738,
    LangShell = 0xE795,
};

/** Carrega as fontes embutidas; chamar uma vez após criar o QApplication. */
void loadFonts();
QString symbolFamily();
QString monoFamily();

QIcon icon(Glyph glyph, const QColor &color, int size = 18);
QString glyphText(Glyph glyph);
Glyph forLanguage(const QString &languageId);
QColor colorForLanguage(const QString &languageId);

} // namespace Icons
