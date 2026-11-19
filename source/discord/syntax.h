// SPDX-License-Identifier: MIT
//
// Syntax colouring for code blocks (```lang). A small tokenizer per language
// family rather than a full parser: keywords, types, strings, numbers,
// comments, function calls, and the special shapes of diff, JSON, markup,
// CSS and config files. Like Discord, code blocks without a language (or
// with one we don't know) stay plain. Platform-free.
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace discord::syntax {

enum Kind : uint8_t {
    Plain,
    Keyword,
    Type,      // types, classes, built-ins
    Literal,   // true/false/null...
    String,
    Number,
    Comment,
    Function,  // a name being called or defined
    Meta,      // preprocessor, decorators, annotations
    Tag,       // markup tags
    Attr,      // markup attributes, JSON/YAML/CSS keys
    Variable,  // shell $vars
    Inserted,  // diff +
    Deleted,   // diff -
    kKindCount
};

// Whether the language name (or alias: "js", "py", "c++"...) is known.
bool supported(std::string_view lang);
// The kind of each byte of code; empty when the language isn't supported.
std::vector<uint8_t> highlight(std::string_view code, std::string_view lang);

}  // namespace discord::syntax
