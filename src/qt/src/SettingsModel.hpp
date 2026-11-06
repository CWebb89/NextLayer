// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>

#include <memory>

class SettingsModel : public QObject
{
    Q_OBJECT

public:
    explicit SettingsModel(QObject* parent = nullptr);
    ~SettingsModel() override;

    Q_INVOKABLE QVariantList tabList() const;
    Q_INVOKABLE QVariantList groups(const QString& tab, const QString& subtab, const QString& filter) const;
    Q_INVOKABLE QVariant value(const QString& name) const;
    /// The block a setting belongs to: "printer", "print" or "filament"; empty
    /// when this engine has no such setting.
    Q_INVOKABLE QString blockOf(const QString& name) const;
    // A point-list setting flattened to [x0, y0, x1, y1, ...]. A nested
    // QVariantList reaches QML as an array-like object whose elements are not
    // plain arrays, so the flat form is what a QML editor can index safely.
    Q_INVOKABLE QVariantList points(const QString& name) const;
    Q_INVOKABLE void setValue(const QString& name, const QVariant& value);
    /// Applies a value translated from another slicer's preset: a scalar for a
    /// per-tool setting becomes a one-element list, and a point list arrives as
    /// [x, y] pairs. False when this setting cannot hold the value.
    bool applyImported(const QString& name, const QVariant& value);
    Q_INVOKABLE void reset(const QString& name);
    Q_INVOKABLE bool seedFromConfigJson(const QString& json);
    Q_INVOKABLE void resetSubtab(const QString& tab, const QString& subtab);
    Q_INVOKABLE void resetTab(const QString& tab);
    Q_INVOKABLE int modifiedCount(const QString& tab) const;

    /// Every setting whose value differs from the preset it came from, one entry
    /// per setting: the name, its label and sub-tab, and the two values. This is
    /// what a preset diff and a "save changes" confirmation both read.
    Q_INVOKABLE QVariantList modifiedValues(const QString& tab) const;

    /// Every print setting a single object may override, in the same shape the
    /// settings panel uses for its editors (label, type, gui type, choices, units,
    /// and the value the current preset resolves to). One entry per setting, so a
    /// per-object override starts from the value that is actually in force.
    Q_INVOKABLE QVariantList objectSettingDefs(const QString& filter) const;
    Q_INVOKABLE QString tabDump() const;
    /// Every setting name the sidebar exposes, one per line: the tab tree
    /// measured against the engine's own config instead of a hand-written list.
    Q_INVOKABLE QString nameDump() const;
    Q_INVOKABLE QString configJson() const;
    Q_INVOKABLE QString valuesYaml(const QString& tab, bool changedOnly) const;
    Q_INVOKABLE void clearModified();

signals:
    void changed();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
