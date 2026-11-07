//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "backend/event_bridge.hpp"

#include <vss/core/hub.hpp>
#include <vss/core/profile.hpp>

#include <QElapsedTimer>
#include <QMetaType>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <deque>
#include <iterator>
#include <limits>
#include <mutex>
#include <utility>
#include <vector>

namespace vss::gui
{

struct EventBridge::state
{
    mutable std::mutex mutex;
    std::deque<vss::endpoint_event> events;
    std::size_t max_events = 8192;
    std::atomic<std::uint64_t> dropped{0};
    std::atomic<std::uint64_t> delivered{0};
    std::atomic<bool> accepting{true};
};

namespace
{

gui_event convert(const vss::endpoint_event& event)
{
    gui_event out;
    out.kind = event.kind;
    out.endpoint = event.endpoint;
    out.name = QString::fromStdString(event.name);
    out.direction = event.direction;
    out.lines = event.lines;
    out.message = QString::fromStdString(event.message);
    if (event.error)
        out.error = QString::fromStdString(event.error.message());
    out.when = event.when;
    if (event.data && !event.data->empty())
    {
        out.data = QByteArray(reinterpret_cast<const char*>(event.data->data()),
                              static_cast<qsizetype>(event.data->size()));
    }
    return out;
}

} // namespace

EventBridge::EventBridge(vss::hub& hub, QObject* parent)
    : EventBridge(hub, options{}, parent)
{
}

EventBridge::EventBridge(vss::hub& hub, options settings, QObject* parent)
    : QObject(parent), hub_(hub), state_(std::make_shared<state>())
{
    state_->max_events = std::max<std::size_t>(settings.max_events, 1);
    max_batch_ = std::max<std::size_t>(settings.max_batch, 1);
    timer_ = new QTimer(this);
    timer_->setInterval(std::max(settings.interval_ms, 1));
    connect(timer_, &QTimer::timeout, this, &EventBridge::drain);
    qRegisterMetaType<vss::gui::gui_event>("vss::gui::gui_event");
    qRegisterMetaType<vss::gui::event_batch>("vss::gui::event_batch");
}

EventBridge::~EventBridge()
{
    stop();
}

void EventBridge::start()
{
    if (running_)
        return;

    // A restart starts clean: do not deliver anything queued before stop().
    state_->accepting.store(false);
    {
        std::lock_guard lock(state_->mutex);
        state_->events.clear();
    }
    state_->accepting.store(true);

    subscription_ = hub_.subscribe(
        {},
        [state = state_](const vss::endpoint_event& event)
        {
            if (!state->accepting.load())
                return;
            std::lock_guard lock(state->mutex);
            if (state->events.size() >= state->max_events)
            {
                state->events.pop_front();
                state->dropped.fetch_add(1, std::memory_order_relaxed);
            }
            state->events.push_back(event);
            state->delivered.fetch_add(1, std::memory_order_relaxed);
        });

    timer_->start();
    running_ = true;
}

void EventBridge::stop()
{
    state_->accepting.store(false);
    if (subscription_ != 0)
    {
        hub_.unsubscribe(subscription_);
        subscription_ = 0;
    }
    timer_->stop();
    running_ = false;
}

void EventBridge::set_interval(int milliseconds)
{
    timer_->setInterval(std::max(milliseconds, 1));
    if (running_)
        timer_->start();
}

int EventBridge::interval() const noexcept
{
    return timer_->interval();
}

void EventBridge::flush()
{
    Q_ASSERT_X(QThread::currentThread() == thread(), "EventBridge::flush",
               "drain on the GUI thread");
    if (QThread::currentThread() != thread())
        return;
    deliver(std::numeric_limits<std::size_t>::max());
}

std::uint64_t EventBridge::delivered() const noexcept
{
    return state_->delivered.load(std::memory_order_relaxed);
}

std::uint64_t EventBridge::dropped() const noexcept
{
    return state_->dropped.load(std::memory_order_relaxed);
}

std::size_t EventBridge::pending() const
{
    std::lock_guard lock(state_->mutex);
    return state_->events.size();
}

void EventBridge::drain()
{
    QElapsedTimer spent;
    spent.start();
    const bool more = deliver(max_batch_);
    last_slice_ms_ = static_cast<int>(spent.elapsed());
    if (more)
        continue_draining();
}

void EventBridge::continue_draining()
{
    if (continuing_)
        return;
    continuing_ = true;
    // A turn of the event loop of its own, after a pause twice as long as the
    // last slice took, so the window keeps at least two thirds of its time for
    // input, painting and its other timers however fast events arrive.
    const int pause = std::max(1, 2 * last_slice_ms_);
    QTimer::singleShot(pause, this,
                       [this]
                       {
                           continuing_ = false;
                           if (!running_)
                               return;
                           QElapsedTimer spent;
                           spent.start();
                           const bool more = deliver(max_batch_);
                           last_slice_ms_ = static_cast<int>(spent.elapsed());
                           if (more)
                               continue_draining();
                       });
}

bool EventBridge::deliver(std::size_t limit)
{
    VSS_PROFILE_ZONE("gui.bridge.drain");
    std::deque<vss::endpoint_event> pending;
    bool more = false;
    {
        std::lock_guard lock(state_->mutex);
        if (state_->events.size() <= limit)
        {
            pending.swap(state_->events);
        }
        else
        {
            const auto cut =
                state_->events.begin() + static_cast<std::ptrdiff_t>(limit);
            pending.assign(std::make_move_iterator(state_->events.begin()),
                           std::make_move_iterator(cut));
            state_->events.erase(state_->events.begin(), cut);
            more = true;
        }
    }

    if (pending.empty())
        return false;
    VSS_PROFILE_PLOT("gui.bridge.batch_events", pending.size());
    VSS_PROFILE_PLOT("gui.bridge.backlog", more ? state_->events.size() : 0);

    event_batch batch;
    batch.reserve(static_cast<qsizetype>(pending.size()));
    for (const auto& event : pending)
        batch.push_back(convert(event));

    {
        VSS_PROFILE_ZONE("gui.bridge.emit");
        Q_EMIT batch_ready(batch);
    }
    return more;
}

} // namespace vss::gui
