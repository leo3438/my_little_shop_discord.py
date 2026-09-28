#include "retromanager/parsers/CfgDocument.hpp"

#include <algorithm>
#include <cctype>

namespace rm {

namespace {

constexpr std::string_view kBom = "\xEF\xBB\xBF";

bool isSpace(char c) { return c == ' ' || c == '\t'; }

bool isKeyChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '-';
}

// Unquoted values are only safe when they cannot be misread.
bool fitsUnquoted(std::string_view value) {
    return !value.empty() && std::none_of(value.begin(), value.end(), [](char c) { return isSpace(c) || c == '#'; });
}

}  // namespace

bool CfgDocument::isValidKey(std::string_view key) {
    return !key.empty() && std::all_of(key.begin(), key.end(), isKeyChar);
}

CfgDocument::Line CfgDocument::parseLine(std::string text, std::string eol) {
    Line line;
    line.text = std::move(text);
    line.eol = std::move(eol);
    const std::string& t = line.text;

    std::size_t pos = 0;
    while (pos < t.size() && isSpace(t[pos])) ++pos;
    if (pos == t.size() || t[pos] == '#') return line;  // blank, comment, #include

    std::size_t keyBegin = pos;
    while (pos < t.size() && isKeyChar(t[pos])) ++pos;
    if (pos == keyBegin) return line;
    std::string key = t.substr(keyBegin, pos - keyBegin);

    while (pos < t.size() && isSpace(t[pos])) ++pos;
    if (pos == t.size() || t[pos] != '=') return line;
    ++pos;
    while (pos < t.size() && isSpace(t[pos])) ++pos;

    if (pos < t.size() && t[pos] == '"') {
        std::size_t close = t.find('"', pos + 1);
        if (close == std::string::npos) return line;  // unterminated: leave the line alone
        line.quoted = true;
        line.valueBegin = pos + 1;
        line.valueEnd = close;
    } else {
        std::size_t end = t.size();
        while (end > pos && isSpace(t[end - 1])) --end;
        line.quoted = false;
        line.valueBegin = pos;
        line.valueEnd = end;
    }
    line.isAssignment = true;
    line.key = std::move(key);
    return line;
}

CfgDocument CfgDocument::parse(std::string_view text) {
    CfgDocument doc;
    if (text.substr(0, kBom.size()) == kBom) {
        doc.bom_ = std::string(kBom);
        text.remove_prefix(kBom.size());
    }
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t newline = text.find('\n', pos);
        if (newline == std::string_view::npos) {
            doc.lines_.push_back(parseLine(std::string(text.substr(pos)), ""));
            break;
        }
        std::size_t contentEnd = newline;
        std::string eol = "\n";
        if (contentEnd > pos && text[contentEnd - 1] == '\r') {
            --contentEnd;
            eol = "\r\n";
        }
        doc.lines_.push_back(parseLine(std::string(text.substr(pos, contentEnd - pos)), eol));
        pos = newline + 1;
    }
    return doc;
}

std::string CfgDocument::serialize() const {
    std::string out = bom_;
    for (const Line& line : lines_) {
        out += line.text;
        out += line.eol;
    }
    return out;
}

std::optional<std::string> CfgDocument::get(std::string_view key) const {
    for (const Line& line : lines_) {
        if (line.isAssignment && line.key == key) return line.text.substr(line.valueBegin, line.valueEnd - line.valueBegin);
    }
    return std::nullopt;
}

std::vector<std::string> CfgDocument::keys() const {
    std::vector<std::string> out;
    for (const Line& line : lines_) {
        if (line.isAssignment) out.push_back(line.key);
    }
    return out;
}

std::string CfgDocument::defaultEol() const {
    for (const Line& line : lines_) {
        if (!line.eol.empty()) return line.eol;
    }
    return "\n";
}

Status CfgDocument::set(std::string_view key, std::string_view value) {
    if (!isValidKey(key)) return makeError(ErrorCode::InvalidArgument, "invalid cfg key \"" + std::string(key) + "\"");
    if (value.find_first_of("\"\r\n") != std::string_view::npos) {
        return makeError(ErrorCode::InvalidArgument, "cfg values cannot contain quotes or line breaks");
    }

    bool found = false;
    for (Line& line : lines_) {
        if (!line.isAssignment || line.key != key) continue;
        found = true;
        std::string_view current(line.text.data() + line.valueBegin, line.valueEnd - line.valueBegin);
        if (current == value) continue;

        if (line.quoted || fitsUnquoted(value)) {
            line.text.replace(line.valueBegin, line.valueEnd - line.valueBegin, value);
        } else {
            // Unquoted value that now needs quotes.
            line.text.replace(line.valueBegin, line.valueEnd - line.valueBegin, "\"" + std::string(value) + "\"");
            line.quoted = true;
            ++line.valueBegin;
        }
        line.valueEnd = line.valueBegin + value.size();
        modified_ = true;
    }
    if (found) return success();

    std::string eol = defaultEol();
    if (!lines_.empty() && lines_.back().eol.empty()) lines_.back().eol = eol;
    lines_.push_back(parseLine(std::string(key) + " = \"" + std::string(value) + "\"", eol));
    modified_ = true;
    return success();
}

bool CfgDocument::remove(std::string_view key) {
    auto isTarget = [&](const Line& line) { return line.isAssignment && line.key == key; };
    auto end = std::remove_if(lines_.begin(), lines_.end(), isTarget);
    if (end == lines_.end()) return false;
    lines_.erase(end, lines_.end());
    modified_ = true;
    return true;
}

}  // namespace rm
