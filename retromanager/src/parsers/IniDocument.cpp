#include "retromanager/parsers/IniDocument.hpp"

#include <algorithm>
#include <cctype>

namespace rm {

namespace {

constexpr std::string_view kBom = "\xEF\xBB\xBF";

bool isBlank(char c) { return c == ' ' || c == '\t'; }

std::string lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool sameSection(std::string_view a, std::string_view b) { return lower(a) == lower(b); }

bool isValidKey(std::string_view key) {
    return !key.empty() && std::none_of(key.begin(), key.end(), [](char c) {
        return isBlank(c) || c == '=' || c == ';' || c == '#' || c == '[' || c == ']' || c == '\r' || c == '\n';
    });
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && isBlank(s.front())) s.remove_prefix(1);
    while (!s.empty() && isBlank(s.back())) s.remove_suffix(1);
    return s;
}

}  // namespace

IniDocument::Line IniDocument::parseLine(std::string text, std::string eol, const std::string& currentSection) {
    Line line;
    line.text = std::move(text);
    line.eol = std::move(eol);
    line.section = currentSection;
    const std::string& t = line.text;

    std::size_t pos = 0;
    while (pos < t.size() && isBlank(t[pos])) ++pos;
    if (pos == t.size() || t[pos] == ';' || t[pos] == '#') return line;

    if (t[pos] == '[') {
        std::size_t close = t.find(']', pos + 1);
        if (close == std::string::npos) return line;  // "[broken": kept as plain text
        line.isHeader = true;
        line.section = std::string(trim(std::string_view(t).substr(pos + 1, close - pos - 1)));
        return line;
    }

    std::size_t equals = t.find('=', pos);
    if (equals == std::string::npos) return line;
    std::string_view key = trim(std::string_view(t).substr(pos, equals - pos));
    if (!isValidKey(key)) return line;

    std::size_t valueBegin = equals + 1;
    while (valueBegin < t.size() && isBlank(t[valueBegin])) ++valueBegin;
    std::size_t valueEnd = t.size();
    while (valueEnd > valueBegin && isBlank(t[valueEnd - 1])) --valueEnd;

    line.isAssignment = true;
    line.key = std::string(key);
    line.valueBegin = valueBegin;
    line.valueEnd = valueEnd;
    return line;
}

IniDocument IniDocument::parse(std::string_view text) {
    IniDocument doc;
    if (text.substr(0, kBom.size()) == kBom) {
        doc.bom_ = std::string(kBom);
        text.remove_prefix(kBom.size());
    }
    std::string section;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t newline = text.find('\n', pos);
        std::string eol;
        std::size_t contentEnd;
        if (newline == std::string_view::npos) {
            contentEnd = text.size();
        } else {
            contentEnd = newline;
            eol = "\n";
            if (contentEnd > pos && text[contentEnd - 1] == '\r') {
                --contentEnd;
                eol = "\r\n";
            }
        }
        Line line = parseLine(std::string(text.substr(pos, contentEnd - pos)), eol, section);
        if (line.isHeader) section = line.section;
        doc.lines_.push_back(std::move(line));
        if (newline == std::string_view::npos) break;
        pos = newline + 1;
    }
    return doc;
}

std::string IniDocument::serialize() const {
    std::string out = bom_;
    for (const Line& line : lines_) out += line.text + line.eol;
    return out;
}

std::optional<std::string> IniDocument::get(std::string_view section, std::string_view key) const {
    for (const Line& line : lines_) {
        if (line.isAssignment && line.key == key && sameSection(line.section, section)) {
            return line.text.substr(line.valueBegin, line.valueEnd - line.valueBegin);
        }
    }
    return std::nullopt;
}

bool IniDocument::hasSection(std::string_view section) const {
    if (section.empty()) return true;
    return std::any_of(lines_.begin(), lines_.end(),
                       [&](const Line& l) { return l.isHeader && sameSection(l.section, section); });
}

std::vector<std::string> IniDocument::sections() const {
    std::vector<std::string> out;
    for (const Line& line : lines_) {
        if (line.isHeader) out.push_back(line.section);
    }
    return out;
}

std::string IniDocument::defaultEol() const {
    for (const Line& line : lines_) {
        if (!line.eol.empty()) return line.eol;
    }
    return "\n";
}

std::string IniDocument::separatorStyle() const {
    for (const Line& line : lines_) {
        if (!line.isAssignment) continue;
        std::size_t keyEnd = line.text.find(line.key) + line.key.size();
        return line.text.substr(keyEnd, line.valueBegin - keyEnd);
    }
    return "=";
}

Status IniDocument::set(std::string_view section, std::string_view key, std::string_view value) {
    if (!isValidKey(key)) return makeError(ErrorCode::InvalidArgument, "invalid ini key \"" + std::string(key) + "\"");
    if (value.find_first_of("\r\n") != std::string_view::npos) {
        return makeError(ErrorCode::InvalidArgument, "ini values cannot contain line breaks");
    }
    if (section.empty() || section.find_first_of("[]\r\n") != std::string_view::npos) {
        return makeError(ErrorCode::InvalidArgument, "invalid ini section \"" + std::string(section) + "\"");
    }

    // 1. Update in place wherever the key already exists in this section.
    bool found = false;
    for (Line& line : lines_) {
        if (!line.isAssignment || line.key != key || !sameSection(line.section, section)) continue;
        found = true;
        if (std::string_view(line.text).substr(line.valueBegin, line.valueEnd - line.valueBegin) == value) continue;
        line.text.replace(line.valueBegin, line.valueEnd - line.valueBegin, value);
        line.valueEnd = line.valueBegin + value.size();
        modified_ = true;
    }
    if (found) return success();

    const std::string eol = defaultEol();
    auto makeAssignment = [&](const std::string& separator, const std::string& owner) {
        return parseLine(std::string(key) + separator + std::string(value), eol, owner);
    };

    // 2. Insert after the last key of the section's first occurrence.
    std::optional<std::size_t> header;
    for (std::size_t i = 0; i < lines_.size(); ++i) {
        if (lines_[i].isHeader && sameSection(lines_[i].section, section)) {
            header = i;
            break;
        }
    }
    if (header) {
        // No key yet: after the header and the comment lines glued to it.
        std::size_t insertAfter = *header;
        while (insertAfter + 1 < lines_.size() && !lines_[insertAfter + 1].isHeader &&
               !trim(lines_[insertAfter + 1].text).empty()) {
            ++insertAfter;
        }
        std::string separator = separatorStyle();
        for (std::size_t i = *header + 1; i < lines_.size() && !lines_[i].isHeader; ++i) {
            if (lines_[i].isAssignment) {
                insertAfter = i;
                std::size_t keyEnd = lines_[i].text.find(lines_[i].key) + lines_[i].key.size();
                separator = lines_[i].text.substr(keyEnd, lines_[i].valueBegin - keyEnd);  // neighbour's style
            }
        }
        if (lines_[insertAfter].eol.empty()) lines_[insertAfter].eol = eol;
        lines_.insert(lines_.begin() + static_cast<std::ptrdiff_t>(insertAfter) + 1,
                      makeAssignment(separator, lines_[*header].section));
        modified_ = true;
        return success();
    }

    // 3. New section block at the end, separated by a blank line.
    std::string separator = separatorStyle();
    if (!lines_.empty()) {
        if (lines_.back().eol.empty()) lines_.back().eol = eol;
        if (!trim(lines_.back().text).empty()) lines_.push_back(parseLine("", eol, lines_.back().section));
    }
    Line headerLine = parseLine("[" + std::string(section) + "]", eol, "");
    std::string owner = headerLine.section;
    lines_.push_back(std::move(headerLine));
    lines_.push_back(makeAssignment(separator, owner));
    modified_ = true;
    return success();
}

}  // namespace rm
