// SPDX-License-Identifier: MIT
#include "discord/syntax.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <unordered_set>

namespace discord::syntax {

namespace {

enum class Family { Code, Diff, Json, Markup, Css, Config };

struct Lang {
    const char *names;  // space-separated aliases
    Family family = Family::Code;
    const char *keywords = "";
    const char *types = "";
    const char *literals = "";
    const char *line_comments = "";  // space-separated
    const char *block_open = nullptr, *block_close = nullptr;
    const char *quotes = "\"'";
    bool triple = false;       // """ and ''' strings
    bool hash_meta = false;    // # at the start of a line: preprocessor / attribute
    bool at_meta = false;      // @decorators, @annotations
    bool upper_types = false;  // CapitalisedNames are types
    bool ci = false;           // case-insensitive keywords
    bool dollar_vars = false;  // $var, ${var}
    bool bang_macros = false;  // name! is a macro
};

const char *kCKeywords =
    "auto break case const continue default do else enum extern for goto if inline register restrict return "
    "sizeof static struct switch typedef union volatile while";
const char *kCTypes =
    "char double float int long short signed unsigned void bool _Bool size_t ssize_t ptrdiff_t intptr_t uintptr_t "
    "int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t FILE wchar_t";

const Lang kLangs[] = {
    {"c h", Family::Code, kCKeywords, kCTypes, "true false NULL", "//", "/*", "*/", "\"'", false, true},
    {"cpp c++ cc cxx hpp hh hxx ino cu", Family::Code,
     "auto break case catch class concept const consteval constexpr constinit const_cast continue co_await "
     "co_return co_yield decltype default delete do dynamic_cast else enum explicit export extern final for "
     "friend goto if inline mutable namespace new noexcept operator override private protected public "
     "register reinterpret_cast requires return sizeof static static_assert static_cast struct switch template "
     "this thread_local throw try typedef typeid typename union using virtual volatile while",
     "char char8_t char16_t char32_t double float int long short signed unsigned void bool wchar_t size_t "
     "int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t std string string_view vector map "
     "unordered_map set unique_ptr shared_ptr optional",
     "true false nullptr NULL", "//", "/*", "*/", "\"'", false, true},
    {"cs csharp c#", Family::Code,
     "abstract as async await base break case catch checked class const continue default delegate do else "
     "enum event explicit extern finally fixed for foreach get goto if implicit in init interface internal is "
     "lock namespace new operator out override params partial private protected public readonly record ref "
     "return sealed set sizeof stackalloc static struct switch this throw try typeof unchecked unsafe using "
     "var virtual void volatile when where while yield",
     "bool byte char decimal double float int long object sbyte short string uint ulong ushort dynamic",
     "true false null", "//", "/*", "*/", "\"'", false, true, false, true},
    {"java", Family::Code,
     "abstract assert break case catch class const continue default do else enum extends final finally for "
     "goto if implements import instanceof interface native new package private protected public record "
     "return sealed static strictfp super switch synchronized this throw throws transient try var void "
     "volatile while yield",
     "boolean byte char double float int long short String Object", "true false null", "//", "/*", "*/",
     "\"'", false, false, true, true},
    {"kt kotlin kts", Family::Code,
     "abstract annotation as break by catch class companion const constructor continue data do else enum "
     "external final finally for fun get if import in infix init inline inner interface internal is lateinit "
     "object open operator out override package private protected public reified return sealed set super "
     "suspend this throw try typealias val var vararg when where while",
     "Any Boolean Byte Char Double Float Int Long Nothing Short String Unit", "true false null", "//", "/*",
     "*/", "\"'", true, false, true, true},
    {"swift", Family::Code,
     "actor as associatedtype async await break case catch class continue default defer deinit do else enum "
     "extension fallthrough fileprivate final for func guard if import in init inout internal is lazy let "
     "mutating nonisolated open operator override private protocol public repeat rethrows return self Self "
     "some static struct subscript super switch throw throws try typealias var weak where while",
     "Any Bool Character Double Float Int String Void", "true false nil", "//", "/*", "*/", "\"", true,
     false, true, true},
    {"js javascript jsx mjs cjs ts typescript tsx", Family::Code,
     "abstract as async await break case catch class const constructor continue debugger declare default "
     "delete do else enum export extends finally for from function get if implements import in instanceof "
     "interface keyof let namespace new of private protected public readonly return satisfies set static "
     "super switch this throw try type typeof var void while with yield",
     "any boolean never number object string symbol unknown bigint console window document Math JSON Promise",
     "true false null undefined NaN Infinity", "//", "/*", "*/", "\"'`", false, false, true, true},
    {"dart", Family::Code,
     "abstract as assert async await break case catch class const continue covariant default deferred do "
     "dynamic else enum export extends extension external factory final finally for get if implements import "
     "in interface is late library mixin new on operator part required rethrow return set show static super "
     "switch sync this throw try typedef var void while with yield",
     "bool double int num String List Map Set Object", "true false null", "//", "/*", "*/", "\"'", true,
     false, true, true},
    {"py python python3 py3 gyp pyw", Family::Code,
     "and as assert async await break class continue def del elif else except finally for from global if "
     "import in is lambda match case nonlocal not or pass raise return try while with yield",
     "int float str bool bytes list dict set tuple object type print len range open super isinstance "
     "enumerate zip map filter sorted min max sum abs self cls",
     "True False None", "#", nullptr, nullptr, "\"'", true, false, true, true},
    {"rs rust", Family::Code,
     "as async await break const continue crate dyn else enum extern fn for if impl in let loop match mod "
     "move mut pub ref return self Self static struct super trait type unsafe use where while",
     "i8 i16 i32 i64 i128 isize u8 u16 u32 u64 u128 usize f32 f64 bool char str String Vec Option Result Box",
     "true false None Some Ok Err", "//", "/*", "*/", "\"", false, true, false, true, false, false, true},
    {"go golang", Family::Code,
     "break case chan const continue default defer else fallthrough for func go goto if import interface "
     "map package range return select struct switch type var",
     "bool byte complex64 complex128 error float32 float64 int int8 int16 int32 int64 rune string uint "
     "uint8 uint16 uint32 uint64 uintptr any make len cap append new panic",
     "true false nil iota", "//", "/*", "*/", "\"'`"},
    {"lua luau", Family::Code,
     "and break do else elseif end for function goto if in local not or repeat return then until while",
     "print pairs ipairs table string math type tostring tonumber require game workspace", "true false nil",
     "--", "--[[", "]]", "\"'"},
    {"php", Family::Code,
     "abstract and as break case catch class clone const continue declare default do echo else elseif "
     "empty enddeclare endfor endforeach endif endswitch endwhile extends final finally fn for foreach "
     "function global goto if implements include include_once instanceof insteadof interface isset list "
     "match namespace new or print private protected public readonly require require_once return static "
     "switch throw trait try unset use var while xor yield",
     "array bool float int string object mixed void", "true false null TRUE FALSE NULL", "// #", "/*", "*/",
     "\"'", false, false, false, true, false, true},
    {"rb ruby", Family::Code,
     "alias and begin break case class def defined? do else elsif end ensure for if in module next not or "
     "redo rescue retry return self super then undef unless until when while yield require attr_accessor "
     "attr_reader puts",
     "", "true false nil", "#", nullptr, nullptr, "\"'`", false, false, true, true},
    {"sh bash shell zsh fish console shellsession", Family::Code,
     "if then else elif fi for while until do done case esac function in return break continue export "
     "local readonly declare unset shift source alias",
     "echo printf cd ls grep sed awk cat cp mv rm mkdir chmod chown curl wget sudo git make test read exit "
     "eval exec set", "true false", "#", nullptr, nullptr, "\"'`", false, false, false, false, false, true},
    {"sql mysql postgres postgresql pgsql sqlite plsql tsql", Family::Code,
     "select from where and or not insert into values update set delete create table drop alter add column "
     "primary key foreign references index view join inner left right outer full on as group by order "
     "having limit offset union all distinct case when then else end exists in between like is begin commit "
     "rollback transaction returning with default constraint unique if asc desc",
     "int integer bigint smallint text varchar char boolean bool date timestamp float real numeric decimal "
     "serial blob json jsonb count sum avg min max coalesce now",
     "true false null", "--", "/*", "*/", "'\"`", false, false, false, false, true},
    {"diff patch", Family::Diff},
    {"json jsonc json5", Family::Json, "", "", "true false null", "//", "/*", "*/"},
    {"html htm xml svg xhtml vue plist", Family::Markup},
    {"css scss sass less", Family::Css, "", "", "", "//", "/*", "*/"},
    {"yaml yml toml ini cfg conf properties env dotenv", Family::Config, "", "",
     "true false null yes no on off True False", "#"},
};

bool listHas(const char *list, std::string_view word, bool ci = false)
{
    const char *p = list;
    while (*p) {
        while (*p == ' ')
            p++;
        const char *s = p;
        while (*p && *p != ' ')
            p++;
        size_t n = p - s;
        if (n == word.size()) {
            bool eq = true;
            for (size_t i = 0; i < n && eq; i++)
                eq = ci ? std::tolower((unsigned char)s[i]) == std::tolower((unsigned char)word[i]) : s[i] == word[i];
            if (eq)
                return true;
        }
    }
    return false;
}

const Lang *findLang(std::string_view name)
{
    std::string lower(name);
    for (auto &c : lower)
        c = (char)std::tolower((unsigned char)c);
    if (lower.empty())
        return nullptr;
    for (const Lang &l : kLangs)
        if (listHas(l.names, lower))
            return &l;
    return nullptr;
}

bool identStart(char c) { return std::isalpha((unsigned char)c) || c == '_' || ((unsigned char)c & 0x80); }
bool identChar(char c) { return std::isalnum((unsigned char)c) || c == '_' || ((unsigned char)c & 0x80); }

struct Out {
    std::vector<uint8_t> &k;
    void set(size_t from, size_t to, Kind kind)
    {
        for (size_t i = from; i < to && i < k.size(); i++)
            k[i] = kind;
    }
};

bool at(std::string_view s, size_t i, const char *p) { return p && s.compare(i, std::strlen(p), p) == 0; }

// A quoted string from i (s[i] is the quote); returns the end (exclusive).
size_t stringEnd(std::string_view s, size_t i, bool multiline)
{
    char q = s[i];
    size_t k = i + 1;
    while (k < s.size()) {
        if (s[k] == '\\') {
            k += 2;
            continue;
        }
        if (s[k] == q)
            return k + 1;
        if (s[k] == '\n' && !multiline)
            return k;
        k++;
    }
    return s.size();
}

size_t numberEnd(std::string_view s, size_t i)
{
    size_t k = i;
    if (s[k] == '0' && k + 1 < s.size() && std::strchr("xXbBoO", s[k + 1]))
        k += 2;
    while (k < s.size()) {
        char c = s[k];
        if (std::isalnum((unsigned char)c) || c == '_' || c == '.') {
            if (c == '.' && (k + 1 >= s.size() || !std::isdigit((unsigned char)s[k + 1])))
                break;  // 1..2 or a method call
            k++;
        } else if ((c == '+' || c == '-') && (s[k - 1] == 'e' || s[k - 1] == 'E') &&
                   !(s[i] == '0' && k > i + 1 && (s[i + 1] == 'x' || s[i + 1] == 'X'))) {
            k++;
        } else {
            break;
        }
    }
    return k;
}

size_t lineEnd(std::string_view s, size_t i)
{
    size_t nl = s.find('\n', i);
    return nl == std::string_view::npos ? s.size() : nl;
}

bool atLineStart(std::string_view s, size_t i)
{
    while (i > 0 && (s[i - 1] == ' ' || s[i - 1] == '\t'))
        i--;
    return i == 0 || s[i - 1] == '\n';
}

void highlightCode(std::string_view s, const Lang &L, Out &o)
{
    std::string comments = L.line_comments;
    std::vector<std::string> lc;
    for (size_t p = 0; p < comments.size();) {
        size_t sp = comments.find(' ', p);
        if (sp == std::string::npos)
            sp = comments.size();
        if (sp > p)
            lc.push_back(comments.substr(p, sp - p));
        p = sp + 1;
    }
    Kind next_name = Plain;  // after def/fn/class...: the name being defined
    size_t i = 0;
    while (i < s.size()) {
        char c = s[i];
        // Block comments before line comments (Lua's --[[ starts with --).
        if (L.block_open && at(s, i, L.block_open)) {
            size_t end = s.find(L.block_close, i + std::strlen(L.block_open));
            end = end == std::string_view::npos ? s.size() : end + std::strlen(L.block_close);
            o.set(i, end, Comment);
            i = end;
            continue;
        }
        bool comment = false;
        for (const auto &m : lc)
            if (at(s, i, m.c_str()) && (m != "#" || !L.dollar_vars || i == 0 || std::isspace((unsigned char)s[i - 1]))) {
                comment = true;
                break;
            }
        if (comment) {
            size_t end = lineEnd(s, i);
            o.set(i, end, Comment);
            i = end;
            continue;
        }
        if (L.hash_meta && c == '#' && atLineStart(s, i)) {
            size_t end = lineEnd(s, i);
            o.set(i, end, Meta);
            i = end;
            continue;
        }
        if (L.triple && (at(s, i, "\"\"\"") || at(s, i, "'''"))) {
            std::string q(3, c);
            size_t end = s.find(q, i + 3);
            end = end == std::string_view::npos ? s.size() : end + 3;
            o.set(i, end, String);
            i = end;
            continue;
        }
        if (std::strchr(L.quotes, c) && c) {
            size_t end = stringEnd(s, i, c == '`');
            // A lone ' with no partner on the line (Rust lifetimes, English).
            if (c == '\'' && (end > s.size() || end == lineEnd(s, i)) && (end == s.size() || s[end - 1] != '\'')) {
                i++;
                continue;
            }
            o.set(i, end, String);
            i = end;
            continue;
        }
        if (L.at_meta && c == '@' && i + 1 < s.size() && identStart(s[i + 1])) {
            size_t k = i + 1;
            while (k < s.size() && (identChar(s[k]) || s[k] == '.'))
                k++;
            o.set(i, k, Meta);
            i = k;
            continue;
        }
        if (L.dollar_vars && c == '$' && i + 1 < s.size()) {
            size_t k = i + 1;
            if (s[k] == '{') {
                size_t end = s.find('}', k);
                k = end == std::string_view::npos ? s.size() : end + 1;
            } else if (identStart(s[k])) {
                while (k < s.size() && identChar(s[k]))
                    k++;
            } else if (std::isdigit((unsigned char)s[k]) || std::strchr("@#?*!$-", s[k])) {
                k++;
            }
            if (k > i + 1) {
                o.set(i, k, Variable);
                i = k;
                continue;
            }
        }
        if (std::isdigit((unsigned char)c) && (i == 0 || !identChar(s[i - 1]))) {
            size_t end = numberEnd(s, i);
            o.set(i, end, Number);
            i = end;
            continue;
        }
        if (identStart(c)) {
            size_t k = i;
            while (k < s.size() && identChar(s[k]))
                k++;
            if (L.names[0] == 'r' && L.names[1] == 'b' && k < s.size() && (s[k] == '?' || s[k] == '!'))
                k++;  // Ruby predicate/bang methods
            std::string_view word = s.substr(i, k - i);
            size_t after = k;
            while (after < s.size() && (s[after] == ' ' || s[after] == '\t'))
                after++;
            Kind kind = Plain;
            bool member = i > 0 && s[i - 1] == '.';
            if (!member && listHas(L.keywords, word, L.ci)) {
                kind = Keyword;
                if (word == "def" || word == "fn" || word == "func" || word == "function" || word == "fun")
                    next_name = Function;
                else if (word == "class" || word == "struct" || word == "enum" || word == "interface" ||
                         word == "trait" || word == "type" || word == "record" || word == "protocol" ||
                         word == "union" || word == "module")
                    next_name = Type;
                o.set(i, k, kind);
                i = k;
                continue;
            }
            if (listHas(L.literals, word, L.ci))
                kind = Literal;
            else if (L.bang_macros && k < s.size() && s[k] == '!')
                kind = Meta, k++;
            else if (next_name != Plain)
                kind = next_name;
            else if (after < s.size() && s[after] == '(')
                kind = Function;
            else if (!member && listHas(L.types, word, L.ci))
                kind = Type;
            else if (L.upper_types && std::isupper((unsigned char)word[0]) && word.size() > 1 &&
                     std::any_of(word.begin(), word.end(), [](char ch) { return std::islower((unsigned char)ch); }))
                kind = Type;
            next_name = Plain;
            o.set(i, k, kind);
            i = k;
            continue;
        }
        if (!std::isspace((unsigned char)c))
            next_name = (c == '*' || c == '&') ? next_name : Plain;
        i++;
    }
}

void highlightDiff(std::string_view s, Out &o)
{
    for (size_t i = 0; i < s.size();) {
        size_t end = lineEnd(s, i);
        std::string_view line = s.substr(i, end - i);
        Kind k = Plain;
        if (line.rfind("+++", 0) == 0 || line.rfind("---", 0) == 0 || line.rfind("@@", 0) == 0 ||
            line.rfind("diff ", 0) == 0 || line.rfind("index ", 0) == 0)
            k = Meta;
        else if (!line.empty() && line[0] == '+')
            k = Inserted;
        else if (!line.empty() && line[0] == '-')
            k = Deleted;
        o.set(i, end, k);
        i = end + 1;
    }
}

void highlightJson(std::string_view s, const Lang &L, Out &o)
{
    size_t i = 0;
    while (i < s.size()) {
        char c = s[i];
        if (at(s, i, "//")) {
            size_t end = lineEnd(s, i);
            o.set(i, end, Comment);
            i = end;
        } else if (at(s, i, "/*")) {
            size_t end = s.find("*/", i + 2);
            end = end == std::string_view::npos ? s.size() : end + 2;
            o.set(i, end, Comment);
            i = end;
        } else if (c == '"' || c == '\'') {
            size_t end = stringEnd(s, i, false), k = end;
            while (k < s.size() && std::isspace((unsigned char)s[k]))
                k++;
            o.set(i, end, k < s.size() && s[k] == ':' ? Attr : String);
            i = end;
        } else if (std::isdigit((unsigned char)c) || (c == '-' && i + 1 < s.size() && std::isdigit((unsigned char)s[i + 1]))) {
            size_t end = numberEnd(s, c == '-' ? i + 1 : i);
            o.set(i, end, Number);
            i = end;
        } else if (identStart(c)) {
            size_t k = i;
            while (k < s.size() && identChar(s[k]))
                k++;
            if (listHas(L.literals, s.substr(i, k - i)))
                o.set(i, k, Literal);
            i = k;
        } else {
            i++;
        }
    }
}

void highlightMarkup(std::string_view s, Out &o)
{
    size_t i = 0;
    while (i < s.size()) {
        if (at(s, i, "<!--")) {
            size_t end = s.find("-->", i + 4);
            end = end == std::string_view::npos ? s.size() : end + 3;
            o.set(i, end, Comment);
            i = end;
            continue;
        }
        if (s[i] == '&') {
            size_t semi = s.find(';', i);
            if (semi != std::string_view::npos && semi - i < 10 && semi > i + 1) {
                o.set(i, semi + 1, Literal);
                i = semi + 1;
                continue;
            }
        }
        if (s[i] == '<' && i + 1 < s.size() && (identStart(s[i + 1]) || s[i + 1] == '/' || s[i + 1] == '!' || s[i + 1] == '?')) {
            bool meta = s[i + 1] == '!' || s[i + 1] == '?';
            size_t k = i + 1;
            if (s[k] == '/' || meta)
                k++;
            while (k < s.size() && (identChar(s[k]) || s[k] == '-' || s[k] == ':' || s[k] == '.'))
                k++;
            o.set(i, k, meta ? Meta : Tag);
            // Attributes until the tag closes.
            while (k < s.size() && s[k] != '>') {
                if (s[k] == '"' || s[k] == '\'') {
                    size_t end = stringEnd(s, k, true);
                    o.set(k, end, String);
                    k = end;
                } else if (identStart(s[k])) {
                    size_t a = k;
                    while (k < s.size() && (identChar(s[k]) || s[k] == '-' || s[k] == ':' || s[k] == '@'))
                        k++;
                    o.set(a, k, meta ? Meta : Attr);
                } else if (s[k] == '/' || s[k] == '?') {
                    o.set(k, k + 1, meta ? Meta : Tag);
                    k++;
                } else {
                    k++;
                }
            }
            if (k < s.size()) {
                o.set(k, k + 1, meta ? Meta : Tag);
                k++;
            }
            i = k;
            continue;
        }
        i++;
    }
}

void highlightCss(std::string_view s, Out &o)
{
    int depth = 0;
    size_t i = 0;
    while (i < s.size()) {
        char c = s[i];
        if (at(s, i, "/*")) {
            size_t end = s.find("*/", i + 2);
            end = end == std::string_view::npos ? s.size() : end + 2;
            o.set(i, end, Comment);
            i = end;
        } else if (at(s, i, "//") && (i == 0 || s[i - 1] != ':')) {
            size_t end = lineEnd(s, i);
            o.set(i, end, Comment);
            i = end;
        } else if (c == '"' || c == '\'') {
            size_t end = stringEnd(s, i, false);
            o.set(i, end, String);
            i = end;
        } else if (c == '{') {
            depth++, i++;
        } else if (c == '}') {
            depth = std::max(0, depth - 1), i++;
        } else if (c == '@' || (c == '$' && i + 1 < s.size() && identStart(s[i + 1]))) {
            size_t k = i + 1;
            while (k < s.size() && (identChar(s[k]) || s[k] == '-'))
                k++;
            o.set(i, k, c == '@' ? Keyword : Variable);
            i = k;
        } else if (c == '#' && depth > 0) {
            size_t k = i + 1;
            while (k < s.size() && std::isxdigit((unsigned char)s[k]))
                k++;
            o.set(i, k, Number);
            i = k;
        } else if ((std::isdigit((unsigned char)c) || (c == '.' && i + 1 < s.size() && std::isdigit((unsigned char)s[i + 1]))) &&
                   (i == 0 || !identChar(s[i - 1]))) {
            size_t k = i;
            while (k < s.size() && (std::isalnum((unsigned char)s[k]) || s[k] == '.' || s[k] == '%'))
                k++;
            o.set(i, k, Number);
            i = k;
        } else if (identStart(c) || c == '-' || ((c == '.' || c == '#' || c == ':') && depth == 0)) {
            size_t k = i + 1;
            while (k < s.size() && (identChar(s[k]) || s[k] == '-'))
                k++;
            size_t after = k;
            while (after < s.size() && s[after] == ' ')
                after++;
            Kind kind = Plain;
            if (depth > 0 && after < s.size() && s[after] == ':' && (c != ':'))
                kind = Attr;  // property
            else if (depth > 0 && after < s.size() && s[after] == '(')
                kind = Function;
            else if (depth == 0 || (after < s.size() && s[after] == '{'))
                kind = c == '.' || c == '#' ? Type : c == ':' ? Keyword : Tag;  // selectors
            else if (identStart(c))
                kind = Literal;  // values like bold, flex
            o.set(i, k, kind);
            i = k;
        } else {
            i++;
        }
    }
}

void highlightConfig(std::string_view s, const Lang &L, Out &o)
{
    for (size_t i = 0; i < s.size();) {
        size_t end = lineEnd(s, i);
        size_t k = i;
        while (k < end && (s[k] == ' ' || s[k] == '\t'))
            k++;
        if (k < end && (s[k] == '#' || s[k] == ';')) {
            o.set(k, end, Comment);
        } else if (k < end && s[k] == '[') {
            o.set(k, end, Type);  // [section]
        } else if (k < end) {
            if (s[k] == '-' && k + 1 < end && s[k + 1] == ' ')
                k += 2;  // YAML list item
            // key: value / key = value
            size_t sep = k;
            while (sep < end && s[sep] != ':' && s[sep] != '=' && s[sep] != '#')
                sep++;
            size_t v = k;
            if (sep < end && s[sep] != '#' && sep > k) {
                o.set(k, sep, Attr);
                v = sep + 1;
            }
            // The value: string, number, literal, trailing comment.
            while (v < end) {
                while (v < end && s[v] == ' ')
                    v++;
                if (v >= end)
                    break;
                if (s[v] == '#') {
                    o.set(v, end, Comment);
                    break;
                }
                if (s[v] == '"' || s[v] == '\'') {
                    size_t e = std::min(stringEnd(s, v, false), end);
                    o.set(v, e, String);
                    v = e;
                    continue;
                }
                size_t w = v;
                while (w < end && s[w] != ' ' && s[w] != ',' && s[w] != ']' && s[w] != '}')
                    w++;
                std::string_view word = s.substr(v, w - v);
                bool num = !word.empty() && (std::isdigit((unsigned char)word[0]) ||
                                             (word[0] == '-' && word.size() > 1 && std::isdigit((unsigned char)word[1])));
                if (num)
                    o.set(v, w, Number);
                else if (listHas(L.literals, word))
                    o.set(v, w, Literal);
                else if (sep < end)
                    o.set(v, w, String);  // unquoted YAML/INI values
                v = w == v ? v + 1 : w;
            }
        }
        i = end + 1;
    }
}

}  // namespace

bool supported(std::string_view lang) { return findLang(lang) != nullptr; }

std::vector<uint8_t> highlight(std::string_view code, std::string_view lang)
{
    const Lang *L = findLang(lang);
    if (!L)
        return {};
    std::vector<uint8_t> kinds(code.size(), Plain);
    Out o{kinds};
    switch (L->family) {
    case Family::Code: highlightCode(code, *L, o); break;
    case Family::Diff: highlightDiff(code, o); break;
    case Family::Json: highlightJson(code, *L, o); break;
    case Family::Markup: highlightMarkup(code, o); break;
    case Family::Css: highlightCss(code, o); break;
    case Family::Config: highlightConfig(code, *L, o); break;
    }
    return kinds;
}

}  // namespace discord::syntax
