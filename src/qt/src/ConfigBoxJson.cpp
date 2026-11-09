// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#include "ConfigBoxJson.hpp"

#include <Slic3r/Domain/Config.hpp>
#include <Slic3r/Domain/ConfigDefsFDM.hpp>
#include <Slic3r/Domain/ConfigValue.hpp>
#include <Slic3r/Domain/Percentage.hpp>

#include <QVariantList>

#include <algorithm>
#include <type_traits>
#include <vector>

namespace next_layer {

namespace {

/// One config value as the settings map stores it. The types are the engine's
/// own: a percentage keeps the pair `load_box` reads back, and an enum keeps the
/// name it prints, because the index means nothing without the definition.
bool toVariant(const Slic3r::Domain::ConfigItem& item, QVariant& out)
{
    bool handled = true;
    item.visit([&](auto&& value) {
        using ValueType = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::is_same_v<ValueType, bool>) {
            out = QVariant(value);
        } else if constexpr (std::is_same_v<ValueType, int>) {
            out = QVariant(value);
        } else if constexpr (std::is_same_v<ValueType, double>) {
            out = QVariant(value);
        } else if constexpr (std::is_same_v<ValueType, std::string>) {
            out = QVariant(QString::fromStdString(value));
        } else if constexpr (std::is_same_v<ValueType, std::optional<int>>) {
            if (!value.has_value()) {
                handled = false;
                return;
            }
            out = QVariant(*value);
        } else if constexpr (std::is_same_v<ValueType, Slic3r::Domain::EnumWrapper>) {
            out = QVariant(QString::fromStdString(std::string(value.get_string())));
        } else if constexpr (std::is_same_v<ValueType, Slic3r::Domain::Percentage>) {
            QVariantMap percentage;
            percentage.insert(QStringLiteral("value"), value.value);
            percentage.insert(QStringLiteral("is_percent"), true);
            out = percentage;
        } else if constexpr (std::is_same_v<ValueType, Slic3r::Domain::FloatOrPercentage>) {
            QVariantMap either;
            either.insert(QStringLiteral("value"), value.is_percentage()
                                                       ? value.percentage().value
                                                       : value.float_value());
            either.insert(QStringLiteral("is_percent"), value.is_percentage());
            out = either;
        } else if constexpr (std::is_same_v<ValueType, Slic3r::Domain::EnumVectorWrapper>) {
            QVariantList names;
            for (const std::string_view name : value.get_strings())
                names.append(QString::fromLatin1(name.data(), int(name.size())));
            out = names;
        } else if constexpr (std::is_same_v<ValueType, std::vector<int>>) {
            QVariantList numbers;
            for (const int number : value)
                numbers.append(number);
            out = numbers;
        } else if constexpr (std::is_same_v<ValueType, std::vector<double>>) {
            QVariantList numbers;
            for (const double number : value)
                numbers.append(number);
            out = numbers;
        } else if constexpr (std::is_same_v<ValueType, std::vector<std::string>>) {
            QVariantList strings;
            for (const std::string& text : value)
                strings.append(QString::fromStdString(text));
            out = strings;
        } else if constexpr (std::is_same_v<ValueType, std::vector<bool>>) {
            QVariantList flags;
            for (const bool flag : value)
                flags.append(flag);
            out = flags;
        } else {
            handled = false;
        }
    });
    return handled;
}

} // namespace


namespace {

/// One value as the settings map stores it, recovered from the text a project file
/// printed. The setting's own definition decides the type: a box refuses a value
/// that does not match, so guessing here would only move the failure.
bool fromText(const QString& text, const Slic3r::Domain::ConfigValue& definition, QVariant& out)
{
    bool handled = true;
    definition.visit([&](auto&& value) {
        using ValueType = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::is_same_v<ValueType, bool>) {
            out = QVariant(text == QLatin1String("1") || text.compare(QLatin1String("true"),
                                                                     Qt::CaseInsensitive) == 0);
        } else if constexpr (std::is_same_v<ValueType, int>) {
            bool ok = false;
            const int number = text.toInt(&ok);
            if (ok)
                out = QVariant(number);
            else
                handled = false;
        } else if constexpr (std::is_same_v<ValueType, double>) {
            bool ok = false;
            const double number = text.toDouble(&ok);
            if (ok)
                out = QVariant(number);
            else
                handled = false;
        } else if constexpr (std::is_same_v<ValueType, std::string>) {
            out = QVariant(text);
        } else if constexpr (std::is_same_v<ValueType, Slic3r::Domain::EnumWrapper>) {
            // The definition owns the vocabulary: a name it does not list is not a
            // value, and the box would only fail later with less to go on.
            const std::string serialized = text.toStdString();
            for (const Slic3r::Domain::EnumValueDef& entry : value.def()) {
                if (entry.str_serialized == serialized) {
                    out = QVariant(text);
                    return;
                }
            }
            handled = false;
        } else if constexpr (std::is_same_v<ValueType, Slic3r::Domain::Percentage>) {
            QString digits = text;
            digits.remove(QLatin1Char('%'));
            bool ok = false;
            const double number = digits.trimmed().toDouble(&ok);
            if (!ok) {
                handled = false;
                return;
            }
            QVariantMap percentage;
            percentage.insert(QStringLiteral("value"), number);
            percentage.insert(QStringLiteral("is_percent"), true);
            out = percentage;
        } else if constexpr (std::is_same_v<ValueType, Slic3r::Domain::FloatOrPercentage>) {
            const bool percent = text.endsWith(QLatin1Char('%'));
            QString digits = text;
            digits.remove(QLatin1Char('%'));
            bool ok = false;
            const double number = digits.trimmed().toDouble(&ok);
            if (!ok) {
                handled = false;
                return;
            }
            QVariantMap either;
            either.insert(QStringLiteral("value"), number);
            either.insert(QStringLiteral("is_percent"), percent);
            out = either;
        } else if constexpr (std::is_same_v<ValueType, std::vector<double>>
                             || std::is_same_v<ValueType, std::vector<int>>
                             || std::is_same_v<ValueType, std::vector<std::string>>
                             || std::is_same_v<ValueType, std::vector<bool>>) {
            // One value is written for all the tools it could apply to: a setting
            // that is per-extruder here is a single number in a project from
            // another slicer. A semicolon-separated list is honoured as written.
            const QStringList parts = text.split(QLatin1Char(';'), Qt::SkipEmptyParts);
            if (parts.isEmpty()) {
                handled = false;
                return;
            }
            QVariantList values;
            for (const QString& part : parts) {
                const QString token = part.trimmed();
                if constexpr (std::is_same_v<ValueType, std::vector<double>>) {
                    bool ok = false;
                    const double number = token.toDouble(&ok);
                    if (!ok) {
                        handled = false;
                        return;
                    }
                    values.append(number);
                } else if constexpr (std::is_same_v<ValueType, std::vector<int>>) {
                    bool ok = false;
                    const int number = token.toInt(&ok);
                    if (!ok) {
                        handled = false;
                        return;
                    }
                    values.append(number);
                } else if constexpr (std::is_same_v<ValueType, std::vector<bool>>) {
                    values.append(token == QLatin1String("1")
                                  || token.compare(QLatin1String("true"),
                                                   Qt::CaseInsensitive) == 0);
                } else {
                    values.append(token);
                }
            }
            out = values;
        } else {
            handled = false;
        }
    });
    return handled;
}

} // namespace

QVariantMap settingsFromText(const QHash<QString, QString>& record, QStringList& refused)
{
    QVariantMap settings;
    static const std::vector<Slic3r::Domain::ConfigItemDef>& defs =
        Slic3r::Domain::get_defs_fdm().defs();
    for (auto it = record.constBegin(); it != record.constEnd(); ++it) {
        const auto found = std::find_if(defs.begin(), defs.end(),
                                        [&](const Slic3r::Domain::ConfigItemDef& definition) {
                                            return definition.name == it.key().toStdString();
                                        });
        if (found == defs.end() || !found->init_fn) {
            refused.append(it.key());
            continue;
        }
        const Slic3r::Domain::ConfigValue definition = found->init_fn();
        QVariant value;
        if (!fromText(it.value(), definition, value)) {
            refused.append(it.key());
            continue;
        }
        settings.insert(it.key(), value);
    }
    return settings;
}

QVariantMap overriddenSettings(const Slic3r::Domain::ConfigBox& box, QStringList& unreadable)
{
    QVariantMap settings;
    for (const std::reference_wrapper<const Slic3r::Domain::ConfigItem>& item :
         box.overrides.overridden_items()) {
        QVariant value;
        if (!toVariant(item.get(), value)) {
            unreadable.append(QString::fromStdString(item.get().name()));
            continue;
        }
        settings.insert(QString::fromStdString(item.get().name()), value);
    }
    return settings;
}

}
