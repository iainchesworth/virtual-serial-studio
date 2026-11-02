//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "dialogs/network_uri.hpp"

#include <QCoreApplication>
#include <QStringList>

#include <algorithm>

namespace vss::gui
{

QString network_scheme(network_transport transport)
{
    switch (transport)
    {
    case network_transport::tcp:
        return QStringLiteral("tcp");
    case network_transport::udp:
        return QStringLiteral("udp");
    case network_transport::rfc2217:
        return QStringLiteral("rfc2217");
    }
    return QStringLiteral("tcp");
}

QString network_transport_name(network_transport transport)
{
    switch (transport)
    {
    case network_transport::tcp:
        return QStringLiteral("TCP");
    case network_transport::udp:
        return QStringLiteral("UDP");
    case network_transport::rfc2217:
        return QStringLiteral("RFC 2217");
    }
    return QStringLiteral("TCP");
}

QString build_network_uri(network_transport transport, const QString& host,
                          int port)
{
    QString address = host.trimmed();
    if (address.contains(QLatin1Char(':')) &&
        !address.startsWith(QLatin1Char('[')))
    {
        address = QStringLiteral("[%1]").arg(address);
    }
    return QStringLiteral("%1://%2:%3")
        .arg(network_scheme(transport), address, QString::number(port));
}

bool valid_port(int port)
{
    return port >= 1 && port <= 65535;
}

bool parse_port(const QString& text, int& port)
{
    bool ok = false;
    const int value = text.trimmed().toInt(&ok);
    if (!ok || !valid_port(value))
        return false;
    port = value;
    return true;
}

std::optional<network_endpoint> parse_network_uri(const QString& text,
                                                  QString* error)
{
    if (error != nullptr)
        error->clear();
    const QString value = text.trimmed();
    const int scheme_end = value.indexOf(QStringLiteral("://"));
    if (scheme_end <= 0)
    {
        if (error != nullptr)
            *error = network_address_error();
        return std::nullopt;
    }

    const QString scheme = value.left(scheme_end);
    network_endpoint endpoint;
    if (scheme == QLatin1String("tcp"))
        endpoint.transport = network_transport::tcp;
    else if (scheme == QLatin1String("udp"))
        endpoint.transport = network_transport::udp;
    else if (scheme == QLatin1String("rfc2217"))
        endpoint.transport = network_transport::rfc2217;
    else
    {
        if (error != nullptr)
            *error = network_address_error();
        return std::nullopt;
    }

    QString rest = value.mid(scheme_end + 3);
    const int query = rest.indexOf(QLatin1Char('?'));
    if (query >= 0)
        rest = rest.left(query);
    if (rest.isEmpty())
    {
        if (error != nullptr)
            *error = network_address_error();
        return std::nullopt;
    }

    QString host;
    QString port_text;
    if (rest.startsWith(QLatin1Char('[')))
    {
        const int close = rest.indexOf(QLatin1Char(']'));
        if (close < 0 || close + 1 >= rest.size() ||
            rest.at(close + 1) != QLatin1Char(':'))
        {
            if (error != nullptr)
                *error = network_address_error();
            return std::nullopt;
        }
        host = rest.mid(1, close - 1);
        port_text = rest.mid(close + 2);
    }
    else
    {
        const int colon = rest.lastIndexOf(QLatin1Char(':'));
        if (colon <= 0)
        {
            if (error != nullptr)
                *error = network_address_error();
            return std::nullopt;
        }
        host = rest.left(colon);
        port_text = rest.mid(colon + 1);
    }

    int port = 0;
    if (!parse_port(port_text, port))
    {
        if (error != nullptr)
            *error = network_port_error();
        return std::nullopt;
    }
    if (host.trimmed().isEmpty() || host.contains(QLatin1Char(' ')) ||
        host.contains(QLatin1Char('/')))
    {
        if (error != nullptr)
            *error = network_address_error();
        return std::nullopt;
    }

    endpoint.host = host;
    endpoint.port = port;
    return endpoint;
}

int network_port_from_uri(const QString& uri)
{
    const auto parsed = parse_network_uri(uri);
    return parsed ? parsed->port : 0;
}

QString network_address_error()
{
    return QCoreApplication::translate(
        "NetworkEndpoint", "Enter an address such as tcp://192.168.1.50:4001.");
}

QString network_port_error()
{
    return QCoreApplication::translate("NetworkEndpoint",
                                       "Choose a port from 1 to 65535.");
}

} // namespace vss::gui
