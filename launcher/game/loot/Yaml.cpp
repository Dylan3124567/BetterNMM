// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BetterNMM - Nexus Mods mod manager (fork of Prism Launcher)
 *  Copyright (C) 2026 BetterNMM Contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "Yaml.h"

#include <QHash>
#include <QObject>

namespace Loot {

const YamlNode* YamlNode::get(QStringView key) const
{
    if (kind != Kind::Mapping) {
        return nullptr;
    }
    for (size_t i = 0; i + 1 < items.size(); i += 2) {
        if (items[i].kind == Kind::Scalar && items[i].scalar == key) {
            return &items[i + 1];
        }
    }
    return nullptr;
}

namespace {

using Kind = YamlNode::Kind;

struct YamlError {
    int line;
    QString message;
};

struct Line {
    int number;
    int indent;    ///< column the text starts at
    QString text;  ///< the line from that column on
};

bool isSpace(QChar c)
{
    return c == ' ' || c == '\t';
}

bool isSequenceEntry(const QString& text)
{
    return text == "-" || (text.size() > 1 && text[0] == '-' && isSpace(text[1]));
}

bool isBlankText(const QString& text)
{
    for (const QChar c : text) {
        if (c == '#') {
            return true;
        }
        if (!isSpace(c)) {
            return false;
        }
    }
    return true;
}

/// The index just past the closing quote of the quoted scalar starting at @p from, or -1 when it does not end on this line
int quotedEnd(const QString& text, int from)
{
    const QChar quote = text[from];
    for (int i = from + 1; i < text.size(); i++) {
        if (quote == '"' && text[i] == '\\') {
            i++;
        } else if (text[i] == quote) {
            if (quote == '\'' && i + 1 < text.size() && text[i + 1] == '\'') {
                i++;
            } else {
                return i + 1;
            }
        }
    }
    return -1;
}

/// The index of the ':' that makes @p text a "key: value" pair, or -1
int keyEnd(const QString& text)
{
    if (text.isEmpty() || QStringLiteral("[{|>*&!%@`").contains(text[0])) {
        return -1;
    }
    auto isIndicator = [&text](int i) { return text[i] == ':' && (i + 1 == text.size() || isSpace(text[i + 1])); };
    if (text[0] == '\'' || text[0] == '"') {
        int i = quotedEnd(text, 0);
        if (i < 0) {
            return -1;
        }
        while (i < text.size() && isSpace(text[i])) {
            i++;
        }
        return i < text.size() && isIndicator(i) ? i : -1;
    }
    for (int i = 0; i < text.size(); i++) {
        if (isIndicator(i)) {
            return i;
        }
        if (text[i] == '#' && i > 0 && isSpace(text[i - 1])) {
            return -1;
        }
    }
    return -1;
}

/// A plain scalar's text without its trailing comment
QString stripComment(const QString& text)
{
    for (int i = 1; i < text.size(); i++) {
        if (text[i] == '#' && isSpace(text[i - 1])) {
            return text.left(i);
        }
    }
    return text;
}

YamlNode plainNode(const QString& value, int line)
{
    YamlNode node;
    node.line = line;
    if (!(value.isEmpty() || value == "~" || value == "null" || value == "Null" || value == "NULL")) {
        node.kind = Kind::Scalar;
        node.scalar = value;
    }
    return node;
}

class Parser {
   public:
    explicit Parser(const QString& text)
    {
        int number = 1;
        for (auto line : text.split('\n')) {
            if (line.endsWith('\r')) {
                line.chop(1);
            }
            int indent = 0;
            while (indent < line.size() && line[indent] == ' ') {
                indent++;
            }
            m_lines.push_back({ number++, indent, line.mid(indent) });
        }
    }

    YamlNode document()
    {
        skipBlank();
        if (m_pos < m_lines.size() && m_lines[m_pos].indent == 0 && (m_lines[m_pos].text == "---" || m_lines[m_pos].text.startsWith("--- "))) {
            auto& line = m_lines[m_pos];
            line.text = line.text.mid(3).trimmed();
            line.indent = 4;
            if (isBlankText(line.text)) {
                m_pos++;
            }
        }
        auto node = blockNode(-1, true);
        skipBlank();
        if (m_pos < m_lines.size() && m_lines[m_pos].text != "...") {
            throw YamlError{ m_lines[m_pos].number, QObject::tr("unexpected content") };
        }
        return node;
    }

   private:
    /// Reads characters across lines for quoted scalars and flow collections; a line break reads as '\n'
    struct Stream {
        Parser& parser;
        int column = 0;

        const QString& text() const { return parser.m_lines[parser.m_pos].text; }
        QChar peek() const
        {
            if (column < text().size()) {
                return text()[column];
            }
            return parser.m_pos + 1 < parser.m_lines.size() ? QChar('\n') : QChar();
        }
        void next()
        {
            if (column < text().size()) {
                column++;
            } else if (parser.m_pos + 1 < parser.m_lines.size()) {
                parser.m_pos++;
                column = 0;
            }
        }
        int line() const { return parser.m_lines[parser.m_pos].number; }
    };

    void skipBlank()
    {
        while (m_pos < m_lines.size() && isBlankText(m_lines[m_pos].text)) {
            m_pos++;
        }
    }

    [[noreturn]] void fail(int line, const QString& message) const { throw YamlError{ line, message }; }

    YamlNode alias(const QString& name, int line) const
    {
        const auto it = m_anchors.constFind(name);
        if (it == m_anchors.constEnd()) {
            fail(line, QObject::tr("unknown alias \"%1\"").arg(name));
        }
        return *it;
    }

    /// The node starting on the current line, which belongs to a parent indented by @p parentIndent. Block collections
    /// may not start on the line of a "key:" or after node properties on the same line.
    YamlNode blockNode(int parentIndent, bool allowCollections)
    {
        skipBlank();
        if (m_pos >= m_lines.size() || m_lines[m_pos].indent <= parentIndent) {
            YamlNode null;
            null.line = m_pos < m_lines.size() ? m_lines[m_pos].number : static_cast<int>(m_lines.size());
            return null;
        }
        auto& line = m_lines[m_pos];
        QString anchor;
        bool hadProperties = false;
        while (line.text.startsWith('&') || line.text.startsWith('!')) {
            int end = 1;
            while (end < line.text.size() && !isSpace(line.text[end])) {
                end++;
            }
            if (line.text[0] == '&') {
                anchor = line.text.mid(1, end - 1);
            }
            while (end < line.text.size() && isSpace(line.text[end])) {
                end++;
            }
            line.indent += end;
            line.text = line.text.mid(end);
            hadProperties = true;
        }
        YamlNode node;
        if (isBlankText(line.text)) {
            m_pos++;
            node = blockNode(parentIndent, true);
        } else if (line.text.startsWith('*')) {
            int end = 1;
            while (end < line.text.size() && !isSpace(line.text[end])) {
                end++;
            }
            if (!isBlankText(line.text.mid(end))) {
                fail(line.number, QObject::tr("unexpected content after an alias"));
            }
            node = alias(line.text.mid(1, end - 1), line.number);
            m_pos++;
        } else if (allowCollections && !hadProperties && isSequenceEntry(line.text)) {
            node = sequence(line.indent);
        } else if (allowCollections && !hadProperties && keyEnd(line.text) >= 0) {
            node = mapping(line.indent);
        } else {
            node = scalarOrFlow(parentIndent);
        }
        if (!anchor.isEmpty()) {
            m_anchors.insert(anchor, node);
        }
        return node;
    }

    YamlNode sequence(int indent)
    {
        YamlNode node;
        node.kind = Kind::Sequence;
        node.line = m_lines[m_pos].number;
        while (true) {
            skipBlank();
            if (m_pos >= m_lines.size()) {
                break;
            }
            auto& line = m_lines[m_pos];
            if (line.indent != indent || !isSequenceEntry(line.text)) {
                if (line.indent > indent) {
                    fail(line.number, QObject::tr("unexpected indentation"));
                }
                break;
            }
            int start = 1;
            while (start < line.text.size() && isSpace(line.text[start])) {
                start++;
            }
            if (isBlankText(line.text.mid(start))) {
                m_pos++;
            } else {
                line.indent += start;
                line.text = line.text.mid(start);
            }
            node.items.push_back(blockNode(indent, true));
        }
        return node;
    }

    YamlNode mapping(int indent)
    {
        YamlNode node;
        node.kind = Kind::Mapping;
        node.line = m_lines[m_pos].number;
        while (true) {
            skipBlank();
            if (m_pos >= m_lines.size()) {
                break;
            }
            auto& line = m_lines[m_pos];
            if (line.indent != indent || isSequenceEntry(line.text)) {
                if (line.indent > indent) {
                    fail(line.number, QObject::tr("unexpected indentation"));
                }
                break;
            }
            const int colon = keyEnd(line.text);
            if (colon < 0) {
                fail(line.number, QObject::tr("expected \"key: value\""));
            }
            YamlNode key;
            key.kind = Kind::Scalar;
            key.line = line.number;
            const auto keyText = line.text.left(colon).trimmed();
            if (keyText.startsWith('\'') || keyText.startsWith('"')) {
                Stream stream{ *this, static_cast<int>(line.text.indexOf(keyText)) };
                key.scalar = quoted(stream);
            } else {
                key.scalar = keyText;
            }
            int start = colon + 1;
            while (start < line.text.size() && isSpace(line.text[start])) {
                start++;
            }
            YamlNode value;
            if (isBlankText(line.text.mid(start))) {
                m_pos++;
                skipBlank();
                if (m_pos < m_lines.size() && m_lines[m_pos].indent == indent && isSequenceEntry(m_lines[m_pos].text)) {
                    // a sequence may sit at the indentation of its key
                    value = sequence(indent);
                } else {
                    value = blockNode(indent, true);
                }
            } else {
                line.indent += start;
                line.text = line.text.mid(start);
                value = blockNode(indent, false);
            }
            node.items.push_back(std::move(key));
            node.items.push_back(std::move(value));
        }
        return node;
    }

    YamlNode scalarOrFlow(int parentIndent)
    {
        const auto& line = m_lines[m_pos];
        const QChar first = line.text[0];
        if (first == '[' || first == '{' || first == '\'' || first == '"') {
            Stream stream{ *this, 0 };
            YamlNode node;
            if (first == '[' || first == '{') {
                node = flowNode(stream);
            } else {
                node.kind = Kind::Scalar;
                node.line = stream.line();
                node.scalar = quoted(stream);
            }
            if (!isBlankText(stream.text().mid(stream.column))) {
                fail(stream.line(), QObject::tr("unexpected content after a value"));
            }
            m_pos++;
            return node;
        }
        if (first == '|' || first == '>') {
            return blockScalar(parentIndent);
        }
        // a plain scalar continues on following lines indented more than its parent
        const int number = line.number;
        QString value = stripComment(line.text).trimmed();
        bool ended = value.size() != line.text.trimmed().size();  // a comment ends it
        m_pos++;
        while (!ended) {
            size_t next = m_pos;
            int emptyLines = 0;
            while (next < m_lines.size() && m_lines[next].text.trimmed().isEmpty()) {
                next++;
                emptyLines++;
            }
            if (next >= m_lines.size() || m_lines[next].indent <= parentIndent || m_lines[next].text.startsWith('#')) {
                break;
            }
            const auto& more = m_lines[next];
            const auto text = stripComment(more.text).trimmed();
            ended = text.size() != more.text.trimmed().size();
            value += emptyLines > 0 ? QString(emptyLines, '\n') : QString(" ");
            value += text;
            m_pos = next + 1;
        }
        return plainNode(value, number);
    }

    /// '|' (literal) and '>' (folded) scalars, with the strip ('-') and keep ('+') indicators
    YamlNode blockScalar(int parentIndent)
    {
        const auto& header = m_lines[m_pos];
        const bool folded = header.text[0] == '>';
        QChar chomp;
        int explicitIndent = 0;
        int i = 1;
        for (; i < header.text.size() && !isSpace(header.text[i]); i++) {
            const QChar c = header.text[i];
            if (c == '+' || c == '-') {
                chomp = c;
            } else if (c.isDigit()) {
                explicitIndent = c.digitValue();
            } else {
                fail(header.number, QObject::tr("invalid block scalar header"));
            }
        }
        if (!isBlankText(header.text.mid(i))) {
            fail(header.number, QObject::tr("invalid block scalar header"));
        }
        YamlNode node;
        node.kind = Kind::Scalar;
        node.line = header.number;
        m_pos++;
        int contentIndent = explicitIndent > 0 ? qMax(parentIndent, 0) + explicitIndent : -1;
        QStringList lines;
        while (m_pos < m_lines.size()) {
            const auto& line = m_lines[m_pos];
            if (line.text.trimmed().isEmpty()) {
                lines << QString();
                m_pos++;
                continue;
            }
            if (contentIndent < 0) {
                contentIndent = line.indent;
            }
            if (line.indent < contentIndent || line.indent <= parentIndent) {
                break;
            }
            lines << QString(line.indent - contentIndent, ' ') + line.text;
            m_pos++;
        }
        int trailing = 0;
        while (!lines.isEmpty() && lines.last().isEmpty()) {
            lines.removeLast();
            trailing++;
        }
        QString text;
        for (int k = 0; k < lines.size(); k++) {
            if (k > 0) {
                const bool joinWithSpace = folded && !lines[k - 1].isEmpty() && !lines[k].isEmpty() && !lines[k - 1].startsWith(' ') &&
                                           !lines[k].startsWith(' ');
                if (joinWithSpace) {
                    text += ' ';
                } else if (!(folded && lines[k - 1].isEmpty() && k > 1 && !lines[k - 2].startsWith(' '))) {
                    text += '\n';
                }
            }
            text += lines[k];
        }
        if (chomp == '+') {
            text += QString(trailing + (lines.isEmpty() ? 0 : 1), '\n');
        } else if (chomp != '-' && !lines.isEmpty()) {
            text += '\n';
        }
        node.scalar = text;
        return node;
    }

    QString quoted(Stream& s)
    {
        const QChar quote = s.peek();
        const int startLine = s.line();
        s.next();
        QString out;
        while (true) {
            const QChar c = s.peek();
            if (c.isNull()) {
                fail(startLine, QObject::tr("unterminated quoted string"));
            }
            if (c == quote) {
                s.next();
                if (quote == '\'' && s.peek() == '\'') {
                    out += '\'';
                    s.next();
                    continue;
                }
                return out;
            }
            if (c == '\n') {
                // line folding: white space around the break goes, one break reads as a space, further ones stay
                while (out.endsWith(' ') || out.endsWith('\t')) {
                    out.chop(1);
                }
                int breaks = 0;
                while (s.peek() == '\n' || isSpace(s.peek())) {
                    breaks += s.peek() == '\n' ? 1 : 0;
                    s.next();
                }
                out += breaks == 1 ? QString(" ") : QString(breaks - 1, '\n');
                continue;
            }
            s.next();
            if (quote == '"' && c == '\\') {
                out += escape(s);
            } else {
                out += c;
            }
        }
    }

    QString escape(Stream& s)
    {
        const QChar c = s.peek();
        if (c == '\n') {
            // an escaped line break joins the lines without a space
            while (s.peek() == '\n' || isSpace(s.peek())) {
                s.next();
            }
            return {};
        }
        s.next();
        auto hex = [&](int digits) {
            QString code;
            for (int i = 0; i < digits; i++) {
                code += s.peek();
                s.next();
            }
            bool ok = false;
            const char32_t value = code.toUInt(&ok, 16);
            if (!ok) {
                fail(s.line(), QObject::tr("invalid escape sequence"));
            }
            return QString::fromUcs4(&value, 1);
        };
        switch (c.unicode()) {
            case '0': return QString(QChar(0));
            case 'a': return QString(QChar(7));
            case 'b': return QString(QChar(8));
            case 't':
            case '\t': return QString(QChar('\t'));
            case 'n': return QString(QChar('\n'));
            case 'v': return QString(QChar(11));
            case 'f': return QString(QChar(12));
            case 'r': return QString(QChar('\r'));
            case 'e': return QString(QChar(27));
            case ' ':
            case '"':
            case '/':
            case '\\': return QString(c);
            case 'N': return QString(QChar(0x85));
            case '_': return QString(QChar(0xA0));
            case 'L': return QString(QChar(0x2028));
            case 'P': return QString(QChar(0x2029));
            case 'x': return hex(2);
            case 'u': return hex(4);
            case 'U': return hex(8);
            default: fail(s.line(), QObject::tr("invalid escape sequence"));
        }
    }

    void skipFlowSpace(Stream& s)
    {
        while (true) {
            const QChar c = s.peek();
            if (c == '\n' || isSpace(c)) {
                s.next();
            } else if (c == '#') {
                s.column = static_cast<int>(s.text().size());
            } else {
                return;
            }
        }
    }

    static bool endsFlowPlain(QChar c) { return c.isNull() || c == ',' || c == '[' || c == ']' || c == '{' || c == '}'; }

    QString flowToken(Stream& s)
    {
        QString token;
        while (!endsFlowPlain(s.peek()) && s.peek() != '\n' && !isSpace(s.peek())) {
            token += s.peek();
            s.next();
        }
        return token;
    }

    YamlNode flowScalar(Stream& s)
    {
        const int line = s.line();
        if (s.peek() == '\'' || s.peek() == '"') {
            YamlNode node;
            node.kind = Kind::Scalar;
            node.line = line;
            node.scalar = quoted(s);
            return node;
        }
        QString value;
        while (true) {
            const QChar c = s.peek();
            if (endsFlowPlain(c)) {
                break;
            }
            if (c == ':') {
                s.next();
                const QChar after = s.peek();
                if (after == '\n' || isSpace(after) || endsFlowPlain(after)) {
                    s.column--;  // leave the ':' for the mapping
                    break;
                }
                value += c;
                continue;
            }
            if (c == '#' && (value.isEmpty() || isSpace(value.back()))) {
                break;
            }
            s.next();
            if (c == '\n') {
                // a line break and the white space around it fold into one space
                while (value.endsWith(' ') || value.endsWith('\t')) {
                    value.chop(1);
                }
                value += ' ';
                while (isSpace(s.peek())) {
                    s.next();
                }
            } else {
                value += c;
            }
        }
        return plainNode(value.trimmed(), line);
    }

    YamlNode flowNode(Stream& s)
    {
        skipFlowSpace(s);
        QString anchor;
        while (s.peek() == '&' || s.peek() == '!') {
            const bool isAnchor = s.peek() == '&';
            s.next();
            const auto name = flowToken(s);
            if (isAnchor) {
                anchor = name;
            }
            skipFlowSpace(s);
        }
        YamlNode node;
        node.line = s.line();
        const QChar c = s.peek();
        if (c == '[' || c == '{') {
            const bool isMapping = c == '{';
            const QChar close = isMapping ? '}' : ']';
            node.kind = isMapping ? Kind::Mapping : Kind::Sequence;
            s.next();
            while (true) {
                skipFlowSpace(s);
                if (s.peek() == close) {
                    s.next();
                    break;
                }
                if (isMapping) {
                    auto key = flowScalar(s);
                    key.kind = Kind::Scalar;
                    skipFlowSpace(s);
                    YamlNode value;
                    value.line = s.line();
                    if (s.peek() == ':') {
                        s.next();
                        skipFlowSpace(s);
                        if (s.peek() != ',' && s.peek() != close) {
                            value = flowNode(s);
                        }
                    }
                    node.items.push_back(std::move(key));
                    node.items.push_back(std::move(value));
                } else {
                    node.items.push_back(flowNode(s));
                }
                skipFlowSpace(s);
                if (s.peek() == ',') {
                    s.next();
                } else if (s.peek() != close) {
                    fail(s.line(), QObject::tr("expected ',' or '%1'").arg(close));
                }
            }
        } else if (c == '*') {
            s.next();
            node = alias(flowToken(s), s.line());
        } else {
            node = flowScalar(s);
        }
        if (!anchor.isEmpty()) {
            m_anchors.insert(anchor, node);
        }
        return node;
    }

    std::vector<Line> m_lines;
    size_t m_pos = 0;
    QHash<QString, YamlNode> m_anchors;
};

void processMergeKeys(YamlNode& node)
{
    for (auto& child : node.items) {
        processMergeKeys(child);
    }
    if (node.kind != Kind::Mapping) {
        return;
    }
    for (size_t i = 0; i + 1 < node.items.size(); i += 2) {
        if (node.items[i].kind != Kind::Scalar || node.items[i].scalar != "<<") {
            continue;
        }
        const YamlNode value = std::move(node.items[i + 1]);
        node.items.erase(node.items.begin() + static_cast<std::ptrdiff_t>(i), node.items.begin() + static_cast<std::ptrdiff_t>(i) + 2);
        auto merge = [&node](const YamlNode& source) {
            if (source.kind != Kind::Mapping) {
                throw YamlError{ source.line, QObject::tr("invalid YAML merge key value") };
            }
            for (size_t k = 0; k + 1 < source.items.size(); k += 2) {
                if (!node.get(source.items[k].scalar)) {
                    node.items.push_back(source.items[k]);
                    node.items.push_back(source.items[k + 1]);
                }
            }
        };
        if (value.kind == Kind::Sequence) {
            for (const auto& element : value.items) {
                merge(element);
            }
        } else {
            merge(value);
        }
        return;
    }
}

}  // namespace

Result<YamlNode> parseYaml(const QString& text)
{
    try {
        Parser parser(text);
        auto document = parser.document();
        processMergeKeys(document);
        return document;
    } catch (const YamlError& error) {
        return std::unexpected(QObject::tr("YAML error on line %1: %2").arg(error.line).arg(error.message));
    }
}

}  // namespace Loot
