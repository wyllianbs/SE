#include "Language.h"

#include <QFileInfo>
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
    static const Language text{QStringLiteral("text"), QStringLiteral("Plain Text"),
                               {"txt"}, {}, {}, {}, {}, {}, {}};
    return text;
}

const Language &Language::forFileName(const QString &fileName)
{
    const QString ext = QFileInfo(fileName).suffix().toLower();
    for (const Language &lang : all()) {
        if (lang.extensions.contains(ext))
            return lang;
    }
    return plainText();
}

const Language &Language::byId(const QString &id)
{
    for (const Language &lang : all()) {
        if (lang.id == id)
            return lang;
    }
    return plainText();
}

QString Language::commandFor(const QString &filePath, const QString &tool, const QString &flags) const
{
    const QFileInfo info(filePath);
    QString cmd = runCommand;
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
