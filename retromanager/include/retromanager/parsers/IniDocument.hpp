#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "retromanager/core/Result.hpp"

namespace rm {

// Lossless editor for .ini files with [sections] and key=value lines
// (sys-clk's config.ini, and later other homebrew configs).
//
// Same contract as CfgDocument: the file is kept line by line and only the
// value of an edited key changes; comments (';' or '#'), blank lines,
// spacing, CRLF, a BOM and a missing final newline are preserved, and
// serialize() of an unmodified document returns the input unchanged.
//
// Section names compare case-insensitively (sys-clk title ids are hex);
// keys are case-sensitive. Keys before the first header belong to section "".
class IniDocument {
  public:
    static IniDocument parse(std::string_view text);
    std::string serialize() const;

    std::optional<std::string> get(std::string_view section, std::string_view key) const;
    bool hasSection(std::string_view section) const;
    std::vector<std::string> sections() const;  // in file order, as written

    // Updates the key in every occurrence of the section that has it; else
    // inserts it after the last key of the first occurrence of the section;
    // else appends a new "[section]" block at the end. New lines reuse the
    // file's own "key=value" / "key = value" style. InvalidArgument for
    // empty keys/sections, keys containing '=' ';' '#' '[' or blanks, and
    // values or sections containing line breaks or brackets.
    Status set(std::string_view section, std::string_view key, std::string_view value);

    bool modified() const { return modified_; }

  private:
    struct Line {
        std::string text;
        std::string eol;
        std::string section;  // section this line belongs to (for headers: their own)
        bool isHeader = false;
        bool isAssignment = false;
        std::string key;
        std::size_t valueBegin = 0;
        std::size_t valueEnd = 0;
    };

    static Line parseLine(std::string text, std::string eol, const std::string& currentSection);
    std::string defaultEol() const;
    std::string separatorStyle() const;

    std::string bom_;
    std::vector<Line> lines_;
    bool modified_ = false;
};

}  // namespace rm
