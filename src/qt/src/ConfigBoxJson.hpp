// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include <QVariantMap>
#include <QHash>
#include <QStringList>

namespace Slic3r::Domain {
struct ConfigBox;
}

namespace next_layer {

/// The settings a config box overrides, as this app stores them.
///
/// A box carries every setting it knows at its own location -- defaults in one
/// list, the ones that were actually set in another. Only the second is an
/// override: writing the whole box back would turn a default into a decision and
/// pin it against the preset it came from. Values keep their types, because
/// `load_box` is typed and a 0.3 arriving as a string is rejected.
///
/// Types this reader does not represent are named in \a unreadable rather than
/// dropped quietly, and a caller that gets one has something to report.
QVariantMap overriddenSettings(const Slic3r::Domain::ConfigBox& box, QStringList& unreadable);

/// The same, for the key/value lines a project file writes: an INI-shaped record
/// names each setting and prints its value, so the type has to be recovered from
/// the setting's own definition. A key this build does not know, or a value its
/// type cannot accept, is named in \a refused; nothing is guessed.
QVariantMap settingsFromText(const QHash<QString, QString>& record, QStringList& refused);

/// Whether this engine lets a setting be overridden per object. A setting that
/// belongs to the print or the machine alone is refused by the engine when an
/// object claims it, so a foreign project's per-object copy of one has to be kept
/// rather than applied -- that is the difference between a file this app can save
/// and one it cannot.
bool overridesAtObject(const QString& name);

}
