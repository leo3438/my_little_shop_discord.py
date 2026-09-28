#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "retromanager/core/Result.hpp"

namespace rm {

// Lossless editor for RetroArch-style configuration files (retroarch.cfg,
// core overrides, .cht cheat files): lines of `key = "value"`.
//
// The file is kept line by line, exactly as read. Editing a key rewrites only
// the value part of the lines holding it; everything else (comments, blank
// lines, #include directives, spacing around '=', quoting style, CRLF line
// endings, a missing final newline, a UTF-8 BOM) is written back byte for
// byte. serialize() of an unmodified document returns the input unchanged.
//
// Parsing never fails: a line that is not `key = value` is kept verbatim and
// simply ignored by get()/set().
class CfgDocument {
  public:
    static CfgDocument parse(std::string_view text);

    std::string serialize() const;

    // Value of the first assignment of `key` (quotes removed).
    std::optional<std::string> get(std::string_view key) const;
    bool contains(std::string_view key) const { return get(key).has_value(); }

    // Updates every assignment of `key` in place (so the result is right
    // whatever duplicate-key rule the reader applies), or appends
    // `key = "value"` at the end when absent. InvalidArgument for keys that
    // are not [A-Za-z0-9_.-]+ and for values containing '"' or a line break,
    // which the format cannot represent.
    Status set(std::string_view key, std::string_view value);

    // Removes every assignment of `key`. Returns false when absent.
    bool remove(std::string_view key);

    // All keys, in file order, duplicates included.
    std::vector<std::string> keys() const;

    // True once set()/remove() changed something.
    bool modified() const { return modified_; }

  private:
    struct Line {
        std::string text;  // content without the line ending
        std::string eol;   // "\n", "\r\n" or "" (last line without newline)
        // For `key = value` lines: where the key and the value sit in `text`.
        bool isAssignment = false;
        std::string key;
        std::size_t valueBegin = 0;  // first char of the value (after the opening quote)
        std::size_t valueEnd = 0;    // one past its last char (before the closing quote)
        bool quoted = false;
    };

    static Line parseLine(std::string text, std::string eol);
    static bool isValidKey(std::string_view key);
    std::string defaultEol() const;

    std::string bom_;
    std::vector<Line> lines_;
    bool modified_ = false;
};

}  // namespace rm
