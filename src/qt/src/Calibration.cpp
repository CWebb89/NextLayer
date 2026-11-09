// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#include "Calibration.hpp"

#include "PlateDocument.hpp"
#include "SettingsModel.hpp"

#include <QLineF>
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
        filamentOption("PLA", 230, 190),      filamentOption("ABS/ASA", 270, 230),
        filamentOption("PETG", 250, 230),     filamentOption("PCTG", 280, 240),
        filamentOption("TPU", 240, 210),      filamentOption("PA-CF", 320, 280),
        filamentOption("PET-CF", 320, 280),   option("Custom", "Custom"),
    };
}

QVariantList shaperChoices()
{
    return {option("zv", "ZV"),       option("mzv", "MZV"),
            option("ei", "EI"),       option("2hump_ei", "2HUMP_EI"),
            option("3hump_ei", "3HUMP_EI")};
}

/// What the generated toolpaths need from the profile that is about to slice
/// them: the bead the nozzle lays, the layer it sits on, and the flavour whose
/// pressure-advance command the firmware understands.
struct Machine
{
    Family family = Family::Klipper;
    QString flavour;
    double nozzle = 0.4;
    double filament = 1.75;
    double flow = 1.0;
    double lineWidth = 0.45;
    double layer = 0.2;
    double firstLayer = 0.2;

    double ePerMm(double width, double height) const
    {
        const double radius = filament / 2.0;
        return width * height * flow / (M_PI * radius * radius);
    }
};

/// A five-cell stroke font for the numbers a calibration test prints beside each
/// band. Glyphs are drawn in a 3 by 5 cell box, y up, and come back as line
/// segments in cells so the caller decides the physical size. The set is digits
/// and the marks a value needs — enough for "0.024", "-10", "100" — because a
/// label with words in it would need a real typeface, and a real typeface on the
/// bed is a mesh that occupies the plate.
QVector<QLineF> glyphStrokes(QChar c)
{
    static const QLineF top(0.0, 5.0, 3.0, 5.0);
    static const QLineF upperRight(3.0, 5.0, 3.0, 2.5);
    static const QLineF lowerRight(3.0, 2.5, 3.0, 0.0);
    static const QLineF bottom(0.0, 0.0, 3.0, 0.0);
    static const QLineF lowerLeft(0.0, 2.5, 0.0, 0.0);
    static const QLineF upperLeft(0.0, 5.0, 0.0, 2.5);
    static const QLineF middle(0.0, 2.5, 3.0, 2.5);

    switch (c.unicode()) {
    case '0': return {top, upperRight, lowerRight, bottom, lowerLeft, upperLeft};
    case '1': return {upperRight, lowerRight};
    case '2': return {top, upperRight, middle, lowerLeft, bottom};
    case '3': return {top, upperRight, middle, lowerRight, bottom};
    case '4': return {upperLeft, middle, upperRight, lowerRight};
    case '5': return {top, upperLeft, middle, lowerRight, bottom};
    case '6': return {top, upperLeft, middle, lowerLeft, bottom, lowerRight};
    case '7': return {top, upperRight, lowerRight};
    case '8': return {top, upperRight, lowerRight, bottom, lowerLeft, upperLeft, middle};
    case '9': return {top, upperRight, lowerRight, bottom, upperLeft, middle};
    case '-': return {middle};
    case '.': return {QLineF(0.0, 0.0, 0.9, 0.0)};
    case ',': return {QLineF(0.0, 0.6, 0.6, 0.0)};
    case 'x':
    case 'X': return {QLineF(0.0, 4.0, 3.0, 0.0), QLineF(0.0, 0.0, 3.0, 4.0)};
    case '/': return {QLineF(0.0, 0.0, 3.0, 5.0)};
    default: return {};
    }
}

/// One label as printer millimetres, stroke by stroke: `origin` is the left end
/// of the baseline, `cell` the size of one font cell.
QVector<QVector<QPointF>> labelStrokes(const QString& text, const QPointF& origin, double cell)
{
    QVector<QVector<QPointF>> strokes;
    double cursor = 0.0;
    for (const QChar c : text) {
        if (c == QLatin1Char(' ')) {
            cursor += cell * 4.0;
            continue;
        }
        for (const QLineF& segment : glyphStrokes(c)) {
            QVector<QPointF> stroke;
            stroke.append(origin + QPointF(cursor + segment.x1() * cell, segment.y1() * cell));
            stroke.append(origin + QPointF(cursor + segment.x2() * cell, segment.y2() * cell));
            strokes.append(stroke);
        }
        cursor += cell * 4.0;
    }
    return strokes;
}

/// How wide a label prints, in millimetres, for laying a row of them out.
double labelWidth(const QString& text, double cell)
{
    return std::max(0.0, double(text.size()) * 4.0 - 1.0) * cell;
}

double firstNumber(const QVariant& value, double fallback)
{
    if (value.canConvert<QVariantList>()) {
        const QVariantList list = value.toList();
        if (!list.isEmpty()) {
            bool ok = false;
            const double parsed = list.front().toDouble(&ok);
            if (ok)
                return parsed;
        }
    }
    if (value.canConvert<QVariantMap>()) {
        const QVariantMap map = value.toMap();
        bool ok = false;
        const double parsed = map.value(QStringLiteral("value")).toDouble(&ok);
        if (ok)
            return parsed;
    }
    bool ok = false;
    const double parsed = value.toDouble(&ok);
    return ok ? parsed : fallback;
}

/// A width or height is either a number in millimetres or a percentage of the
/// nozzle (widths) or of the layer height (heights), which is how the engine
/// stores the `FloatOrPercent` settings.
double absoluteLength(const QVariant& value, double fallback, double base)
{
    if (value.canConvert<QVariantMap>()) {
        const QVariantMap map = value.toMap();
        const double number = map.value(QStringLiteral("value")).toDouble();
        if (map.value(QStringLiteral("is_percent")).toBool())
            return number / 100.0 * base;
        return number > 0.0 ? number : fallback;
    }
    const double parsed = firstNumber(value, 0.0);
    return parsed > 0.0 ? parsed : fallback;
}

Machine machineProfile(SettingsModel* settings)
{
    Machine machine;
    if (settings == nullptr)
        return machine;

    const auto get = [settings](const char* key) {
        return settings->value(QString::fromLatin1(key));
    };

    machine.nozzle = firstNumber(get("nozzle_diameter"), machine.nozzle);
    machine.filament = firstNumber(get("filament_diameter"), machine.filament);
    machine.flow = firstNumber(get("extrusion_multiplier"), machine.flow);
    machine.lineWidth = absoluteLength(get("perimeter_extrusion_width"),
                                       machine.nozzle * 1.125, machine.nozzle);
    machine.layer = absoluteLength(get("layer_height"), 0.2, machine.nozzle);
    machine.firstLayer = absoluteLength(get("first_layer_height"), machine.layer,
                                        machine.layer);
    machine.family = familyOf(get("gcode_flavor").toString());
    machine.flavour = get("gcode_flavor").toString();
    if (machine.flavour.isEmpty())
        machine.flavour = QStringLiteral("marlin");
    return machine;
}

QString pressureAdvanceCommand(Family family, double value)
{
    switch (family) {
    case Family::Klipper:
        return QStringLiteral("SET_PRESSURE_ADVANCE ADVANCE=%1").arg(number(value, 3));
    case Family::RepRap:
        return QStringLiteral("M572 D0 S%1").arg(number(value, 3));
    case Family::Marlin:
        break;
    }
    return QStringLiteral("M900 K%1").arg(number(value, 3));
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

    add("temperature", "Temperature",
        "Stacked bands, each printed at its own nozzle temperature, with the value printed on the "
        "bed beside it. Take the band with the cleanest overhangs and strongest layer bonding.",
        "first_layer_temperature", 1.0,
        {choiceParam("filament", "Filament type", "PLA", filamentChoices()),
         numberParam("start", "Start temp", 230, "C", 155, 500, 5, 0),
         numberParam("end", "End temp", 190, "C", 155, 500, 5, 0),
         flagParam("auto_scale", "Resize the model to the nozzle diameter", true),
         numberParam("nozzle", "Nozzle diameter", 0.4, "mm", 0.2, 1.2, 0.05, 2)});

    add("pressure_advance", "Pressure advance",
        "PA Tower prints one tower with the value raised every layer. PA Line prints one line per value, "
        "fast then slow, all on the first layer. PA Pattern prints a fan of square corners, one pattern "
        "per value, four layers high. Take the line or pattern without gaps at the corners.",
        "pressure_advance", 1.0,
        {choiceParam("method", "Method", "tower",
                     {option("tower", "PA Tower"), option("line", "PA Line"),
                      option("pattern", "PA Pattern")}),
         choiceParam("extruder", "Extruder type", "dde",
                     {option("dde", "DDE"), option("bowden", "Bowden")}),
         numberParam("start", "Start PA", 0, "", 0, 2, 0.001, 3),
         numberParam("end", "End PA", 0.1, "", 0, 2, 0.001, 3),
         numberParam("step", "PA step", 0.002, "", 0.001, 0.5, 0.001, 3),
         flagParam("numbers", "Print numbers", true)});

    add("flow", "Flow ratio",
        "Measure a wall on each patch and divide what you measured by what it should be. Pass 1 "
        "finds the neighbourhood, pass 2 refines it. YOLO prints a graded linear flow test.",
        "extrusion_multiplier", 0.01,
        {choiceParam("pass", "Calibration Test Type", "yolo",
                     {option("pass1", "Pass 1 (Coarse)"), option("pass2", "Pass 2 (Fine)"),
                      option("yolo", "YOLO (Recommended)"),
                      option("yolo_fine", "YOLO (Perfectionist)")}),
         choiceParam("pattern", "Top Surface Pattern", "archimedean",
                     {option("archimedean", "Archimedean Chords"),
                      option("monotonic", "Monotonic")}),
         numberParam("size", "Patch size", 50, "mm", 20, 120, 5, 0),
         flagParam("numbers", "Print the value beside each patch", true)});

    add("max_flow", "Max flowrate",
        "Rising volumetric flow per tower. The first one whose walls thin out or gap is past what "
        "the hot end can melt; save the step below it.",
        "filament_max_volumetric_speed", 1.0,
        {numberParam("start", "Start volumetric speed", 5, "mm3/s", 0.5, 60, 0.5, 1),
         numberParam("end", "End volumetric speed", 20, "mm3/s", 1, 80, 0.5, 1),
         numberParam("step", "Step", 0.5, "mm3/s", 0.5, 20, 0.5, 1),
         flagParam("numbers", "Print the value beside each tower", true)});

    add("retraction", "Retraction",
        "Two towers a gap apart, each band at its own retraction length. Raise the length until "
        "the string between them stops.",
        "retract_length", 1.0,
        {numberParam("start", "Start retraction length", 0, "mm", 0, 10, 0.1, 1),
         numberParam("end", "End retraction length", 2, "mm", 0.1, 12, 0.1, 1),
         numberParam("step", "Step", 0.1, "mm", 0.05, 5, 0.05, 2),
         flagParam("numbers", "Print the value beside each band", true)});

    add("first_layer", "First layer squish",
        "Patches at different nozzle heights. Keep the one with solid, uncrushed lines and set "
        "that height as the printer's Z offset.",
        "", 1.0,
        {numberParam("step", "Height step", 0.05, "mm", 0.01, 0.2, 0.01, 2),
         numberParam("size", "Patch size", 20, "mm", 10, 60, 5, 0),
         flagParam("numbers", "Print the offset beside each patch", true)});

    add("vfa", "VFA",
        "Rings at rising speed. Vertical fine artefacts show as banding on one speed range; that "
        "is the range to keep out of.",
        "", 1.0,
        {numberParam("start", "Start speed", 40, "mm/s", 10, 500, 5, 0),
         numberParam("end", "End speed", 200, "mm/s", 20, 600, 5, 0),
         numberParam("step", "Step", 10, "mm/s", 5, 100, 5, 0),
         flagParam("auto_scale", "Resize the model to the nozzle diameter", true),
         flagParam("auto_adjust", "Auto-adjust to max volumetric speed", true),
         numberParam("nozzle", "Nozzle diameter", 0.4, "mm", 0.2, 1.2, 0.05, 2),
         numberParam("layer", "Layer height", 0.2, "mm", 0.05, 1, 0.05, 2),
         numberParam("speed", "Print speed at 100 %", 100, "mm/s", 10, 800, 5, 0),
         flagParam("numbers", "Print the speed beside each ring", true)});

    add("input_shaping_freq", "Input Shaping Frequency",
        "A tall tower whose bands carry different shaper frequencies. The band with the least "
        "ringing after a corner gives the frequency.",
        "", 1.0,
        {choiceParam("model", "Test model", "ringing",
                     {option("ringing", "Ringing Tower"), option("fast", "Fast Tower")}),
         choiceParam("shaper", "Input shaper type", "mzv", shaperChoices()),
         numberParam("start_x", "X start", 15, "Hz", 5, 500, 1, 0),
         numberParam("end_x", "X end", 110, "Hz", 5, 500, 1, 0),
         numberParam("start_y", "Y start", 15, "Hz", 5, 500, 1, 0),
         numberParam("end_y", "Y end", 110, "Hz", 5, 500, 1, 0),
         numberParam("damping", "Damp", 0.15, "", 0, 1, 0.01, 3),
         flagParam("numbers", "Print the frequency beside each band", true)});

    add("input_shaping_damp", "Input Shaping Damping/zeta factor",
        "Frequency fixed at what you measured, damping varied per band. Pick the band where "
        "ringing decays fastest without overshooting.",
        "", 1.0,
        {choiceParam("model", "Test model", "ringing",
                     {option("ringing", "Ringing Tower"), option("fast", "Fast Tower")}),
         choiceParam("shaper", "Input shaper type", "mzv", shaperChoices()),
         numberParam("freq_x", "Frequency X", 30, "Hz", 5, 500, 1, 0),
         numberParam("freq_y", "Frequency Y", 30, "Hz", 5, 500, 1, 0),
         numberParam("start", "Damp start", 0.0, "", 0, 1, 0.01, 3),
         numberParam("end", "Damp end", 0.40, "", 0, 1, 0.01, 3),
         flagParam("numbers", "Print the damping beside each band", true)});

    add("cornering", "Cornering",
        "Bands at rising square-corner velocity. Lower values give sharper corners and slower "
        "printing; the band that stops rounding is the value to keep.",
        "", 1.0,
        {choiceParam("model", "Test model", "ringing",
                     {option("ringing", "Ringing Tower"), option("fast", "Fast Tower"),
                      option("scv", "SCV-V2")}),
         numberParam("start", "Start", 1.0, "mm/s", 0, 100, 0.5, 3),
         numberParam("end", "End", 15.0, "mm/s", 0.1, 100, 0.5, 3),
         flagParam("numbers", "Print the value beside each band", true)});

    return list;
}

} // namespace

Calibration::Calibration(PlateDocument* document, SettingsModel* settings, QObject* parent)
    : QObject(parent), m_document(document), m_settings(settings)
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
    const Machine machine = machineProfile(m_settings);
    const Family family = machine.family;

    auto failure = [this](const char* what) {
        const QString message = m_document->error();
        return message.isEmpty() ? QString::fromLatin1(what) : message;
    };

    m_document->clear();
    m_document->clearLayerGcodes();
    m_document->clearInLayerPolylines();

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
        // Orca interpolates these tests across a fixed tower; our bands carry one
        // command each, so the tower has to fit the machine. A finer step than the
        // reference therefore shortens the bands rather than growing the tower past
        // the print height.
        const double bandHeight = std::min(height, std::max(0.4, 200.0 / double(count)));
        for (int band = 0; band < count; ++band) {
            const int id = m_document->addShape(QString::fromLatin1(shape), size, size, bandHeight);
            if (id < 0)
                return false;
            m_document->renameObject(id, QStringLiteral("%1 %2 (%3)")
                                              .arg(QString::fromLatin1(prefix))
                                              .arg(band + 1)
                                              .arg(value(band)));
            m_document->setPosition(id, QVector3D(float(centre.x() + x), float(centre.y()),
                                                  float(band * bandHeight)));
        }
        for (int band = 0; band < count; ++band)
            m_document->addLayerGcode(band * bandHeight + 0.2, QStringLiteral("gcode"),
                                      command(band)
                                          + QStringLiteral(" ; calibration band %1").arg(band + 1));
        return true;
    };

    /// One row of labels in front of the test, printed inside the first layer as
    /// strokes rather than laid on the bed as text meshes: a label that is part of
    /// the print costs no plate space, cannot be pushed off the bed by the test
    /// itself, and prints with the layer it belongs to.
    auto addLabels = [this, centre, machine](int count, double front,
                                             const std::function<QString(int)>& text) {
        const double cell = 1.2;
        QList<QString> words;
        double total = 0.0;
        for (int band = 0; band < count; ++band) {
            words.append(text(band));
            total += labelWidth(words.back(), cell) + 4.0;
        }
        double cursor = centre.x() - total / 2.0;
        for (int band = 0; band < count; ++band) {
            const QVector<QVector<QPointF>> strokes =
                labelStrokes(words[band], QPointF(cursor, centre.y() + front), cell);
            for (const QVector<QPointF>& stroke : strokes)
                m_document->addInLayerPolyline(machine.firstLayer, stroke, machine.lineWidth,
                                               machine.firstLayer, 30.0);
            cursor += labelWidth(words[band], cell) + 4.0;
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
        const bool bowden = params.text("extruder", "dde") == QLatin1String("bowden");
        const double start = params.number("start", 0);
        double end = params.number("end", bowden ? 1.0 : 0.08);
        const double step = std::max(0.001, params.number("step", bowden ? 0.02 : 0.008));
        if (end <= start)
            end = start + step * 5.0;
        const QString method = params.text("method", "tower");
        const int count = bandCount(start, end, step, method == QLatin1String("line") ? 48 : 32);
        const auto advance = [start, step](int band) { return start + step * band; };
        const QVector3D bed = m_document->plateSize();
        const auto anchorHeight = std::max(machine.firstLayer, machine.layer);

        auto addAnchor = [this, centre, bed](double height) {
            const int id = m_document->addShape(QStringLiteral("box"), 10.0, 10.0, height);
            if (id < 0)
                return false;
            m_document->renameObject(id, QStringLiteral("Test anchor"));
            m_document->setPosition(id, QVector3D(centre.x(),
                                                  centre.y() + bed.y() / 2.0 - 25.0,
                                                  0.0f));
            return true;
        };

        if (method == QLatin1String("line")) {
            const double spacing = 3.5;
            const double shortLength = 20.0;
            const double longLength = 40.0;
            const double totalLength = shortLength * 2.0 + longLength;
            const double x0 = centre.x() - totalLength / 2.0;
            const double y0 = centre.y() - (count - 1) * spacing / 2.0;
            const double z = machine.firstLayer;

            // Orca's line test in its own terms: three segments per line — short,
            // long, short — so one line shows the same pressure-advance value at two
            // speeds and the extrusion that follows. The segments are extruded by
            // the engine through its writer, which keeps E and the nozzle position
            // correct; the value itself travels as the command in front of each line.
            for (int line = 0; line < count; ++line) {
                const double y = y0 + line * spacing;
                const QString command = pressureAdvanceCommand(machine.family, advance(line));
                const struct
                {
                    double from;
                    double to;
                    double speed;
                } segments[] = {{x0, x0 + shortLength, 20.0},
                                {x0 + shortLength, x0 + shortLength + longLength, 100.0},
                                {x0 + shortLength + longLength, x0 + totalLength, 20.0}};
                bool first = true;
                for (const auto& segment : segments) {
                    m_document->addInLayerPolyline(
                        z, {QPointF(segment.from, y), QPointF(segment.to, y)},
                        machine.lineWidth, machine.firstLayer, segment.speed,
                        first ? command : QString());
                    first = false;
                }
            }
            if (!addAnchor(anchorHeight))
                return failure("could not build the PA line test");

            if (params.flag("numbers", true)) {
                const double cell = 0.6;
                for (int line = 0; line < count; line += 2) {
                    const QPointF origin(x0 + totalLength + 12.0, y0 + line * spacing - 1.5);
                    for (const QVector<QPointF>& stroke :
                         labelStrokes(number(advance(line), 3), origin, cell))
                        m_document->addInLayerPolyline(z, stroke, machine.lineWidth,
                                                       machine.firstLayer, 30.0);
                }
            }
            return QStringLiteral("PA line: %1 lines from %2 to %3 (%4)")
                .arg(count)
                .arg(number(start, 3), number(advance(count - 1), 3),
                     bowden ? QStringLiteral("Bowden") : QStringLiteral("direct drive"));
        }

        if (method == QLatin1String("pattern")) {
            const double side = 30.0;
            const double cosine = std::cos(M_PI / 4.0);
            const double sine = std::sin(M_PI / 4.0);
            const double lineSpacing = std::max(0.1, machine.lineWidth
                                                         - machine.layer * (1.0 - M_PI / 4.0));
            const double wallAdvance = lineSpacing / sine;
            const double patternAdvance = wallAdvance * 3.0 + 2.0 + machine.lineWidth;
            const double extent = (count - 1) * patternAdvance + wallAdvance * 2.0
                                  + side * cosine;
            const double x0 = centre.x() - extent / 2.0;
            const double y0 = centre.y() - 20.0;
            const int layers = 4;

            for (int layer = 0; layer < layers; ++layer) {
                const double z = machine.firstLayer + layer * machine.layer;
                for (int pattern = 0; pattern < count; ++pattern) {
                    const double base = x0 + pattern * patternAdvance;
                    bool first = true;
                    for (int wall = 0; wall < 3; ++wall) {
                        const double x = base + (wall + 1) * wallAdvance;
                        const QPointF start(x, y0);
                        const QPointF middle(x + side * cosine, y0 + side * sine);
                        const QPointF end(x, y0 + side * sine * 2.0);
                        m_document->addInLayerPolyline(
                            z, {start, middle}, machine.lineWidth, machine.layer, 100.0,
                            first ? pressureAdvanceCommand(machine.family, advance(pattern))
                                  : QString());
                        m_document->addInLayerPolyline(z, {middle, end}, machine.lineWidth,
                                                       machine.layer, 100.0);
                        first = false;
                    }
                }
            }
            if (!addAnchor(machine.firstLayer + (layers - 1) * machine.layer + 0.01))
                return failure("could not build the PA pattern test");

            if (params.flag("numbers", true)) {
                const double cell = 0.6;
                for (int pattern = 0; pattern < count; pattern += 2) {
                    const QPointF origin(x0 + pattern * patternAdvance + wallAdvance,
                                         y0 - 4.5);
                    for (const QVector<QPointF>& stroke :
                         labelStrokes(number(advance(pattern), 3), origin, cell))
                        m_document->addInLayerPolyline(machine.firstLayer, stroke,
                                                       machine.lineWidth, machine.firstLayer, 30.0);
                }
            }
            return QStringLiteral("PA pattern: %1 patterns from %2 to %3, %4 layers")
                .arg(count)
                .arg(number(start, 3), number(advance(count - 1), 3))
                .arg(layers);
        }

        const double height = count * machine.layer;
        const int id = m_document->addShape(QStringLiteral("box"), 20.0, 20.0, height);
        if (id < 0)
            return failure("could not build the PA tower");
        m_document->renameObject(id, QStringLiteral("PA Tower"));
        m_document->setPosition(id, QVector3D(centre.x(), centre.y(), 0.0f));
        for (int layer = 0; layer < count; ++layer) {
            m_document->addLayerGcode(machine.firstLayer + layer * machine.layer,
                                      QStringLiteral("gcode"),
                                      pressureAdvanceCommand(machine.family, advance(layer))
                                          + QStringLiteral(" ; calibration layer %1").arg(layer + 1));
        }
        if (params.flag("numbers", true)) {
            const int label = m_document->addText(
                QStringLiteral("%1-%2").arg(number(start, 3), number(advance(count - 1), 3)),
                4.0, 0.6, QString());
            if (label >= 0)
                m_document->setPosition(label, QVector3D(float(centre.x()),
                                                         float(centre.y() - 20.0), 0.0f));
        }
        return QStringLiteral("PA tower: %1 layers from %2 to %3 (%4)")
            .arg(count)
            .arg(number(start, 3), number(advance(count - 1), 3),
                 bowden ? QStringLiteral("Bowden") : QStringLiteral("direct drive"));
    }

    if (kind == QLatin1String("flow")) {
        const QString pass = params.text("pass", "yolo");
        double start = 90.0;
        double end = 110.0;
        double step = 5.0;
        double height = 2.0;
        if (pass == QLatin1String("pass2")) {
            start = 98.0;
            end = 102.0;
            step = 1.0;
            height = 1.0;
        } else if (pass == QLatin1String("yolo")) {
            start = 99.0;
            end = 101.0;
            step = 0.5;
            height = 1.0;
        } else if (pass == QLatin1String("yolo_fine")) {
            start = 99.5;
            end = 100.5;
            step = 0.1;
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
        const int count = bandCount(start, end, step, 40);
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
        const int count = bandCount(start, end, step, 32);
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
        //
        // The patch is exactly one layer tall and starts on a layer boundary of
        // the profile about to slice it: the first patch is one first layer,
        // every later one is one layer above the previous. A patch shorter than
        // the layer height produces no layer at all and the plate fails to
        // slice, which is what a fixed 0.2 mm patch did on a 0.3 mm profile; a
        // patch that straddles a boundary contributes to two layers and its
        // offset lands in the wrong one.
        const double firstLayer = machine.firstLayer > 0.0 ? machine.firstLayer
                                                          : (machine.layer > 0.0 ? machine.layer : 0.2);
        const double layer = machine.layer > 0.0 ? machine.layer : 0.2;
        const double offsets[] = {0.0, -1.0, 1.0, -2.0, 2.0, -3.0};
        const int count = int(std::size(offsets));
        double bottom = 0.0;
        for (int index = 0; index < count; ++index) {
            const double height = index == 0 ? firstLayer : layer;
            const int id = m_document->addShape(QStringLiteral("box"), size, size, height);
            if (id < 0)
                return failure("could not build a first layer patch");
            m_document->renameObject(id, QStringLiteral("Squish %1 (%2 mm)")
                                              .arg(index + 1)
                                              .arg(number(offsets[index] * step, 2)));
            m_document->setPosition(id, QVector3D(centre.x(), centre.y(), float(bottom)));
            m_document->addLayerGcode(bottom + height * 0.5, QStringLiteral("gcode"),
                                      QStringLiteral("SET_GCODE_OFFSET Z=%1 ; calibration band %2")
                                          .arg(number(offsets[index] * step, 2))
                                          .arg(index + 1));
            bottom += height;
        }
        m_document->addLayerGcode(bottom + 0.02, QStringLiteral("gcode"),
                                  QStringLiteral("SET_GCODE_OFFSET Z=0 ; squish ladder done"));
        if (params.flag("numbers", true))
            addLabels(count, -(size / 2.0 + 14.0),
                      [&](int index) { return number(offsets[index] * step, 2); });
        return QStringLiteral("first layer: %1 patches from %2 mm to %3 mm of nozzle height")
            .arg(count)
            .arg(number(-3.0 * step, 2), number(2.0 * step, 2));
    }

    if (kind == QLatin1String("vfa")) {
        const double start = params.number("start", 40);
        const double end = params.number("end", 200);
        const double step = std::max(1.0, params.number("step", 20));
        const double base = std::max(1.0, params.number("speed", 100));
        const int count = bandCount(start, end, step, 24);
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
        // SET_INPUT_SHAPER is a Klipper command. Marlin and RepRap Firmware do
        // take input shaping, but through M593, whose arguments differ enough
        // between the two that a tower built for one is a fiction on the other.
        // Refusing says so; a tower that changes nothing between bands does not.
        if (family != Family::Klipper)
            return QStringLiteral("input shaping is a Klipper feature: this printer speaks %1, "
                                  "where SET_INPUT_SHAPER is not a command. No test was built")
                .arg(machine.flavour);
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
