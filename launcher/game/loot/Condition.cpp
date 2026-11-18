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

#include "Condition.h"

#include <QBuffer>
#include <QDir>
#include <QDirListing>
#include <QFile>
#include <QFileInfo>
#include <QObject>

#include <array>

#include "Plugin.h"

namespace Loot {

bool supportsLightPlugins(GameType game)
{
    return game == GameType::SkyrimSE || game == GameType::Fallout4 || game == GameType::Starfield;
}

////// Paths (loot-condition-interpreter path.rs) //////

namespace {

bool isUnghostedPluginExtension(GameType game, const QString& extension)
{
    return extension.compare("esp", Qt::CaseInsensitive) == 0 || extension.compare("esm", Qt::CaseInsensitive) == 0 ||
           (supportsLightPlugins(game) && extension.compare("esl", Qt::CaseInsensitive) == 0);
}

/// The text after the last '.' of the file name, as Rust's Path::extension; empty when there is none
QString extensionOf(const QString& fileName)
{
    const auto name = fileName.mid(fileName.lastIndexOf('/') + 1);
    const auto dot = name.lastIndexOf('.');
    return dot > 0 ? name.mid(dot + 1) : QString();
}

QString stemOf(const QString& fileName)
{
    const auto name = fileName.mid(fileName.lastIndexOf('/') + 1);
    const auto dot = name.lastIndexOf('.');
    return dot > 0 ? name.left(dot) : name;
}

bool hasUnghostedPluginExtension(GameType game, const QString& path)
{
    return isUnghostedPluginExtension(game, extensionOf(path));
}

bool hasPluginExtension(GameType game, const QString& path)
{
    const auto extension = extensionOf(path);
    if (extension.compare("ghost", Qt::CaseInsensitive) == 0) {
        return hasUnghostedPluginExtension(game, stemOf(path));
    }
    return isUnghostedPluginExtension(game, extension);
}

/// A directory entry's name with a plugin's ".ghost" extension removed
QString normaliseFileName(GameType game, const QString& name)
{
    if (extensionOf(name).compare("ghost", Qt::CaseInsensitive) == 0 && hasUnghostedPluginExtension(game, stemOf(name))) {
        return stemOf(name);
    }
    return name;
}

QString joinPath(const QString& base, const QString& path)
{
    return QDir(base).filePath(path);
}

QString resolvePath(const ConditionState& state, const QString& path)
{
    const auto joined = joinPath(state.dataPath, path);
    if (!QFileInfo::exists(joined) && hasUnghostedPluginExtension(state.game, path)) {
        return joined + ".ghost";
    }
    return joined;
}

/// Calls @p evaluator for each entry of a folder until it returns true; false when the folder can not be read
template <typename F>
bool anyEntry(const ConditionState& state, const QString& parentPath, F evaluator)
{
    const auto folder = joinPath(state.dataPath, parentPath);
    if (!QFileInfo(folder).isDir()) {
        return false;
    }
    for (const auto& entry : QDirListing(folder, QDirListing::IteratorFlag::IncludeHidden)) {
        if (evaluator(normaliseFileName(state.game, entry.fileName()))) {
            return true;
        }
    }
    return false;
}

bool fullMatch(const QRegularExpression& regex, const QString& text)
{
    return regex.match(text).hasMatch();
}

/// Whether the file is an executable whose resources can be read (is_executable)
bool hasVersionResource(const QString& path);

}  // namespace

////// Evaluation (eval.rs) //////

Result<bool> Condition::Function::eval(ConditionState& state) const
{
    const bool slow = type != Type::ActivePath && type != Type::ActiveRegex && type != Type::ManyActive && type != Type::Checksum;
    if (slow) {
        if (const auto it = state.cache.constFind(key); it != state.cache.constEnd()) {
            return *it;
        }
    }
    auto compare = [this](const Version& actual) {
        const Version given(version);
        switch (comparator) {
            case Comparator::Equal: return actual == given;
            case Comparator::NotEqual: return !(actual == given);
            case Comparator::LessThan: return actual.compare(given) < 0;
            case Comparator::GreaterThan: return actual.compare(given) > 0;
            case Comparator::LessThanOrEqual: return actual.compare(given) <= 0;
            case Comparator::GreaterThanOrEqual: return actual.compare(given) >= 0;
        }
        return false;
    };
    Result<bool> result = false;
    switch (type) {
        case Type::FilePath:
            result = QFileInfo::exists(resolvePath(state, path));
            break;
        case Type::FileRegex:
            result = anyEntry(state, path, [this](const QString& name) { return fullMatch(regex, name); });
            break;
        case Type::FileSize: {
            const QFileInfo info(resolvePath(state, path));
            result = info.exists() && static_cast<quint64>(info.size()) == number;
            break;
        }
        case Type::Readable:
            // as the original: the folder test looks at the path as written, relative to the working directory
            if (QFileInfo(path).isDir()) {
                result = QDir(resolvePath(state, path)).isReadable();
            } else {
                QFile file(resolvePath(state, path));
                result = file.open(QIODevice::ReadOnly);
            }
            break;
        case Type::IsExecutable:
            result = hasVersionResource(resolvePath(state, path));
            break;
        case Type::ActivePath:
            result = state.activePlugins.contains(path.toLower());
            break;
        case Type::ActiveRegex:
        case Type::ManyActive: {
            int found = 0;
            for (const auto& plugin : state.activePlugins) {
                found += regex.match(plugin).hasMatch() ? 1 : 0;
            }
            result = type == Type::ActiveRegex ? found > 0 : found > 1;
            break;
        }
        case Type::IsMaster: {
            const auto resolved = resolvePath(state, path);
            const auto header = readPluginHeader(state.game, resolved);
            result = header && isMasterFile(state.game, resolved, header->flags);
            break;
        }
        case Type::Many: {
            int found = 0;
            result = anyEntry(state, path, [this, &found](const QString& name) { return fullMatch(regex, name) && ++found > 1; });
            break;
        }
        case Type::Checksum: {
            const auto cacheKey = path.toLower();
            if (const auto it = state.crcs.constFind(cacheKey); it != state.crcs.constEnd()) {
                result = *it == number;
                break;
            }
            const auto resolved = resolvePath(state, path);
            if (!QFileInfo(resolved).isFile()) {
                result = false;
                break;
            }
            const auto crc = fileCrc32(resolved);
            if (!crc) {
                result = std::unexpected(QObject::tr("Could not read %1").arg(QDir::toNativeSeparators(resolved)));
                break;
            }
            state.crcs.insert(cacheKey, *crc);
            result = *crc == number;
            break;
        }
        case Type::Version:
        case Type::ProductVersion: {
            const auto resolved = resolvePath(state, path);
            if (!QFileInfo(resolved).isFile()) {
                result = false;
                break;
            }
            Result<std::optional<Version>> actual = std::optional<Version>();
            if (type == Type::ProductVersion) {
                actual = readProductVersion(resolved);
            } else if (const auto it = state.pluginVersions.constFind(QFileInfo(resolved).fileName().toLower());
                       it != state.pluginVersions.constEnd()) {
                actual = std::optional<Version>(Version(*it));
            } else if (!hasPluginExtension(state.game, resolved)) {
                actual = readFileVersion(resolved);
            }
            if (!actual) {
                result = std::unexpected(actual.error());
            } else {
                result = actual->has_value() && compare(**actual);
            }
            break;
        }
        case Type::FilenameVersion:
            result = anyEntry(state, path, [this, &compare](const QString& name) {
                const auto match = regex.match(name);
                return match.hasMatch() && match.capturedStart(1) >= 0 && compare(Version(match.captured(1)));
            });
            break;
        case Type::DescriptionContains: {
            const auto header = readPluginHeader(state.game, resolvePath(state, path));
            result = header && header->description && regex.match(*header->description).hasMatch();
            break;
        }
    }
    if (slow && result) {
        state.cache.insert(key, *result);
    }
    return result;
}

namespace {

Result<bool> evalExpression(const Condition::Expression& expression, ConditionState& state)
{
    for (const auto& compound : expression) {
        bool all = true;
        for (const auto& term : compound) {
            TRY_INTO(bool value, term.function ? term.function->eval(state) : evalExpression(term.expression, state));
            if (value == term.inverted) {
                all = false;
                break;
            }
        }
        if (all) {
            return true;
        }
    }
    return false;
}

}  // namespace

Result<bool> Condition::eval(ConditionState& state) const
{
    return evalExpression(m_expression, state);
}

////// Parsing (lib.rs, function/parse.rs) //////

namespace {

using Function = Condition::Function;
using Type = Function::Type;

struct ParseError {
    QString message;
};

class ConditionParser {
   public:
    explicit ConditionParser(const QString& text) : m_text(text) {}

    Condition::Expression all()
    {
        auto expression = parseExpression();
        if (m_pos != m_text.size()) {
            throw ParseError{ QObject::tr("unexpected text \"%1\"").arg(m_text.mid(m_pos)) };
        }
        return expression;
    }

   private:
    void skipSpace()
    {
        while (m_pos < m_text.size() && m_text[m_pos].isSpace()) {
            m_pos++;
        }
    }

    /// Consumes @p tag when the text continues with it
    bool take(QStringView tag)
    {
        if (QStringView(m_text).mid(m_pos).startsWith(tag)) {
            m_pos += tag.size();
            return true;
        }
        return false;
    }

    /// Consumes white space, @p word and white space when the text continues that way
    bool takeWord(QStringView word)
    {
        const auto start = m_pos;
        skipSpace();
        if (take(word)) {
            skipSpace();
            return true;
        }
        m_pos = start;
        return false;
    }

    void expect(QStringView tag)
    {
        if (!take(tag)) {
            throw ParseError{ QObject::tr("expected \"%1\" at \"%2\"").arg(tag.toString(), m_text.mid(m_pos)) };
        }
    }

    Condition::Expression parseExpression()
    {
        Condition::Expression expression;
        expression.push_back(parseCompound());
        while (takeWord(u"or")) {
            expression.push_back(parseCompound());
        }
        return expression;
    }

    std::vector<Condition::Term> parseCompound()
    {
        std::vector<Condition::Term> compound;
        compound.push_back(parseTerm());
        while (takeWord(u"and")) {
            compound.push_back(parseTerm());
        }
        return compound;
    }

    Condition::Term parseTerm()
    {
        Condition::Term term;
        skipSpace();
        if (takeWord(u"not")) {
            term.inverted = true;
        }
        if (take(u"(")) {
            skipSpace();
            term.expression = parseExpression();
            skipSpace();
            expect(u")");
            skipSpace();
        } else {
            term.function = parseFunction();
        }
        return term;
    }

    /// Text up to (not including) the first character of @p stops, at least one character
    QString until(QStringView stops)
    {
        const auto start = m_pos;
        while (m_pos < m_text.size() && !stops.contains(m_text[m_pos])) {
            m_pos++;
        }
        if (m_pos == start) {
            throw ParseError{ QObject::tr("expected a value at \"%1\"").arg(m_text.mid(start)) };
        }
        return m_text.mid(start, m_pos - start);
    }

    static QRegularExpression regex(const QString& pattern)
    {
        QRegularExpression regex(pattern, QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
        if (!regex.isValid()) {
            throw ParseError{ QObject::tr("invalid regular expression \"%1\": %2").arg(pattern, regex.errorString()) };
        }
        return regex;
    }

    static QRegularExpression anchored(const QString& pattern) { return regex("^" + pattern + "$"); }

    QString quotedPath()
    {
        expect(u"\"");
        auto path = until(u"\":*?<>|");
        expect(u"\"");
        return path;
    }

    void comma()
    {
        skipSpace();
        expect(u",");
        skipSpace();
    }

    void regexPath(Function& f)
    {
        const auto text = until(u"\"<>");
        if (text.endsWith('/')) {
            throw ParseError{ QObject::tr("the path \"%1\" ends in a directory separator").arg(text) };
        }
        const auto slash = text.lastIndexOf('/');
        f.path = slash < 0 ? QStringLiteral(".") : text.left(slash);
        f.regex = anchored(slash < 0 ? text : text.mid(slash + 1));
    }

    Function::Comparator comparator()
    {
        for (const auto& [tag, value] : { std::pair{ u"==", Function::Comparator::Equal }, std::pair{ u"!=", Function::Comparator::NotEqual },
                                          std::pair{ u"<=", Function::Comparator::LessThanOrEqual },
                                          std::pair{ u">=", Function::Comparator::GreaterThanOrEqual },
                                          std::pair{ u"<", Function::Comparator::LessThan }, std::pair{ u">", Function::Comparator::GreaterThan } }) {
            if (take(QStringView(tag))) {
                return value;
            }
        }
        throw ParseError{ QObject::tr("expected a comparison operator at \"%1\"").arg(m_text.mid(m_pos)) };
    }

    QString quotedVersion()
    {
        expect(u"\"");
        auto version = until(u"\"");
        expect(u"\"");
        return version;
    }

    /// The version and comparator, in either order
    void versionArguments(Function& f)
    {
        comma();
        if (m_pos < m_text.size() && m_text[m_pos] == '"') {
            f.version = quotedVersion();
            comma();
            f.comparator = comparator();
        } else {
            f.comparator = comparator();
            comma();
            f.version = quotedVersion();
        }
    }

    /// A path without regex characters followed by `")`, else the position is restored
    bool plainPathCall(Function& f, Type type)
    {
        const auto start = m_pos;
        const auto end = m_text.indexOf(QStringLiteral("\")"), m_pos);
        if (end > m_pos) {
            const auto candidate = m_text.mid(m_pos, end - m_pos);
            if (std::none_of(candidate.begin(), candidate.end(), [](QChar c) { return QStringView(u"\":*?<>|\\").contains(c); })) {
                f.type = type;
                f.path = candidate;
                m_pos = end + 2;
                return true;
            }
        }
        m_pos = start;
        return false;
    }

    Function parseFunction()
    {
        Function f;
        const auto start = m_pos;
        if (take(u"file(\"")) {
            if (!plainPathCall(f, Type::FilePath)) {
                f.type = Type::FileRegex;
                regexPath(f);
                expect(u"\")");
            }
        } else if (take(u"file_size(")) {
            f.type = Type::FileSize;
            f.path = quotedPath();
            comma();
            f.number = number(10);
            expect(u")");
        } else if (take(u"readable(\"")) {
            if (!plainPathCall(f, Type::Readable)) {
                throw ParseError{ QObject::tr("invalid path in \"%1\"").arg(m_text.mid(start)) };
            }
        } else if (take(u"is_executable(\"")) {
            if (!plainPathCall(f, Type::IsExecutable)) {
                throw ParseError{ QObject::tr("invalid path in \"%1\"").arg(m_text.mid(start)) };
            }
        } else if (take(u"active(\"")) {
            if (!plainPathCall(f, Type::ActivePath)) {
                f.type = Type::ActiveRegex;
                f.regex = anchored(until(u"\"<>"));
                expect(u"\")");
            }
        } else if (take(u"is_master(\"")) {
            if (!plainPathCall(f, Type::IsMaster)) {
                throw ParseError{ QObject::tr("invalid path in \"%1\"").arg(m_text.mid(start)) };
            }
        } else if (take(u"many(\"")) {
            f.type = Type::Many;
            regexPath(f);
            expect(u"\")");
        } else if (take(u"many_active(\"")) {
            f.type = Type::ManyActive;
            f.regex = anchored(until(u"\"<>"));
            expect(u"\")");
        } else if (take(u"version(") || take(u"product_version(")) {
            f.type = m_text.mid(start).startsWith(u"version(") ? Type::Version : Type::ProductVersion;
            f.path = quotedPath();
            versionArguments(f);
            expect(u")");
        } else if (take(u"filename_version(")) {
            f.type = Type::FilenameVersion;
            expect(u"\"");
            regexPath(f);
            expect(u"\"");
            versionArguments(f);
            expect(u")");
            if (f.regex.captureCount() != 1) {
                throw ParseError{ QObject::tr("the regular expression in \"%1\" needs exactly one capturing group").arg(m_text.mid(start)) };
            }
        } else if (take(u"checksum(")) {
            f.type = Type::Checksum;
            f.path = quotedPath();
            comma();
            f.number = number(16);
            expect(u")");
        } else if (take(u"description_contains(")) {
            f.type = Type::DescriptionContains;
            f.path = quotedPath();
            comma();
            expect(u"\"");
            f.regex = regex(until(u"\""));
            expect(u"\"");
            expect(u")");
        } else {
            throw ParseError{ QObject::tr("unknown function at \"%1\"").arg(m_text.mid(start)) };
        }
        f.key = QStringLiteral("%1|%2|%3|%4|%5|%6")
                    .arg(static_cast<int>(f.type))
                    .arg(f.path.toLower(), f.regex.pattern().toLower())
                    .arg(f.number)
                    .arg(f.version.toLower())
                    .arg(static_cast<int>(f.comparator));
        return f;
    }

    quint64 number(int base)
    {
        const auto start = m_pos;
        auto isDigit = [base](char16_t c) {
            return (c >= '0' && c <= '9') || (base == 16 && ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')));
        };
        while (m_pos < m_text.size() && isDigit(m_text[m_pos].unicode())) {
            m_pos++;
        }
        bool ok = false;
        const auto value = m_text.mid(start, m_pos - start).toULongLong(&ok, base);
        if (!ok || (base == 16 && value > 0xFFFFFFFFull)) {
            throw ParseError{ QObject::tr("invalid number at \"%1\"").arg(m_text.mid(start)) };
        }
        return value;
    }

    const QString& m_text;
    qsizetype m_pos = 0;
};

}  // namespace

Result<Condition> Condition::parse(const QString& text)
{
    try {
        Condition condition;
        condition.m_expression = ConditionParser(text).all();
        return condition;
    } catch (const ParseError& error) {
        return std::unexpected(QObject::tr("Invalid condition \"%1\": %2").arg(text, error.message));
    }
}

////// Versions (function/version.rs) //////

namespace {

/// Rust's u32 parsing: an optional '+' and decimal digits that fit 32 bits
std::optional<quint32> parseU32(QStringView text)
{
    if (text.startsWith('+')) {
        text = text.mid(1);
    }
    if (text.isEmpty()) {
        return std::nullopt;
    }
    quint64 value = 0;
    for (const QChar c : text) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        value = value * 10 + (c.unicode() - '0');
        if (value > 0xFFFFFFFFull) {
            return std::nullopt;
        }
    }
    return static_cast<quint32>(value);
}

int compareText(const QString& a, const QString& b)
{
    // Rust compares strings by code point
    const auto ua = a.toUcs4();
    const auto ub = b.toUcs4();
    return ua < ub ? -1 : (ub < ua ? 1 : 0);
}

}  // namespace

Version::Version(const QString& input)
{
    QString text = input.isEmpty() ? QStringLiteral("0") : input.section('+', 0, 0);
    auto toId = [](const QString& part) {
        Id id;
        if (const auto number = parseU32(part.trimmed())) {
            id.numeric = true;
            id.number = *number;
        } else {
            id.text = part.toLower();
        }
        return id;
    };
    static const QRegularExpression fourNumbers(QStringLiteral("\\d+, \\d+, \\d+, \\d+"), QRegularExpression::UseUnicodePropertiesOption);
    QString release = text;
    QString preRelease;
    if (!fourNumbers.match(text).hasMatch()) {
        const auto separator = std::find_if(text.begin(), text.end(), [](QChar c) { return c == '-' || c == ' ' || c == ':' || c == '_'; });
        if (separator != text.end()) {
            const auto at = separator - text.begin();
            release = text.left(at);
            preRelease = text.mid(at + 1);
        }
    }
    for (const auto& part : release.split(QRegularExpression(QStringLiteral("[.,]")))) {
        m_release << toId(part);
    }
    // split_terminator: no element for an empty string or after a final separator
    auto parts = preRelease.split(QRegularExpression(QStringLiteral("[.\\- :_]")));
    if (!parts.isEmpty() && parts.last().isEmpty()) {
        parts.removeLast();
    }
    for (const auto& part : parts) {
        m_preRelease << toId(part);
    }
}

Version::Version(quint32 major, quint32 minor, quint32 patch, quint32 build)
{
    for (const auto number : { major, minor, patch, build }) {
        m_release << Id{ true, number, {} };
    }
}

namespace {

/// The leading number of a non-numeric release identifier and whether anything follows it (Rust u32_from_str)
std::pair<std::optional<quint32>, bool> leadingNumber(const QString& text)
{
    qsizetype digits = 0;
    while (digits < text.size() && text[digits] >= '0' && text[digits] <= '9') {
        digits++;
    }
    if (digits == text.size()) {
        return { parseU32(text.trimmed()), false };
    }
    if (digits == 0) {
        return { std::nullopt, true };
    }
    return { parseU32(text.left(digits)), true };
}

}  // namespace

bool Version::operator==(const Version& other) const
{
    const auto count = qMax(m_release.size(), other.m_release.size());
    for (qsizetype i = 0; i < count; i++) {
        const Id zero{ true, 0, {} };
        const auto& a = i < m_release.size() ? m_release[i] : zero;
        const auto& b = i < other.m_release.size() ? other.m_release[i] : zero;
        const bool equal = a.numeric && b.numeric ? a.number == b.number
                           : !a.numeric && !b.numeric ? a.text == b.text
                           // are_numeric_values_equal: the text never parses, or it would be numeric
                                                      : parseU32((a.numeric ? b.text : a.text).trimmed()) == (a.numeric ? a.number : b.number);
        if (!equal) {
            return false;
        }
    }
    if (m_preRelease.size() != other.m_preRelease.size()) {
        return false;
    }
    for (qsizetype i = 0; i < m_preRelease.size(); i++) {
        const auto& a = m_preRelease[i];
        const auto& b = other.m_preRelease[i];
        if (a.numeric != b.numeric || (a.numeric ? a.number != b.number : a.text != b.text)) {
            return false;
        }
    }
    return true;
}

int Version::compare(const Version& other) const
{
    const auto count = qMax(m_release.size(), other.m_release.size());
    for (qsizetype i = 0; i < count; i++) {
        const Id zero{ true, 0, {} };
        const auto& a = i < m_release.size() ? m_release[i] : zero;
        const auto& b = i < other.m_release.size() ? other.m_release[i] : zero;
        int order = 0;
        if (a.numeric && b.numeric) {
            order = a.number < b.number ? -1 : (a.number > b.number ? 1 : 0);
        } else if (!a.numeric && !b.numeric) {
            order = compareText(a.text, b.text);
        } else {
            // compare_heterogeneous_ids, from the numeric side
            const auto number = a.numeric ? a.number : b.number;
            const auto [leading, more] = leadingNumber(a.numeric ? b.text : a.text);
            int numericSide = -1;
            if (leading) {
                numericSide = number < *leading ? -1 : (number > *leading ? 1 : (more ? -1 : 0));
            }
            order = a.numeric ? numericSide : -numericSide;
        }
        if (order != 0) {
            return order;
        }
    }
    if (m_preRelease.isEmpty() != other.m_preRelease.isEmpty()) {
        return m_preRelease.isEmpty() ? 1 : -1;
    }
    for (qsizetype i = 0; i < qMin(m_preRelease.size(), other.m_preRelease.size()); i++) {
        const auto& a = m_preRelease[i];
        const auto& b = other.m_preRelease[i];
        int order = 0;
        if (a.numeric != b.numeric) {
            order = a.numeric ? -1 : 1;  // numeric identifiers sort first
        } else if (a.numeric) {
            order = a.number < b.number ? -1 : (a.number > b.number ? 1 : 0);
        } else {
            order = compareText(a.text, b.text);
        }
        if (order != 0) {
            return order;
        }
    }
    return m_preRelease.size() < other.m_preRelease.size() ? -1 : (m_preRelease.size() > other.m_preRelease.size() ? 1 : 0);
}

////// Executable versions (function/version/pe.rs) //////

namespace {

struct PeError {
    QString message;
};

class PeReader {
   public:
    explicit PeReader(QIODevice& device) : m_device(device) {}

    QByteArray read(qint64 size)
    {
        const auto data = m_device.read(size);
        if (data.size() != size) {
            throw PeError{ QObject::tr("unexpected end of file") };
        }
        return data;
    }
    quint16 u16() { return le<quint16>(read(2), 0); }
    quint32 u32() { return le<quint32>(read(4), 0); }
    void seek(qint64 at)
    {
        if (!m_device.seek(at)) {
            throw PeError{ QObject::tr("unexpected end of file") };
        }
    }
    void skip(qint64 count) { seek(m_device.pos() + count); }
    qint64 pos() const { return m_device.pos(); }

    template <typename T>
    static T le(const QByteArray& data, qsizetype at)
    {
        T value = 0;
        for (qsizetype i = 0; i < static_cast<qsizetype>(sizeof(T)); i++) {
            value |= static_cast<T>(static_cast<quint8>(data[at + i])) << (8 * i);
        }
        return value;
    }

   private:
    QIODevice& m_device;
};

/// The VS_VERSIONINFO resource of an executable, empty when it has none
QByteArray versionResource(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        throw PeError{ file.errorString() };
    }
    PeReader reader(file);
    if (reader.read(2) != "MZ") {
        throw PeError{ QObject::tr("Unknown file magic") };
    }
    reader.seek(0x3C);
    reader.seek(reader.u16());
    if (reader.read(4) != QByteArray("PE\0\0", 4)) {
        throw PeError{ QObject::tr("Unexpected PE header magic bytes") };
    }
    reader.skip(2);
    const auto sectionCount = reader.u16();
    reader.skip(12);
    const auto optionalHeaderSize = reader.u16();
    if (optionalHeaderSize == 0) {
        throw PeError{ QObject::tr("COFF optional header is unexpectedly missing") };
    }
    reader.skip(2);

    const auto optionalStart = reader.pos();
    const auto magic = reader.u16();
    reader.skip(magic == 0x10b ? 90 : 106);
    const auto directoryCount = reader.u32();
    if (reader.pos() - optionalStart + 8LL * directoryCount > optionalHeaderSize) {
        throw PeError{ QObject::tr("unexpected end of the optional header") };
    }
    quint32 resourceAddress = 0;
    quint32 resourceSize = 0;
    for (quint32 i = 0; i < directoryCount; i++) {
        const auto address = reader.u32();
        const auto size = reader.u32();
        if (i == 2) {
            resourceAddress = address;
            resourceSize = size;
        }
    }
    if (directoryCount <= 2) {
        return {};
    }
    for (quint32 s = 0; s < sectionCount; s++) {
        reader.skip(8);
        const auto virtualSize = reader.u32();
        const auto virtualAddress = reader.u32();
        const auto rawSize = reader.u32();
        const auto rawOffset = reader.u32();
        reader.skip(16);
        const quint32 sectionEnd = virtualAddress + qMin(rawSize, virtualSize);
        if (resourceAddress < virtualAddress || resourceAddress + resourceSize > sectionEnd) {
            continue;
        }
        reader.seek(static_cast<qint64>(rawOffset) + (resourceAddress - virtualAddress));
        QByteArray table = reader.read(resourceSize);

        // the resource directory: type (RT_VERSION = 16) -> name -> language -> data entry
        QBuffer buffer(&table);
        buffer.open(QIODevice::ReadOnly);
        PeReader tables(buffer);
        auto readTable = [&tables] {
            tables.skip(12);
            const int count = tables.u16() + tables.u16();
            std::vector<std::pair<quint32, quint32>> entries;
            for (int i = 0; i < count; i++) {
                const auto id = tables.u32();
                entries.emplace_back(id, tables.u32());
            }
            return entries;
        };
        constexpr quint32 subdirectory = 0x80000000u;
        for (const auto& [id, offset] : readTable()) {
            if (id != 16 || (offset & subdirectory) == 0) {
                continue;
            }
            tables.seek(offset ^ subdirectory);
            for (const auto& [nameId, nameOffset] : readTable()) {
                if ((nameOffset & subdirectory) == 0) {
                    continue;
                }
                tables.seek(nameOffset ^ subdirectory);
                for (const auto& [languageId, languageOffset] : readTable()) {
                    if ((languageOffset & subdirectory) != 0) {
                        continue;
                    }
                    tables.seek(languageOffset);
                    const auto dataAddress = tables.u32();
                    const auto dataSize = tables.u32();
                    if (dataAddress < virtualAddress) {
                        throw PeError{ QObject::tr("invalid version resource address") };
                    }
                    tables.seek(dataAddress - virtualAddress);
                    return tables.read(dataSize);
                }
            }
        }
        return {};
    }
    return {};
}

constexpr qsizetype keyOffset = 6;

QByteArray utf16Key(const char* key)
{
    QByteArray out;
    for (const char* c = key; *c; c++) {
        out.append(*c).append('\0');
    }
    return out.append('\0').append('\0');
}

bool hasAt(const QByteArray& data, qsizetype at, const QByteArray& needle)
{
    return at >= 0 && at + needle.size() <= data.size() && data.mid(at, needle.size()) == needle;
}

QByteArray subslice(const QByteArray& data, qsizetype at, qsizetype size)
{
    if (at < 0 || size < 0 || at + size > data.size()) {
        throw PeError{ QObject::tr("Invalid subslice of size %1 at offset %2").arg(size).arg(at) };
    }
    return data.mid(at, size);
}

quint16 structSize(const QByteArray& data)
{
    if (data.size() < 2) {
        throw PeError{ QObject::tr("The buffer was too small to hold a struct size field") };
    }
    return PeReader::le<quint16>(data, 0);
}

/// wLength and wValueLength of VS_VERSIONINFO, checked
std::pair<qsizetype, qsizetype> versionInfoHeaders(const QByteArray& data)
{
    if (data.size() < 4) {
        throw PeError{ QObject::tr("The buffer was too small to hold a struct") };
    }
    const qsizetype length = PeReader::le<quint16>(data, 0);
    const qsizetype valueLength = PeReader::le<quint16>(data, 2);
    if (length != data.size()) {
        throw PeError{ QObject::tr("Unexpected length of VS_VERSIONINFO struct") };
    }
    if (!hasAt(data, keyOffset, utf16Key("VS_VERSION_INFO"))) {
        throw PeError{ QObject::tr("The szKey field's value is not valid for a VS_VERSIONINFO struct") };
    }
    return { length, valueLength };
}

qsizetype alignedOffset(qsizetype length)
{
    return length % 4 == 0 ? length : length + 2;
}

/// @p data from @p offset on; a zero-length struct is an error here because the original would loop forever on it
QByteArray advance(const QByteArray& data, qsizetype offset)
{
    if (offset == 0 || offset > data.size()) {
        throw PeError{ QObject::tr("Failed to get subslice at offset %1").arg(offset) };
    }
    return data.mid(offset);
}

bool hasVersionResource(const QString& path)
{
    try {
        versionResource(path);
        return true;
    } catch (const PeError&) {
        return false;
    }
}

template <typename F>
Result<std::optional<Version>> peVersion(const QString& path, F fromVersionInfo)
{
    try {
        const auto data = versionResource(path);
        if (data.isEmpty()) {
            return std::optional<Version>();
        }
        return fromVersionInfo(data);
    } catch (const PeError& error) {
        return std::unexpected(QObject::tr("Could not read the version of %1: %2").arg(QDir::toNativeSeparators(path), error.message));
    }
}

}  // namespace

Result<std::optional<Version>> readFileVersion(const QString& path)
{
    return peVersion(path, [](const QByteArray& data) -> std::optional<Version> {
        const auto [length, valueLength] = versionInfoHeaders(data);
        if (valueLength == 0) {
            return std::nullopt;
        }
        const auto fixed = subslice(data, 40, valueLength);
        if (fixed.size() != 0x34) {
            throw PeError{ QObject::tr("Unexpected length of VS_VERSIONINFO value") };
        }
        if (!hasAt(fixed, 0, QByteArray("\xBD\x04\xEF\xFE", 4))) {
            throw PeError{ QObject::tr("Unexpected first four bytes of VS_VERSIONINFO struct") };
        }
        const auto minor = PeReader::le<quint16>(fixed, 8);
        const auto major = PeReader::le<quint16>(fixed, 10);
        const auto build = PeReader::le<quint16>(fixed, 12);
        const auto patch = PeReader::le<quint16>(fixed, 14);
        return Version(major, minor, patch, build);
    });
}

Result<std::optional<Version>> readProductVersion(const QString& path)
{
    return peVersion(path, [](const QByteArray& data) -> std::optional<Version> {
        const auto [length, valueLength] = versionInfoHeaders(data);
        const qsizetype childrenOffset = 40 + valueLength;
        auto children = subslice(data, childrenOffset, length - childrenOffset);
        const auto stringFileInfoKey = utf16Key("StringFileInfo");
        const auto productVersionKey = utf16Key("ProductVersion");
        while (!children.isEmpty()) {
            const qsizetype childLength = structSize(children);
            if (hasAt(children, keyOffset, stringFileInfoKey)) {
                const qsizetype tablesOffset = keyOffset + stringFileInfoKey.size();
                if (childLength < tablesOffset) {
                    throw PeError{ QObject::tr("The StringFileInfo struct's header is too small") };
                }
                auto tables = subslice(children, tablesOffset, childLength - tablesOffset);
                while (!tables.isEmpty()) {
                    const qsizetype tableLength = structSize(tables);
                    if (tableLength < 24) {
                        throw PeError{ QObject::tr("The StringTable struct's header is too small") };
                    }
                    auto strings = subslice(tables, 24, tableLength - 24);
                    while (!strings.isEmpty()) {
                        if (strings.size() < 4) {
                            throw PeError{ QObject::tr("The buffer was too small to hold a String struct") };
                        }
                        const qsizetype stringLength = PeReader::le<quint16>(strings, 0);
                        const qsizetype stringValueLength = PeReader::le<quint16>(strings, 2);
                        if (hasAt(strings, keyOffset, productVersionKey)) {
                            const auto bytes = subslice(strings, keyOffset + productVersionKey.size(), stringValueLength * 2);
                            QString value = QString::fromUtf16(reinterpret_cast<const char16_t*>(bytes.constData()), bytes.size() / 2);
                            value.chop(1);  // the terminating null
                            return Version(value);
                        }
                        strings = advance(strings, alignedOffset(stringLength));
                    }
                    // as the original, the next table is looked for relative to the start of the StringFileInfo
                    tables = advance(children, alignedOffset(tableLength));
                }
            }
            children = advance(children, alignedOffset(childLength));
        }
        return std::nullopt;
    });
}

////// Checksums and text //////

quint32 crc32(const QByteArray& data, quint32 crc)
{
    static const auto table = [] {
        std::array<quint32, 256> t{};
        for (quint32 i = 0; i < 256; i++) {
            quint32 c = i;
            for (int k = 0; k < 8; k++) {
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            t[i] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (const char byte : data) {
        crc = table[(crc ^ static_cast<quint8>(byte)) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

std::optional<quint32> fileCrc32(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    quint32 crc = 0;
    while (!file.atEnd()) {
        const auto chunk = file.read(1 << 20);
        if (chunk.isEmpty()) {
            return std::nullopt;
        }
        crc = crc32(chunk, crc);
    }
    return crc;
}

QString fromWindows1252(QByteArrayView bytes)
{
    // 0x80-0x9F differ from Latin-1; the five bytes Windows-1252 leaves undefined map to the C1 controls (WHATWG)
    static constexpr char16_t high[32] = { 0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
                                           0x2039, 0x0152, 0x008D, 0x017D, 0x008F, 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                                           0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178 };
    QString out;
    out.reserve(bytes.size());
    for (const char byte : bytes) {
        const auto b = static_cast<quint8>(byte);
        out += b >= 0x80 && b < 0xA0 ? QChar(high[b - 0x80]) : QChar(b);
    }
    return out;
}

}  // namespace Loot
