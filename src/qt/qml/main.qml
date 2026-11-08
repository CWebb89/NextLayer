// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtCore
import NextLayer 1.0
import "theme"
import "components"
import "views"

Window {
    id: root

    readonly property string modelMode: "model"
    readonly property string previewMode: "preview"

    property string mode: toolpath.loaded ? root.previewMode : root.modelMode
    readonly property string deviceMode: "device"
    property bool panelOnLeft: false
    property real panelWidth: Metrics.settingsWidth
    readonly property real minPanelWidth: 320
    readonly property real maxPanelWidth: 680
    property string activeTool: ""
    property string viewportStatus: ""
    property string lastModelPath: ""
    property string lastModelFolder: ""
    property bool objectListVisible: false

    readonly property bool hasModel: session.hasModel

    // The version the binary was compiled with, so the header and About cannot
    // disagree with the build.
    readonly property string appVersion: typeof appVersionString !== "undefined"
                                         ? appVersionString : ""
    readonly property string engineVersion: typeof engineVersionString !== "undefined"
                                            ? engineVersionString : ""
    readonly property string dataDirectory: typeof dataDirString !== "undefined"
                                            ? dataDirString : ""
    readonly property string presetDirectory: typeof presetDirString !== "undefined"
                                              ? presetDirString : ""
    property string projectPath: ""
    property bool fileMenuVisible: false
    property point fileMenuAt: Qt.point(Metrics.panelPadding, Metrics.headerHeight + 4)
    property bool calibrationVisible: false
    property bool objectMenuVisible: false
    property int objectMenuObjectId: 0
    property real objectMenuX: 0
    property real objectMenuY: 0

    // What the object right-click offers: the actions that act on one object,
    // grouped by what they act on -- edit, transform, plate, then the
    // volumes that change what the object becomes.
    function openObjectMenu(objectId, x, y) {
        if (objectId <= 0)
            return
        root.objectMenuObjectId = objectId
        root.objectMenuX = x
        root.objectMenuY = y
        root.objectMenuVisible = true
    }

    readonly property var objectMenuEntries: {
        const id = root.objectMenuObjectId
        const object = id > 0 ? plateDocument.objectInfo(id) : ({})
        return [
            { id: "objSettings", label: "Per-object settings…", hint: "layer height, walls, infill" },
            { id: "objSettingsReset", label: "Drop every override" },
            { id: "objSep1", label: "" },
            { id: "objDuplicate", label: "Duplicate" },
            { id: "objDelete", label: "Delete", hint: "Del" },
            { id: "objIsolate", label: "Isolate" },
            { id: "objShowAll", label: "Show all" },
            { id: "objStatistics", label: "Statistics…", hint: "volume, area, mass" },
            { id: "objHollow", label: "Hollow…", hint: "an inner wall" },
            { id: "objMerge", label: "Merge selection", disabled: plateDocument.selectedCount < 2 },
            { id: "objSplit", label: "Split into parts" },
            { id: "objSep2", label: "" },
            { id: "objCentre", label: "Centre on the bed" },
            { id: "objDrop", label: "Drop to bed" },
            { id: "objOrient", label: "Auto-orient" },
            { id: "objLayFace", label: "Lay on face", hint: "then click a facet" },
            { id: "objSep3", label: "" },
            { id: "objModifier", label: "Add modifier" },
            { id: "objNegative", label: "Add negative volume" },
            { id: "objBlocker", label: "Add support blocker" },
            { id: "objEnforcer", label: "Add support enforcer" }
        ]
    }

    // The window's close guard reads these two: unsavedChanges decides whether
    // there is a question to ask, allowClose is set once it has been answered.
    readonly property bool unsavedChanges: plateDocument.modified
    property bool allowClose: false
    property bool closeGuardOpen: false

    function promptClose() {
        root.closeGuardOpen = true
    }

    function closeWithoutSaving() {
        root.closeGuardOpen = false
        root.allowClose = true
        root.close()
    }

    function saveThenClose() {
        const path = plateDocument.projectPath
        if (path.length === 0) {
            root.closeGuardOpen = false
            saveProjectDialog.open()
            return
        }
        if (!root.saveProject(path))
            return
        root.closeWithoutSaving()
    }
    property bool addMenuVisible: false
    property point addMenuAt: Qt.point(Metrics.railWidth + Metrics.md,
                                       Metrics.headerHeight + Metrics.md)
    property bool layerMenuVisible: false
    property bool plateMenuVisible: false
    property bool plateListVisible: false
    property real plateListX: 0
    property real plateListY: 0
    property int plateMenuIndex: 0
    property real plateMenuX: 0
    property real plateMenuY: 0

    // The plate selector's list: every plate with what it holds, the active one
    // marked, then the options of the plate you are on. One entry per plate keeps
    // the bar down to a single control however many plates a project has.
    readonly property var plateListEntries: {
        const list = []
        const source = root.plates
        for (let i = 0; i < source.length; ++i) {
            const entry = source[i]
            list.push({ id: "plateGo:" + i,
                        label: entry.name,
                        hint: entry.locked ? "locked" : (i === root.activePlate ? "active" : ""),
                        active: i === root.activePlate })
        }
        list.push({ id: "sep1" })
        list.push({ id: "plateOptions", label: "Plate options\u2026", hint: "" })
        return list
    }

    readonly property var plateMenuEntries: [
        { id: "plateRename", label: "Rename plate\u2026", hint: "" },
        { id: "plateDuplicate", label: "Duplicate plate", hint: "with its objects" },
        { id: "plateLock",
          label: plateDocument.plateLocked(root.plateMenuIndex) ? "Unlock plate" : "Lock plate",
          hint: "read-only until unlocked" },
        { id: "plateClear", label: "Clear the plate", hint: "keep the plate" },
        { id: "sep1" },
        { id: "plateDelete", label: "Delete plate", hint: "",
          disabled: plateDocument.plateCount <= 1 }
    ]

    property real layerMenuX: 0
    property real layerMenuY: 0

    readonly property var layerMenuEntries: [
        { id: "layerGcode", label: "Add custom G-code\u2026", hint: "at this layer" },
        { id: "layerPause", label: "Add a pause\u2026", hint: "M601 at this layer" },
        { id: "layerToolChange", label: "Add a tool change\u2026", hint: "switch tool here" },
        { id: "sep1" },
        { id: "layerRemove", label: "Remove the marker here", hint: "", disabled: true }
    ]

    readonly property var addMenuEntries: [
        { id: "addShapeBox", label: "Add box", hint: "40 x 40 x 20 mm" },
        { id: "addShapeCylinder", label: "Add cylinder", hint: "40 x 40 mm" },
        { id: "addShapeSphere", label: "Add sphere", hint: "40 mm" },
        { id: "addText", label: "Add text…", hint: "glyphs as a solid" },
        { id: "sep1" },
        { id: "addModifier", label: "Add modifier volume", hint: "", disabled: !root.hasModel },
        { id: "addNegative", label: "Add negative volume", hint: "", disabled: !root.hasModel },
        { id: "addBlocker", label: "Add support blocker", hint: "", disabled: !root.hasModel },
        { id: "addEnforcer", label: "Add support enforcer", hint: "", disabled: !root.hasModel }
    ]

    readonly property var objectActions: [
        { id: "open", icon: "folder_open", label: "Add model", hint: "Add a model to this plate (Ctrl+O)",
          enabled: true },
        { id: "objects", icon: "list", label: "Object list", hint: "Show the objects on this plate (Ctrl+L)",
          enabled: root.hasModel },
        { id: "delete", icon: "delete", label: "Delete", hint: "Delete the selected object",
          enabled: root.hasModel, danger: true },
        { id: "duplicate", icon: "content_copy", label: "Duplicate", hint: "Duplicate the selected object",
          enabled: root.hasModel },
        { id: "split", icon: "call_split", label: "Split", hint: "Split needs multi-object scenes; not exposed by the engine CLI",
          enabled: false },
        { id: "centre", icon: "filter_center_focus", label: "Centre", hint: "Centre the object on the bed",
          enabled: root.hasModel },
        { id: "drop", icon: "vertical_align_bottom", label: "Drop to bed", hint: "Drop the object onto the bed",
          enabled: root.hasModel },
        { id: "arrange", icon: "grid_view", label: "Arrange", hint: "Arrange the plate with the engine's own packer",
          enabled: root.hasModel },
        { id: "orient", icon: "autorenew", label: "Auto-orient", hint: "Auto-orient the selection: lowest, least-supported pose",
          enabled: root.hasModel }
    ]

    // One table drives both the key handling and the shortcut map, so a binding
    // cannot exist in the code and be missing from the help. Entries with no
    // `mods` are handled where they make sense (a viewport key, a mouse drag) and
    // are listed here for discoverability only.
    readonly property var shortcutList: [
        { id: "newPlate", keys: "Ctrl+N", key: Qt.Key_N, mods: "Ctrl", group: "File",
          action: "Start an empty plate" },
        { id: "open", keys: "Ctrl+O", key: Qt.Key_O, mods: "Ctrl", group: "File",
          action: "Add a model to this plate" },
        { id: "openProject", keys: "Ctrl+Shift+O", key: Qt.Key_O, mods: "Ctrl+Shift", group: "File",
          action: "Open a project file" },
        { id: "saveProject", keys: "Ctrl+S", key: Qt.Key_S, mods: "Ctrl", group: "File",
          action: "Save the project" },
        { id: "saveProjectAs", keys: "Ctrl+Shift+S", key: Qt.Key_S, mods: "Ctrl+Shift", group: "File",
          action: "Save the project under a new name" },
        { id: "quit", keys: "Ctrl+Q", key: Qt.Key_Q, mods: "Ctrl", group: "Application",
          action: "Quit" },
        { id: "about", keys: "", key: 0, mods: "", group: "Application",
          action: "About next-layer" },
        { id: "slice", keys: "Return", key: Qt.Key_Return, mods: "", group: "File",
          action: "Slice the plate" },
        { id: "export", keys: "Ctrl+E", key: Qt.Key_E, mods: "Ctrl", group: "File",
          action: "Export the G-code" },
        { id: "exportConfig", keys: "Ctrl+Shift+E", key: Qt.Key_E, mods: "Ctrl+Shift", group: "File",
          action: "Export the current configuration" },
        { id: "importConfig", keys: "Ctrl+Shift+I", key: Qt.Key_I, mods: "Ctrl+Shift", group: "File",
          action: "Load a configuration file" },
        { id: "preferences", keys: "Ctrl+,", key: Qt.Key_Comma, mods: "Ctrl", group: "Application",
          action: "Open preferences" },
        { id: "shortcuts", keys: "?", key: Qt.Key_Question, mods: "Shift", group: "Application",
          action: "This shortcut map" },
        { id: "setup", keys: "Ctrl+Alt+S", key: Qt.Key_S, mods: "Ctrl+Alt", group: "Application",
          action: "Reopen the printer setup flow" },
        { id: "undo", keys: "Ctrl+Z", key: Qt.Key_Z, mods: "Ctrl", group: "Edit",
          action: "Undo" },
        { id: "redo", keys: "Ctrl+Shift+Z", key: Qt.Key_Z, mods: "Ctrl+Shift", group: "Edit",
          action: "Redo" },
        { id: "delete", keys: "Delete", key: Qt.Key_Delete, mods: "", group: "Edit",
          action: "Delete the selection" },
        { id: "duplicate", keys: "Ctrl+D", key: Qt.Key_D, mods: "Ctrl", group: "Edit",
          action: "Duplicate the selection" },
        { id: "selectAll", keys: "Ctrl+A", key: Qt.Key_A, mods: "Ctrl", group: "Select",
          action: "Select every object" },
        { id: "clearSelection", keys: "Escape", key: Qt.Key_Escape, mods: "", group: "Select",
          action: "Deselect, then leave the current tool" },
        { id: "isolate", keys: "Ctrl+I", key: Qt.Key_I, mods: "Ctrl", group: "View",
          action: "Hide everything but the selection" },
        { id: "showAll", keys: "Ctrl+Shift+H", key: Qt.Key_H, mods: "Ctrl+Shift", group: "View",
          action: "Show every hidden object again" },
        { id: "objects", keys: "Ctrl+L", key: Qt.Key_L, mods: "Ctrl", group: "View",
          action: "Show or hide the object list" },
        { id: "arrange", keys: "Ctrl+Shift+A", key: Qt.Key_A, mods: "Ctrl+Shift", group: "Tools",
          action: "Arrange the plate" },
        { id: "orient", keys: "Ctrl+R", key: Qt.Key_R, mods: "Ctrl", group: "Tools",
          action: "Auto-orient the selection" },
        { id: "face", keys: "L", key: Qt.Key_L, mods: "", group: "Tools",
          action: "Lay on face: click a face" },
        { id: "move", keys: "M", key: Qt.Key_M, mods: "", group: "Tools",
          action: "Move tool" },
        { id: "measure", keys: "U", key: Qt.Key_U, mods: "", group: "Tools",
          action: "Measure between two points" },
        { id: "objectPanel", keys: "Ctrl+Shift+P", key: Qt.Key_P, mods: "Ctrl+Shift",
          group: "Object", action: "This object's settings, layer height and paint" },
        { id: "diff", keys: "Ctrl+Shift+D", key: Qt.Key_D, mods: "Ctrl+Shift", group: "Presets",
          action: "Show the settings changed from the preset" },
        { id: "preview", keys: "Space", key: Qt.Key_Space, mods: "", group: "View",
          action: "Switch between model and preview" },
        { id: "resetView", keys: "R", key: Qt.Key_R, mods: "", group: "Camera",
          action: "Reset the camera" },
        { id: "fitPlate", keys: "F", key: Qt.Key_F, mods: "", group: "Camera",
          action: "Frame the whole bed" },
        { id: "ortho", keys: "O", key: Qt.Key_O, mods: "", group: "Camera",
          action: "Toggle orthographic projection" },
        { id: "views", keys: "1–7", key: 0, mods: "", group: "Camera",
          action: "Iso, top, bottom, front, rear, left, right" },
        { id: "orbit", keys: "Left drag", key: 0, mods: "", group: "Camera",
          action: "Orbit; with the Move tool the same drag moves the object" },
        { id: "pan", keys: "Right or middle drag", key: 0, mods: "", group: "Camera",
          action: "Pan the camera" },
        { id: "zoom", keys: "Wheel", key: 0, mods: "", group: "Camera",
          action: "Zoom about the cursor" },
        { id: "layerStep", keys: "↑ / ↓", key: 0, mods: "", group: "Preview",
          action: "Step one layer in the preview" }
    ]

    function modifierMask(mods) {
        let mask = Qt.NoModifier
        if (mods.indexOf("Ctrl") >= 0)
            mask |= Qt.ControlModifier
        if (mods.indexOf("Shift") >= 0)
            mask |= Qt.ShiftModifier
        if (mods.indexOf("Alt") >= 0)
            mask |= Qt.AltModifier
        return mask
    }

    function shortcutFor(event) {
        const mask = event.modifiers & (Qt.ControlModifier | Qt.ShiftModifier | Qt.AltModifier)
        for (let i = 0; i < root.shortcutList.length; ++i) {
            const entry = root.shortcutList[i]
            if (entry.key === 0 || entry.key !== event.key)
                continue
            if (root.modifierMask(entry.mods) !== mask)
                continue
            return entry
        }
        return null
    }

    function runShortcut(id) {
        switch (id) {
        case "open": openDialog.open(); break
        case "openProject": openProjectDialog.open(); break
        case "saveProject": {
            if (root.projectPath.length > 0)
                root.saveProject(root.projectPath)
            else
                saveProjectDialog.open()
            break
        }
        case "saveProjectAs": saveProjectDialog.open(); break
        case "newPlate": root.newPlate(); break
        case "about": aboutDialog.open(); break
        case "quit": Qt.quit(); break
        case "slice": footer.startSlice(); break
        case "export": exportDialog.open(); break
        case "exportConfig": exportConfigDialog.open(); break
        case "importConfig": importConfigDialog.open(); break
        case "calibration": root.calibrationVisible = true; break
        case "sendToPrinter": printHostDialog.open(); break
        case "preferences": preferencesDialog.openWithCurrent(); break
        case "shortcuts": shortcutMap.openWithCurrent(); break
        case "setup": setupFlow.reopen(); break
        case "undo": {
            const label = plateDocument.undoLabel
            plateDocument.undo()
            root.setStatus(label.length > 0 ? "undid " + label.toLowerCase() : "nothing to undo")
            break
        }
        case "redo": {
            const label = plateDocument.redoLabel
            plateDocument.redo()
            root.setStatus(label.length > 0 ? "redid " + label.toLowerCase() : "nothing to redo")
            break
        }
        case "delete": plateDocument.removeSelection(); break
        case "duplicate": plateDocument.duplicateSelection(); break
        case "selectAll": plateDocument.selectAll(); break
        case "isolate": plateDocument.isolateSelection(); break
        case "showAll": plateDocument.showAll(); break
        case "objects": root.objectListVisible = !root.objectListVisible; break
        case "arrange": root.runAction("arrange"); break
        case "orient": root.runAction("orient"); break
        case "face": root.runTool("face"); break
        case "move": root.runTool("move"); break
        case "measure": root.runTool("measure"); break
        case "diff": presetDiffDialog.openFor(root.mode === root.previewMode ? "print" : ""); break
        case "objectPanel":
            if (plateDocument.selectedId > 0)
                objectPanel.openFor(plateDocument.selectedId)
            else
                root.setStatus("select an object first")
            break
        default: break
        }
    }

    readonly property var tools: [
        { id: "move", icon: "open_with", label: "Move", state: "ready",
          hint: "Move — drag the model on the bed, or centre it at X/Y" },
        { id: "rotate", icon: "rotate_right", label: "Rotate", state: "ready",
          hint: "Rotate about X/Y/Z in degrees" },
        { id: "scale", icon: "open_in_full", label: "Scale", state: "ready",
          hint: "Scale uniformly" },
        { id: "face", icon: "layers", label: "Lay on face", state: "ready",
          hint: "Lay on face — click a face to put it flat on the bed" },
        { id: "cut", icon: "content_cut", label: "Cut", state: "ready",
          hint: "Cut the object at a Z plane" },
        { id: "measure", icon: "straighten", label: "Measure", state: "ready",
          hint: "Measure — click two points for distance and angle" },
        { id: "text", icon: "text_fields", label: "Text", state: "ready",
          hint: "Add a line of text as a solid, from any installed font" },
        { id: "support", icon: "brush", label: "Support painting", state: "ready",
          hint: "Support painting — brush facets as an enforcer or a blocker" },
        { id: "seam", icon: "gesture", label: "Seam painting", state: "ready",
          hint: "Seam painting — brush where the seam should run" },
        { id: "colour", icon: "palette", label: "Colour painting", state: "ready",
          hint: "Colour painting — brush facets to another tool of the changer" },
        { id: "fuzzy", icon: "grain", label: "Fuzzy skin", state: "ready",
          hint: "Open the fuzzy skin settings" },
        { id: "layers", icon: "height", label: "Variable layer height", state: "ready",
          hint: "Variable layer height — a layer height band on this object" },
        { id: "assemble", icon: "view_in_ar", label: "Assemble", state: "engine",
          hint: "Assembling split parts needs multi-object scenes" },
        { id: "calibrate", icon: "tune", label: "Calibration", state: "ready",
          hint: "Calibration — flow, temperature, pressure advance, retraction and first layer" }
    ]

    // Plates belong to the document -- count, names and locks -- because the
    // viewport draws a bed per plate and has to agree with the bar above it. The
    // toolpath each plate last sliced is the slice controller's, one per plate, so
    // switching plates shows what was sliced rather than the last run's output.

    readonly property var plates: {
        const source = plateDocument.plates
        // Reading the version is what makes this re-evaluate when a plate is
        // sliced: the toolpaths live in the slice controller, which owns them.
        const version = sliceController.gcodeVersion
        const list = []
        for (let i = 0; i < source.length; ++i) {
            const entry = source[i]
            // The bar shows how many objects each plate holds: the only signal
            // that an empty plate is empty rather than broken.
            entry.gcode = version >= 0 ? sliceController.plateGcode(i) : ""
            entry.label = entry.count > 0 ? entry.name + " \u00b7 " + entry.count : entry.name
            list.push(entry)
        }
        return list
    }
    property int activePlate: 0

    function openModel(path) {
        root.addModels([path])
    }

    function addModels(paths) {
        // A plate is only as big as the machine on it. Without a printer there is
        // no bed to place anything on, so the answer is the setup, not a model
        // clamped into a zero-sized bed.
        if (Host.bedShape.length < 3) {
            root.setStatus("choose a printer first — there is no bed yet")
            setupFlow.reopen()
            return 0
        }
        const added = session.addModels(paths)
        if (added > 0) {
            root.mode = root.modelMode
            // A model that lands somewhere the camera is not looking is a model the
            // user has to hunt for: frame what was just added, once.
            viewport.frameContent()
            root.setStatus(added === 1 ? "added 1 model" : "added " + added + " models")
        } else if (session.error.length > 0) {
            root.setStatus(session.error)
        }
        return added
    }

    function openNewModel(path) {
        if (session.open(path))
            root.mode = root.modelMode
    }

    function moveObjectTo(centreX, centreY) {
        const id = plateDocument.selectedId
        const info = id > 0 ? plateDocument.objectInfo(id) : null
        if (info === null || info.x === undefined)
            return
        plateDocument.setPosition(id, Qt.vector3d(centreX, centreY, info.z))
    }

    function rotateSelected(x, y, z) {
        const id = plateDocument.selectedId
        if (id > 0)
            plateDocument.setRotation(id, Qt.vector3d(x, y, z))
    }

    function scaleSelected(factor) {
        const id = plateDocument.selectedId
        if (id > 0)
            plateDocument.setScale(id, Qt.vector3d(factor, factor, factor))
    }

    function duplicateSelected(count) {
        const id = plateDocument.selectedId
        if (id <= 0)
            return
        for (let i = 0; i < Math.max(1, count); ++i)
            plateDocument.duplicateObject(id)
    }

    // Plates live in the document, not in the window: every plate is a set of
    // objects on its own bed, and switching plates only changes which set the
    // viewport, the object list and the slice all read. Each plate keeps its own
    // toolpath so switching back shows what was sliced.
    function switchPlate(index) {
        if (index < 0 || index >= root.plates.length)
            return
        if (index !== root.activePlate)
            root.activePlate = index
        

        root.activeTool = ""
        viewport.selected = false
        viewport.clearMeasure()
        viewport.measureActive = false
        viewport.layOnFaceActive = false
        viewport.paintActive = false
        plateDocument.setActivePlate(index)
        // The camera follows the plate: switching tabs frames the plate you
        // switched to, wherever it sits in the scene.
        viewport.fitPlate()

        const plate = root.plates[index]
        const gcode = sliceController.plateGcode(index)
        if (plate !== undefined && gcode.length > 0 && toolpath.load(gcode)) {
            toolpath.setLayer(toolpath.layerCount - 1)
            root.mode = root.previewMode
        } else {
            toolpath.clear()
            root.mode = root.modelMode
        }
    }

    function addPlate() {
        const index = plateDocument.addPlate()
        sliceController.shiftPlateGcodes(index, 1)
        root.switchPlate(index)
        root.setStatus("added plate " + (index + 1))
    }

    function removePlate(index) {
        if (plateDocument.plateCount <= 1)
            return
        plateDocument.removePlate(index)
        sliceController.shiftPlateGcodes(index, -1)
        root.activePlate = Math.min(index, plateDocument.plateCount - 1)
        root.switchPlate(root.activePlate)
    }

    function duplicatePlate(index) {
        const target = plateDocument.duplicatePlate(index)
        sliceController.duplicatePlateGcode(index, target)
        root.switchPlate(target)
        root.setStatus("cloned plate " + (index + 1) + " as plate " + (target + 1))
    }

    function togglePlateLock(index) {
        const locked = !plateDocument.plateLocked(index)
        plateDocument.setPlateLocked(index, locked)
        root.setStatus((locked ? "locked" : "unlocked") + " plate " + (index + 1))
    }

    function setStatus(text) {
        root.viewportStatus = text
        statusTimer.restart()
    }

    function clearObject() {
        session.clear()
        toolpath.clear()
        viewport.selected = false
        root.activeTool = ""
        root.mode = root.modelMode
        root.setStatus("plate cleared")
    }

    function saveProject(path) {
        const error = plateDocument.saveProject(path)
        if (error.length > 0) {
            root.setStatus(error)
            return false
        }
        root.projectPath = path
        root.setStatus("saved " + path.split("/").pop())
        return true
    }

    function openProject(path) {
        const error = plateDocument.loadProject(path)
        if (error.length > 0) {
            root.setStatus(error)
            return false
        }
        root.projectPath = path
        const printer = plateDocument.printSetupPrinter()
        const process = plateDocument.printSetupProcess()
        const filament = plateDocument.printSetupFilament()
        if (printer.length > 0 && process.length > 0 && filament.length > 0)
            presetStore.reload(printer, process, filament)
        toolpath.clear()
        root.mode = root.modelMode
        viewport.selected = false
        viewport.resetView()
        root.setStatus("opened " + path.split("/").pop())
        return true
    }

    function newPlate() {
        plateDocument.clear()
        toolpath.clear()
        root.projectPath = ""
        root.mode = root.modelMode
        root.setStatus("empty plate")
    }

    function runAction(id) {
        // The plate list's rows carry the plate they mean in the id, so one list can
        // address any number of plates without a second model.
        if (typeof id === "string" && id.indexOf("plateGo:") === 0) {
            root.switchPlate(parseInt(id.substring("plateGo:".length), 10))
            return
        }
        switch (id) {
        case "open": openDialog.open(); break
        case "objects": root.objectListVisible = !root.objectListVisible; break
        case "delete": plateDocument.removeSelection(); break
        case "duplicate": plateDocument.duplicateSelection(); break
        case "centre": plateDocument.centreSelection(); break
        case "drop": plateDocument.dropSelection(); break
        case "selectAll": plateDocument.selectAll(); break
        case "arrange":
            root.setStatus(plateDocument.arrangePlate() ? "arranged the plate"
                                                        : "arrange could not place every object")
            viewport.fitPlate()
            break
        case "orient":
            root.setStatus(plateDocument.autoOrientSelection() ? "auto-oriented the selection"
                                                               : "nothing to orient")
            break
        case "isolate": plateDocument.isolateSelection(); break
        case "showAll": plateDocument.showAll(); break
        case "export": exportDialog.open(); break
        case "addShapeBox":
            root.addShape("box", 40, 40, 20); break
        case "addShapeCylinder":
            root.addShape("cylinder", 40, 40, 40); break
        case "addShapeSphere":
            root.addShape("sphere", 40, 40, 40); break
        case "addText": textDialog.open(); break
        case "addModifier":
            root.addVolume("modifier"); break
        case "addNegative":
            root.addVolume("negative"); break
        case "addBlocker":
            root.addVolume("blocker"); break
        case "addEnforcer":
            root.addVolume("enforcer"); break
        case "layerGcode": layerGcodeDialog.openFor(toolpath.layerZ, toolpath.currentLayer, "gcode"); break
        case "layerPause": layerGcodeDialog.openFor(toolpath.layerZ, toolpath.currentLayer, "pause"); break
        case "layerToolChange":
            layerGcodeDialog.openFor(toolpath.layerZ, toolpath.currentLayer, "tool"); break
        case "plateRename":
            textPrompt.openWith("Rename plate", plateDocument.plateName(root.plateMenuIndex),
                                "", "Rename")
            break
        case "plateDuplicate":
            root.duplicatePlate(root.plateMenuIndex)
            break
        case "plateLock":
            root.togglePlateLock(root.plateMenuIndex)
            break
        case "plateClear":
            plateDocument.clearPlate(root.plateMenuIndex)
            root.setStatus("cleared plate " + (root.plateMenuIndex + 1))
            break
        case "plateDelete":
            if (root.activePlate === root.plateMenuIndex)
                root.removePlate(root.plateMenuIndex)
            else
                root.setStatus("switch to the plate first")
            break
        case "layerRemove":
            if (!plateDocument.removeLayerGcode(toolpath.layerZ))
                root.setStatus("no marker at " + toolpath.layerZ.toFixed(2) + " mm")
            else
                root.setStatus("removed the marker at " + toolpath.layerZ.toFixed(2) + " mm")
            break
        // A menu row and a key are the same command. Everything reachable only by
        // key lives in runShortcut, and a second switch that forgot half of them is
        // exactly why the File menu's rows did nothing when clicked.
        default: root.runShortcut(id); break
        }
    }

    /// A primitive straight onto the plate. It is an object like any other, so
    /// every tool, every per-object setting and the project file all work on it.
    function addShape(kind, width, depth, height) {
        const id = plateDocument.addShape(kind, width, depth, height)
        if (id < 0) {
            root.setStatus(plateDocument.error.length > 0 ? plateDocument.error
                                                          : "could not add a " + kind)
            return
        }
        viewport.fitPlate()
        root.setStatus("added a " + kind + " (" + width + " x " + depth + " x " + height + " mm)")
    }

    /// A volume attached to the selection: what turns a model into a part. The
    /// engine's own slice does the boolean, so the viewport overlay and the
    /// toolpath are the same geometry.
    function addVolume(type) {
        const id = plateDocument.selectedId
        if (id <= 0) {
            root.setStatus("select an object first")
            return
        }
        const info = plateDocument.objectInfo(id)
        const size = Math.max(4, Math.min(30, Math.min(info.width, info.depth, info.height) * 0.6))
        const volumeId = plateDocument.addVolume(id, "box", type, size, size, size)
        if (volumeId < 0) {
            root.setStatus(plateDocument.error.length > 0 ? plateDocument.error
                                                          : "could not add a " + type + " volume")
            return
        }
        root.setStatus("added a " + type + " volume to " + info.name)
    }

    function runTool(id) {
        if (id === "move" || id === "rotate" || id === "scale" || id === "cut"
                || id === "duplicate") {
            root.activeTool = root.activeTool === id ? "" : id
            return
        }
        if (id === "face") {
            viewport.layOnFaceActive = !viewport.layOnFaceActive
            root.activeTool = viewport.layOnFaceActive ? "face" : ""
            root.setStatus(viewport.layOnFaceActive ? "click a face to lay it on the bed"
                                                    : "")
            return
        }
        if (id === "measure") {
            viewport.measureActive = !viewport.measureActive
            if (!viewport.measureActive)
                viewport.clearMeasure()
            root.activeTool = viewport.measureActive ? "measure" : ""
            return
        }
        if (id === "support" || id === "seam" || id === "colour") {
            const objectId = plateDocument.selectedId
            if (objectId <= 0) {
                root.setStatus("select an object first")
                return
            }
            const kind = id === "seam" ? "seam" : id === "colour" ? "tool" : "supports"
            const already = viewport.paintActive && viewport.paintObjectId === objectId
                                           && viewport.paintKind === kind
            viewport.paintActive = !already
            viewport.paintObjectId = objectId
            viewport.paintKind = kind
            viewport.paintRadius = objectPanel.brushRadius
            viewport.paintErase = objectPanel.erasing
            root.activeTool = viewport.paintActive ? id : ""
            const label = kind === "tool" ? "tool " + objectPanel.paintTool
                                         : kind === "seam" ? "seam" : "support"
            root.setStatus(viewport.paintActive
                           ? "painting " + label + " — drag on "
                             + plateDocument.objectInfo(objectId).name
                           : "")
            objectPanel.openFor(objectId)
            objectPanel.settingsTab = false
            return
        }
        if (id === "layers") {
            const objectId = plateDocument.selectedId
            if (objectId <= 0) {
                root.setStatus("select an object first")
                return
            }
            objectPanel.openFor(objectId)
            objectPanel.settingsTab = false
            return
        }
        if (id === "fuzzy") {
            settingsPanel.showSubtab("process", "101")
            root.mode = root.modelMode
            root.setStatus("Fuzzy skin lives in Process \u203a Walls & Perimeters")
        }
    }

    Timer {
        id: statusTimer
        interval: 6000
        onTriggered: root.viewportStatus = ""
    }

    width: Metrics.windowWidth
    height: Metrics.windowHeight
    minimumWidth: 1000
    minimumHeight: 700
    visible: true
    title: "next-layer"
    color: Colors.background

    // A window that comes up without keyboard focus swallows every key until
    // something is clicked -- the first `F` or `Ctrl+O` of a session did nothing,
    // which reads as shortcuts being broken rather than as focus being elsewhere.
    onActiveChanged: {
        if (active && shell.activeFocusItem === null)
            shell.forceActiveFocus()
    }

    Item {
        id: shell
        anchors.fill: parent
        focus: true

        // Modal dialogs are drawn by the Controls overlay, and their dimming comes
        // from the platform style: under qt6ct that scrim is fully transparent, so a
        // dialog floated over a bright toolpath with nothing behind it. Attached to
        // the window's content item, one rule covers every dialog in the app.
        Overlay.modal: Rectangle {
            color: Colors.alpha("#000000", 0.55)
        }

        // Every binding comes from root.shortcutList, the same table the map
        // dialog shows. Escape is handled before the table because it means
        // "leave what you are doing" first and "deselect" only after that.
        Keys.onPressed: (event) => {
            // The close guard answers to keys as well as the mouse: it is the one
            // dialog a person meets when a script or a terminal asks the window to
            // close, and a button that can only be hit with the pointer cannot be
            // answered by anything but a pointer. `D` discards, `S` saves and
            // closes, Escape cancels.
            if (root.closeGuardOpen) {
                if (event.key === Qt.Key_D) {
                    root.closeWithoutSaving()
                    event.accepted = true
                    return
                }
                if (event.key === Qt.Key_S) {
                    root.saveThenClose()
                    event.accepted = true
                    return
                }
            }
            if (event.key === Qt.Key_Escape && root.addMenuVisible) {
                root.addMenuVisible = false
                event.accepted = true
                return
            }
            if (event.key === Qt.Key_Escape && root.fileMenuVisible) {
                root.fileMenuVisible = false
                event.accepted = true
                return
            }
            if (event.key === Qt.Key_Escape) {
                // An open dialog owns Escape. The shell keeps focus while a dialog
                // is up -- a popup only takes it when something inside can hold it --
                // so dismissing it has to happen here or Escape does nothing.
                // `opened` is the Popup property; asking for `closed` matched
                // nothing and every dialog swallowed Escape in silence.
                const overlays = Overlay.overlay ? Overlay.overlay.children : []
                for (let i = overlays.length - 1; i >= 0; --i) {
                    const overlay = overlays[i]
                    if (overlay && overlay.opened === true && typeof overlay.close === "function") {
                        overlay.close()
                        event.accepted = true
                        return
                    }
                }
                // Panels drawn inside the scene rather than in the overlay are
                // not Popups, so the loop above cannot see them.
                if (root.closeGuardOpen) {
                    root.closeGuardOpen = false
                    event.accepted = true
                    return
                }
                if (setupFlow.visible) {
                    setupFlow.close()
                    event.accepted = true
                    return
                }
                if (welcome.visible) {
                    welcome.close()
                    event.accepted = true
                    return
                }
                if (viewport.measureActive) {
                    viewport.clearMeasure()
                    viewport.measureActive = false
                    root.activeTool = ""
                    event.accepted = true
                    return
                }
                if (viewport.layOnFaceActive || viewport.paintActive) {
                    viewport.layOnFaceActive = false
                    viewport.paintActive = false
                    objectPanel.painting = false
                    root.activeTool = ""
                    event.accepted = true
                    return
                }
                if (root.activeTool !== "") {
                    root.activeTool = ""
                    event.accepted = true
                    return
                }
                if (root.mode !== root.modelMode) {
                    root.mode = root.modelMode
                    event.accepted = true
                    return
                }
                root.runShortcut("clearSelection")
                plateDocument.clearSelection()
                event.accepted = true
                return
            }

            const entry = root.shortcutFor(event)
            if (entry === null)
                return

            // A text field owns Return and Space while it has the keyboard: the
            // device tab's address field is typed into and confirmed with Return.
            if (root.editingText() && (entry.id === "slice" || entry.id === "preview")) {
                event.accepted = false
                return
            }

            if (entry.id === "delete" && settingsPanel.filterFocused) {
                event.accepted = false
                return
            }
            if (entry.id === "slice" && settingsPanel.filterFocused) {
                event.accepted = false
                return
            }
            if (entry.id === "preview") {
                if (toolpath.loaded) {
                    root.mode = root.mode === root.previewMode ? root.modelMode : root.previewMode
                    event.accepted = true
                }
                return
            }
            root.runShortcut(entry.id)
            event.accepted = true
        }

        Header {
            id: header
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: Metrics.headerHeight
            mode: root.mode
            version: root.appVersion
            documentName: session.hasModel ? session.displayName() : ""
            onModeRequested: (requested) => root.mode = requested
            onFileMenuRequested: (x, y) => {
                root.fileMenuAt = Qt.point(x, y)
                root.fileMenuVisible = !root.fileMenuVisible
            }
            onSavePresetRequested: {
                root.mode = "model"
                settingsPanel.openPresetEditor(settingsPanel.activeTab)
            }
        }

        Rail {
            id: rail
            anchors.left: parent.left
            anchors.top: header.bottom
            anchors.bottom: footer.top
            width: Metrics.railWidth
            currentView: root.mode
            objectListVisible: root.objectListVisible
            objectCount: plateDocument.count
            onViewRequested: (requested) => root.mode = requested
            onObjectListRequested: root.objectListVisible = !root.objectListVisible
            onResetViewRequested: viewport.resetView()
            onSetupRequested: setupFlow.reopen()
        }

        Welcome {
            id: welcome
            anchors.fill: parent
            version: root.appVersion
            onSetupRequested: setupFlow.reopen()
            onSkipRequested: root.setStatus("printer setup is in the rail gear and Ctrl+,")
            onImported: (printer) => {
                if (printer.length === 0) {
                    root.setStatus("imported presets, no printer among them")
                    return
                }
                presetStore.selectPrinter(printer)
                root.setStatus("imported " + printer)
            }
            onRestartRequested: {
                root.allowClose = true
                presetStore.relaunch()
            }
        }

        SetupFlow {
            id: setupFlow
            anchors.fill: parent
            onClosed: root.setStatus("setup closed")
        }

        Connections {
            target: Host
            function onBedChanged() {
                viewport.resetView()
                root.setStatus("bed for " + Host.printerModel)
            }
        }

        // Nothing is configured for a machine until a preset resolves, so this is
        // the state a fresh install is actually in: no bed, no plate, and the one
        // action that fixes it.
        Item {
            id: noPrinter

            anchors.top: header.bottom
            anchors.bottom: footer.top
            x: root.panelOnLeft
               ? Metrics.railWidth + root.panelWidth
               : Metrics.railWidth
            width: shell.width - Metrics.railWidth - root.panelWidth
            visible: Host.bedShape.length < 3 && root.mode !== root.deviceMode
            z: 4

            Rectangle {
                anchors.centerIn: parent
                width: Math.min(parent.width - Metrics.panelMargin * 4, 460)
                height: card.implicitHeight + Metrics.panelPadding * 2
                radius: Metrics.radiusLarge
                color: Colors.surfaceContainerLow
                border.width: Metrics.borderWidth
                border.color: Colors.edge

                Column {
                    id: card

                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: Metrics.panelPadding
                    spacing: Metrics.sm

                    Text {
                        text: "No printer yet"
                        font.family: Type.family
                        font.pixelSize: Type.sizeLarge
                        font.bold: true
                        color: Colors.textPrimary
                    }

                    Text {
                        width: parent.width
                        text: "next-layer ships no machine of its own. Pick your printer and the bed, its height and its materials become yours; until then there is nothing to slice onto."
                        wrapMode: Text.WordWrap
                        font.family: Type.family
                        font.pixelSize: Type.sizeSmall
                        color: Colors.textSecondary
                    }

                    Item { width: 1; height: Metrics.xs }

                    Button {
                        text: "Choose my printer"
                        primary: true
                        onClicked: setupFlow.reopen()
                    }
                }
            }
        }

        Viewport {
            id: viewport
            objectName: "viewport"
            anchors.top: header.bottom
            anchors.bottom: footer.top
            x: root.panelOnLeft
               ? Metrics.railWidth + root.panelWidth
               : Metrics.railWidth
            width: shell.width - Metrics.railWidth - root.panelWidth
            visible: root.mode !== root.deviceMode
            mode: root.mode
            moveEnabled: root.activeTool === "move"
            onPlacedOnFace: (objectId, facet) => {
                root.setStatus("laid face " + facet + " on the bed")
            }
            onPainted: (objectId, facets) => {
                root.setStatus("painted " + facets + " facet(s)")
            }
            onBandSelected: (count) => {
                root.setStatus(count === 1 ? "selected 1 object"
                                            : "selected " + count + " objects")
            }
            onPlateSelected: (index) => root.switchPlate(index)
            onPlateAddRequested: root.addPlate()
            onPlateRemoveRequested: (index) => root.removePlate(index)
            onPlateLockRequested: (index) => root.togglePlateLock(index)
            onPlateRenameRequested: (index) => {
                root.plateMenuIndex = index
                textPrompt.openWith("Rename plate", plateDocument.plateName(index), "", "Rename")
            }
            // Both act on the plate you are on: switching first is what makes the
            // click mean "this plate" rather than "whichever plate was active".
            onPlateOrientRequested: (index) => {
                root.switchPlate(index)
                root.runAction("orient")
            }
            onPlateArrangeRequested: (index) => {
                root.switchPlate(index)
                root.runAction("arrange")
            }

            Behavior on x { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
            Behavior on width { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
        }


        DeviceView {
            id: deviceView
            anchors.top: header.bottom
            anchors.bottom: footer.top
            x: root.panelOnLeft
               ? Metrics.railWidth + root.panelWidth
               : Metrics.railWidth
            width: shell.width - Metrics.railWidth - root.panelWidth
            visible: root.mode === root.deviceMode
            onStatus: (message) => root.setStatus(message)
        }

        ViewportToolbar {
            id: viewportToolbar
            visible: root.mode !== root.deviceMode
            anchors.left: viewport.left
            anchors.right: viewport.right
            anchors.top: viewport.top
            anchors.leftMargin: Metrics.md
            anchors.rightMargin: Metrics.md
            anchors.topMargin: Metrics.md
            plates: root.plates
            activePlate: root.activePlate
            tools: root.tools
            activeTool: root.activeTool
            actions: root.objectActions
            slicing: footer.slicing
            canSlice: session.hasModel
            onPlateSelected: (index) => root.switchPlate(index)
            onPlateAdded: root.addPlate()
            onPlateRemoved: (index) => root.removePlate(index)
            onPlateLockToggled: (index) => root.togglePlateLock(index)
            onPlateCloned: (index) => root.duplicatePlate(index)
            onPlateMenuRequested: (index, x, y) => {
                root.plateMenuIndex = index
                root.plateMenuX = x
                root.plateMenuY = y
                root.plateMenuVisible = true
            }
            onPlateListRequested: (x, y) => {
                root.plateListX = x
                root.plateListY = y
                root.plateListVisible = true
            }
            onToolTriggered: (id) => root.runTool(id)
            onActionTriggered: (id) => root.runAction(id)
            onAddRequested: (x, y) => {
                root.addMenuAt = Qt.point(x, y)
                root.addMenuVisible = !root.addMenuVisible
            }
            onSliceRequested: footer.startSlice()
            onSliceAllRequested: footer.startSliceAll()
        }

        // What the last action did, in the same corner the tool strip calls home.
        // Every tool reports through setStatus, and until now nothing on screen
        // read it: the message went into a property and vanished.
        Rectangle {
            id: statusToast
            visible: root.viewportStatus.length > 0
            anchors.left: viewport.left
            anchors.bottom: viewport.bottom
            anchors.leftMargin: Metrics.md
            anchors.bottomMargin: Metrics.md
            width: Math.min(statusLabel.implicitWidth + Metrics.md * 2, viewport.width - Metrics.md * 2)
            height: Metrics.rowHeight
            radius: Metrics.radiusSmall
            color: Colors.alpha(Colors.surfaceContainerLow, 0.94)
            border.width: Metrics.borderWidth
            border.color: Colors.edge
            z: 5

            Text {
                id: statusLabel
                anchors.centerIn: parent
                width: Math.min(implicitWidth, statusToast.width - Metrics.md * 2)
                text: root.viewportStatus
                elide: Text.ElideRight
                font.family: Type.family
                font.pixelSize: Type.sizeCaption
                color: Colors.textSecondary
            }
        }

        // The object list slides out of the settings panel's inner edge. Its
        // toggle is the rail's third entry, so the list has no tab of its
        // own to sit in the way of the viewport.
        ObjectListPanel {
            id: objectList
            anchors.top: viewportToolbar.bottom
            anchors.topMargin: Metrics.sm
            anchors.bottom: footer.top
            z: 4
            visible: root.objectListVisible
            x: root.panelOnLeft
               ? settingsPanel.x + settingsPanel.width
               : settingsPanel.x - width
            width: Metrics.drawerWidth
            onCloseRequested: root.objectListVisible = false
            onContextRequested: (objectId, x, y) => root.openObjectMenu(objectId, x, y)
            onVolumeSettingsRequested: (objectId, volumeId) => {
                objectPanel.openFor(objectId)
                objectPanel.volumeId = volumeId
            }
        }

        TransformPanel {
            id: transformPanel
            anchors.left: viewport.left
            anchors.leftMargin: Metrics.md
            anchors.top: viewportToolbar.bottom
            anchors.topMargin: Metrics.sm
            tool: root.activeTool
            busy: transformController.busy
            onRotateRequested: (x, y, z) => root.rotateSelected(x, y, z)
            onScaleRequested: (factor) => root.scaleSelected(factor)
            onMoveRequested: (x, y) => root.moveObjectTo(x, y)
            onCutRequested: (z) => {
                const parts = plateDocument.cutAtZ(plateDocument.selectedId, z, true)
                root.setStatus(parts > 1 ? "cut into " + parts + " parts"
                                         : plateDocument.error.length > 0 ? plateDocument.error
                                                                          : "nothing to cut there")
            }
            onDuplicateRequested: (count) => root.duplicateSelected(count)
            onCenterRequested: plateDocument.centreObject(plateDocument.selectedId)
            onDropRequested: plateDocument.dropToBed(plateDocument.selectedId)
            onDismissed: root.activeTool = ""
        }

        SettingsPanel {
            id: settingsPanel
            anchors.top: header.bottom
            anchors.bottom: footer.top
            width: root.panelWidth
            x: root.panelOnLeft ? Metrics.railWidth : shell.width - width

            onRememberedPrinterMissing: (name) => {
                root.setStatus("the printer last used, " + name + ", is no longer in the profile list")
                setupFlow.reopen()
            }

            Behavior on x { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
        }

        // Resize handle on the panel's inner edge, so the panel can be narrowed
        // or widened to taste. Dragging away from the anchored edge grows the
        // panel; the width is clamped and persisted.
        Item {
            id: panelResizeHandle
            z: 3
            width: 10
            anchors.top: settingsPanel.top
            anchors.bottom: settingsPanel.bottom
            x: root.panelOnLeft
               ? settingsPanel.x + settingsPanel.width - width / 2
               : settingsPanel.x - width / 2

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: Metrics.xxs
                color: panelResizeMouse.containsMouse || panelResizeMouse.pressed
                     ? Colors.accent : Colors.separator
            }

            MouseArea {
                id: panelResizeMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.SizeHorCursor
                property real pressWidth: 0
                property real pressMouseX: 0
                onPressed: {
                    pressWidth = root.panelWidth
                    pressMouseX = mouseX
                }
                onPositionChanged: {
                    if (!pressed)
                        return
                    const delta = mouseX - pressMouseX
                    const sign = root.panelOnLeft ? 1 : -1
                    root.panelWidth = Math.max(root.minPanelWidth,
                        Math.min(root.maxPanelWidth, pressWidth + delta * sign))
                }
                onDoubleClicked: root.panelWidth = Metrics.settingsWidth
            }
        }

        // The preview's right-hand column: the legend, then the g-code window,
        // through one stack so they cannot both claim the same pixels. It stops
        // above the camera card, which the viewport positions itself.
        Item {
            id: rightStack
            anchors.right: viewport.right
            anchors.top: viewport.top
            anchors.bottom: viewport.bottom
            anchors.rightMargin: Metrics.md + viewport.rightInset
            anchors.topMargin: Metrics.md + viewportToolbar.implicitHeight + Metrics.md
            anchors.bottomMargin: Metrics.md + viewport.bottomInset + viewport.cameraBarHeight + Metrics.md
            width: Math.min(380, viewport.width * 0.42)
            clip: true

            Column {
                width: parent.width
                spacing: Metrics.md

                PreviewLegend {
                    id: legend
                    width: parent.width
                    active: root.mode === root.previewMode
                }

                GcodeWindow {
                    id: gcodeWindow
                    width: parent.width
                    active: root.mode === root.previewMode
                    maxHeight: Math.max(120, rightStack.height - legend.height - Metrics.md)
                }
            }
        }

        LayerSlider {
            id: layerSlider
            objectName: "layerSlider"
            activeFocusOnTab: false
            visible: root.mode === root.previewMode && toolpath.layerCount > 0
            width: 132
            height: Math.min(Math.round(viewport.height * 0.8), 560)
            anchors.right: viewport.right
            anchors.rightMargin: Metrics.sm
            anchors.verticalCenter: viewport.verticalCenter
            from: 0
            to: Math.max(0, toolpath.layerCount - 1)
            step: 1
            labelOffset: 1
            lowerValue: toolpath.firstLayer
            upperValue: toolpath.currentLayer
            upperDetail: Number(toolpath.layerZ).toFixed(2) + " mm"
            onLowerMoved: (value) => toolpath.setFirstLayer(Math.round(value))
            onUpperMoved: (value) => toolpath.setLayer(Math.round(value))
            onStepped: (delta) => toolpath.setLayer(toolpath.currentLayer + delta)
            markers: plateDocument.layerGcodeMarkers
            onContextRequested: (x, y) => {
                root.layerMenuX = x
                root.layerMenuY = y
                root.layerMenuVisible = true
            }
        }

        // The layer view modes — the part up to the slider, everything, or
        // just that layer — sitting on top of the slider they act on.
        Item {
            id: layerModeButton
            objectName: "layerModeButton"
            visible: layerSlider.visible
            width: 74
            height: 24
            anchors.horizontalCenter: layerSlider.horizontalCenter
            anchors.bottom: layerSlider.top
            anchors.bottomMargin: Metrics.sm

            Rectangle {
                anchors.fill: parent
                radius: Metrics.radiusSmall
                color: layerModeMouse.containsMouse ? Colors.hoverFill : Colors.surfaceContainerHigh
                border.width: Metrics.borderWidth
                border.color: toolpath.layerMode > 0 ? Colors.accent : Colors.edge
            }

            Text {
                anchors.centerIn: parent
                text: toolpath.layerMode === 0 ? "RANGE"
                    : toolpath.layerMode === 1 ? "ALL" : "1 LAYER"
                font.family: Type.family
                font.pixelSize: Type.sizeCaption
                font.bold: true
                color: toolpath.layerMode > 0 ? Colors.accent : Colors.textSecondary
            }

            MouseArea {
                id: layerModeMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: toolpath.cycleLayerMode()
            }
        }

        Text {
            id: movesLabel
            visible: movesSlider.visible
            anchors.horizontalCenter: movesSlider.horizontalCenter
            anchors.bottom: movesSlider.top
            anchors.bottomMargin: Metrics.xxs
            text: "Move " + (Math.round(movesSlider.liveLower) + 1) + "-"
                  + (Math.round(movesSlider.liveUpper) + 1) + " / " + toolpath.layerMoveCount
            font.family: Type.family
            font.pixelSize: Type.sizeCaption
            color: Colors.textSecondary
        }

        MovesSlider {
            id: movesSlider
            objectName: "movesSlider"
            visible: root.mode === root.previewMode && toolpath.layerMoveCount > 1 && width > 200
            anchors.left: viewport.left
            anchors.right: layerSlider.left
            anchors.leftMargin: Metrics.md
            anchors.rightMargin: Metrics.md
            anchors.bottom: viewport.bottom
            anchors.bottomMargin: Metrics.md
            from: 0
            to: Math.max(0, toolpath.layerMoveCount - 1)
            step: 1
            tickCount: toolpath.layerMoveCount
            lowerValue: toolpath.firstMove
            upperValue: toolpath.lastMove
            onLowerMoved: (value) => toolpath.setFirstMove(Math.round(value))
            onUpperMoved: (value) => toolpath.setLastMove(Math.round(value))
        }

        // The moves slider spans the viewport's bottom, so the viewport is told how
        // much of that edge it has lost and pushes its own camera card above it.
        Binding {
            target: viewport
            property: "rightInset"
            value: layerSlider.visible ? layerSlider.width + Metrics.sm : 0
        }

        Binding {
            target: viewport
            property: "bottomInset"
            value: movesSlider.visible ? movesSlider.height + Metrics.sm : 0
        }

        FooterBar {            id: footer
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: Metrics.footerHeight
            onPreviewReady: root.mode = root.previewMode
        }
    }

    Connections {
        target: session

        function onModelChanged() {
            toolpath.clear()
            root.mode = root.modelMode
            root.title = "next-layer · " + session.displayName()
            // Only a different model moves the camera. Re-framing on every document
            // change is what made a plain move or delete look like a zoom.
            if (session.path !== root.lastModelPath) {
                root.lastModelPath = session.path
                viewport.frameContent()
            }
        }
    }

    Connections {
        target: sliceController

        function onFinished() {
            // A single slice belongs to the active plate; a slice-all gave every
            // plate its own toolpath, so the viewport reloads the active plate's.
            if (sliceController.slicedAll)
                root.switchPlate(root.activePlate)
        }
    }

    Connections {
        target: transformController

        function onApplied(path, message) {
            root.mode = root.modelMode
            root.setStatus(message)
            root.activeTool = ""
        }

        function onFailed(message) {
            root.setStatus(message)
        }
    }

    Settings {
        category: "next-layer"
        property alias panelOnLeft: root.panelOnLeft
        property alias panelWidth: root.panelWidth
        property alias lastModelFolder: root.lastModelFolder
    }

    DropArea {
        anchors.fill: parent
        onDropped: (drop) => {
            if (drop.urls.length === 0)
                return
            const paths = []
            for (let i = 0; i < drop.urls.length; ++i)
                paths.push(drop.urls[i].toString())
            root.addModels(paths)
        }
    }

    // Modifier combinations are window shortcuts, so they work whatever has
    // focus -- a text field included, which is why the table marks them and the
    // bare keys stay on the focus-aware handler below.
    // The map has a dedicated window shortcut as well: '?' is a shifted key and
    // the focused viewport consumes some of those before the window sees them.
    Shortcut {
        sequences: ["F1"]
        onActivated: shortcutMap.openWithCurrent()
    }

    // Modifier combinations, plus the two bare keys that must work whatever has
    // focus: Return slices, Space toggles the preview. Delete and Escape stay on
    // the focus-aware handler, because a text field owns them.
    readonly property var windowShortcuts: root.shortcutList.filter(
        (entry) => entry.key !== 0
                   && (entry.mods.indexOf("Ctrl") >= 0 || entry.mods.indexOf("Alt") >= 0
                       || entry.id === "slice" || entry.id === "preview"))

    /// Whether the keyboard belongs to a text input right now. Return and Space
    /// are window shortcuts so they work wherever the focus is, which used to
    /// mean that typing a URL and pressing Return sliced the plate instead.
    function editingText() {
        if (deviceView.typingAddress)
            return true
        const item = root.activeFocusItem
        if (item === null || item === undefined)
            return false
        // A Controls text field exposes these on the control itself; the QML
        // engine does not answer `in` for them, so ask for the values instead.
        return typeof item.cursorPosition !== "undefined"
               || typeof item.acceptableInput !== "undefined"
    }

    Instantiator {
        model: root.windowShortcuts

        delegate: Shortcut {
            required property var modelData
            sequence: modelData.keys
            // A Shortcut swallows its key before the focused item sees it, so the
            // two bare-key shortcuts are switched off while a text field has the
            // keyboard -- otherwise Return in the device tab's address field sliced
            // the plate and never reached the field.
            enabled: !(root.editingText()
                       && (modelData.id === "slice" || modelData.id === "preview"))
            onActivated: root.runShortcut(modelData.id)
        }
    }

    ObjectSettingsDialog {
        id: objectPanel

        onPaintRequested: (objectId, kind, radius, erase, tool) => {
            viewport.paintActive = objectId > 0
            viewport.paintObjectId = objectId
            viewport.paintKind = kind.length > 0 ? kind : "supports"
            viewport.paintRadius = radius
            viewport.paintErase = erase
            viewport.paintTool = tool
            root.activeTool = objectId > 0
                              ? (kind === "tool" ? "colour" : kind === "seam" ? "seam" : "support")
                              : ""
        }
    }

    // The Add menu, dropped from the toolbar's Add button: a shape as a new object,
    // or a volume that changes what an existing object becomes.
    MenuPanel {
        id: addMenu
        parent: Overlay.overlay
        anchors.fill: parent
        visible: root.addMenuVisible
        z: 110
        entries: root.addMenuEntries
        menuWidth: 300
        anchorX: Math.max(Metrics.xs, Math.min(root.width - menuWidth - Metrics.xs, root.addMenuAt.x))
        anchorY: root.addMenuAt.y
        onTriggered: (id) => root.runAction(id)
        onDismissed: root.addMenuVisible = false
    }

    PrintHostDialog {
        id: printHostDialog
    }

    CalibrationDialog {
        id: calibrationDialog
        visible: root.calibrationVisible
        onClosed: {
            root.calibrationVisible = false
            if (calibrationDialog.message.length > 0)
                root.setStatus(calibrationDialog.message)
            viewport.fitPlate()
        }
    }

    MenuPanel {
        id: objectMenu
        parent: Overlay.overlay
        anchors.fill: parent
        visible: root.objectMenuVisible
        z: 112
        menuWidth: 300
        entries: root.objectMenuEntries
        anchorX: Math.min(root.objectMenuX, root.width - 320)
        anchorY: Math.min(root.objectMenuY, root.height - 420)
        onTriggered: (id) => {
            const objectId = root.objectMenuObjectId
            root.objectMenuVisible = false
            if (objectId <= 0)
                return
            if (!plateDocument.isSelected(objectId))
                plateDocument.selectObject(objectId)
            switch (id) {
            case "objSettings": objectPanel.openFor(objectId); break
            case "objSettingsReset": plateDocument.clearObjectSettings(objectId); break
            case "objDuplicate": plateDocument.duplicateObject(objectId); break
            case "objDelete": plateDocument.removeObject(objectId); break
            case "objIsolate": plateDocument.isolateSelection(); break
            case "objShowAll": plateDocument.showAll(); break
            case "objCentre": plateDocument.centreObject(objectId); break
            case "objDrop": plateDocument.dropSelection(); break
            case "objOrient": plateDocument.autoOrient(objectId); break
            case "objLayFace": root.runTool("face"); break
            case "objModifier": plateDocument.addVolume(objectId, "box", "modifier", 20, 20, 10); break
            case "objNegative": plateDocument.addVolume(objectId, "box", "negative", 20, 20, 10); break
            case "objBlocker": plateDocument.addVolume(objectId, "box", "blocker", 20, 20, 10); break
            case "objEnforcer": plateDocument.addVolume(objectId, "box", "enforcer", 20, 20, 10); break
            case "objStatistics": statisticsDialog.openFor(objectId); break
            case "objHollow": hollowDialog.openFor(objectId); break
            case "objMerge": plateDocument.mergeSelection(); break
            case "objSplit": {
                const added = plateDocument.splitToParts(objectId)
                root.setStatus(added > 0 ? "split into " + (added + 1) + " parts"
                                         : "this object is already a single part")
                break
            }
            }
        }
        onDismissed: root.objectMenuVisible = false
    }

    // The layer slider's right-click: markers live at a Z height on the plate, so
    // this menu hangs where the click happened rather than under a button.
    MenuPanel {
        id: layerMenu
        parent: Overlay.overlay
        anchors.fill: parent
        visible: root.layerMenuVisible
        z: 110
        menuWidth: 320
        entries: {
            const list = root.layerMenuEntries.slice()
            const z = toolpath.layerZ
            let hasMarker = false
            const markers = plateDocument.layerGcodeMarkers
            for (let i = 0; i < markers.length; ++i)
                if (Math.abs(Number(markers[i].z) - z) < 0.001)
                    hasMarker = true
            list[list.length - 1] = { id: "layerRemove", label: "Remove the marker here",
                                      hint: hasMarker ? Number(z).toFixed(2) + " mm" : "",
                                      disabled: !hasMarker }
            return list
        }
        anchorX: Math.min(root.layerMenuX, root.width - 340)
        anchorY: Math.min(root.layerMenuY, root.height - 240)
        onTriggered: (id) => root.runAction(id)
        onDismissed: root.layerMenuVisible = false
    }

    // The plate options menu, dropped from the plate selector.
    MenuPanel {
        id: plateMenu
        parent: Overlay.overlay
        anchors.fill: parent
        visible: root.plateMenuVisible
        z: 110
        menuWidth: 280
        entries: root.plateMenuEntries
        anchorX: Math.min(root.plateMenuX, root.width - 300)
        anchorY: Math.min(root.plateMenuY + 8, root.height - 200)
        onTriggered: (id) => root.runAction(id)
        onDismissed: root.plateMenuVisible = false
    }

    // The plate list: every plate in the selector, so fourteen plates cost one
    // control on the bar instead of fourteen.
    MenuPanel {
        id: plateList
        parent: Overlay.overlay
        anchors.fill: parent
        visible: root.plateListVisible
        z: 110
        menuWidth: 300
        entries: root.plateListEntries
        anchorX: Math.min(root.plateListX, root.width - 320)
        anchorY: Math.min(root.plateListY + 8, root.height - 320)
        onTriggered: (id) => {
            if (id === "plateOptions") {
                root.plateMenuIndex = root.activePlate
                root.plateMenuX = root.plateListX
                root.plateMenuY = root.plateListY
                root.plateMenuVisible = true
                return
            }
            root.runAction(id)
        }
        onDismissed: root.plateListVisible = false
    }

    TextPromptDialog {
        id: textPrompt
        onSubmitted: (text) => {
            plateDocument.setPlateName(root.plateMenuIndex, text)
            root.setStatus("renamed plate " + (root.plateMenuIndex + 1)
                           + " to " + plateDocument.plateName(root.plateMenuIndex))
        }
    }

    LayerGcodeDialog {
        id: layerGcodeDialog
        onSubmitted: (kind, text) => {
            const z = layerGcodeDialog.layerZ
            if (!plateDocument.addLayerGcode(z, kind, text)) {
                root.setStatus("could not add a marker at " + z.toFixed(2) + " mm")
                return
            }
            root.setStatus("marker at " + z.toFixed(2)
                           + " mm \u2014 slice again to write it into the G-code")
        }
    }

    // The File menu. It is a plain overlay rather than a Popup: a Popup opened
    // from a header button is closed again by the same press on some platforms,
    // and this has to work the first time, every time.
    Item {
        id: fileMenu
        parent: Overlay.overlay
        anchors.fill: parent
        visible: root.fileMenuVisible
        z: 100

        property var entries: [
            { id: "newPlate", label: "New plate", keys: "Ctrl+N" },
            { id: "open", label: "Add model…", keys: "Ctrl+O" },
            { id: "openProject", label: "Open project…", keys: "Ctrl+Shift+O" },
            { id: "saveProject", label: "Save project", keys: "Ctrl+S" },
            { id: "saveProjectAs", label: "Save project as…", keys: "Ctrl+Shift+S" },
            { id: "sep1", label: "" },
            { id: "export", label: "Export G-code…", keys: "Ctrl+E" },
            { id: "sendToPrinter", label: "Send to printer…", keys: "" },
            { id: "exportConfig", label: "Export configuration…", keys: "Ctrl+Shift+E" },
            { id: "importConfig", label: "Load configuration…", keys: "Ctrl+Shift+I" },
            { id: "sepCalibration", label: "" },
            { id: "calibration", label: "Calibration…", keys: "" },
            { id: "sep2", label: "" },
            { id: "preferences", label: "Preferences…", keys: "Ctrl+," },
            { id: "shortcuts", label: "Keyboard shortcuts", keys: "F1" },
            { id: "about", label: "About next-layer", keys: "" },
            { id: "sep3", label: "" },
            { id: "quit", label: "Quit", keys: "Ctrl+Q" }
        ]

        function openAt() {
            root.fileMenuVisible = true
        }

        MouseArea {
            anchors.fill: parent
            onClicked: root.fileMenuVisible = false
        }

        Rectangle {
            id: menuPanel

            // Under the button that opened it, and never off the window: the menu
            // used to sit at the far left under the logo whatever opened it.
            x: Math.max(Metrics.xs, Math.min(root.width - width - Metrics.xs, root.fileMenuAt.x))
            y: root.fileMenuAt.y
            width: 340
            height: menuColumn.implicitHeight + Metrics.sm * 2
            radius: Metrics.radiusMedium
            color: Colors.surfaceContainerLow
            border.width: Metrics.borderWidth
            border.color: Colors.outlineVariant

            Column {
                id: menuColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.topMargin: Metrics.sm
                spacing: 0

                Repeater {
                    model: fileMenu.entries

                    delegate: Item {
                        id: menuRow
                        required property var modelData

                        readonly property bool separator: menuRow.modelData.id.indexOf("sep") === 0

                        width: menuPanel.width
                        height: separator ? Metrics.md : 34

                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: 2
                            radius: Metrics.radiusSmall
                            color: rowMouse.containsMouse && !menuRow.separator
                                   ? Colors.alpha(Colors.accent, 0.16) : "transparent"
                        }

                        Rectangle {
                            visible: menuRow.separator
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: Metrics.md
                            anchors.rightMargin: Metrics.md
                            height: Metrics.borderWidth
                            color: Colors.outlineVariant
                        }

                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: Metrics.md
                            anchors.rightMargin: Metrics.md
                            visible: !menuRow.separator
                            spacing: Metrics.sm

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width - keysText.width - Metrics.sm
                                text: menuRow.modelData.label
                                font.family: Type.family
                                font.pixelSize: Type.sizeSmall
                                color: Colors.textPrimary
                                elide: Text.ElideRight
                            }

                            Text {
                                id: keysText
                                anchors.verticalCenter: parent.verticalCenter
                                text: menuRow.modelData.keys === undefined ? "" : menuRow.modelData.keys
                                font.family: Type.family
                                font.pixelSize: Type.sizeCaption
                                color: Colors.textMuted
                            }
                        }

                        MouseArea {
                            id: rowMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            enabled: !menuRow.separator
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                root.fileMenuVisible = false
                                root.runAction(menuRow.modelData.id)
                            }
                        }
                    }
                }
            }
        }
    }

    FileDialog {
        id: openProjectDialog
        title: "Open project"
        fileMode: FileDialog.OpenFile
        nameFilters: ["next-layer project (*.nextproj)", "All files (*)"]
        onAccepted: root.openProject(selectedFile)
    }

    FileDialog {
        id: saveProjectDialog
        title: "Save project"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "nextproj"
        nameFilters: ["next-layer project (*.nextproj)"]
        onAccepted: root.saveProject(selectedFile)
    }

    // The question the close guard asks. It is deliberately blunt: save, discard,
    // or stay. Built like every other dialog in the app -- header row with a close
    // X, a body, and its own footer row for the buttons -- so the three answers
    // never share a line with the sentence that asks the question.
    Popup {
        id: closeGuard

        width: Math.min(Math.max(560, closeContent.implicitWidth + Metrics.xl * 2),
                        parent ? parent.width - Metrics.xl * 2 : 560)
        height: closeContent.implicitHeight + Metrics.xl * 2
        x: parent ? Math.round((parent.width - width) / 2) : 0
        y: parent ? Math.round((parent.height - height) / 2) : 0
        visible: root.closeGuardOpen
        modal: true
        dim: true
        padding: 0
        closePolicy: Popup.NoAutoClose

        onVisibleChanged: if (!visible) root.closeGuardOpen = false

        background: Rectangle {
            radius: Metrics.radiusMedium
            color: Colors.surfaceContainerLow
            border.width: Metrics.borderWidth
            border.color: Colors.outlineVariant
        }

        contentItem: Item {
            implicitWidth: closeContent.implicitWidth
            implicitHeight: closeContent.implicitHeight

            ColumnLayout {
                id: closeContent
                anchors.fill: parent
                spacing: Metrics.md

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: Metrics.xl
                    Layout.rightMargin: Metrics.xl
                    Layout.topMargin: Metrics.xl
                    spacing: Metrics.md

                    Text {
                        Layout.fillWidth: true
                        text: "Save this plate before closing?"
                        font.family: Type.family
                        font.pixelSize: Type.sizeHeading
                        font.bold: true
                        color: Colors.textPrimary
                    }

                    ToolButton {
                        icon: "close"
                        label: "Cancel"
                        hint: "Stay here (Esc)"
                        onClicked: root.closeGuardOpen = false
                    }
                }

                Divider {
                    Layout.fillWidth: true
                    Layout.leftMargin: Metrics.xl
                    Layout.rightMargin: Metrics.xl
                }

                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: Metrics.xl
                    Layout.rightMargin: Metrics.xl
                    text: plateDocument.count + (plateDocument.count === 1 ? " object on the plate" : " objects on the plate")
                          + (plateDocument.projectPath.length > 0
                             ? ", last saved to " + plateDocument.projectPath
                             : "") + "."
                    wrapMode: Text.WordWrap
                    font.family: Type.family
                    font.pixelSize: Type.sizeBody
                    color: Colors.textSecondary
                }

                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: Metrics.xl
                    Layout.rightMargin: Metrics.xl
                    text: "Nothing is saved until you say so. Closing without saving loses this plate."
                    wrapMode: Text.WordWrap
                    font.family: Type.family
                    font.pixelSize: Type.sizeCaption
                    color: Colors.textTertiary
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: Metrics.xl
                    Layout.rightMargin: Metrics.xl
                    Layout.bottomMargin: Metrics.xl
                    spacing: Metrics.sm

                    Button {
                        text: "Save and close"
                        hint: "S"
                        primary: true
                        onClicked: root.saveThenClose()
                    }

                    Button {
                        text: "Discard"
                        hint: "D"
                        onClicked: root.closeWithoutSaving()
                    }

                    Item { Layout.fillWidth: true }

                    Button {
                        text: "Cancel"
                        hint: "Esc"
                        onClicked: root.closeGuardOpen = false
                    }
                }
            }

            ResizeGrip {
                dialog: closeGuard
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.rightMargin: Metrics.xs
                anchors.bottomMargin: Metrics.xs
            }
        }
    }

    AboutDialog {
        id: aboutDialog
        version: root.appVersion
        engineVersion: root.engineVersion
        dataDir: root.dataDirectory
        presetDir: root.presetDirectory
    }

    PreferencesDialog {
        id: preferencesDialog
        onPreferencesChanged: root.applyPreferences()
    }

    ShortcutMapDialog {
        id: shortcutMap
        shortcuts: root.shortcutList
    }

    PresetDiffDialog {
        id: presetDiffDialog
    }

    StatisticsDialog {
        id: statisticsDialog
    }

    TextObjectDialog {
        id: textDialog
        fonts: plateDocument.fonts()
        onTextRequested: (text, height, depth, fontPath) => {
            const id = plateDocument.addText(text, height, depth, fontPath)
            root.setStatus(id > 0 ? "added text \"" + text + "\""
                                  : plateDocument.error.length > 0 ? plateDocument.error
                                                                   : "could not add the text")
        }
    }

    HollowDialog {
        id: hollowDialog
        onHollowRequested: (objectId, thickness) => {
            if (plateDocument.hollowObject(objectId, thickness))
                root.setStatus("hollowed with a " + thickness.toFixed(1) + " mm wall")
            else
                root.setStatus(plateDocument.error.length > 0 ? plateDocument.error
                                                              : "hollowing failed")
        }
    }

    FileDialog {
        id: exportConfigDialog
        title: "Export configuration"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: ["Configuration (*.json)", "All files (*)"]
        onAccepted: {
            const error = presetStore.exportConfiguration(selectedFile)
            root.setStatus(error.length > 0 ? error : "exported the configuration")
        }
    }

    FileDialog {
        id: importConfigDialog
        title: "Load configuration"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Configuration (*.json)", "All files (*)"]
        onAccepted: {
            const error = presetStore.importConfiguration(selectedFile)
            root.setStatus(error.length > 0 ? error : "loaded the configuration")
        }
    }

    // Preferences that reach into the scene are applied here, once, rather than
    // read from the dialog by every component.
    function applyPreferences() {
        viewport.fieldOfViewSetting = preferencesDialog.fieldOfView
        viewport.showGrid = preferencesDialog.showGrid
        viewport.showBedPlate = preferencesDialog.showBed
        statusTimer.interval = preferencesDialog.statusSeconds * 1000
    }

    Connections {
        target: settingsModel
        ignoreUnknownSignals: true
        function onChanged() {
            if (!preferencesDialog.autoslice || !toolpath.loaded || footer.slicing)
                return
            autosliceDelay.restart()
        }
    }

    Timer {
        id: autosliceDelay
        interval: 900
        onTriggered: {
            if (preferencesDialog.autoslice && toolpath.loaded && !footer.slicing)
                footer.startSlice()
        }
    }

    FileDialog {
        id: openDialog
        title: "Add model"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["Models (" + plateDocument.importWildcards() + ")", "All files (*)"]
        // The dialog opens where models were last found. A home directory can hold
        // both `Downloads` and `downloads` -- only one of them is where the files
        // actually are -- so the folder that was last used wins over the default.
        currentFolder: root.lastModelFolder.length > 0
                        ? root.lastModelFolder : "file://" + Host.downloadFolder
        onAccepted: {
            const paths = []
            for (let i = 0; i < selectedFiles.length; ++i)
                paths.push(selectedFiles[i].toString())
            if (selectedFiles.length > 0) {
                const folder = selectedFiles[0].toString()
                root.lastModelFolder = folder.substring(0, folder.lastIndexOf("/"))
            }
            root.addModels(paths)
        }
    }

    FileDialog {
        id: exportDialog
        title: "Export G-code"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "gcode"
        nameFilters: ["G-code (*.gcode)", "All files (*)"]
        onAccepted: {
            if (toolpath.loaded && Host.copyFile(sliceController.gcodePath, selectedFile))
                root.setStatus("exported to " + selectedFile)
            else
                root.setStatus("export failed")
        }
    }

    Component.onCompleted: {
        shell.forceActiveFocus()
        preferencesDialog.load()
        root.applyPreferences()
        if (typeof startupPanelLeft !== "undefined" && startupPanelLeft)
            root.panelOnLeft = true
        Colors.loadFromHost()
        if (session.hasModel)
            if (typeof startupPlates !== "undefined")
            for (let i = 1; i < startupPlates; ++i)
                root.addPlate()
        if (startupGcodePath.length > 0 && toolpath.load(startupGcodePath)) {
            const last = Math.max(0, toolpath.layerCount - 1)
            const layer = startupLayer >= 0 ? Math.min(startupLayer, last) : last
            toolpath.setLayer(layer)
            if (startupLayerFrom > 0)
                toolpath.setFirstLayer(Math.min(startupLayerFrom, layer))
            if (startupMovesFrom > 0)
                toolpath.setFirstMove(startupMovesFrom - 1)
            if (startupMovesTo > 0)
                toolpath.setLastMove(startupMovesTo - 1)
            if (startupSingleLayer)
                toolpath.setSingleLayer(true)
            for (let i = 0; i < startupHiddenFeatures.length; ++i)
                toolpath.setFeatureVisible(startupHiddenFeatures[i], false)
            if (typeof startupViewType !== "undefined" && startupViewType.length > 0)
                toolpath.setViewType(startupViewType)
            for (let i = 0; i < startupHiddenOptions.length; ++i)
                toolpath.setOptionVisible(startupHiddenOptions[i], false)
        }
        if (typeof startupCameraView !== "undefined" && startupCameraView.length > 0)
            viewport.setView(startupCameraView)
        if (typeof startupOrthographic !== "undefined")
            viewport.orthographic = startupOrthographic
        root.title = "next-layer · " + session.displayName()
        if (setupFlow.firstRun()) {
            if (!welcome.seen)
                welcome.open()
            else
                setupFlow.open()
        }
        else
            setupFlow.remember()
    }
}