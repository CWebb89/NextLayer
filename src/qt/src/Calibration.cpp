// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#include "Calibration.hpp"

#include "PlateDocument.hpp"

#include <QVector3D>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>

namespace next_layer {
namespace {

enum class Family { Klipper, Marlin, RepRap };

Family familyOf(const QString& flavour)
{
    if (flavour.startsWith(QLatin1String("klipper")))
        return Family::Klipper;
    if (flavour.startsWith(QLatin1String("reprap")) || flavour.startsWith(QLatin1String("smoothie"))
        || flavour.startsWith(QLatin1String("griffin")))
        return Family::RepRap;
    return Family::Marlin;
}

QString number(double value, int decimals)
{
    return QString::number(value, 'f', decimals);
}

QVariantMap numberParam(const char* name, const char* label, double value, const char* unit,
                        double minimum, double maximum, double step, int decimals)
{
    QVariantMap param;
    param.insert(QStringLiteral("name"), QString::fromLatin1(name));
    param.insert(QStringLiteral("label"), QString::fromLatin1(label));
    param.insert(QStringLiteral("kind"), QStringLiteral("number"));
    param.insert(QStringLiteral("value"), value);
    param.insert(QStringLiteral("unit"), QString::fromLatin1(unit));
    param.insert(QStringLiteral("min"), minimum);
    param.insert(QStringLiteral("max"), maximum);
    param.insert(QStringLiteral("step"), step);
    param.insert(QStringLiteral("decimals"), decimals);
    return param;
}

QVariantMap choiceParam(const char* name, const char* label, const char* value,
                        const QVariantList& choices)
{
    QVariantMap param;
    param.insert(QStringLiteral("name"), QString::fromLatin1(name));
    param.insert(QStringLiteral("label"), QString::fromLatin1(label));
    param.insert(QStringLiteral("kind"), QStringLiteral("choice"));
    param.insert(QStringLiteral("value"), QString::fromLatin1(value));
    param.insert(QStringLiteral("choices"), choices);
    return param;
}

QVariantMap flagParam(const char* name, const char* label, bool value)
{
    QVariantMap param;
    param.insert(QStringLiteral("name"), QString::fromLatin1(name));
    param.insert(QStringLiteral("label"), QString::fromLatin1(label));
    param.insert(QStringLiteral("kind"), QStringLiteral("bool"));
    param.insert(QStringLiteral("value"), value);
    return param;
}

QVariantMap option(const char* value, const char* label)
{
    QVariantMap entry;
    entry.insert(QStringLiteral("value"), QString::fromLatin1(value));
    entry.insert(QStringLiteral("label"), QString::fromLatin1(label));
    return entry;
}

/// The temperature dialog: picking a filament type restates the range, so the
/// two numbers it carries travel with the choice rather than in a lookup table.
QVariantMap filamentOption(const char* name, double start, double end)
{
    QVariantMap entry = option(name, name);
    entry.insert(QStringLiteral("start"), start);
    entry.insert(QStringLiteral("end"), end);
    return entry;
}

QVariantList filamentChoices()
{
    return {
        filamentOption("PLA", 230, 190),      filamentOption("ABS/ASA", 260, 230),
        filamentOption("PETG", 250, 220),     filamentOption("PCTG", 260, 230),
        filamentOption("TPU", 230, 200),      filamentOption("PA-CF", 290, 260),
        filamentOption("PET-CF", 300, 270),   option("Custom", "Custom"),
    };
}

QVariantList shaperChoices()
{
    return {option("zv", "ZV"),       option("mzv", "MZV"),
            option("ei", "EI"),       option("2hump_ei", "2HUMP_EI"),
            option("3hump_ei", "3HUMP_EI")};
}

class Params
{
public:
    explicit Params(const QVariantMap& values) : m_values(values) {}

    double number(const char* name, double fallback) const
    {
        const QVariant value = m_values.value(QString::fromLatin1(name));
        bool ok = false;
        const double parsed = value.toDouble(&ok);
        return ok ? parsed : fallback;
    }

    QString text(const char* name, const char* fallback) const
    {
        const QString value = m_values.value(QString::fromLatin1(name)).toString();
        return value.isEmpty() ? QString::fromLatin1(fallback) : value;
    }

    bool flag(const char* name, bool fallback) const
    {
        if (!m_values.contains(QString::fromLatin1(name)))
            return fallback;
        return m_values.value(QString::fromLatin1(name)).toBool();
    }

private:
    QVariantMap m_values;
};

struct Test
{
    const char* kind;
    const char* title;
    const char* instruction;
    const char* apply;
    double applyScale;
    QVariantList params;
};

QVariantList testCatalogue()
{
    QVariantList list;

    auto add = [&list](const char* kind, const char* title, const char* instruction,
                       const char* apply, double applyScale, const QVariantList& params) {
        QVariantMap entry;
        entry.insert(QStringLiteral("kind"), QString::fromLatin1(kind));
        entry.insert(QStringLiteral("title"), QString::fromLatin1(title));
        entry.insert(QStringLiteral("instruction"), QString::fromLatin1(instruction));
        entry.insert(QStringLiteral("apply"), QString::fromLatin1(apply));
        entry.insert(QStringLiteral("applyScale"), applyScale);
        entry.insert(QStringLiteral("params"), params);
        list.append(entry);
    };

    add("temperature", "Temperature tower",
        "Stacked bands, each printed at its own nozzle temperature, with the value printed on the "
        "bed beside it. Take the band with the cleanest overhangs and strongest layer bonding.",
        "first_layer_temperature", 1.0,
        {choiceParam("filament", "Filament type", "PLA", filamentChoices()),
         numberParam("start", "Start temperature", 230, "C", 150, 320, 5, 0),
         numberParam("end", "End temperature", 190, "C", 140, 320, 5, 0),
         numberParam("step", "Temperature step", 5, "C", 1, 30, 1, 0),
         flagParam("auto_scale", "Scale the model for the nozzle", true),
         numberParam("nozzle", "Nozzle diameter", 0.4, "mm", 0.2, 1.2, 0.05, 2)});

    add("pressure_advance", "Pressure advance",
        "Bands whose corners are square without bulging give the pressure advance to save. "
        "Direct drive starts near 0.03, Bowden near 0.3.",
        "pressure_advance", 1.0,
        {choiceParam("extruder", "Extruder type", "dd",
                     {option("dd", "Direct drive"), option("bowden", "Bowden")}),
         numberParam("start", "Start advance", 0, "", 0, 2, 0.01, 3),
         numberParam("end", "End advance", 0.08, "", 0, 2, 0.01, 3),
         numberParam("step", "Advance step", 0.008, "", 0.001, 0.5, 0.001, 3),
         flagParam("numbers", "Print the value beside each band", true)});

    add("flow", "Flow ratio",
        "Measure a wall on each patch and divide what you measured by what it should be. Pass 1 "
        "finds the neighbourhood, pass 2 refines it.",
        "extrusion_multiplier", 0.01,
        {choiceParam("pass", "Pass", "coarse",
                     {option("coarse", "Pass 1 (coarse)"), option("fine", "Pass 2 (fine)"),
                      option("finer", "Pass 3 (very fine)")}),
         numberParam("size", "Patch size", 50, "mm", 20, 120, 5, 0),
         flagParam("numbers", "Print the value beside each patch", true)});

    add("max_flow", "Max volumetric speed",
        "Rising volumetric flow per tower. The first one whose walls thin out or gap is past what "
        "the hot end can melt; save the step below it.",
        "filament_max_volumetric_speed", 1.0,
        {numberParam("start", "Start volumetric speed", 5, "mm3/s", 0.5, 60, 0.5, 1),
         numberParam("end", "End volumetric speed", 20, "mm3/s", 1, 80, 0.5, 1),
         numberParam("step", "Step", 2.5, "mm3/s", 0.5, 20, 0.5, 1),
         numberParam("width", "Line width", 0.45, "mm", 0.2, 1.5, 0.05, 2),
         numberParam("layer", "Layer height", 0.2, "mm", 0.05, 1, 0.05, 2),
         numberParam("speed", "Print speed at 100 %", 100, "mm/s", 10, 800, 5, 0),
         flagParam("numbers", "Print the value beside each tower", true)});

    add("retraction", "Retraction",
        "Two towers a gap apart, each band at its own retraction length. Raise the length until "
        "the string between them stops.",
        "retract_length", 1.0,
        {numberParam("start", "Start length", 0, "mm", 0, 10, 0.1, 1),
         numberParam("end", "End length", 2, "mm", 0.1, 12, 0.1, 1),
         numberParam("step", "Step", 0.4, "mm", 0.05, 5, 0.05, 2),
         flagParam("numbers", "Print the value beside each band", true)});

    add("first_layer", "First layer squish",
        "Patches at different nozzle heights. Keep the one with solid, uncrushed lines and set "
        "that height as the printer's Z offset.",
        "", 1.0,
        {numberParam("step", "Height step", 0.05, "mm", 0.01, 0.2, 0.01, 2),
         numberParam("size", "Patch size", 20, "mm", 10, 60, 5, 0),
         flagParam("numbers", "Print the offset beside each patch", true)});

    add("vfa", "VFA tower",
        "Rings at rising speed. Vertical fine artefacts show as banding on one speed range; that "
        "is the range to keep out of.",
        "", 1.0,
        {numberParam("start", "Start speed", 40, "mm/s", 10, 500, 5, 0),
         numberParam("end", "End speed", 200, "mm/s", 20, 600, 5, 0),
         numberParam("step", "Step", 20, "mm/s", 5, 100, 5, 0),
         numberParam("layer", "Layer height", 0.2, "mm", 0.05, 1, 0.05, 2),
         numberParam("speed", "Print speed at 100 %", 100, "mm/s", 10, 800, 5, 0),
         flagParam("numbers", "Print the speed beside each ring", true)});

    add("input_shaping_freq", "Input shaping: frequency",
        "A tall tower whose bands carry different shaper frequencies. The band with the least "
        "ringing after a corner gives the frequency.",
        "", 1.0,
        {choiceParam("shaper", "Shaper type", "mzv", shaperChoices()),
         numberParam("start_x", "Frequency X start", 15, "Hz", 5, 300, 1, 0),
         numberParam("end_x", "Frequency X end", 80, "Hz", 5, 300, 1, 0),
         numberParam("start_y", "Frequency Y start", 15, "Hz", 5, 300, 1, 0),
         numberParam("end_y", "Frequency Y end", 80, "Hz", 5, 300, 1, 0),
         numberParam("damping", "Damping ratio", 0.1, "", 0.01, 1, 0.01, 2),
         flagParam("numbers", "Print the frequency beside each band", true)});

    add("input_shaping_damp", "Input shaping: damping",
        "Frequency fixed at what you measured, damping varied per band. Pick the band where "
        "ringing decays fastest without overshooting.",
        "", 1.0,
        {choiceParam("shaper", "Shaper type", "mzv", shaperChoices()),
         numberParam("freq_x", "Frequency X", 30, "Hz", 5, 300, 1, 0),
         numberParam("freq_y", "Frequency Y", 30, "Hz", 5, 300, 1, 0),
         numberParam("start", "Damping start", 0.05, "", 0.01, 1, 0.01, 2),
         numberParam("end", "Damping end", 0.25, "", 0.01, 1, 0.01, 2),
         numberParam("step", "Damping step", 0.05, "", 0.01, 0.5, 0.01, 2),
         flagParam("numbers", "Print the damping beside each band", true)});

    add("cornering", "Cornering",
        "Bands at rising square-corner velocity. Lower values give sharper corners and slower "
        "printing; the band that stops rounding is the value to keep.",
        "", 1.0,
        {numberParam("start", "Start velocity", 1, "mm/s", 0, 60, 1, 1),
         numberParam("end", "End velocity", 10, "mm/s", 1, 60, 1, 1),
         numberParam("step", "Step", 1, "mm/s", 0.5, 20, 0.5, 2),
         flagParam("numbers", "Print the value beside each band", true)});

    return list;
}

} // namespace

Calibration::Calibration(PlateDocument* document, QObject* parent)
    : QObject(parent), m_document(document)
{
}

QVariantList Calibration::tests() const
{
    return testCatalogue();
}

QVariantMap Calibration::defaultParams(const QString& kind) const
{
    QVariantMap values;
    for (const QVariant& entry : testCatalogue()) {
        const QVariantMap test = entry.toMap();
        if (test.value(QStringLiteral("kind")).toString() != kind)
            continue;
        for (const QVariant& param : test.value(QStringLiteral("params")).toList())
            values.insert(param.toMap().value(QStringLiteral("name")).toString(),
                          param.toMap().value(QStringLiteral("value")));
        break;
    }
    return values;
}

QString Calibration::build(const QString& kind, double baseTemperature)
{
    QVariantMap params = defaultParams(kind);
    if (baseTemperature > 0.0 && params.contains(QStringLiteral("start")))
        params.insert(QStringLiteral("start"), baseTemperature);
    return build(kind, params);
}

QString Calibration::build(const QString& kind, const QVariantMap& values)
{
    if (m_document == nullptr)
        return QStringLiteral("there is no plate to build on");

    const Params params(values);
    const Family family = familyOf(params.text("flavour", "klipper"));

    auto failure = [this](const char* what) {
        const QString message = m_document->error();
        return message.isEmpty() ? QString::fromLatin1(what) : message;
    };

    m_document->clear();
    m_document->clearLayerGcodes();

    const auto bandCount = [](double start, double end, double step, int limit) {
        const double span = std::fabs(start - end);
        const double count = step > 0.0 ? std::floor(span / step + 0.5) + 1.0 : 1.0;
        return int(std::clamp(count, 1.0, double(limit)));
    };

    const QVector3D centre = m_document->bedCentre();

    /// A stack of bands in one column: every band is a solid whose height range
    /// carries exactly one layer G-code command, which is what makes the plate a
    /// test rather than a shape. The bands have to share a Z range with their
    /// command, so they stack: one layer-change G-code per band is the only
    /// channel the app has into a running print.
    auto addBands = [this, centre](int count, double height, double size, double x, const char* shape,
                           const char* prefix,
                           const std::function<QString(int)>& value,
                           const std::function<QString(int)>& command) {
        for (int band = 0; band < count; ++band) {
            const int id = m_document->addShape(QString::fromLatin1(shape), size, size, height);
            if (id < 0)
                return false;
            m_document->renameObject(id, QStringLiteral("%1 %2 (%3)")
                                              .arg(QString::fromLatin1(prefix))
                                              .arg(band + 1)
                                              .arg(value(band)));
            m_document->setPosition(id, QVector3D(float(centre.x() + x), float(centre.y()),
                                                  float(band * height)));
        }
        for (int band = 0; band < count; ++band)
            m_document->addLayerGcode(band * height + 0.2, QStringLiteral("gcode"),
                                      command(band)
                                          + QStringLiteral(" ; calibration band %1").arg(band + 1));
        return true;
    };

    /// One row of labels on the bed in front of the test. Laid out after the
    /// glyphs are built, so the spacing follows the real width of each word.
    auto addLabels = [this, centre](int count, double front,
                                    const std::function<QString(int)>& text) {
        QList<int> ids;
        QList<double> widths;
        for (int band = 0; band < count; ++band) {
            const int id = m_document->addText(text(band), 6.0, 0.8, QString());
            if (id < 0)
                return;
            ids.append(id);
            widths.append(m_document->objectInfo(id).value(QStringLiteral("width")).toDouble());
        }
        double total = 0.0;
        for (double width : widths)
            total += width + 4.0;
        double cursor = centre.x() - total / 2.0;
        for (int band = 0; band < ids.size(); ++band) {
            m_document->setPosition(ids[band], QVector3D(float(cursor + widths[band] / 2.0),
                                                         float(centre.y() + front), 0.0f));
            cursor += widths[band] + 4.0;
        }
    };

    if (kind == QLatin1String("temperature")) {
        const double start = params.number("start", 230);
        double end = params.number("end", 190);
        const double step = std::max(1.0, params.number("step", 5));
        if (end > start - step)
            end = start - step;
        const double nozzle = params.number("nozzle", 0.4);
        const double nozzleScale = nozzle / 0.4;
        const int count = bandCount(start, end, step, 24);
        const double band = 10.0 * nozzleScale;
        const double size = 20.0 * nozzleScale;

        // Our own tower, built from primitives rather than cut out of the reference
        // slicer's test mesh. That mesh carries that project's logo embossed in the
        // geometry, and cutting a temperature range out of a fixed stack left the
        // top band sliced through the middle of its own number. Here a band is a
        // band: a block with a shelf cantilevered out of its top, so each temperature
        // shows overhang, bridging and stringing, and the value itself prints on the
        // bed beside it.
        const double shelfHeight = 2.0 * nozzleScale;
        for (int index = 0; index < count; ++index) {
            const double value = start - step * index;
            const double bottom = index * band;

            const int block = m_document->addShape(QStringLiteral("box"), size, size, band);
            if (block < 0)
                return failure("could not build the temperature tower");
            m_document->renameObject(block, QStringLiteral("Band %1 (%2 C)")
                                                .arg(index + 1)
                                                .arg(number(value, 0)));
            m_document->setPosition(block, QVector3D(centre.x(), centre.y(), float(bottom)));

            const int shelf = m_document->addShape(QStringLiteral("box"), size * 0.8,
                                                   size * 0.6, shelfHeight);
            if (shelf < 0)
                return failure("could not build the temperature tower");
            m_document->renameObject(shelf, QStringLiteral("Band %1 shelf").arg(index + 1));
            m_document->setPosition(shelf, QVector3D(centre.x() + float(size * 0.9), centre.y(),
                                                     float(bottom + band - shelfHeight)));

            m_document->addLayerGcode(bottom + 0.2, QStringLiteral("gcode"),
                                      QStringLiteral("M104 S%1 ; calibration band %2")
                                          .arg(number(value, 0))
                                          .arg(index + 1));
        }

        addLabels(count, -(size * 0.5 + 12.0), [start, step](int index) {
            return QString::number(start - step * index, 'f', 0);
        });

        return QStringLiteral("temperature tower: %1 bands from %2 C down to %3 C")
            .arg(count)
            .arg(number(start, 0), number(start - step * (count - 1), 0));
    }

    if (kind == QLatin1String("pressure_advance")) {
        const bool bowden = params.text("extruder", "dd") == QLatin1String("bowden");
        const double start = params.number("start", 0);
        double end = params.number("end", bowden ? 0.5 : 0.08);
        const double step = std::max(0.001, params.number("step", bowden ? 0.05 : 0.008));
        if (end <= start)
            end = start + step * 5.0;
        const int count = bandCount(start, end, step, 20);
        const auto advance = [start, step](int band) { return start + step * band; };
        const auto command = [family](double value) {
            switch (family) {
            case Family::Klipper:
                return QStringLiteral("SET_PRESSURE_ADVANCE ADVANCE=%1").arg(number(value, 3));
            case Family::RepRap:
                return QStringLiteral("M572 D0 S%1").arg(number(value, 3));
            case Family::Marlin:
                break;
            }
            return QStringLiteral("M900 K%1").arg(number(value, 3));
        };
        if (!addBands(count, 15.0, 25.0, 0.0, "box", "Band",
                      [&](int band) { return number(advance(band), 3); },
                      [&](int band) { return command(advance(band)); }))
            return failure("could not build a pressure advance band");
        if (params.flag("numbers", true))
            addLabels(count, -26.5, [&](int band) { return number(advance(band), 3); });
        return QStringLiteral("pressure advance tower: %1 bands from %2 to %3 (%4)")
            .arg(count)
            .arg(number(start, 3), number(advance(count - 1), 3),
                 bowden ? QStringLiteral("Bowden") : QStringLiteral("direct drive"));
    }

    if (kind == QLatin1String("flow")) {
        const QString pass = params.text("pass", "coarse");
        double start = 90.0;
        double end = 110.0;
        double step = 5.0;
        double height = 2.0;
        if (pass == QLatin1String("fine")) {
            start = 98.0;
            end = 102.0;
            step = 1.0;
            height = 1.0;
        } else if (pass == QLatin1String("finer")) {
            start = 99.0;
            end = 101.0;
            step = 0.5;
            height = 1.0;
        }
        const int count = bandCount(start, end, step, 21);
        const double size = params.number("size", 50);
        const auto flow = [start, step](int band) { return start + step * band; };
        if (!addBands(count, height, size, 0.0, "box", "Flow",
                      [&](int band) { return QStringLiteral("%1 %").arg(number(flow(band), 1)); },
                      [&](int band) {
                          return QStringLiteral("M221 S%1").arg(number(flow(band), 1));
                      }))
            return failure("could not build a flow patch");
        if (params.flag("numbers", true))
            addLabels(count, -(size / 2.0 + 14.0),
                      [&](int band) { return number(flow(band), 1); });
        return QStringLiteral("flow %1: %2 patches from %3 % to %4 %")
            .arg(pass, QString::number(count), number(start, 1), number(flow(count - 1), 1));
    }

    if (kind == QLatin1String("max_flow")) {
        const double start = params.number("start", 5);
        const double end = params.number("end", 20);
        const double step = std::max(0.5, params.number("step", 2.5));
        const double width = std::max(0.1, params.number("width", 0.45));
        const double layer = std::max(0.05, params.number("layer", 0.2));
        const double base = std::max(1.0, params.number("speed", 100));
        const int count = bandCount(start, end, step, 20);
        const auto flow = [start, step](int band) { return start + step * band; };
        const auto percent = [&](int band) {
            return std::min(300.0, flow(band) / (width * layer) / base * 100.0);
        };
        if (!addBands(count, 10.0, 20.0, 0.0, "box", "Tower",
                      [&](int band) { return QStringLiteral("%1 mm3/s").arg(number(flow(band), 1)); },
                      [&](int band) {
                          return QStringLiteral("M220 S%1").arg(number(percent(band), 0));
                      }))
            return failure("could not build a volumetric speed tower");
        if (params.flag("numbers", true))
            addLabels(count, -24.0, [&](int band) { return number(flow(band), 1); });
        return QStringLiteral("max volumetric speed: %1 towers from %2 to %3 mm3/s at %4 x %5 mm")
            .arg(count)
            .arg(number(start, 1), number(flow(count - 1), 1), number(width, 2), number(layer, 2));
    }

    if (kind == QLatin1String("retraction")) {
        const double start = params.number("start", 0);
        double end = params.number("end", 2);
        const double step = std::max(0.05, params.number("step", 0.4));
        if (end <= start)
            end = start + step * 5.0;
        const int count = bandCount(start, end, step, 20);
        const auto length = [start, step](int band) { return start + step * band; };
        const auto command = [family](double value) {
            switch (family) {
            case Family::Klipper:
                return QStringLiteral("SET_RETRACTION RETRACT_LENGTH=%1").arg(number(value, 2));
            case Family::RepRap:
                return QStringLiteral("M207 S%1").arg(number(value, 2));
            case Family::Marlin:
                break;
            }
            return QStringLiteral("M207 S%1").arg(number(value, 2));
        };
        const auto bands = [&](double x, const char* name) {
            return addBands(count, 10.0, 8.0, x, "cylinder", name,
                            [&](int band) { return number(length(band), 2) + QStringLiteral(" mm"); },
                            [&](int band) { return command(length(band)); });
        };
        if (!bands(-30.0, "Tower 1") || !bands(30.0, "Tower 2"))
            return failure("could not build a retraction tower");
        if (params.flag("numbers", true))
            addLabels(count, -18.0, [&](int band) { return number(length(band), 2); });
        return QStringLiteral("retraction: %1 bands from %2 to %3 mm on two towers 60 mm apart")
            .arg(count)
            .arg(number(start, 2), number(length(count - 1), 2));
    }

    if (kind == QLatin1String("first_layer")) {
        const double step = std::max(0.01, params.number("step", 0.05));
        const double size = params.number("size", 20);
        // One patch per layer, stacked, each printed at its own live Z offset:
        // the squish is the gap the nozzle leaves, so it is the offset that
        // varies, and a negative one has to stay off the geometry or the plate
        // starts below the bed and never slices.
        const double offsets[] = {0.0, -1.0, 1.0, -2.0, 2.0, -3.0};
        const int count = int(std::size(offsets));
        for (int patch = 0; patch < count; ++patch) {
            const int id = m_document->addShape(QStringLiteral("box"), size, size, 0.2);
            if (id < 0)
                return failure("could not build a first layer patch");
            m_document->renameObject(id, QStringLiteral("Squish %1 (%2 mm)")
                                              .arg(patch + 1)
                                              .arg(number(offsets[patch] * step, 2)));
            m_document->setPosition(id, QVector3D(centre.x(), centre.y(), float(patch * 0.2)));
            m_document->addLayerGcode(patch * 0.2 + 0.02, QStringLiteral("gcode"),
                                      QStringLiteral("SET_GCODE_OFFSET Z=%1 ; calibration band %2")
                                          .arg(number(offsets[patch] * step, 2))
                                          .arg(patch + 1));
        }
        m_document->addLayerGcode(count * 0.2 + 0.02, QStringLiteral("gcode"),
                                  QStringLiteral("SET_GCODE_OFFSET Z=0 ; squish ladder done"));
        if (params.flag("numbers", true))
            addLabels(count, -(size / 2.0 + 14.0),
                      [&](int patch) { return number(offsets[patch] * step, 2); });
        return QStringLiteral("first layer: %1 patches from %2 mm to %3 mm of nozzle height")
            .arg(count)
            .arg(number(-3.0 * step, 2), number(2.0 * step, 2));
    }

    if (kind == QLatin1String("vfa")) {
        const double start = params.number("start", 40);
        const double end = params.number("end", 200);
        const double step = std::max(1.0, params.number("step", 20));
        const double base = std::max(1.0, params.number("speed", 100));
        const int count = bandCount(start, end, step, 16);
        const auto speed = [start, step](int band) { return start + step * band; };
        if (!addBands(count, 10.0, 20.0, 0.0, "cylinder", "Ring",
                      [&](int band) { return number(speed(band), 0) + QStringLiteral(" mm/s"); },
                      [&](int band) {
                          return QStringLiteral("M220 S%1")
                              .arg(number(std::min(300.0, speed(band) / base * 100.0), 0));
                      }))
            return failure("could not build a VFA ring");
        if (params.flag("numbers", true))
            addLabels(count, -24.0, [&](int band) { return number(speed(band), 0); });
        return QStringLiteral("VFA: %1 rings from %2 to %3 mm/s")
            .arg(count)
            .arg(number(start, 0), number(speed(count - 1), 0));
    }

    if (kind == QLatin1String("input_shaping_freq")
        || kind == QLatin1String("input_shaping_damp")) {
        const bool damping = kind == QLatin1String("input_shaping_damp");
        const QString shaper = params.text("shaper", "mzv");
        const double startX = params.number("start_x", params.number("freq_x", 30));
        const double endX = params.number("end_x", startX);
        const double startY = params.number("start_y", params.number("freq_y", 30));
        const double endY = params.number("end_y", startY);
        const double dampingValue = params.number("damping", 0.1);
        double start = 0.0;
        double step = 0.0;
        if (damping) {
            start = params.number("start", 0.05);
            step = std::max(0.01, params.number("step", 0.05));
        } else if (endX <= startX) {
            start = startX;
        }
        const int count = damping ? bandCount(start, params.number("end", 0.25), step, 12) : 6;
        const auto value = [&](int band) {
            const double fraction = count > 1 ? double(band) / double(count - 1) : 0.0;
            return damping ? start + step * band : startX + (endX - startX) * fraction;
        };
        const auto command = [&](int band) {
            if (damping)
                return QStringLiteral("SET_INPUT_SHAPER SHAPER_TYPE_X=%1 SHAPER_TYPE_Y=%1 "
                                      "SHAPER_DAMPING_RATIO=%2")
                    .arg(shaper, number(value(band), 2));
            const double fraction = count > 1 ? double(band) / double(count - 1) : 0.0;
            const double frequencyX = startX + (endX - startX) * fraction;
            const double frequencyY = startY + (endY - startY) * fraction;
            return QStringLiteral("SET_INPUT_SHAPER SHAPER_TYPE_X=%1 SHAPER_TYPE_Y=%1 "
                                  "SHAPER_FREQ_X=%2 SHAPER_FREQ_Y=%3 SHAPER_DAMPING_RATIO=%4")
                .arg(shaper, number(frequencyX, 1), number(frequencyY, 1),
                     number(dampingValue, 2));
        };
        if (!addBands(count, 20.0, 15.0, 0.0, "box", "Band",
                      [&](int band) { return number(value(band), damping ? 2 : 0) + QStringLiteral(" Hz"); },
                      command))
            return failure("could not build an input shaping band");
        if (params.flag("numbers", true))
            addLabels(count, -21.5,
                      [&](int band) { return number(value(band), damping ? 2 : 0); });
        return damping
                   ? QStringLiteral("input shaping damping: %1 bands from %2 to %3 at %4 Hz (%5)")
                         .arg(count)
                         .arg(number(start, 2), number(value(count - 1), 2), number(startX, 1),
                              shaper)
                   : QStringLiteral("input shaping frequency: %1 bands, X %2 to %3 Hz, Y %4 to %5 "
                                    "Hz (%6)")
                         .arg(count)
                         .arg(number(startX, 1), number(endX, 1), number(startY, 1),
                              number(endY, 1), shaper);
    }

    if (kind == QLatin1String("cornering")) {
        const double start = params.number("start", 1);
        double end = params.number("end", 10);
        const double step = std::max(0.1, params.number("step", 1));
        if (end <= start)
            end = start + step * 5.0;
        const int count = bandCount(start, end, step, 16);
        const auto velocity = [start, step](int band) { return start + step * band; };
        const auto command = [family](double value) {
            switch (family) {
            case Family::Klipper:
                return QStringLiteral("SET_VELOCITY_LIMIT SQUARE_CORNER_VELOCITY=%1")
                    .arg(number(value, 2));
            case Family::RepRap:
                return QStringLiteral("M566 P1 X%1 Y%1").arg(number(value * 60.0, 0));
            case Family::Marlin:
                break;
            }
            return QStringLiteral("M205 J%1").arg(number(value, 2));
        };
        if (!addBands(count, 15.0, 25.0, 0.0, "box", "Band",
                      [&](int band) { return number(velocity(band), 2) + QStringLiteral(" mm/s"); },
                      [&](int band) { return command(velocity(band)); }))
            return failure("could not build a cornering band");
        if (params.flag("numbers", true))
            addLabels(count, -26.5, [&](int band) { return number(velocity(band), 2); });
        return QStringLiteral("cornering: %1 bands from %2 to %3 mm/s")
            .arg(count)
            .arg(number(start, 2), number(velocity(count - 1), 2));
    }

    return QStringLiteral("%1 is not a calibration test").arg(kind);
}

} // namespace next_layer
