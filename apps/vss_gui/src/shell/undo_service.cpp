//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "shell/undo_service.hpp"

#include <utility>

namespace vss::gui
{

UndoService::UndoService(QObject* parent) : QObject(parent) {}

void UndoService::push(undo_step step)
{
    redo_.clear();
    undo_.push_back(std::move(step));
    while (undo_.size() > max_depth)
        undo_.pop_front();
    Q_EMIT changed();
}

QString UndoService::undo_text() const
{
    return undo_.empty() ? QString() : undo_.back().text;
}

QString UndoService::redo_text() const
{
    return redo_.empty() ? QString() : redo_.back().text;
}

bool UndoService::undo()
{
    if (undo_.empty())
        return false;
    undo_step step = std::move(undo_.back());
    undo_.pop_back();
    if (!step.undo || !step.undo())
    {
        undo_.clear();
        redo_.clear();
        Q_EMIT changed();
        return false;
    }
    redo_.push_back(std::move(step));
    Q_EMIT changed();
    return true;
}

bool UndoService::redo()
{
    if (redo_.empty())
        return false;
    undo_step step = std::move(redo_.back());
    redo_.pop_back();
    if (!step.redo || !step.redo())
    {
        undo_.clear();
        redo_.clear();
        Q_EMIT changed();
        return false;
    }
    undo_.push_back(std::move(step));
    Q_EMIT changed();
    return true;
}

void UndoService::clear()
{
    if (undo_.empty() && redo_.empty())
        return;
    undo_.clear();
    redo_.clear();
    Q_EMIT changed();
}

} // namespace vss::gui
