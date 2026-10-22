#pragma once

#include <QColor>
#include <QFont>
#include <QIcon>
#include <QString>

/** Ícones desenhados a partir dos glifos da Symbols Nerd Font embutida. */
namespace Icons {

enum Glyph : char32_t {
    NewFile = 0xEA7F,
    Open = 0xEAF7,
    Save = 0xEB4B,
    SaveAs = 0xEB4A,
    Undo = 0xF054D,
    Redo = 0xF044F,
    Cut = 0xF190,
    Copy = 0xF18F,
    Paste = 0xF192,
    SelectAll = 0xEA6C,
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
    Profiles = 0xEB99,
    Code = 0xEAC4,
    Languages = 0xF1AB,
    Symbols = 0xEB64,
    Folder = 0xEA83,
    Check = 0xEAB2,
    ChevronUp = 0xEAB7,
    ChevronDown = 0xEAB4,
    ChevronRight = 0xEAB6,
    GitBranch = 0xE725,
    Server = 0xEB50,
    SaveAll = 0xEB49,
    Help = 0xEB32,
    Add = 0xEA60,
    Bug = 0xEAAF,
    Moon = 0xF186,
    Sun = 0xF185,
    RunInteractive = 0xEBCA,
    StepOver = 0xEAD6,
    StepInto = 0xEAD4,
    StepOut = 0xEAD5,
    Continue = 0xEACF,
    Stop = 0xEAD7,
    DebugArrow = 0xEB8B,
    SplitSideBySide = 0xEB56,
    SplitStacked = 0xEB57,
    CloseAll = 0xEAC1,
    EyeClosed = 0xEAE7,
    LivePreview = 0xEA70, //< olho (codicon): pré-visualização no navegador
    LangC = 0xE61E,
    LangCpp = 0xE61D,
    LangPython = 0xE606,
    LangRust = 0xE7A8,
    LangGo = 0xE626,
    LangJs = 0xE60C,
    LangJava = 0xE738,
    LangShell = 0xE795,
    LangRuby = 0xE739,
    LangPerl = 0xE769,
    LangLua = 0xE620,
    LangPhp = 0xE73D,
    LangR = 0xE68A,
    LangJulia = 0xE624,
    LangHaskell = 0xE777,
    LangFortran = 0xF121A,
    LangCMake = 0xE794,
    LangMakefile = 0xE673,
    LangMarkdown = 0xE609,
    LangJson = 0xE80B,
    LangHtml = 0xE60E,
    LangCss = 0xE614,
    LangXml = 0xE619,
    LangToml = 0xE6B2,
    LangSql = 0xE706,
};

/** Carrega as fontes embutidas; chamar uma vez após criar o QApplication. */
void loadFonts();
QString symbolFamily();
QString monoFamily();
/**
 * Fonte monoespaçada do editor sem ligaduras de programação (<=, ->, != viram
 * ≤, →, ≠ em fontes como a JetBrains Mono). Na área de edição cada símbolo tem
 * de aparecer como foi digitado.
 */
QFont editorFont(const QString &family, int pointSize);

QIcon icon(Glyph glyph, const QColor &color, int size = 18);
QString glyphText(Glyph glyph);
/** PNG em memória como data-URL (para url() em folhas de estilo); sem disco. */
QString glyphUrl(Glyph glyph, const QColor &color, int size);
Glyph forLanguage(const QString &languageId);
QColor colorForLanguage(const QString &languageId);

} // namespace Icons
