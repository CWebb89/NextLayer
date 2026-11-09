// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include <QString>

namespace next_layer {

/// What became of a setting another slicer wrote.
enum class ForeignSetting
{
    /// It has a counterpart here, named and valued the way this engine prints it.
    Translated,
    /// It has no counterpart here, and that is a known, deliberate gap: the value
    /// means something this engine does not do. Nothing is reported, because a
    /// person opening a downloaded project can do nothing about it.
    NoCounterpart,
    /// Nothing here recognises the name. Reported, because it may be a setting this
    /// app should learn.
    Unknown,
};

/// Translate one setting from the Bambu/Orca dialect into this engine's.
///
/// The two slicers share a lineage, so some names and values are the same, some are
/// the same meaning under a different name, and some describe behaviour this engine
/// does not have. Guessing at the last group would change prints silently, so it is
/// separated by name and left alone.
ForeignSetting translateForeignSetting(const QString& key, const QString& value,
                                       QString& translatedName, QString& translatedValue);

}
