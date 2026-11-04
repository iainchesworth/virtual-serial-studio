//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "advanced/json_validation.hpp"

#include "backend/view_types.hpp"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cstddef>

namespace vss::gui
{
namespace
{

QString tr(const char* text)
{
    return QCoreApplication::translate("TopologyJson", text);
}

QString ports_check_text(int count, bool unique)
{
    if (unique)
    {
        return count == 1 ? tr("1 port, all names unique")
                          : tr("%1 ports, all names unique").arg(count);
    }
    return count == 1 ? tr("1 port, a name is used twice")
                      : tr("%1 ports, a name is used twice").arg(count);
}

QString links_check_text(int count, bool no_loops)
{
    if (no_loops)
    {
        return count == 1 ? tr("1 link, no loops")
                          : tr("%1 links, no loops").arg(count);
    }
    return count == 1 ? tr("1 link, a loop was found")
                      : tr("%1 links, a loop was found").arg(count);
}

int edit_distance(const QString& left, const QString& right)
{
    const int rows = static_cast<int>(left.size()) + 1;
    const int columns = static_cast<int>(right.size()) + 1;
    QVector<int> previous(columns);
    QVector<int> current(columns);
    for (int column = 0; column < columns; ++column)
        previous[column] = column;
    for (int row = 1; row < rows; ++row)
    {
        current[0] = row;
        for (int column = 1; column < columns; ++column)
        {
            const int cost = left.at(row - 1) == right.at(column - 1) ? 0 : 1;
            current[column] =
                std::min({previous[column] + 1, current[column - 1] + 1,
                          previous[column - 1] + cost});
        }
        previous = current;
    }
    return previous.last();
}

QString closest_mode(const QString& candidate)
{
    QString best;
    int best_distance = 3;
    for (const char* mode : vss::config::link_modes)
    {
        const QString name = QString::fromLatin1(mode);
        const int distance = edit_distance(candidate.toLower(), name);
        if (distance < best_distance)
        {
            best_distance = distance;
            best = name;
        }
    }
    return best;
}

struct string_match
{
    int line = 0;
    QString value;
};

/// Every `"key": "value"` occurrence with its 1-based line.  Used to put a
/// schema error on the line the user can see, without changing the schema.
QVector<string_match> string_matches(const QString& text, const QString& key)
{
    QVector<string_match> out;
    const QString pattern = QLatin1Char('"') + QRegularExpression::escape(key) +
                            QLatin1String("\"\\s*:\\s*\"([^\"]*)\"");
    const QRegularExpression expression(pattern);
    auto matches = expression.globalMatch(text);
    while (matches.hasNext())
    {
        const QRegularExpressionMatch match = matches.next();
        out.push_back(
            {line_for_offset(text, static_cast<int>(match.capturedStart())),
             match.captured(1)});
    }
    return out;
}

int nth_line(const QVector<string_match>& matches, int index)
{
    if (index < 0 || index >= matches.size())
        return 0;
    return matches.at(index).line;
}

/// The line of a key regardless of its value type, for `"version": 1`.
int key_line(const QString& text, const QString& key, int occurrence = 0)
{
    const QRegularExpression expression(QLatin1Char('"') +
                                        QRegularExpression::escape(key) +
                                        QLatin1String("\"\\s*:"));
    auto matches = expression.globalMatch(text);
    int seen = 0;
    while (matches.hasNext())
    {
        const QRegularExpressionMatch match = matches.next();
        if (seen == occurrence)
            return line_for_offset(text,
                                   static_cast<int>(match.capturedStart()));
        ++seen;
    }
    return 0;
}

bool reachable(const QHash<QString, QSet<QString>>& edges, const QString& from,
               const QString& to)
{
    if (from == to)
        return true;
    QSet<QString> visited;
    QVector<QString> pending{from};
    while (!pending.isEmpty())
    {
        const QString current = pending.takeLast();
        if (visited.contains(current))
            continue;
        visited.insert(current);
        const auto it = edges.constFind(current);
        if (it == edges.constEnd())
            continue;
        for (const QString& next : it.value())
        {
            if (next == to)
                return true;
            pending.push_back(next);
        }
    }
    return false;
}

} // namespace

int line_for_offset(const QString& text, int offset)
{
    const int limit = std::clamp(offset, 0, static_cast<int>(text.size()));
    int line = 1;
    for (int index = 0; index < limit; ++index)
    {
        if (text.at(index) == QLatin1Char('\n'))
            ++line;
    }
    return line;
}

QString json_validation::status_text() const
{
    if (valid())
        return tr("Valid, version 1");
    if (problem)
        return problem->summary;
    return tr("Not valid");
}

json_validation validate_json_document(const QString& text)
{
    json_validation out;

    QJsonParseError parse_error;
    const QJsonDocument document =
        QJsonDocument::fromJson(text.toUtf8(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError)
    {
        const int line =
            line_for_offset(text, static_cast<int>(parse_error.offset));
        out.checks = {{tr("Valid JSON, version 1"), check_state::failed},
                      {tr("Ports, all names unique"), check_state::not_checked},
                      {tr("Links, no loops"), check_state::not_checked}};
        out.problem = json_problem{
            tr("Line %1: %2").arg(line).arg(parse_error.errorString()),
            tr("The line is highlighted and nothing was applied."),
            {},
            {},
            line};
        return out;
    }
    out.json_valid = true;

    if (!document.isObject())
    {
        out.checks = {{tr("Valid JSON, version 1"), check_state::failed},
                      {tr("Ports, all names unique"), check_state::not_checked},
                      {tr("Links, no loops"), check_state::not_checked}};
        out.problem =
            json_problem{tr("Line 1: the document must be a JSON object"),
                         tr("A setup file starts with a { and ends with a }."),
                         {},
                         {},
                         1};
        return out;
    }

    const QJsonObject root = document.object();
    const QJsonValue version_value = root.value(QStringLiteral("version"));
    out.version = version_value.isDouble() ? version_value.toInt() : 0;
    if (out.version != 1)
    {
        const int line = key_line(text, QStringLiteral("version"));
        out.checks = {{tr("Valid JSON, version 1"), check_state::failed},
                      {tr("Ports, all names unique"), check_state::not_checked},
                      {tr("Links, no loops"), check_state::not_checked}};
        json_problem problem;
        problem.line = line;
        problem.summary = line > 0 ? tr("Line %1: version must be 1").arg(line)
                                   : tr("The document needs \"version\": 1");
        problem.detail = tr("Nothing was applied.");
        out.problem = problem;
        return out;
    }

    out.name =
        root.value(QStringLiteral("name")).toString(QStringLiteral("untitled"));
    if (out.name.isEmpty())
        out.name = QStringLiteral("untitled");

    // -- ports ------------------------------------------------------------
    QSet<QString> port_names;
    const QJsonValue ports_value = root.value(QStringLiteral("ports"));
    if (!ports_value.isUndefined() && !ports_value.isArray())
    {
        out.checks = {{tr("Valid JSON, version 1"), check_state::passed},
                      {tr("Ports, all names unique"), check_state::failed},
                      {tr("Links, no loops"), check_state::not_checked}};
        out.problem = json_problem{tr("\"ports\" must be a list"),
                                   tr("Nothing was applied."),
                                   {},
                                   {},
                                   0};
        return out;
    }

    const QJsonArray ports = ports_value.toArray();
    const auto name_lines = string_matches(text, QStringLiteral("name"));
    const auto kind_lines = string_matches(text, QStringLiteral("kind"));
    const auto virtual_lines =
        string_matches(text, QStringLiteral("virtual_kind"));
    for (int index = 0; index < ports.size(); ++index)
    {
        const QJsonValue value = ports.at(index);
        if (!value.isObject())
        {
            out.checks = {{tr("Valid JSON, version 1"), check_state::passed},
                          {tr("Ports, all names unique"), check_state::failed},
                          {tr("Links, no loops"), check_state::not_checked}};
            out.problem = json_problem{tr("Each port must be an object"),
                                       tr("Nothing was applied."),
                                       {},
                                       {},
                                       0};
            return out;
        }
        const QJsonObject port = value.toObject();
        const QJsonValue name_value = port.value(QStringLiteral("name"));
        const QString name = name_value.toString();
        if (!name_value.isString() || name.isEmpty())
        {
            out.checks = {{tr("Valid JSON, version 1"), check_state::passed},
                          {tr("Ports, all names unique"), check_state::failed},
                          {tr("Links, no loops"), check_state::not_checked}};
            json_problem problem;
            problem.line = nth_line(kind_lines, index);
            problem.summary =
                problem.has_line()
                    ? tr("Line %1: a port has no name").arg(problem.line)
                    : tr("A port has no name");
            problem.detail =
                tr("Every port needs a name so links can refer to it.");
            out.problem = problem;
            return out;
        }
        if (port_names.contains(name))
        {
            int line = 0;
            int seen = 0;
            for (const auto& match : name_lines)
            {
                if (match.value != name)
                    continue;
                ++seen;
                if (seen == 2)
                {
                    line = match.line;
                    break;
                }
            }
            out.checks = {{tr("Valid JSON, version 1"), check_state::passed},
                          {tr("Ports, all names unique"), check_state::failed},
                          {tr("Links, no loops"), check_state::not_checked}};
            json_problem problem;
            problem.line = line;
            problem.summary =
                line > 0 ? tr("Line %1: the name \"%2\" is used twice")
                               .arg(line)
                               .arg(name)
                         : tr("The name \"%1\" is used twice").arg(name);
            problem.detail =
                tr("Port names identify ports in links, so each name must be "
                   "unique. Nothing was applied.");
            out.problem = problem;
            return out;
        }
        port_names.insert(name);

        const QString kind = port.value(QStringLiteral("kind"))
                                 .toString(QStringLiteral("virtual"));
        bool kind_known = false;
        for (const char* candidate : vss::config::port_kinds)
        {
            if (kind == QLatin1String(candidate))
                kind_known = true;
        }
        if (!kind_known)
        {
            int line = 0;
            for (int position = 0; position < kind_lines.size(); ++position)
            {
                if (kind_lines.at(position).value == kind)
                {
                    line = kind_lines.at(position).line;
                    break;
                }
            }
            out.checks = {{tr("Valid JSON, version 1"), check_state::passed},
                          {tr("Ports, all names unique"), check_state::failed},
                          {tr("Links, no loops"), check_state::not_checked}};
            json_problem problem;
            problem.line = line;
            problem.summary = line > 0
                                  ? tr("Line %1: unknown port kind \"%2\"")
                                        .arg(line)
                                        .arg(kind)
                                  : tr("Unknown port kind \"%1\"").arg(kind);
            problem.detail = tr(
                "Use virtual, physical, tcp, udp, rfc2217 or mock. Nothing was "
                "applied.");
            out.problem = problem;
            return out;
        }

        const QString virtual_kind = port.value(QStringLiteral("virtual_kind"))
                                         .toString(QStringLiteral("auto"));
        if (kind == QLatin1String("virtual"))
        {
            bool virtual_known = false;
            for (const char* candidate : vss::config::virtual_port_kinds)
            {
                if (virtual_kind == QLatin1String(candidate))
                    virtual_known = true;
            }
            if (!virtual_known)
            {
                int line = 0;
                for (int position = 0; position < virtual_lines.size();
                     ++position)
                {
                    if (virtual_lines.at(position).value == virtual_kind)
                    {
                        line = virtual_lines.at(position).line;
                        break;
                    }
                }
                out.checks = {
                    {tr("Valid JSON, version 1"), check_state::passed},
                    {tr("Ports, all names unique"), check_state::failed},
                    {tr("Links, no loops"), check_state::not_checked}};
                json_problem problem;
                problem.line = line;
                problem.summary =
                    line > 0
                        ? tr("Line %1: unknown virtual kind \"%2\"")
                              .arg(line)
                              .arg(virtual_kind)
                        : tr("Unknown virtual kind \"%1\"").arg(virtual_kind);
                problem.detail =
                    tr("Use auto, pty, named-pipe, loopback or windows-driver. "
                       "Nothing was applied.");
                out.problem = problem;
                return out;
            }
        }

        if (kind != QLatin1String("virtual") && kind != QLatin1String("mock"))
        {
            const QString uri = port.value(QStringLiteral("uri")).toString();
            if (uri.isEmpty())
            {
                out.checks = {
                    {tr("Valid JSON, version 1"), check_state::passed},
                    {tr("Ports, all names unique"), check_state::failed},
                    {tr("Links, no loops"), check_state::not_checked}};
                json_problem problem;
                problem.line = nth_line(kind_lines, index);
                problem.summary =
                    problem.has_line()
                        ? tr("Line %1: port \"%2\" has no address")
                              .arg(problem.line)
                              .arg(name)
                        : tr("Port \"%1\" has no address").arg(name);
                problem.detail =
                    tr("A physical or network port needs a uri. Nothing was "
                       "applied.");
                out.problem = problem;
                return out;
            }
        }
    }

    // -- links ------------------------------------------------------------
    const QJsonValue links_value = root.value(QStringLiteral("links"));
    if (!links_value.isUndefined() && !links_value.isArray())
    {
        out.checks = {{tr("Valid JSON, version 1"), check_state::passed},
                      {tr("Ports, all names unique"), check_state::passed},
                      {tr("Links, no loops"), check_state::failed}};
        out.problem = json_problem{tr("\"links\" must be a list"),
                                   tr("Nothing was applied."),
                                   {},
                                   {},
                                   0};
        return out;
    }

    const QJsonArray links = links_value.toArray();
    out.ports = static_cast<int>(ports.size());
    out.links = static_cast<int>(links.size());
    const auto mode_lines = string_matches(text, QStringLiteral("mode"));
    QHash<QString, QSet<QString>> edges;
    for (int index = 0; index < links.size(); ++index)
    {
        const QJsonValue value = links.at(index);
        const int link_line = nth_line(mode_lines, index);
        if (!value.isObject())
        {
            out.checks = {{tr("Valid JSON, version 1"), check_state::passed},
                          {tr("Ports, all names unique"), check_state::passed},
                          {tr("Links, no loops"), check_state::failed}};
            json_problem problem;
            problem.line = link_line;
            problem.summary =
                link_line > 0
                    ? tr("Line %1: each link must be an object").arg(link_line)
                    : tr("Each link must be an object");
            problem.detail = tr("Nothing was applied.");
            out.problem = problem;
            return out;
        }
        const QJsonObject link = value.toObject();
        const QString mode = link.value(QStringLiteral("mode"))
                                 .toString(QStringLiteral("duplex"));
        bool mode_known = false;
        for (const char* candidate : vss::config::link_modes)
        {
            if (mode == QLatin1String(candidate))
                mode_known = true;
        }
        if (!mode_known)
        {
            out.checks = {{tr("Valid JSON, version 1"), check_state::passed},
                          {tr("Ports, all names unique"), check_state::passed},
                          {tr("Links, no loops"), check_state::failed}};
            json_problem problem;
            problem.line = link_line;
            problem.summary = link_line > 0
                                  ? tr("Line %1: unknown link mode \"%2\"")
                                        .arg(link_line)
                                        .arg(mode)
                                  : tr("Unknown link mode \"%1\"").arg(mode);
            const QString suggestion = closest_mode(mode);
            if (!suggestion.isEmpty() && suggestion != mode)
            {
                problem.detail =
                    tr("Did you mean \"%1\"? The line is highlighted and "
                       "nothing was applied.")
                        .arg(suggestion);
                problem.fix_from = mode;
                problem.fix_to = suggestion;
            }
            else
            {
                problem.detail = tr(
                    "Use duplex, split, join, redirect or monitor. Nothing was "
                    "applied.");
            }
            out.problem = problem;
            return out;
        }

        QStringList sources;
        QStringList targets;
        const auto read_names = [](const QJsonObject& object, const char* key,
                                   QStringList& out_list) -> bool
        {
            const QJsonValue array_value =
                object.value(QString::fromLatin1(key));
            if (!array_value.isArray())
                return false;
            const QJsonArray array = array_value.toArray();
            for (const QJsonValue& entry : array)
            {
                if (!entry.isString())
                    return false;
                out_list << entry.toString();
            }
            return !out_list.isEmpty();
        };
        if (!read_names(link, "sources", sources) ||
            !read_names(link, "targets", targets))
        {
            out.checks = {{tr("Valid JSON, version 1"), check_state::passed},
                          {tr("Ports, all names unique"), check_state::passed},
                          {tr("Links, no loops"), check_state::failed}};
            json_problem problem;
            problem.line = link_line;
            problem.summary =
                link_line > 0 ? tr("Line %1: a link needs ports on both sides")
                                    .arg(link_line)
                              : tr("A link needs ports on both sides");
            problem.detail =
                tr("sources and targets must be non-empty lists of port names. "
                   "Nothing was applied.");
            out.problem = problem;
            return out;
        }

        const bool one_source = sources.size() == 1;
        const bool one_target = targets.size() == 1;
        bool arity_ok = true;
        if (mode == QLatin1String("duplex") ||
            mode == QLatin1String("redirect") ||
            mode == QLatin1String("monitor"))
            arity_ok = one_source && one_target;
        else if (mode == QLatin1String("split"))
            arity_ok = one_source;
        else if (mode == QLatin1String("join"))
            arity_ok = one_target;
        if (!arity_ok)
        {
            out.checks = {{tr("Valid JSON, version 1"), check_state::passed},
                          {tr("Ports, all names unique"), check_state::passed},
                          {tr("Links, no loops"), check_state::failed}};
            json_problem problem;
            problem.line = link_line;
            problem.summary =
                link_line > 0
                    ? tr("Line %1: \"%2\" has the wrong number of ports")
                          .arg(link_line)
                          .arg(mode)
                    : tr("\"%1\" has the wrong number of ports").arg(mode);
            problem.detail = tr(
                "A duplex, redirect or monitor link joins one port to one "
                "port; a split has one source; a join has one target. Nothing "
                "was applied.");
            out.problem = problem;
            return out;
        }

        for (const QString& name : sources + targets)
        {
            if (!port_names.contains(name))
            {
                out.checks = {
                    {tr("Valid JSON, version 1"), check_state::passed},
                    {tr("Ports, all names unique"), check_state::passed},
                    {tr("Links, no loops"), check_state::failed}};
                json_problem problem;
                problem.line = link_line;
                problem.summary =
                    link_line > 0 ? tr("Line %1: no port is called \"%2\"")
                                        .arg(link_line)
                                        .arg(name)
                                  : tr("No port is called \"%1\"").arg(name);
                problem.detail = tr("Nothing was applied.");
                out.problem = problem;
                return out;
            }
        }

        // A loop is refused the same way the hub refuses one: check the
        // routes that already exist before adding this link.
        bool loop = false;
        for (const QString& source : sources)
        {
            for (const QString& target : targets)
            {
                if (source == target)
                    loop = true;
                if (reachable(edges, target, source))
                    loop = true;
                if (mode == QLatin1String("duplex") &&
                    reachable(edges, source, target))
                    loop = true;
            }
        }
        if (loop)
        {
            out.checks = {{tr("Valid JSON, version 1"), check_state::passed},
                          {tr("Ports, all names unique"), check_state::passed},
                          {tr("Links, no loops"), check_state::failed}};
            json_problem problem;
            problem.line = link_line;
            problem.summary =
                link_line > 0
                    ? tr("Line %1: these links create a loop").arg(link_line)
                    : tr("These links create a loop");
            problem.detail = tr(
                "Bytes would go round forever, so the hub refuses a loop and "
                "nothing was applied.");
            out.problem = problem;
            return out;
        }
        for (const QString& source : sources)
        {
            for (const QString& target : targets)
            {
                edges[source].insert(target);
                if (mode == QLatin1String("duplex"))
                    edges[target].insert(source);
            }
        }
    }

    // -- the engine has the final word ------------------------------------
    out.checks = {{tr("Valid JSON, version 1"), check_state::passed},
                  {ports_check_text(out.ports, true), check_state::passed},
                  {links_check_text(out.links, true), check_state::passed}};
    const auto parsed = vss::config::from_json(text.toStdString());
    if (!parsed)
    {
        out.schema_valid = false;
        out.checks.push_back({tr("The setup format rejected this document"),
                              check_state::failed});
        out.problem =
            json_problem{tr("The setup format rejected this document"),
                         error_text(parsed.error()),
                         {},
                         {},
                         0};
        return out;
    }
    out.document = *parsed;
    out.name = QString::fromStdString(out.document.name);
    out.ports = static_cast<int>(out.document.ports.size());
    out.links = static_cast<int>(out.document.links.size());
    out.checks[1] = {ports_check_text(out.ports, true), check_state::passed};
    out.checks[2] = {links_check_text(out.links, true), check_state::passed};
    const auto formatted = vss::config::to_json(out.document, true);
    if (!formatted)
    {
        out.schema_valid = false;
        out.problem =
            json_problem{tr("The setup format rejected this document"),
                         error_text(formatted.error()),
                         {},
                         {},
                         0};
        return out;
    }
    out.formatted = QString::fromStdString(*formatted).trimmed();
    out.schema_valid = true;
    return out;
}

} // namespace vss::gui
