#include "Language.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>

namespace {

QString shellQuote(const QString &s)
{
    QString out = s;
    out.replace(QLatin1Char('\''), QLatin1String("'\\''"));
    return QLatin1Char('\'') + out + QLatin1Char('\'');
}

QList<Language> buildLanguages()
{
    QList<Language> list;

    list.append({QStringLiteral("c"), QStringLiteral("C"), {"c", "h"},
        {"auto", "break", "case", "const", "continue", "default", "do", "else", "enum", "extern",
         "for", "goto", "if", "inline", "register", "restrict", "return", "sizeof", "static",
         "struct", "switch", "typedef", "union", "volatile", "while", "true", "false", "NULL"},
        {"char", "double", "float", "int", "long", "short", "signed", "unsigned", "void",
         "size_t", "bool", "int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t",
         "uint32_t", "uint64_t", "FILE"},
        "//", "/*", "*/",
        QStringLiteral("mkdir -p /tmp/slayer_out && {tool} {flags} %f -o /tmp/slayer_out/%b && /tmp/slayer_out/%b")});

    list.append({QStringLiteral("cpp"), QStringLiteral("C++"), {"cpp", "cc", "cxx", "hpp", "hh", "hxx"},
        {"alignas", "alignof", "auto", "break", "case", "catch", "class", "const", "const_cast",
         "consteval", "constexpr", "constinit", "continue", "co_await", "co_return", "co_yield",
         "decltype", "default", "delete", "do", "dynamic_cast", "else", "enum", "explicit",
         "export", "extern", "false", "for", "friend", "goto", "if", "inline", "mutable",
         "namespace", "new", "noexcept", "nullptr", "operator", "private", "protected", "public",
         "reinterpret_cast", "requires", "return", "sizeof", "static", "static_assert",
         "static_cast", "struct", "switch", "template", "this", "thread_local", "throw", "true",
         "try", "typedef", "typeid", "typename", "union", "using", "virtual", "volatile", "while",
         "concept", "import", "module"},
        {"bool", "char", "char8_t", "char16_t", "char32_t", "double", "float", "int", "long",
         "short", "signed", "unsigned", "void", "wchar_t", "size_t", "string", "vector", "map",
         "set", "unique_ptr", "shared_ptr", "ostream", "istream", "std"},
        "//", "/*", "*/",
        QStringLiteral("mkdir -p /tmp/slayer_out && {tool} {flags} %f -o /tmp/slayer_out/%b && /tmp/slayer_out/%b")});

    list.append({QStringLiteral("python"), QStringLiteral("Python"), {"py", "pyw"},
        {"and", "as", "assert", "async", "await", "break", "class", "continue", "def", "del",
         "elif", "else", "except", "finally", "for", "from", "global", "if", "import", "in", "is",
         "lambda", "nonlocal", "not", "or", "pass", "raise", "return", "try", "while", "with",
         "yield", "True", "False", "None", "match", "case", "self"},
        {"int", "float", "str", "bool", "list", "dict", "set", "tuple", "bytes", "object",
         "print", "len", "range", "input", "open", "type"},
        "#", "\"\"\"", "\"\"\"",
        QStringLiteral("{tool} {flags} %f")});

    list.append({QStringLiteral("rust"), QStringLiteral("Rust"), {"rs"},
        {"as", "async", "await", "break", "const", "continue", "crate", "dyn", "else", "enum",
         "extern", "false", "fn", "for", "if", "impl", "in", "let", "loop", "match", "mod", "move",
         "mut", "pub", "ref", "return", "self", "Self", "static", "struct", "super", "trait",
         "true", "type", "union", "unsafe", "use", "where", "while"},
        {"i8", "i16", "i32", "i64", "i128", "isize", "u8", "u16", "u32", "u64", "u128", "usize",
         "f32", "f64", "bool", "char", "str", "String", "Option", "Result", "Vec", "Box", "Rc",
         "Arc"},
        "//", "/*", "*/",
        QStringLiteral("mkdir -p /tmp/slayer_out && {tool} {flags} %f -o /tmp/slayer_out/%b && /tmp/slayer_out/%b")});

    list.append({QStringLiteral("go"), QStringLiteral("Go"), {"go"},
        {"break", "default", "func", "interface", "select", "case", "defer", "go", "map",
         "struct", "chan", "else", "goto", "package", "switch", "const", "fallthrough", "if",
         "range", "type", "continue", "for", "import", "return", "var", "true", "false", "nil"},
        {"bool", "string", "int", "int8", "int16", "int32", "int64", "uint", "uint8", "uint16",
         "uint32", "uint64", "uintptr", "byte", "rune", "float32", "float64", "error", "any"},
        "//", "/*", "*/",
        QStringLiteral("{tool} run {flags} %f")});

    list.append({QStringLiteral("javascript"), QStringLiteral("JavaScript"), {"js", "mjs", "cjs"},
        {"async", "await", "break", "case", "catch", "class", "const", "continue", "debugger",
         "default", "delete", "do", "else", "export", "extends", "finally", "for", "from",
         "function", "if", "import", "in", "instanceof", "let", "new", "of", "return", "static",
         "super", "switch", "this", "throw", "try", "typeof", "var", "void", "while", "yield",
         "true", "false", "null", "undefined"},
        {"Array", "Boolean", "Date", "Error", "Function", "JSON", "Map", "Math", "Number",
         "Object", "Promise", "RegExp", "Set", "String", "Symbol", "console"},
        "//", "/*", "*/",
        QStringLiteral("{tool} {flags} %f")});

    list.append({QStringLiteral("java"), QStringLiteral("Java"), {"java"},
        {"abstract", "assert", "break", "case", "catch", "class", "const", "continue", "default",
         "do", "else", "enum", "extends", "final", "finally", "for", "goto", "if", "implements",
         "import", "instanceof", "interface", "native", "new", "package", "private", "protected",
         "public", "return", "static", "strictfp", "super", "switch", "synchronized", "this",
         "throw", "throws", "transient", "try", "volatile", "while", "true", "false", "null",
         "var", "record"},
        {"boolean", "byte", "char", "double", "float", "int", "long", "short", "void", "String",
         "Integer", "List", "Map", "Object", "System"},
        "//", "/*", "*/",
        QStringLiteral("{tool} {flags} %f")});

    list.append({QStringLiteral("bash"), QStringLiteral("Shell"), {"sh", "bash"},
        {"if", "then", "else", "elif", "fi", "case", "esac", "for", "while", "until", "do",
         "done", "in", "function", "return", "local", "export", "readonly", "exit", "break",
         "continue"},
        {"echo", "printf", "read", "cd", "source", "set", "shift", "test"},
        "#", QString(), QString(),
        QStringLiteral("{tool} {flags} %f")});

    const QString compiled = QStringLiteral("mkdir -p /tmp/slayer_out && {tool} {flags} %f -o /tmp/slayer_out/%b && /tmp/slayer_out/%b");
    const QString interpreted = QStringLiteral("{tool} {flags} %f");

    list.append({QStringLiteral("ruby"), QStringLiteral("Ruby"), {"rb"},
        {"alias", "and", "begin", "break", "case", "class", "def", "defined?", "do", "else", "elsif", "end",
         "ensure", "false", "for", "if", "in", "module", "next", "nil", "not", "or", "redo", "rescue", "retry",
         "return", "self", "super", "then", "true", "undef", "unless", "until", "when", "while", "yield"},
        {"puts", "print", "require", "attr_accessor", "Integer", "String", "Array", "Hash"},
        "#", "=begin", "=end", interpreted});
    list.append({QStringLiteral("perl"), QStringLiteral("Perl"), {"pl", "pm"},
        {"my", "our", "local", "sub", "if", "elsif", "else", "unless", "while", "until", "for", "foreach",
         "last", "next", "redo", "return", "use", "no", "package", "require", "and", "or", "not", "eq", "ne"},
        {"print", "printf", "say", "chomp", "split", "join", "push", "pop", "shift", "keys", "values"},
        "#", QString(), QString(), interpreted});
    list.append({QStringLiteral("lua"), QStringLiteral("Lua"), {"lua"},
        {"and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if", "in",
         "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while"},
        {"print", "pairs", "ipairs", "require", "table", "string", "math", "io"},
        "--", "--[[", "]]", interpreted});
    list.append({QStringLiteral("php"), QStringLiteral("PHP"), {"php"},
        {"abstract", "and", "array", "as", "break", "case", "catch", "class", "const", "continue", "default",
         "do", "echo", "else", "elseif", "extends", "final", "for", "foreach", "function", "if", "implements",
         "interface", "namespace", "new", "or", "private", "protected", "public", "return", "static",
         "switch", "throw", "try", "use", "while", "true", "false", "null"},
        {"int", "float", "string", "bool", "void", "mixed", "self"},
        "//", "/*", "*/", interpreted});
    list.append({QStringLiteral("r"), QStringLiteral("R"), {"r", "R"},
        {"if", "else", "repeat", "while", "function", "for", "in", "next", "break", "TRUE", "FALSE", "NULL",
         "NA", "Inf", "NaN", "return"},
        {"print", "cat", "c", "list", "vector", "data.frame", "library"},
        "#", QString(), QString(), interpreted});
    list.append({QStringLiteral("julia"), QStringLiteral("Julia"), {"jl"},
        {"begin", "end", "function", "return", "if", "elseif", "else", "for", "while", "in", "let", "local",
         "global", "const", "module", "using", "import", "struct", "mutable", "true", "false", "nothing"},
        {"Int", "Float64", "String", "Bool", "Vector", "Array", "println", "print"},
        "#", "#=", "=#", interpreted});
    list.append({QStringLiteral("haskell"), QStringLiteral("Haskell"), {"hs"},
        {"case", "class", "data", "deriving", "do", "else", "if", "import", "in", "instance", "let", "module",
         "newtype", "of", "then", "type", "where", "True", "False"},
        {"Int", "Integer", "Double", "String", "Bool", "IO", "Maybe", "putStrLn", "print"},
        "--", "{-", "-}", interpreted});
    list.append({QStringLiteral("fortran"), QStringLiteral("Fortran"), {"f90", "f95", "f03", "f08", "f"},
        {"program", "end", "implicit", "none", "integer", "real", "character", "logical", "do", "if", "then",
         "else", "call", "subroutine", "function", "module", "use", "contains", "print", "write", "read"},
        {"double", "precision", "complex"},
        "!", QString(), QString(), compiled});
    list.append({QStringLiteral("pascal"), QStringLiteral("Pascal"), {"pas", "pp"},
        {"program", "begin", "end", "var", "const", "type", "procedure", "function", "if", "then", "else",
         "while", "do", "for", "to", "downto", "repeat", "until", "case", "of", "uses", "and", "or", "not"},
        {"integer", "real", "boolean", "char", "string", "writeln", "readln"},
        "//", "{", "}",
        QStringLiteral("mkdir -p /tmp/slayer_out && {tool} {flags} -FE/tmp/slayer_out %f && /tmp/slayer_out/%b")});
    // HTML e CSS: realce e reconhecimento. A pré-visualização (abrir no
    // navegador / servidor) é tratada à parte, não por um comando de shell.
    list.append({QStringLiteral("html"), QStringLiteral("HTML"), {"html", "htm", "xhtml"},
        {"html", "head", "body", "title", "meta", "link", "style", "script", "div", "span", "p",
         "a", "img", "ul", "ol", "li", "table", "thead", "tbody", "tr", "td", "th", "form", "input",
         "button", "label", "select", "option", "textarea", "h1", "h2", "h3", "h4", "h5", "h6",
         "header", "footer", "nav", "section", "article", "main", "aside", "br", "hr", "strong",
         "em", "b", "i", "u", "code", "pre", "blockquote", "iframe", "canvas", "svg", "video",
         "audio", "source", "figure", "figcaption", "small", "abbr", "details", "summary"},
        {"class", "id", "href", "src", "alt", "type", "name", "value", "rel", "content", "charset",
         "lang", "style", "width", "height", "placeholder", "action", "method", "target", "disabled",
         "checked", "selected", "readonly", "required", "for", "colspan", "rowspan", "role", "async",
         "defer", "download"},
        QString(), QStringLiteral("<!--"), QStringLiteral("-->")});
    list.append({QStringLiteral("css"), QStringLiteral("CSS"), {"css"},
        {"color", "background", "background-color", "background-image", "margin", "margin-top",
         "margin-right", "margin-bottom", "margin-left", "padding", "padding-top", "padding-right",
         "padding-bottom", "padding-left", "border", "border-radius", "border-color", "border-width",
         "border-style", "display", "position", "top", "left", "right", "bottom", "width", "height",
         "min-width", "max-width", "min-height", "max-height", "font", "font-size", "font-weight",
         "font-family", "font-style", "line-height", "text-align", "text-decoration",
         "text-transform", "letter-spacing", "flex", "flex-direction", "flex-wrap", "justify-content",
         "align-items", "align-content", "gap", "grid", "grid-template-columns", "grid-template-rows",
         "overflow", "overflow-x", "overflow-y", "z-index", "opacity", "visibility", "transition",
         "transform", "animation", "cursor", "box-shadow", "box-sizing", "content", "float", "clear",
         "white-space", "word-wrap", "list-style", "outline", "vertical-align"},
        {"none", "block", "inline", "inline-block", "absolute", "relative", "fixed", "sticky",
         "auto", "inherit", "initial", "unset", "hidden", "solid", "dashed", "dotted", "bold",
         "normal", "italic", "center", "justify", "middle", "pointer", "transparent", "wrap",
         "nowrap", "column", "row", "uppercase", "lowercase", "capitalize"},
        QString(), QStringLiteral("/*"), QStringLiteral("*/")});
    list.append({QStringLiteral("json"), QStringLiteral("JSON"), {"json", "jsonc", "geojson"},
        {"true", "false", "null"}, {}, QString(), QString(), QString()});
    list.append({QStringLiteral("xml"), QStringLiteral("XML"),
        {"xml", "svg", "xsl", "xslt", "xsd", "rss", "atom", "plist", "qrc", "ui", "kml", "gpx"},
        {}, {}, QString(), QStringLiteral("<!--"), QStringLiteral("-->")});
    list.append({QStringLiteral("markdown"), QStringLiteral("Markdown"), {"md", "markdown", "mdown", "mkd"},
        {}, {}, QString(), QString(), QString()});
    list.append({QStringLiteral("yaml"), QStringLiteral("YAML"), {"yml", "yaml"},
        {"true", "false", "null", "yes", "no", "on", "off"}, {},
        QStringLiteral("#"), QString(), QString()});
    list.append({QStringLiteral("cmake"), QStringLiteral("CMake"), {"cmake"},
        {"cmake_minimum_required", "project", "add_executable", "add_library", "add_subdirectory",
         "add_test", "add_custom_command", "add_custom_target", "add_compile_definitions",
         "add_compile_options", "add_dependencies", "target_link_libraries", "target_include_directories",
         "target_compile_definitions", "target_compile_options", "target_compile_features",
         "target_sources", "find_package", "find_library", "find_path", "find_program", "include",
         "include_directories", "link_directories", "install", "set", "unset", "option", "message",
         "if", "elseif", "else", "endif", "foreach", "endforeach", "while", "endwhile", "function",
         "endfunction", "macro", "endmacro", "return", "list", "string", "file", "configure_file",
         "enable_testing", "execute_process", "get_filename_component", "set_target_properties",
         "set_property", "get_property"},
        {"PRIVATE", "PUBLIC", "INTERFACE", "REQUIRED", "COMPONENTS", "STATIC", "SHARED", "MODULE",
         "VERSION", "LANGUAGES", "DESTINATION", "TARGETS", "FILES", "GLOB", "GLOB_RECURSE",
         "CONFIGURE_DEPENDS", "STATUS", "WARNING", "FATAL_ERROR", "ON", "OFF", "TRUE", "FALSE",
         "AND", "OR", "NOT", "DEFINED", "EQUAL", "STREQUAL", "MATCHES", "EXISTS", "CACHE", "PARENT_SCOPE"},
        QStringLiteral("#"), QString(), QString()});
    list.append({QStringLiteral("makefile"), QStringLiteral("Makefile"), {"mk", "mak"},
        {"ifeq", "ifneq", "ifdef", "ifndef", "else", "endif", "include", "-include", "define", "endef",
         "export", "unexport", "override", "vpath", ".PHONY", ".DEFAULT", ".SUFFIXES"},
        {}, QStringLiteral("#"), QString(), QString()});
    list.append({QStringLiteral("toml"), QStringLiteral("TOML"), {"toml"},
        {"true", "false"}, {}, QStringLiteral("#"), QString(), QString()});
    list.append({QStringLiteral("ini"), QStringLiteral("INI"), {"ini", "cfg", "conf", "desktop"},
        {"true", "false"}, {}, QStringLiteral("#"), QString(), QString()});
    list.append({QStringLiteral("sql"), QStringLiteral("SQL"), {"sql"},
        {"SELECT", "FROM", "WHERE", "INSERT", "INTO", "VALUES", "UPDATE", "SET", "DELETE", "CREATE",
         "TABLE", "DROP", "ALTER", "ADD", "PRIMARY", "KEY", "FOREIGN", "REFERENCES", "JOIN", "INNER",
         "LEFT", "RIGHT", "OUTER", "ON", "GROUP", "BY", "ORDER", "HAVING", "LIMIT", "OFFSET", "AND",
         "OR", "NOT", "NULL", "AS", "DISTINCT", "UNION", "INDEX", "VIEW", "DATABASE", "IF", "EXISTS",
         "DEFAULT", "UNIQUE", "CONSTRAINT", "LIKE", "IN", "BETWEEN", "IS", "ASC", "DESC", "BEGIN",
         "COMMIT", "ROLLBACK", "TRANSACTION"},
        {"INT", "INTEGER", "VARCHAR", "CHAR", "TEXT", "DATE", "DATETIME", "TIMESTAMP", "BOOLEAN",
         "BOOL", "FLOAT", "DOUBLE", "DECIMAL", "NUMERIC", "REAL", "BLOB", "SERIAL", "BIGINT",
         "SMALLINT", "COUNT", "SUM", "AVG", "MIN", "MAX"},
        QStringLiteral("--"), QStringLiteral("/*"), QStringLiteral("*/")});
    list.append({QStringLiteral("typescript"), QStringLiteral("TypeScript"), {"ts", "tsx", "mts", "cts"},
        {"abstract", "as", "async", "await", "break", "case", "catch", "class", "const", "continue",
         "declare", "default", "delete", "do", "else", "enum", "export", "extends", "finally", "for",
         "from", "function", "get", "if", "implements", "import", "in", "infer", "instanceof",
         "interface", "keyof", "let", "namespace", "new", "of", "override", "private", "protected",
         "public", "readonly", "return", "satisfies", "set", "static", "super", "switch", "this",
         "throw", "try", "type", "typeof", "var", "void", "while", "yield", "true", "false", "null",
         "undefined"},
        {"any", "bigint", "boolean", "never", "number", "object", "string", "symbol", "unknown",
         "void", "Array", "Promise", "Record", "Partial", "Readonly", "Map", "Set", "console"},
        "//", "/*", "*/"});

    struct Extra { const char *id; const char *interactive; const char *file; const char *hello; QStringList requires; };
    const QList<Extra> extras{
        {"c", nullptr, "untitled.c",
         "#include <stdio.h>\n\nint main(void)\n{\n    printf(\"Hello, World!\\n\");\n    return 0;\n}\n", {}},
        {"cpp", nullptr, "untitled.cpp",
         "#include <iostream>\n\nint main()\n{\n    std::cout << \"Hello, World!\" << std::endl;\n    return 0;\n}\n", {}},
        {"python", "{tool} {flags} -i %f", "untitled.py", "print(\"Hello, World!\")\n", {}},
        {"rust", nullptr, "untitled.rs", "fn main() {\n    println!(\"Hello, World!\");\n}\n", {}},
        {"go", nullptr, "untitled.go", "package main\n\nimport \"fmt\"\n\nfunc main() {\n\tfmt.Println(\"Hello, World!\")\n}\n", {}},
        {"javascript", "{tool} {flags} -i -e \"$(cat %f)\"", "untitled.js", "console.log(\"Hello, World!\");\n", {}},
        // "java Arquivo.java" precisa do compilador (só o JRE não basta), e a
        // classe pública tem o nome do arquivo.
        {"java", nullptr, "Untitled.java",
         "public class Untitled {\n    public static void main(String[] args) {\n        System.out.println(\"Hello, World!\");\n    }\n}\n",
         {"javac"}},
        {"bash", "{tool} {flags} --rcfile %f -i", "untitled.sh", "#!/usr/bin/env bash\n\necho \"Hello, World!\"\n", {}},
        {"ruby", "irb -r ./%f", "untitled.rb", "puts \"Hello, World!\"\n", {}},
        {"perl", nullptr, "untitled.pl", "#!/usr/bin/env perl\nuse strict;\nuse warnings;\n\nprint \"Hello, World!\\n\";\n", {}},
        {"lua", "{tool} {flags} -i %f", "untitled.lua", "print(\"Hello, World!\")\n", {}},
        {"php", nullptr, "untitled.php", "<?php\n\necho \"Hello, World!\\n\";\n", {}},
        {"r", nullptr, "untitled.R", "cat(\"Hello, World!\\n\")\n", {}},
        {"julia", "{tool} {flags} -i %f", "untitled.jl", "println(\"Hello, World!\")\n", {}},
        {"haskell", nullptr, "untitled.hs", "main :: IO ()\nmain = putStrLn \"Hello, World!\"\n", {}},
        {"fortran", nullptr, "untitled.f90",
         "program hello\n    implicit none\n    print *, \"Hello, World!\"\nend program hello\n", {}},
        {"pascal", nullptr, "untitled.pas", "program Hello;\nbegin\n    writeln('Hello, World!');\nend.\n", {}},
        {"html", nullptr, "untitled.html",
         "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n    <meta charset=\"UTF-8\">\n    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n    <title>Hello</title>\n</head>\n<body>\n    <h1>Hello, World!</h1>\n</body>\n</html>\n", {}},
        {"css", nullptr, "untitled.css",
         "body {\n    font-family: sans-serif;\n    margin: 2rem;\n    color: #222;\n}\n", {}},
    };
    for (Language &lang : list) {
        for (const Extra &e : extras) {
            if (lang.id != QLatin1String(e.id))
                continue;
            if (e.interactive)
                lang.interactiveCommand = QString::fromUtf8(e.interactive);
            lang.newFileName = QString::fromUtf8(e.file);
            lang.helloWorld = QString::fromUtf8(e.hello);
            lang.requires = e.requires;
        }
    }
    return list;
}

} // namespace

const QList<Language> &Language::all()
{
    static const QList<Language> languages = buildLanguages();
    return languages;
}

const Language &Language::plainText()
{
    static const Language text{QStringLiteral("text"), QCoreApplication::translate("Language", "Plain Text"),
                               {"txt"}, {}, {}, {}, {}, {}, {}};
    return text;
}

QString Language::idForSpecialFileName(const QString &fileName)
{
    const QString name = QFileInfo(fileName).fileName();
    if (name == QLatin1String("CMakeLists.txt"))
        return QStringLiteral("cmake");
    if (name == QLatin1String("Makefile") || name == QLatin1String("makefile")
        || name == QLatin1String("GNUmakefile"))
        return QStringLiteral("makefile");
    return QString();
}

const Language &Language::forFileName(const QString &fileName)
{
    if (const QString special = idForSpecialFileName(fileName); !special.isEmpty())
        for (const Language &lang : all())
            if (lang.id == special)
                return lang;
    const QString ext = QFileInfo(fileName).suffix().toLower();
    for (const Language &lang : all()) {
        if (lang.extensions.contains(ext))
            return lang;
    }
    return plainText();
}

bool Language::debuggable(const QString &id)
{
    return id == QLatin1String("python") || id == QLatin1String("c") || id == QLatin1String("cpp");
}

const Language &Language::byId(const QString &id)
{
    for (const Language &lang : all()) {
        if (lang.id == id)
            return lang;
    }
    return plainText();
}

QString Language::commandFor(const QString &filePath, const QString &tool, const QString &flags,
                             bool interactive) const
{
    const QFileInfo info(filePath);
    QString cmd = (interactive && !interactiveCommand.isEmpty()) ? interactiveCommand : runCommand;
    cmd.replace(QLatin1String("{tool}"), tool.trimmed());
    cmd.replace(QLatin1String("{flags}"), flags.trimmed());
    cmd = cmd.simplified(); // flags vazias deixam espaços duplos
    cmd.replace(QLatin1String("%f"), shellQuote(info.fileName()));
    cmd.replace(QLatin1String("%b"), shellQuote(info.completeBaseName()));
    cmd.replace(QLatin1String("%d"), shellQuote(info.absolutePath()));
    return cmd;
}

Language Language::custom(const QString &name, const QStringList &extensions)
{
    Language lang;
    lang.id = QStringLiteral("custom:") + name;
    lang.displayName = name;
    lang.extensions = extensions;
    lang.lineComment = QStringLiteral("#");
    lang.runCommand = QStringLiteral("{tool} {flags} %f");
    return lang;
}

QString Language::helloWorldForTool(const QString &tool)
{
    // Pelo interpretador/compilador (primeira palavra do comando, sem versão).
    QString name = tool.trimmed().section(QLatin1Char(' '), 0, 0).section(QLatin1Char('/'), -1);
    name.remove(QRegularExpression(QStringLiteral("[0-9.]+$")));
    static const QHash<QString, QString> byTool{
        {"python", "python"}, {"node", "javascript"}, {"bash", "bash"}, {"sh", "bash"}, {"zsh", "bash"},
        {"ruby", "ruby"}, {"perl", "perl"}, {"lua", "lua"}, {"php", "php"}, {"Rscript", "r"}, {"julia", "julia"},
        {"runghc", "haskell"}, {"gcc", "c"}, {"clang", "c"}, {"g++", "cpp"}, {"clang++", "cpp"},
        {"gfortran", "fortran"}, {"fpc", "pascal"}, {"rustc", "rust"}, {"go", "go"}, {"java", "java"}};
    const QString id = byTool.value(name);
    if (!id.isEmpty())
        return byId(id).helloWorld;
    if (name == QLatin1String("tclsh"))
        return QStringLiteral("puts \"Hello, World!\"\n");
    if (name == QLatin1String("awk") || name == QLatin1String("gawk"))
        return QStringLiteral("BEGIN { print \"Hello, World!\" }\n");
    return QStringLiteral("Hello, World!\n");
}

QStringList Language::splitExtensions(const QString &text)
{
    QStringList out;
    for (QString e : text.split(QRegularExpression(QStringLiteral("[\\s,;]+")), Qt::SkipEmptyParts)) {
        e = e.trimmed().toLower();
        while (e.startsWith(QLatin1Char('.')))
            e.remove(0, 1);
        if (!e.isEmpty() && !out.contains(e))
            out << e;
    }
    return out;
}
