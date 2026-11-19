// SPDX-License-Identifier: MIT
#include "discord/markdown.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace discord::md {

namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
bool isWordChar(char c) { return std::isalnum((unsigned char)c) || ((unsigned char)c & 0x80); }
bool isPunct(char c) { return std::ispunct((unsigned char)c); }

struct Ctx {
    std::vector<Span> &out;
    int &spoilers;
};

void emit(Ctx &ctx, std::string_view text, uint16_t style, int spoiler, const std::string &url)
{
    if (text.empty())
        return;
    // Merge with the previous span when nothing differs.
    if (!ctx.out.empty()) {
        Span &last = ctx.out.back();
        if (last.style == style && last.spoiler == spoiler && last.url == url && !(style & Code)) {
            last.text.append(text);
            return;
        }
    }
    Span s;
    s.text = std::string(text);
    s.style = style;
    s.spoiler = spoiler;
    s.url = url;
    ctx.out.push_back(std::move(s));
}

// Length of the run of c starting at i.
size_t runLength(std::string_view s, size_t i, char c)
{
    size_t n = 0;
    while (i + n < s.size() && s[i + n] == c)
        n++;
    return n;
}

// Finds the closing delimiter (n copies of c) for content starting at from,
// with at least one character of content. When the closer is part of a
// longer run, the run's last n characters close it (so ***a*** is bold
// around *a*). npos when there is none.
size_t findCloser(std::string_view s, size_t from, char c, size_t n)
{
    for (size_t k = from + 1; k < s.size(); k++) {
        if (s[k] == '\\') {
            k++;
            continue;
        }
        if (s[k] == '`') {
            // Skip inline code: delimiters inside it don't count.
            size_t r = runLength(s, k, '`');
            size_t end = s.find(std::string(r, '`'), k + r);
            if (end != std::string_view::npos) {
                k = end + r - 1;
                continue;
            }
        }
        if (s[k] != c)
            continue;
        size_t r = runLength(s, k, c);
        if (r < n) {
            k += r - 1;
            continue;
        }
        size_t close = k + r - n;
        if (close > from)
            return close;
        k += r - 1;
    }
    return std::string_view::npos;
}

// A Discord token like <@123>, <:name:id>, <t:123:R> or </cmd:id>, kept whole.
size_t tokenLength(std::string_view s, size_t i)
{
    if (s[i] != '<' || i + 2 >= s.size())
        return 0;
    char c = s[i + 1];
    bool known = c == '@' || c == '#' || c == ':' || c == '/' ||
                 (c == 'a' && s[i + 2] == ':') || (c == 't' && s[i + 2] == ':');
    if (!known)
        return 0;
    for (size_t k = i + 2; k < s.size() && k < i + 120; k++) {
        if (s[k] == '>')
            return k + 1 - i;
        if (s[k] == '<' || s[k] == '\n')
            return 0;
    }
    return 0;
}

size_t urlLength(std::string_view s, size_t i)
{
    std::string_view rest = s.substr(i);
    size_t scheme = rest.compare(0, 8, "https://") == 0 ? 8 : rest.compare(0, 7, "http://") == 0 ? 7 : 0;
    if (!scheme)
        return 0;
    size_t n = scheme;
    while (n < rest.size() && !isSpace(rest[n]) && rest[n] != '<' && rest[n] != '>')
        n++;
    // Trailing punctuation belongs to the sentence, not the link (but keep a
    // closing parenthesis that has an opening one in the URL).
    while (n > scheme) {
        char c = rest[n - 1];
        if (c == '.' || c == ',' || c == '!' || c == '?' || c == ':' || c == ';' || c == '"' || c == '\'') {
            n--;
        } else if (c == ')') {
            size_t open = 0, close = 0;
            for (size_t k = 0; k < n; k++)
                open += rest[k] == '(', close += rest[k] == ')';
            if (close > open)
                n--;
            else
                break;
        } else {
            break;
        }
    }
    return n > scheme ? n : 0;
}

void parseRange(Ctx &ctx, std::string_view s, uint16_t style, int spoiler, const std::string &url)
{
    std::string lit;
    auto flush = [&]() {
        emit(ctx, lit, style, spoiler, url);
        lit.clear();
    };
    size_t i = 0;
    while (i < s.size()) {
        char c = s[i];
        // Escapes.
        if (c == '\\' && i + 1 < s.size() && isPunct(s[i + 1])) {
            lit += s[i + 1];
            i += 2;
            continue;
        }
        // Discord tokens stay whole.
        if (size_t t = tokenLength(s, i)) {
            lit.append(s.substr(i, t));
            i += t;
            continue;
        }
        // Inline code.
        if (c == '`') {
            size_t n = runLength(s, i, '`');
            size_t end = s.find(std::string(n, '`'), i + n);
            // The closer must be a run of exactly n.
            while (end != std::string_view::npos && runLength(s, end, '`') != n)
                end = s.find(std::string(n, '`'), end + runLength(s, end, '`'));
            if (end != std::string_view::npos && end > i + n) {
                std::string_view code = s.substr(i + n, end - i - n);
                if (code.size() >= 2 && code.front() == ' ' && code.back() == ' ' &&
                    code.find_first_not_of(' ') != std::string_view::npos)
                    code = code.substr(1, code.size() - 2);
                flush();
                emit(ctx, code, style | Code, spoiler, url);
                i = end + n;
                continue;
            }
            lit.append(s.substr(i, n));
            i += n;
            continue;
        }
        // Links: <https://...>, [text](https://...), bare URLs.
        if (c == '<' && !(style & Link)) {
            if (size_t n = urlLength(s, i + 1); n && i + 1 + n < s.size() && s[i + 1 + n] == '>') {
                flush();
                std::string u(s.substr(i + 1, n));
                emit(ctx, u, style | Link, spoiler, u);
                i += n + 2;
                continue;
            }
        }
        if (c == '[' && !(style & Link)) {
            size_t close = s.find("](", i + 1);
            if (close != std::string_view::npos && close > i + 1) {
                size_t n = urlLength(s, close + 2);
                if (n && close + 2 + n < s.size() && s[close + 2 + n] == ')' &&
                    s.substr(i + 1, close - i - 1).find('\n') == std::string_view::npos) {
                    flush();
                    std::string u(s.substr(close + 2, n));
                    parseRange(ctx, s.substr(i + 1, close - i - 1), style | Link, spoiler, u);
                    i = close + 3 + n;
                    continue;
                }
            }
        }
        if ((c == 'h') && !(style & Link) && (i == 0 || !isWordChar(s[i - 1]))) {
            if (size_t n = urlLength(s, i)) {
                flush();
                std::string u(s.substr(i, n));
                emit(ctx, u, style | Link, spoiler, u);
                i += n;
                continue;
            }
        }
        // Emphasis and friends.
        if (c == '*' || c == '_' || c == '~' || c == '|') {
            size_t r = runLength(s, i, c);
            bool done = false;
            // Two-character delimiters first: ** __ ~~ ||.
            if (r >= 2) {
                size_t close = findCloser(s, i + 2, c, 2);
                if (close != std::string_view::npos && close > i + 2) {
                    uint16_t add = c == '*' ? Bold : c == '_' ? Underline : c == '~' ? Strike : Spoiler;
                    flush();
                    int sp = spoiler;
                    if (add == Spoiler && !(style & Spoiler))
                        sp = ctx.spoilers++;
                    parseRange(ctx, s.substr(i + 2, close - i - 2), style | add, sp, url);
                    i = close + 2;
                    done = true;
                }
            }
            // Single * or _: italic.
            if (!done && (c == '*' || c == '_') && i + 1 < s.size() && !isSpace(s[i + 1])) {
                bool open_ok = c == '*' || i == 0 || !isWordChar(s[i - 1]);
                size_t close = open_ok ? findCloser(s, i + 1, c, 1) : std::string_view::npos;
                // A closing _ can't be inside a word (snake_case stays as is),
                // and the closer can't follow a space.
                while (close != std::string_view::npos &&
                       (isSpace(s[close - 1]) ||
                        (c == '_' && close + 1 < s.size() && isWordChar(s[close + 1]))))
                    close = findCloser(s, close, c, 1);
                if (close != std::string_view::npos) {
                    flush();
                    parseRange(ctx, s.substr(i + 1, close - i - 1), style | Italic, spoiler, url);
                    i = close + 1;
                    done = true;
                }
            }
            if (done)
                continue;
            lit.append(s.substr(i, r));
            i += r;
            continue;
        }
        lit += c;
        i++;
    }
    flush();
}

bool startsWith(std::string_view s, std::string_view p) { return s.compare(0, p.size(), p) == 0; }

void addTextLine(Document &doc, std::string_view line, bool &quote_rest)
{
    Line out;
    std::string_view rest = line;
    if (quote_rest) {
        out.quote = true;
    } else if (startsWith(rest, ">>> ")) {
        quote_rest = true;
        out.quote = true;
        rest.remove_prefix(4);
    } else if (startsWith(rest, "> ")) {
        out.quote = true;
        rest.remove_prefix(2);
    } else if (rest == ">") {
        out.quote = true;
        rest = {};
    }

    size_t spaces = 0;
    while (spaces < rest.size() && rest[spaces] == ' ')
        spaces++;
    std::string_view body = rest.substr(spaces);
    if (spaces == 0 && startsWith(body, "-# ") && body.size() > 3) {
        out.kind = LineKind::Subtext;
        rest = body.substr(3);
    } else if (spaces == 0 && body.size() > 2 && body[0] == '#') {
        size_t h = runLength(body, 0, '#');
        if (h <= 3 && h < body.size() && body[h] == ' ' && body.find_first_not_of(' ', h) != std::string_view::npos) {
            out.kind = h == 1 ? LineKind::Heading1 : h == 2 ? LineKind::Heading2 : LineKind::Heading3;
            rest = body.substr(h + 1);
        }
    } else if (body.size() > 2 && (body[0] == '-' || body[0] == '*') && body[1] == ' ') {
        out.kind = LineKind::ListItem;
        out.bullet = "\xE2\x80\xA2";  // U+2022
        out.indent = (int)std::min<size_t>(2, spaces / 2);
        rest = body.substr(2);
    } else if (!body.empty() && std::isdigit((unsigned char)body[0])) {
        size_t d = 0;
        while (d < body.size() && d < 4 && std::isdigit((unsigned char)body[d]))
            d++;
        if (d < 4 && d + 1 < body.size() && body[d] == '.' && body[d + 1] == ' ') {
            out.kind = LineKind::ListItem;
            out.bullet = std::string(body.substr(0, d + 1));
            out.indent = (int)std::min<size_t>(2, spaces / 2);
            rest = body.substr(d + 2);
        }
    }
    parseInline(rest, out.spans, doc.spoilers);
    doc.lines.push_back(std::move(out));
}

void addText(Document &doc, std::string_view text, bool &quote_rest)
{
    size_t start = 0;
    while (start <= text.size()) {
        size_t nl = text.find('\n', start);
        std::string_view line = text.substr(start, nl == std::string_view::npos ? std::string_view::npos : nl - start);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        addTextLine(doc, line, quote_rest);
        if (nl == std::string_view::npos)
            break;
        start = nl + 1;
    }
}

bool isLang(std::string_view s)
{
    if (s.empty() || s.size() > 32)
        return false;
    for (char c : s)
        if (!(std::isalnum((unsigned char)c) || c == '_' || c == '+' || c == '-' || c == '#' || c == '.'))
            return false;
    return true;
}

// The next ``` that isn't escaped, from i; npos if none.
size_t findFence(std::string_view s, size_t i)
{
    while ((i = s.find("```", i)) != std::string_view::npos) {
        size_t bs = 0;
        while (bs < i && s[i - 1 - bs] == '\\')
            bs++;
        if (bs % 2 == 0)
            return i;
        i += 3;
    }
    return std::string_view::npos;
}

}  // namespace

void parseInline(std::string_view text, std::vector<Span> &out, int &spoilers)
{
    Ctx ctx{out, spoilers};
    parseRange(ctx, text, 0, -1, {});
}

std::string plainText(std::string_view content)
{
    Document doc = parse(content);
    std::string out;
    for (const Line &line : doc.lines) {
        if (!out.empty() && out.back() != ' ')
            out += ' ';
        if (!line.bullet.empty())
            out += line.bullet + " ";
        for (const Span &s : line.spans) {
            if (s.style & Spoiler) {
                if (out.size() < 3 || out.compare(out.size() - 3, 3, "\xE2\x96\x88") != 0)
                    out += "\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88";  // U+2588 x3
                continue;
            }
            for (char c : s.text)
                out += c == '\n' ? ' ' : c;
        }
    }
    return out;
}

Document parse(std::string_view content)
{
    Document doc;
    bool quote_rest = false;
    size_t pos = 0;
    while (pos <= content.size()) {
        size_t open = findFence(content, pos);
        size_t close = open == std::string_view::npos ? open : findFence(content, open + 3);
        if (close == std::string_view::npos) {
            if (pos < content.size() || doc.lines.empty())
                addText(doc, content.substr(pos), quote_rest);
            break;
        }
        // Text before the block, without the newline that ends it.
        std::string_view before = content.substr(pos, open - pos);
        if (!before.empty() && before.back() == '\n')
            before.remove_suffix(1);
        if (!before.empty())
            addText(doc, before, quote_rest);
        // The block: an optional language on the opening line.
        std::string_view inner = content.substr(open + 3, close - open - 3);
        Line block;
        block.kind = LineKind::CodeBlock;
        block.quote = quote_rest;
        size_t nl = inner.find('\n');
        if (nl != std::string_view::npos && isLang(inner.substr(0, nl)) && nl + 1 < inner.size()) {
            block.lang = std::string(inner.substr(0, nl));
            inner.remove_prefix(nl + 1);
        } else if (nl == 0) {
            inner.remove_prefix(1);
        }
        if (!inner.empty() && inner.back() == '\n')
            inner.remove_suffix(1);
        Span code;
        code.text = std::string(inner);
        code.style = Code;
        block.spans.push_back(std::move(code));
        doc.lines.push_back(std::move(block));
        pos = close + 3;
        // Skip the newline after the closing fence.
        if (pos < content.size() && content[pos] == '\n')
            pos++;
        if (pos >= content.size())
            break;
    }
    return doc;
}

}  // namespace discord::md
