//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "panes/fixture_behaviour.hpp"

#include <vss/core/profile.hpp>

#include <QRandomGenerator>
#include <QTimer>

#include <algorithm>

namespace vss::gui
{
namespace
{

constexpr int max_per_second = 50;
constexpr int max_chunk = 256;

} // namespace

FixtureBehaviours::FixtureBehaviours(StudioBackend& backend, QObject* parent)
    : QObject(parent), backend_(backend)
{
    connect(&backend_, &StudioBackend::ports_changed, this,
            [this] { prune(); });
}

bool FixtureBehaviours::is_fixture(vss::endpoint_id id) const
{
    const auto port = backend_.port(id);
    return port && port->kind == vss::endpoint_kind::mock;
}

generator_config FixtureBehaviours::config(vss::endpoint_id id) const
{
    const auto found = states_.constFind(id);
    return found == states_.constEnd() ? generator_config{} : found->config;
}

int FixtureBehaviours::interval_ms(const generator_config& config) const
{
    const int rate = std::clamp(config.per_second, 1, max_per_second);
    return std::max(1, 1000 / rate);
}

void FixtureBehaviours::set_config(vss::endpoint_id id,
                                   const generator_config& config)
{
    generator_config clean = config;
    clean.per_second = std::clamp(clean.per_second, 1, max_per_second);
    clean.chunk = std::clamp(clean.chunk, 1, max_chunk);
    state& entry = states_[id];
    if (entry.config == clean)
        return;
    entry.config = clean;
    if (entry.running && entry.timer != nullptr)
        entry.timer->setInterval(interval_ms(clean));
    Q_EMIT changed(id);
}

bool FixtureBehaviours::generating(vss::endpoint_id id) const
{
    const auto found = states_.constFind(id);
    return found != states_.constEnd() && found->running;
}

bool FixtureBehaviours::parse_hex(const QString& text, QByteArray& out)
{
    QString digits;
    for (const QChar ch : text)
    {
        if (ch.isSpace())
            continue;
        if (!ch.isDigit() && (ch.toLower() < QLatin1Char('a') ||
                              ch.toLower() > QLatin1Char('f')))
            return false;
        digits.append(ch);
    }
    if (digits.isEmpty() || digits.size() % 2 != 0)
        return false;
    out.clear();
    for (qsizetype index = 0; index < digits.size(); index += 2)
    {
        bool ok = false;
        const int value = digits.mid(index, 2).toInt(&ok, 16);
        if (!ok)
            return false;
        out.append(static_cast<char>(value));
    }
    return true;
}

bool FixtureBehaviours::start_generator(vss::endpoint_id id)
{
    if (!is_fixture(id))
    {
        Q_EMIT error_message(tr("Only a mock fixture has a byte generator."));
        return false;
    }
    state& entry = states_[id];
    if (entry.running)
        return true;
    if (entry.config.kind == generator_kind::fixed_pattern)
    {
        QByteArray pattern;
        if (!parse_hex(entry.config.text, pattern))
        {
            Q_EMIT error_message(
                tr("The pattern needs pairs of hex digits, such as 55 AA."));
            return false;
        }
    }
    if (entry.config.kind == generator_kind::text_line &&
        entry.config.text.isEmpty())
    {
        Q_EMIT error_message(tr("Type the line to send."));
        return false;
    }
    if (entry.timer == nullptr)
    {
        entry.timer = new QTimer(this);
        entry.timer->setTimerType(Qt::PreciseTimer);
        connect(entry.timer, &QTimer::timeout, this, [this, id] { tick(id); });
    }
    entry.running = true;
    entry.timer->start(interval_ms(entry.config));
    Q_EMIT changed(id);
    return true;
}

void FixtureBehaviours::stop_generator(vss::endpoint_id id)
{
    const auto found = states_.find(id);
    if (found == states_.end() || !found->running)
        return;
    found->running = false;
    if (found->timer != nullptr)
        found->timer->stop();
    Q_EMIT changed(id);
}

void FixtureBehaviours::toggle_generator(vss::endpoint_id id)
{
    if (generating(id))
        stop_generator(id);
    else
        start_generator(id);
}

std::uint64_t FixtureBehaviours::generated_bytes(vss::endpoint_id id) const
{
    const auto found = states_.constFind(id);
    return found == states_.constEnd() ? 0 : found->bytes;
}

QByteArray FixtureBehaviours::next_payload(vss::endpoint_id id)
{
    state& entry = states_[id];
    const generator_config& config = entry.config;
    QByteArray bytes;
    switch (config.kind)
    {
    case generator_kind::fixed_pattern:
        parse_hex(config.text, bytes);
        break;
    case generator_kind::counter:
        for (int index = 0; index < config.chunk; ++index)
            bytes.append(static_cast<char>(entry.counter++));
        break;
    case generator_kind::random_bytes:
        for (int index = 0; index < config.chunk; ++index)
        {
            bytes.append(
                static_cast<char>(QRandomGenerator::global()->bounded(256)));
        }
        break;
    case generator_kind::text_line:
        bytes = config.text.toUtf8();
        bytes.append("\r\n");
        break;
    }
    return bytes;
}

void FixtureBehaviours::tick(vss::endpoint_id id)
{
    VSS_PROFILE_ZONE("gui.fixture.tick");
    const auto found = states_.constFind(id);
    if (found == states_.constEnd() || !found->running)
        return;
    const QByteArray bytes = next_payload(id);
    if (bytes.isEmpty())
        return;
    // Bound to the fixture it was started on: `id` is captured by the timer,
    // never read from the selection.
    const auto sent = backend_.send(id, bytes);
    if (!sent)
    {
        stop_generator(id);
        Q_EMIT error_message(
            tr("The generator stopped: %1").arg(error_text(sent.error())));
        return;
    }
    states_[id].bytes += static_cast<std::uint64_t>(bytes.size());
    Q_EMIT changed(id);
}

bool FixtureBehaviours::echo_available() const
{
    return backend_.caps().fixture_echo;
}

bool FixtureBehaviours::echoing(vss::endpoint_id id) const
{
    return backend_.echo(id);
}

bool FixtureBehaviours::set_echo(vss::endpoint_id id, bool on)
{
    const auto result = backend_.set_echo(id, on);
    if (!result)
    {
        Q_EMIT error_message(error_text(result.error()));
        return false;
    }
    Q_EMIT changed(id);
    return true;
}

void FixtureBehaviours::toggle_echo(vss::endpoint_id id)
{
    set_echo(id, !echoing(id));
}

void FixtureBehaviours::prune()
{
    const auto ports = backend_.ports();
    QList<vss::endpoint_id> gone;
    for (auto it = states_.cbegin(); it != states_.cend(); ++it)
    {
        const vss::endpoint_id id = it.key();
        const bool present =
            std::any_of(ports.begin(), ports.end(),
                        [id](const port_view& view) { return view.id == id; });
        if (!present)
            gone.append(id);
    }
    for (const vss::endpoint_id id : gone)
    {
        const auto found = states_.find(id);
        if (found->timer != nullptr)
        {
            found->timer->stop();
            found->timer->deleteLater();
        }
        states_.erase(found);
        Q_EMIT changed(id);
    }
}

} // namespace vss::gui
