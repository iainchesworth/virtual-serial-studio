//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_PANES_FIXTURE_BEHAVIOUR_HPP
#define VSS_GUI_PANES_FIXTURE_BEHAVIOUR_HPP

#include "backend/studio_backend.hpp"

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>

class QTimer;

namespace vss::gui
{

/// What a fixture's byte generator sends on each tick.
enum class generator_kind
{
    /// The hex bytes typed, the same every time.
    fixed_pattern,
    /// Bytes that count up from 00 and wrap after FF.
    counter,
    /// Random bytes.
    random_bytes,
    /// A line of text with CRLF.
    text_line
};

struct generator_config
{
    generator_kind kind = generator_kind::text_line;
    /// Hex for a fixed pattern, text for a text line.
    QString text = QStringLiteral("Hello from the fixture");
    /// Sends each second.
    int per_second = 1;
    /// Bytes in each send of a counter or random generator.
    int chunk = 1;

    friend bool operator==(const generator_config&,
                           const generator_config&) = default;
};

/// The things a mock fixture does by itself: an echo, and a byte generator.
///
/// The generator is sends on a timer in the GUI thread through the same
/// `StudioBackend::send` the Send panel uses, bound to the fixture it was
/// started on.  The engine's scripted device cannot count or randomise, so
/// this is not a stand-in for an engine feature.  Echo is the engine's: a
/// backend without `Capabilities::fixture_echo` refuses it and the controls
/// show it disabled.
class FixtureBehaviours final : public QObject
{
    Q_OBJECT

public:
    explicit FixtureBehaviours(StudioBackend& backend,
                               QObject* parent = nullptr);

    /// Whether the port is a mock fixture.
    bool is_fixture(vss::endpoint_id id) const;

    // -- generator --------------------------------------------------------

    generator_config config(vss::endpoint_id id) const;
    /// Changes the settings.  A running generator picks up the new rate and
    /// bytes on its next tick.
    void set_config(vss::endpoint_id id, const generator_config& config);
    bool generating(vss::endpoint_id id) const;
    /// Starts the generator.  False, with `error_message`, when the pattern is
    /// not valid hex or the port is not a fixture.
    bool start_generator(vss::endpoint_id id);
    void stop_generator(vss::endpoint_id id);
    void toggle_generator(vss::endpoint_id id);
    std::uint64_t generated_bytes(vss::endpoint_id id) const;
    /// What one tick sends.  Advances a counter.
    QByteArray next_payload(vss::endpoint_id id);
    /// One tick, as the timer does it.
    void tick(vss::endpoint_id id);

    // -- echo -------------------------------------------------------------

    bool echo_available() const;
    bool echoing(vss::endpoint_id id) const;
    bool set_echo(vss::endpoint_id id, bool on);
    void toggle_echo(vss::endpoint_id id);

    /// Hex pairs such as "55 AA" to bytes; false when they are not pairs.
    static bool parse_hex(const QString& text, QByteArray& out);

Q_SIGNALS:
    /// A generator started or stopped, or its settings or count changed.
    void changed(vss::endpoint_id id);
    void error_message(const QString& message);

private:
    struct state
    {
        generator_config config;
        QTimer* timer = nullptr;
        bool running = false;
        std::uint64_t bytes = 0;
        unsigned char counter = 0;
    };

    void prune();
    int interval_ms(const generator_config& config) const;

    StudioBackend& backend_;
    QHash<vss::endpoint_id, state> states_;
};

} // namespace vss::gui

#endif // VSS_GUI_PANES_FIXTURE_BEHAVIOUR_HPP
