// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

import QtQuick
import QtQuick3D
import QtQuick.Controls
import QtQuick.Layouts
import NextLayer 1.0
import "../theme"
import "../components"

Item {
    id: root

    /// A right-click on an object in the viewport, in window coordinates: the
    /// menu that answers it is a window overlay, not a child of the scene.
    signal contextRequested(int objectId, real x, real y)
    signal placedOnFace(int objectId, int facet)
    signal painted(int objectId, int facets)
    /// The brush bar edits the same settings the object panel does; the owner owns
    /// both, so it is told rather than the viewport reaching for the popup.
    signal paintSettingsChanged(string kind, string mode, real radius)
    /// The bar's Done: the stroke is over and the panel comes back.
    signal paintFinished()
    /// The bar's More: open the full control surface without ending the stroke.
    signal paintMoreRequested()
    /// A click on a plate's number badge. The owner does the plate switch: it also
    /// reframes the camera and swaps the loaded G-code, which the viewport cannot
    /// do on its own.
    signal plateSelected(int index)
    /// How many objects a rubber-band drag picked up, so the window can say so.
    signal bandSelected(int count)
    /// The plate's own actions: remove, and the menu that holds rename, duplicate,
    /// lock and clear.
    signal plateRemoveRequested(int index)
    signal plateRenameRequested(int index)
    signal plateMenuRequested(int index, real x, real y)
    /// The icon column standing on the bed: add a plate, lock this one, and the
    /// two arrangement tools, which the window performs.
    signal plateAddRequested()
    signal plateLockToggled(int index)
    signal actionRequested(string id)

    property string mode: "model"

    property real yaw: 28
    property real pitch: -18
    property real zoom: 1.0
    property real fieldOfViewSetting: 60
    property bool showGrid: true
    property bool showBedPlate: true
    property bool orthographic: false
    property string viewName: "iso"

    property bool measureActive: false
    property var measurePoints: []

    property bool moveEnabled: false

    /// A selected object can be dragged across the bed without first arming the
    /// move tool. Selecting it is the intent; reaching for a tool afterwards to
    /// do the obvious next thing is not. The move tool still exists, for the axis
    /// handles and for dragging an unselected object.
    readonly property bool dragSelectedEnabled: root.mode === "model"
        && !root.measureActive && !root.paintActive && !root.layOnFaceActive
    property bool layOnFaceActive: false

    onLayOnFaceActiveChanged: {
        if (!root.layOnFaceActive)
            plateScene.setHoverFacet(0, -1)
    }
    property bool paintActive: false
    property int paintObjectId: 0
    property string paintKind: "supports"
    property real paintRadius: 4.0
    /// "enforce", "block" or "erase": what a dab writes. Blockers are the
    /// reference's own mode -- a support blocker excludes supports, a seam
    /// blocker keeps the seam away -- so it is a mode of the brush, not a
    /// separate kind.
    property string paintMode: "enforce"
    property int paintTool: 2
    property bool selected: false
    /// What the pointer is over: the cursor is a hand only on a model, and an
    /// arrow everywhere else, so the viewport reads as a canvas rather than a
    /// control that swallows every click.
    property bool hoveringObject: false

    // Rows reserved at the viewport's bottom by chrome that spans it. Everything
    // anchored to the bottom adds this, so two bands cannot land on each other.
    property real bottomInset: 0
    property real rightInset: 0
    readonly property int cameraBarHeight: Metrics.rowHeight - Metrics.sm
    // How much of the viewport's bottom edge is covered by cards pinned there (the
    // camera cluster, the bed label). A plate render for the G-code thumbnail
    // crops this off instead of shipping the chrome inside the preview.
    readonly property int bottomChrome: cameraBarHeight + Metrics.md * 2
    property vector3d panOffset: Qt.vector3d(0, 0, 0)
    property vector3d dragOffset: Qt.vector3d(0, 0, 0)


    readonly property bool measureComplete: root.measurePoints.length === 2
    readonly property var measureVector: {
        if (root.measurePoints.length < 2)
            return Qt.vector3d(0, 0, 0)
        const a = root.measurePoints[0]
        const b = root.measurePoints[1]
        return Qt.vector3d(b[0] - a[0], b[1] - a[1], b[2] - a[2])
    }
    readonly property real measureDistance: root.measureVector.length()
    readonly property real measureAngle: {
        if (!root.measureComplete || root.measureDistance < 1e-6)
            return 0
        const v = root.measureVector
        return Math.acos(Math.max(-1, Math.min(1, v.z / root.measureDistance))) * 180 / Math.PI
    }

    function clearMeasure() {
        root.measurePoints = []
    }

    function addMeasurePoint(x, y) {
        const point = root.measureHit(x, y)
        if (point === null)
            return
        if (root.measurePoints.length >= 2)
            root.measurePoints = []
        root.measurePoints = root.measurePoints.concat([point])
    }

    // A point on the object under the cursor, or failing that the point on the bed
    // beneath it. The reference slicers measure across the plate as well as across a
    // part -- checking whether something fits is the usual reason to reach for this
    // -- so a click that misses every mesh lands on z=0 instead of doing nothing.
    // Off the plate is not a measurement: the ray hits the plane there too, but the
    // number it would give is about nothing.
    function measureHit(x, y) {
        const hit = root.hitAt(x, y)
        if (hit.w > 0)
            return [hit.x, hit.y, hit.z]
        const bed = root.bedPoint(x, y)
        if (bed === null)
            return null
        if (bed.x < root.bedMin.x || bed.x > root.bedMax.x
                || bed.y < root.bedMin.y || bed.y > root.bedMax.y)
            return null
        return [bed.x, bed.y, 0]
    }

    function segmentRotation(from, to) {
        const axis = Qt.vector3d(0, 1, 0)
        const direction = Qt.vector3d(to[0] - from[0], to[1] - from[1], to[2] - from[2])
        const length = direction.length()
        if (length < 1e-6)
            return Qt.quaternion(1, 0, 0, 0)
        const d = vNormal(direction)
        const cross = vCross(axis, d)
        const dot = Math.max(-1, Math.min(1, vDot(axis, d)))
        const angle = Math.acos(dot)
        const sin = Math.sin(angle)
        if (Math.abs(sin) < 1e-6)
            return dot > 0 ? Qt.quaternion(1, 0, 0, 0) : Qt.quaternion(0, 1, 0, 0)
        return Qt.quaternion(Math.cos(angle / 2),
                             cross.x / sin * Math.sin(angle / 2),
                             cross.y / sin * Math.sin(angle / 2),
                             cross.z / sin * Math.sin(angle / 2))
    }

    // How close the camera may come: `fittedDistance` times this is the distance
    // to the pivot, and at 0.03 a 350 mm bed frames about 22 mm of the world -- a
    // nozzle's worth of detail on a benchy. It used to stop at 0.15, which framed
    // 100 mm: a whole part filled the window and no feature inside it could.
    readonly property real minZoom: 0.03
    // The camera fits the model, not the plate, so a small model needs a wide
    // zoom-out range before the whole bed is in frame. The camera pulls back
    // well past the plate, and so does this.
    readonly property real maxZoom: Math.max(6.0, root.bedFitZoom * 2.2)
    readonly property real fillFraction: 0.7
    readonly property real fieldOfView: root.fieldOfViewSetting

    // Orbit around the plate, not around the model. With the pivot on the
    // model, dragging it re-centres the camera and the object appears to hold
    // still while the bed slides underneath; the plate stays fixed here and the
    // object travels across it. Height still comes from the
    // model, so it sits centred instead of hanging below the frame.
    // The plate offset is in printer coordinates; the camera orbits in the
    // scene's Y-up frame, so the Y half of the offset becomes -Z.
    readonly property vector3d plateShiftScene:
        Qt.vector3d(root.plateShift.x, 0, -root.plateShift.y)

    readonly property vector3d focusPoint: {
        const plate = Host.bedCentreWorld
        // The pivot is the bed, never the model: a camera that recentres itself on
        // the objects moves the whole view every time an object is added, moved or
        // deleted, which reads as the viewport doing something on its own. Both
        // lists can be empty before a printer is known, and one undefined
        // component put NaN in the camera -- the whole scene rendered as stripes.
        const px = (plate && plate.length >= 3) ? plate[0] : 0
        const pz = (plate && plate.length >= 3) ? plate[2] : 0
        return Qt.vector3d(px + root.plateShiftScene.x, 0,
                           pz + root.plateShiftScene.z)
    }

    // Millimetres covered by one screen pixel at the current camera distance,
    // which is what lets the bed grid keep a one-pixel hairline at any zoom. The
    // orthographic camera is magnified to match the perspective one, so the same
    // number is right for both.
    readonly property real mmPerPixel: {
        const distance = root.fittedDistance * root.zoom
        const span = 2 * distance * Math.tan(root.fieldOfView * Math.PI / 360)
        const value = span / Math.max(1, view.height)
        return isFinite(value) && value > 0 ? value : 1
    }

    readonly property vector3d bedMin: Host.bedMin
    readonly property vector3d bedMax: Host.bedMax

    // Bed outline in printer coordinates, for the sheet and plate beneath the
    // model. The plate fits the outline's bounding box, like the engine's own
    // bed artwork, which is drawn to the same rectangle.
    readonly property vector3d bedCentre: Qt.vector3d((root.bedMin.x + root.bedMax.x) / 2,
                                                      (root.bedMin.y + root.bedMax.y) / 2, 0)
    readonly property vector3d bedSize: Qt.vector3d(root.bedMax.x - root.bedMin.x,
                                                     root.bedMax.y - root.bedMin.y, 0)

    // The bed itself is drawn from underneath as well, so a view from below shows
    // the plate rather than a hole. Only the artwork rides the top face, and it
    // leaves with the angle that would show its back.
    readonly property bool lookingFromBelow: root.pitch > 0

    // The plate mesh's own underside when the printer ships one, else the slab.
    readonly property real bedUndersideZ: Host.bedPlateLoaded
        ? bedPlateGeometry.boundsMin.z - 0.03
        : -4.03

    // Distance fits the bed, not the model: the camera must not resize itself
    // when an object is added, moved or deleted. Frame the content by pressing F
    // or by loading a model, which assigns `zoom` once.
    readonly property real boundingRadius: Host.bedRadius > 0 ? Host.bedRadius : 200

    readonly property real fittedDistance: {
        const vertical = root.fieldOfView * Math.PI / 180
        const aspect = view.height > 0 ? view.width / view.height : 1.6
        const horizontal = 2 * Math.atan(Math.tan(vertical / 2) * aspect)
        const span = Math.min(vertical, horizontal)
        return root.boundingRadius / (root.fillFraction * Math.tan(span / 2))
    }

    // Qt Quick 3D's OrthographicCamera magnification is pixels per world unit,
    // not a size: measured on Qt 6.12 by projecting a 10 mm cube through one
    // (mag 80 put it at 800 px in a 1080 px frame). So the value that frames the
    // same slab of the world as the perspective camera is the viewport's own
    // height over the height that camera sees at the focus plane. Both axes take
    // the same number, or the image is stretched by the aspect ratio.
    readonly property real orthographicMagnification: {
        const pixels = Math.max(1, view.height)
        const visible = 2 * root.cameraDistance * Math.tan(root.fieldOfView * Math.PI / 360)
        const value = visible > 0 ? pixels / visible : 1
        return value > 0 && isFinite(value) ? value : 1
    }

    function resetView() {
        root.setView("iso")
        // Framed on the plate, not on the model, which is what an import should
        // show: the plate's own chrome -- its name and icon column -- lives at the
        // plate's edges, and a model-framed camera puts all of it off-screen.
        root.zoom = Math.max(root.minZoom, Math.min(root.maxZoom, root.bedFitZoom))
        root.panOffset = Qt.vector3d(0, 0, 0)
    }

    // The camera toolbar: the six orthogonal directions plus the isometric
    // default. Every preset looks at the same point the orbit does, so switching
    // views never recentres the plate.
    // Pitch is the orbit node's X rotation and the camera sits on +Z of it, so a
    // negative pitch lifts the camera above the plate: -89 is top-down, +89 is
    // underneath. These two were swapped, which is why "Top view" rendered a
    // black frame -- the camera was below an opaque bed looking up.
    readonly property var viewPresets: ({
        // The default orientation: the camera sits 45 degrees above the bed and
        // 45 around it. The shallower 18 degrees this used to be flattened the
        // this used to be flattened the plate's own chrome -- the name and the icon
        // column lie in the bed plane, so a low camera turns them into a smear.
        "iso": [45, -45],
        "top": [0, -89],
        "bottom": [0, 89],
        "front": [0, 0],
        "rear": [180, 0],
        "left": [-90, 0],
        "right": [90, 0]
    })

    function setView(name) {
        const preset = root.viewPresets[name]
        if (preset === undefined)
            return
        root.viewName = name
        root.yaw = preset[0]
        root.pitch = preset[1]
    }

    // Near and far clipping follow the camera rather than sitting at fixed
    // millimetres. With a fixed near plane of 1 mm the camera clipped its way into
    // the model as soon as it orbited close, which read as the sliced model being
    // cut off; the far plane scales off the same distance so a zoomed-out bed stays
    // whole without a huge depth range.
    readonly property real cameraDistance: {
        const value = root.fittedDistance * root.zoom
        return isFinite(value) && value > 0 ? value : 1000
    }
    readonly property real clipNear: Math.max(0.02, root.cameraDistance * 0.004)
    readonly property real clipFar: root.cameraDistance * 8 + root.bedRadius * 8 + 500

    // Distance that frames the whole build plate, which is the framing the app
    // opens in and what the "fit plate" button restores.
    //
    // The plate's chrome: the icon column off the right edge, the name and its pencil
    // above the back edge. It lives in the plate's own plane, so it is not a
    // screen-space overlay that can be clamped; the fit has to carry it instead.
    // Framed on the bed alone, both land outside the viewport and the plate's own
    // actions become unreachable. The name is measured the same way the decal
    // measures it, from its length, because the fit is computed above the per-plate
    // delegate; a long name starts at the plate's left corner and overhangs the right.
    readonly property real plateApronRight: Math.max(plateChrome.iconSize + plateChrome.gapLeft * 2,
                                                     root.longestPlateName + plateChrome.gapLeft
                                                         - root.bedSize.x / 2)
    readonly property real plateApronTop: plateChrome.gapTop + plateChrome.nameHeight
                                            + plateChrome.gapLeft
    readonly property real longestPlateName: {
        let longest = 0
        for (let i = 0; i < plateDocument.plates.length; ++i) {
            const label = plateDocument.plates[i].label || ""
            longest = Math.max(longest, label.length)
        }
        const aspect = Math.max(1.2, Math.min(6, longest * 0.55))
        return plateChrome.nameHeight * aspect * 1.3 + plateChrome.gapLeft + plateChrome.pencilSize
    }
    readonly property real bedRadius: {
        const halfWidth = root.bedSize.x / 2 + root.plateApronRight
        const halfDepth = root.bedSize.y / 2 + root.plateApronTop
        return Math.max(1, Math.sqrt(halfWidth * halfWidth + halfDepth * halfDepth))
    }

    readonly property real bedFitZoom: {
        const vertical = root.fieldOfView * Math.PI / 180
        const aspect = view.height > 0 ? view.width / view.height : 1.6
        const horizontal = 2 * Math.atan(Math.tan(vertical / 2) * aspect)
        const span = Math.min(vertical, horizontal)
        return root.fitRadius / (0.92 * Math.tan(span / 2)) / root.fittedDistance
    }

    // With no printer chosen there is no bed to fit, and a one-millimetre radius
    // would zoom the camera into the axes: the model is what the view has, so it
    // is what the view frames.
    readonly property real fitRadius: root.bedSize.x > 1 && root.bedSize.y > 1
                                      ? root.bedRadius
                                      : root.boundingRadius


    // One brush dab: pick the surface under the cursor, then paint a sphere of
    // facets around the hit point in the object's own coordinates. Painting goes
    // through the document, so it is undoable and the slicer reads the same data.
    function paintStroke(x, y) {
        if (root.paintObjectId <= 0)
            return
        const hit = root.hitAt(x, y)
        if (hit.w <= 0 || plateScene.pickedId !== root.paintObjectId)
            return
        // The brush is a circle on screen, so the painter needs the eye. The
        // camera lives in the scene's Y-up frame; every point handed to the
        // document is in printer coordinates, Z-up.
        const eye = perspectiveCamera.scenePosition
        const painted = plateDocument.paintObject(root.paintObjectId, root.paintKind,
                                                  Qt.vector3d(hit.x, hit.y, hit.z),
                                                  Qt.vector3d(eye.x, -eye.z, eye.y),
                                                  root.paintMode, root.paintTool,
                                                  plateScene.pickedFacet)
        if (painted > 0)
            root.painted(root.paintObjectId, painted)
        root.updateBrushRing(x, y)
    }

    // The brush footprint follows the pointer even before the button goes down:
    // aiming at a facet is a separate act from committing paint to it.
    function updateBrushRing(x, y) {
        if (!root.paintActive || root.paintObjectId <= 0) {
            plateScene.clearBrushRing()
            return
        }
        const hit = root.hitAt(x, y)
        if (hit.w <= 0 || plateScene.pickedId !== root.paintObjectId) {
            plateScene.clearBrushRing()
            return
        }
        plateScene.setBrushRing(Qt.vector3d(hit.x, hit.y, hit.z),
                                plateScene.pickedNormal, root.paintRadius)
    }

    function fitPlate() {
        root.panOffset = Qt.vector3d(0, 0, 0)
        root.zoom = Math.max(root.minZoom, Math.min(root.maxZoom, root.bedFitZoom))
    }

    // Frame what is on the plate, once. Not a binding: a camera that recomputes
    // itself from the model resizes the viewport on every move and delete.
    function frameContent() {
        const content = Host.focusWorld
        if (content && content.length >= 3) {
            const focus = root.focusPoint
            root.panOffset = Qt.vector3d(content[0] - focus.x, content[1] - focus.y,
                                         content[2] - focus.z)
        } else {
            root.panOffset = Qt.vector3d(0, 0, 0)
        }
        const radius = Host.modelRadius
        const scale = radius > 0 ? radius / root.boundingRadius : root.bedFitZoom
        root.zoom = Math.max(root.minZoom, Math.min(root.maxZoom, scale))
    }

    /// The plate coordinates of a point on screen, projected the way the objects
    /// are: printer Z-up into the scene's Y-up frame. Used by the rubber band, which
    /// has to know where an object's corners land on screen.
    function screenPointOf(printerPoint) {
        const projected = view.mapFrom3DScene(Qt.vector3d(printerPoint.x, printerPoint.z,
                                                        -printerPoint.y))
        return projected
    }

    // Shift and drag selects everything inside the rectangle, the way every other
    // slicer does it. An object counts when its bounding box overlaps the band, so
    // a drag across a row of parts picks them up without having to enclose each one
    // completely.
    function selectInBand() {
        const left = Math.min(band.startX, band.endX)
        const right = Math.max(band.startX, band.endX)
        const top = Math.min(band.startY, band.endY)
        const bottom = Math.max(band.startY, band.endY)

        const chosen = []
        const objects = plateDocument.objects
        for (let i = 0; i < objects.length; ++i) {
            const entry = objects[i]
            if (entry.min === undefined || entry.max === undefined)
                continue
            let lowX = 1e9, lowY = 1e9, highX = -1e9, highY = -1e9
            for (let corner = 0; corner < 8; ++corner) {
                const px = (corner & 1) ? entry.max[0] : entry.min[0]
                const py = (corner & 2) ? entry.max[1] : entry.min[1]
                const pz = (corner & 4) ? entry.max[2] : entry.min[2]
                const projected = root.screenPointOf(Qt.vector3d(px, py, pz))
                lowX = Math.min(lowX, projected.x)
                highX = Math.max(highX, projected.x)
                lowY = Math.min(lowY, projected.y)
                highY = Math.max(highY, projected.y)
            }
            if (highX >= left && lowX <= right && highY >= top && lowY <= bottom)
                chosen.push(entry.id)
        }
        plateDocument.selectObjects(chosen)
        return chosen.length
    }

    function vAdd(a, b) { return Qt.vector3d(a.x + b.x, a.y + b.y, a.z + b.z) }
    function vSub(a, b) { return Qt.vector3d(a.x - b.x, a.y - b.y, a.z - b.z) }
    function vScale(a, s) { return Qt.vector3d(a.x * s, a.y * s, a.z * s) }
    function vDot(a, b) { return a.x * b.x + a.y * b.y + a.z * b.z }
    function vCross(a, b) {
        return Qt.vector3d(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x)
    }
    function vLength(a) { return Math.sqrt(vDot(a, a)) }
    function vNormal(a) {
        const length = vLength(a)
        return length < 1e-9 ? Qt.vector3d(0, 0, 0) : vScale(a, 1 / length)
    }

    function rotateByQuaternion(quaternion, vector) {
        const axis = Qt.vector3d(quaternion.x, quaternion.y, quaternion.z)
        const cross = vCross(axis, vector)
        const doubleCross = vCross(axis, cross)
        const scaled = vAdd(vScale(cross, quaternion.scalar), doubleCross)
        return vAdd(vector, vScale(scaled, 2))
    }

    readonly property int activePlateIndex: plateDocument.activePlate
    readonly property vector3d plateShift: plateDocument.plateOffset(activePlateIndex)

    function sceneToPrinter(point) {
        return Qt.vector3d(point.x, -point.z, point.y)
    }

    // A DirectionalLight shines along its local -Z, so a light aimed from
    // `toward` the target has its +Z axis on that direction.
    function lightRotation(toward) {
        const axis = Qt.vector3d(0, 0, 1)
        const target = vNormal(toward)
        const dot = Math.max(-1, Math.min(1, vDot(axis, target)))
        if (dot > 1 - 1e-9)
            return Qt.quaternion(1, 0, 0, 0)
        if (dot < -1 + 1e-9)
            return Qt.quaternion(0, 1, 0, 0)
        const cross = vCross(axis, target)
        const angle = Math.acos(dot)
        const sin = Math.sin(angle)
        return Qt.quaternion(Math.cos(angle / 2),
                             cross.x / sin * Math.sin(angle / 2),
                             cross.y / sin * Math.sin(angle / 2),
                             cross.z / sin * Math.sin(angle / 2))
    }

    function rayAt(x, y) {
        if (view.width <= 0 || view.height <= 0)
            return null
        const tanHalf = Math.tan(perspectiveCamera.fieldOfView * Math.PI / 360)
        const aspect = view.width / view.height
        const ndcX = 2 * x / view.width - 1
        const ndcY = 1 - 2 * y / view.height
        const cameraDir = vNormal(Qt.vector3d(ndcX * tanHalf * aspect, ndcY * tanHalf, -1))
        const direction = rotateByQuaternion(perspectiveCamera.sceneRotation, cameraDir)
        return [sceneToPrinter(perspectiveCamera.scenePosition), sceneToPrinter(direction)]
    }

    function hitAt(x, y) {
        const ray = rayAt(x, y)
        if (ray === null)
            return Qt.vector4d(0, 0, 0, -1)
        return plateScene.pick(ray[0], ray[1])
    }

    function pickAt(x, y) {
        return hitAt(x, y).w > 0 ? "modelNode" : "none"
    }

    function findModelPoint() {
        const seen = ({})
        let model = "none"
        for (let y = 160; y < height - 8; y += 32) {
            for (let x = 8; x < width - 8; x += 32) {
                const name = pickAt(x, y)
                if (name === "none")
                    continue
                seen[name === "" ? "unnamed" : name] = true
                if (name === "modelNode" && model === "none")
                    model = x + "," + y
            }
        }
        return model
    }

    function planePoint(x, y, planeZ) {
        const ray = rayAt(x, y)
        if (ray === null)
            return null
        const origin = ray[0]
        const direction = ray[1]
        if (Math.abs(direction.z) < 1e-6)
            return null
        const t = (planeZ - origin.z) / direction.z
        if (t <= 0)
            return null
        return Qt.vector3d(origin.x + direction.x * t, origin.y + direction.y * t, planeZ)
    }

    function bedPoint(x, y) {
        return planePoint(x, y, 0)
    }

    View3D {
        id: view
        anchors.fill: parent
        camera: root.orthographic ? orthoCamera : perspectiveCamera

        environment: SceneEnvironment {
            clearColor: Colors.viewportBackground
            backgroundMode: SceneEnvironment.Color
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
            // No screen-space occlusion, and no shadow map either: the reference
            // slicers shade with lights alone. SSAO was doing two things nobody
            // asked for -- smearing a soft dark blot across the bed and the empty
            // background behind it as the camera turned, and darkening every
            // recess inside a part until the toolpath read as if it were in its
            // own shadow. Shape is carried by the light rig below.
            aoEnabled: false
        }

        // The rig: one key light fixed in the world, so a face keeps its brightness
        // as the camera orbits, plus that light's ambient term. A camera-fixed key
        // would rotate with the view and make the shading lie about the geometry.
        //
        // There is no second light and no occlusion pass. Every addition here has
        // produced an artefact on the bed rather than detail on the part: a
        // camera-fixed fill draws a hard diagonal band across the plate wherever a
        // part stands on it (it survives `castsShadow: false`, and it does not appear
        // in a single headless frame), Qt's shadow map shadows a part with itself and
        // put the whole bed in shadow at a grazing angle, and SSAO smeared a dark
        // blot that followed the camera. Orca and PrusaSlicer shade with light alone.
        Node {
            id: orbit
            position: root.vAdd(root.focusPoint, root.panOffset)
            eulerRotation: Qt.vector3d(root.pitch, root.yaw, 0)

            // The camera sits on the orbit node and nothing else lives here now.

            PerspectiveCamera {
                id: perspectiveCamera
                position: Qt.vector3d(0, 0, root.cameraDistance)
                fieldOfView: root.fieldOfView
                clipNear: root.clipNear
                clipFar: root.clipFar
            }

            OrthographicCamera {
                id: orthoCamera
                position: Qt.vector3d(0, 0, root.cameraDistance)
                verticalMagnification: root.orthographicMagnification
                horizontalMagnification: root.orthographicMagnification
                clipNear: root.clipNear
                clipFar: root.clipFar
            }
        }

        // The key light: fixed in the world, 52 degrees up and a third of a turn
        // around, so every face of a part reads at a different brightness and the
        // form of an overhang is legible without a shadow to outline it. The
        // ambient carries most of the value on purpose: the reference slicers draw
        // their toolpaths in a narrow band (Orca's own capture measures p10/p90
        // luminance 0.64) and a key-dominant rig put ours at 0.56 with black
        // pockets wherever a face turned away.
        DirectionalLight {
            id: keyLight
            eulerRotation: Qt.vector3d(-52, -34, 0)
            color: "#fff4e6"
            castsShadow: false
            brightness: 0.38
            ambientColor: Qt.rgba(0.56, 0.57, 0.59, 1)
        }

        Node {
            id: printSpace
            eulerRotation.x: -90

            Model {
                id: plateNode
                objectName: "plateNode"
                geometry: plateScene.plateGeometry
                visible: root.mode !== "preview"

                materials: PrincipledMaterial {
                    baseColor: Colors.surfaceVariantText
                    roughness: 0.55
                    metalness: 0.0
                }
            }

            // Plates that are not active, dimmed: the plate being worked on is the
            // one that reads as active, and only it is sliced. Same geometry path
            // as the plate above, one draw call, a wash of the surface colour over
            // it so the beds still read as beds.
            Model {
                id: otherPlateNode
                objectName: "otherPlateNode"
                geometry: plateScene.otherPlateGeometry
                visible: plateScene.hasOtherPlates && root.mode !== "preview"

                materials: PrincipledMaterial {
                    baseColor: Colors.surfaceVariantText
                    roughness: 0.7
                    metalness: 0.0
                    opacity: 0.34
                    alphaMode: PrincipledMaterial.Blend
                }
            }

            // The face under the pointer while the lay-on-face tool is armed: one
            // tinted triangle, so the face you are about to lay down is visible
            // before the click rather than only afterwards.
            Model {
                objectName: "hoverNode"
                geometry: plateScene.hoverGeometry
                visible: plateScene.hasHover && root.mode !== "preview" && root.layOnFaceActive

                // A coloured geometry needs the matching material: the colour is
                // per triangle, so it is read from the vertex buffer, and no
                // lighting keeps a flat tint flat.
                materials: PrincipledMaterial {
                    lighting: PrincipledMaterial.NoLighting
                    vertexColorsEnabled: true
                    cullMode: Material.NoCulling
                }
            }

            // The selected object is its own mesh, drawn with the accent tint.
            // Same triangles, one draw call, no depth fight with the plate.
            Model {
                id: selectionNode
                objectName: "selectionNode"
                geometry: plateScene.selectionGeometry
                visible: plateScene.hasSelection && root.mode !== "preview"
                position: Qt.vector3d(root.dragOffset.x, root.dragOffset.y, 0)

                // With modifier volumes attached, the selected object turns into a
                // ghost: the solids are drawn through it, so a volume buried in the
                // middle of a part is still visible where it will cut.
                // A selected object keeps its own shading and takes a tint, rather
                // than being painted in a wash of accent: the plate mesh does not
                // carry the selected object, so a translucent overlay had nothing
                // to blend with and every selected part went flat and colour-cast.
                // A ghost is the one case that wants transparency, so volumes keep
                // the blend and the solids show through.
                materials: PrincipledMaterial {
                    baseColor: plateScene.hasVolumes
                               ? Colors.alpha(Colors.accent, 0.18)
                               : Colors.mix(Colors.surfaceVariantText, Colors.accent, 0.28)
                    alphaMode: plateScene.hasVolumes ? PrincipledMaterial.Blend
                                                     : PrincipledMaterial.Opaque
                    roughness: 0.55
                    metalness: 0.0
                }
            }

            // Painted facets, drawn over the object they belong to. The overlay is
            // its own mesh with one colour per triangle, scaled a hair about the
            // object's own origin so it wins the depth test against the surface it
            // sits on rather than z-fighting with it.
            Model {
                objectName: "paintNode"
                geometry: plateScene.paintGeometry
                visible: root.mode !== "preview"
                position: Qt.vector3d(root.dragOffset.x, root.dragOffset.y, 0)
                scale: Qt.vector3d(1.002, 1.002, 1.002)

                materials: PrincipledMaterial {
                    lighting: PrincipledMaterial.NoLighting
                    vertexColorsEnabled: true
                    cullMode: Material.NoCulling
                }
            }

            // The brush's footprint on the surface under the pointer: a ring in
            // the plane of the facet the paint will land on, so the size setting
            // has a visible meaning. The reference draws the same thing, and
            // without it a brush slider is a number and nothing else.
            Model {
                objectName: "brushRingNode"
                geometry: plateScene.brushRingGeometry
                visible: root.paintActive && root.mode !== "preview"

                materials: PrincipledMaterial {
                    lighting: PrincipledMaterial.NoLighting
                    baseColor: Colors.accent
                    opacity: 0.85
                    alphaMode: PrincipledMaterial.Blend
                    cullMode: Material.NoCulling
                    depthDrawMode: Material.AlwaysDepthDraw
                }
            }

            // Modifier, negative and support volumes of every object, drawn
            // translucent over the model so what they do to the slice is visible
            // before slicing: red subtracts, amber modifies, purple blocks support,
            // green forces it.
            Model {
                objectName: "volumeNode"
                geometry: plateScene.volumeGeometry
                visible: root.mode !== "preview"

                materials: PrincipledMaterial {
                    lighting: PrincipledMaterial.NoLighting
                    vertexColorsEnabled: true
                    alphaMode: PrincipledMaterial.Blend
                    cullMode: Material.NoCulling
                }
            }

            // The move handles: three arrows at the selection's centre, dragged to
            // move that object along one axis. Geometry in printer coordinates, so
            // they share the plate's own frame.
            Model {
                objectName: "gizmoNode"
                geometry: plateScene.gizmoGeometry
                visible: root.mode !== "preview" && root.moveEnabled && plateScene.hasGizmo

                materials: PrincipledMaterial {
                    lighting: PrincipledMaterial.NoLighting
                    vertexColorsEnabled: true
                    cullMode: Material.NoCulling
                    depthDrawMode: Material.NeverDepthDraw
                }
            }

            // Every other plate, drawn as a whole bed -- slab plus the same grid as
            // the active one -- at the position the document gives it. The
            // multi-plate view: a plate you can see is a plate you can click.
            Repeater3D {
                objectName: "otherPlateNode"

                model: plateDocument.plates

                delegate: Node {
                    id: otherPlate

                    required property var modelData

                    visible: root.mode !== "preview" && !otherPlate.modelData.active
                    position: Qt.vector3d(otherPlate.modelData.x, otherPlate.modelData.y, 0)

                    // A plate you are not on is context, not content: its fill
                    // drops to the background and its grid goes faint, the same
                    // relationship inactive plates get in a multi-bed project.
                    readonly property bool dimmed: !otherPlate.modelData.active

                    Model {
                        source: "#Cube"
                        position: Qt.vector3d(root.bedCentre.x, root.bedCentre.y, -0.18)
                        scale: Qt.vector3d(root.bedSize.x / 100, root.bedSize.y / 100, 0.3 / 100)

                        materials: PrincipledMaterial {
                            baseColor: otherPlate.modelData.locked
                                       ? Colors.alpha(Colors.error, 0.22)
                                       : (otherPlate.dimmed ? Qt.darker(Colors.viewportPlate, 1.45)
                                                            : Colors.viewportPlate)
                            roughness: 0.9
                            metalness: 0.0
                            specularAmount: 0.0
                            lighting: PrincipledMaterial.NoLighting
                        }
                    }

                    Model {
                        visible: root.showGrid
                        source: "#Rectangle"
                        position: Qt.vector3d(root.bedCentre.x, root.bedCentre.y, 0.02)
                        scale: Qt.vector3d(root.bedSize.x / 100, root.bedSize.y / 100, 1.0)

                        materials: CustomMaterial {
                            shadingMode: CustomMaterial.Shaded
                            cullMode: Material.NoCulling
                            sourceBlend: CustomMaterial.SrcAlpha
                            destinationBlend: CustomMaterial.OneMinusSrcAlpha
                            fragmentShader: "../shaders/bedgrid.frag"

                            property color lineColor: otherPlate.modelData.locked
                                                    ? Colors.alpha(Colors.error, 0.7)
                                                    : Colors.viewportGrid
                            property real minorAlpha: otherPlate.dimmed ? 0.16 : 0.55
                            property real majorAlpha: otherPlate.dimmed ? 0.3 : 1.0
                            property real pixelSize: root.mmPerPixel
                        }
                    }
                }
            }

            // The plate. A printer that ships a `bed_model` gets its real mesh,
            // which is what makes the bed read as an object from underneath:
            // sides, bevel and all. The top is parked 0.03 mm below the bed plane
            // (the engine's GROUND_Z) so a model resting on z = 0 cannot z-fight it.
            //
            // It goes away while a paint brush is armed, together with the artwork,
            // the origin axes and the plate's own chrome. The reference does the
            // same (`is_bed_visible` is false while a painting gizmo runs, so only
            // the plate list and its grid are left): a textured bed under a part
            // being painted is visual noise, and its chrome is furniture you cannot
            // press while the pointer paints. The grid stays, because it is what
            // still tells you which way is which.
            Model {
                visible: Host.bedPlateLoaded && root.showBedPlate && !root.paintActive
                geometry: bedPlateGeometry
                position: Qt.vector3d(root.plateShift.x, root.plateShift.y, -0.03)

                // Unlit, and that is the point: a shaded bed is not one colour.
                // This mesh is a solid with a bevelled rim, so the top face and the
                // rim catch different amounts of the key light, and that rim band --
                // following the plate's rounded corners -- reads as a second colour
                // behind a curved edge whenever the camera is low. The reference bed
                // is one flat tone from every angle (measured on its own capture: 52
                // to 56 on the top, 52 to 53 on the rim), so this is unlit too.
                materials: PrincipledMaterial {
                    baseColor: Colors.viewportPlate
                    lighting: PrincipledMaterial.NoLighting
                    cullMode: Material.NoCulling
                }
            }

            // No `bed_model` for this printer: a slab with a top face, same idea.
            // The top face has to end below the bed plane, or it covers the grid
            // that is drawn just above z = 0.
            Model {
                visible: !Host.bedPlateLoaded && root.showBedPlate
                source: "#Cube"
                position: Qt.vector3d(root.bedCentre.x + root.plateShift.x,
                                      root.bedCentre.y + root.plateShift.y, -2.03)
                scale: Qt.vector3d(root.bedSize.x / 100, root.bedSize.y / 100, 4.0 / 100)

                materials: PrincipledMaterial {
                    baseColor: Colors.viewportPlateSide
                    lighting: PrincipledMaterial.NoLighting
                }
            }

            Model {
                visible: !Host.bedPlateLoaded && root.showBedPlate && !root.paintActive
                source: "#Cube"
                position: Qt.vector3d(root.bedCentre.x + root.plateShift.x,
                                      root.bedCentre.y + root.plateShift.y, -0.18)
                scale: Qt.vector3d(root.bedSize.x / 100, root.bedSize.y / 100, 0.3 / 100)

                materials: PrincipledMaterial {
                    baseColor: Colors.viewportPlate
                    lighting: PrincipledMaterial.NoLighting
                }
            }

            // The bare plate a paint brush sees. The reference leaves the plate list
            // and its grid up while a painting gizmo runs and takes the textured bed
            // away; this is that plane, and the grid needs it: its distance field is
            // drawn over an opaque surface and the lines do not appear without one.
            Model {
                visible: root.paintActive && root.showBedPlate
                source: "#Rectangle"
                position: Qt.vector3d(root.bedCentre.x + root.plateShift.x,
                                      root.bedCentre.y + root.plateShift.y, -0.01)
                scale: Qt.vector3d(root.bedSize.x / 100, root.bedSize.y / 100, 1.0)

                materials: PrincipledMaterial {
                    baseColor: Colors.viewportPlate
                    lighting: PrincipledMaterial.NoLighting
                    cullMode: Material.NoCulling
                }
            }

            // The bed texture, the way the reference draws it: an unlit quad across
            // the printable area carrying the artwork, drawn over the plate mesh
            // rather than baked into it. `depthBias` lifts it off the plate's own top face -- the
            // two are 10 um apart and would z-fight at a grazing angle -- and it is
            // unlit because a shaded decal tints itself with the camera angle.
            //
            // The PNG is the Voron bundle's `voron_logo.svg`, rendered at 2048 px and
            // placed on a square canvas with a margin; the SVG draws its artwork flush
            // into a corner of its own viewBox, which maps to a plate edge and gets
            // clipped by the rim. Regenerate it with:
            //   rsvg-convert -w 2048 -h 2048 voron24_350_logo.svg -o /tmp/logo.png
            //   magick -size 2048x2048 xc:none \( /tmp/logo.png -trim +repage -resize 620x \)
            //          -gravity southeast -geometry +150+150 -composite voron24_350_bed.png
            Model {
                visible: Host.bedPlateLoaded && root.showBedPlate && !root.lookingFromBelow
                         && !root.paintActive
                source: "#Rectangle"
                depthBias: -64
                position: Qt.vector3d(root.bedCentre.x + root.plateShift.x,
                                      root.bedCentre.y + root.plateShift.y, -0.02)
                scale: Qt.vector3d(root.bedSize.x / 100, root.bedSize.y / 100, 1.0)

                materials: PrincipledMaterial {
                    lighting: PrincipledMaterial.NoLighting
                    baseColor: "#ffffff"
                    alphaMode: PrincipledMaterial.Blend
                    baseColorMap: Texture {
                        source: "qrc:/next-layer/bed/voron24_350_bed.png"
                    }
                }
            }

            // The bed grid: 10 mm hairlines, every fifth heavier, drawn over the
            // plate. The shader keeps each line one pixel wide however far the
            // camera zooms, where the old quad mesh thickened into bands. It goes
            // with the plate rather than staying after it: a grid hanging over the
            // background with no bed under it reads as lines escaping the plate.
            Model {
                visible: root.showGrid
                source: "#Rectangle"
                position: Qt.vector3d(root.bedCentre.x + root.plateShift.x,
                                      root.bedCentre.y + root.plateShift.y, 0.02)
                scale: Qt.vector3d(root.bedSize.x / 100, root.bedSize.y / 100, 1.0)

                materials: CustomMaterial {
                    shadingMode: CustomMaterial.Shaded
                    cullMode: Material.NoCulling
                    sourceBlend: CustomMaterial.SrcAlpha
                    destinationBlend: CustomMaterial.OneMinusSrcAlpha
                    fragmentShader: "../shaders/bedgrid.frag"

                    property color lineColor: Colors.viewportGrid
                    property real minorAlpha: 0.55
                    property real majorAlpha: 1.0
                    property real pixelSize: root.mmPerPixel
                }
            }

            // Origin axes: red X, green Y, blue Z standing at the print origin,
            // so the bed's front-left
            // corner says where zero is however the camera turns. Printer
            // coordinates and printer Z-up, like everything else in the scene.
            Node {
                id: originAxes
                visible: !root.paintActive
                position: Qt.vector3d(root.plateShift.x, root.plateShift.y, 0)

                readonly property real axisLength: 24
                readonly property real headLength: 5
                readonly property real shaftRadius: 0.45
                readonly property real headRadius: 1.5

                Repeater3D {
                    model: [
                        { colour: "#ff4d4d", rotation: Qt.vector3d(0, 0, -90) },
                        { colour: "#4dff6a", rotation: Qt.vector3d(0, 0, 0) },
                        { colour: "#4d8cff", rotation: Qt.vector3d(90, 0, 0) }
                    ]

                    delegate: Node {
                        id: axisNode

                        required property var modelData

                        eulerRotation: axisNode.modelData.rotation

                        Model {
                            source: "#Cylinder"
                            y: (originAxes.axisLength - originAxes.headLength) / 2
                            scale: Qt.vector3d(originAxes.shaftRadius / 50,
                                               (originAxes.axisLength - originAxes.headLength) / 100,
                                               originAxes.shaftRadius / 50)
                            materials: PrincipledMaterial {
                                baseColor: axisNode.modelData.colour
                                lighting: PrincipledMaterial.NoLighting
                                cullMode: Material.NoCulling
                            }
                        }

                        Model {
                            source: "#Cone"
                            y: originAxes.axisLength - originAxes.headLength / 2
                            scale: Qt.vector3d(originAxes.headRadius / 50,
                                               originAxes.headLength / 100,
                                               originAxes.headRadius / 50)
                            materials: PrincipledMaterial {
                                baseColor: axisNode.modelData.colour
                                lighting: PrincipledMaterial.NoLighting
                                cullMode: Material.NoCulling
                            }
                        }
                    }
                }
            }

            Node {
                visible: root.measurePoints.length > 0

                Repeater3D {
                    model: root.measurePoints

                    delegate: Model {
                        required property var modelData
                        source: "#Sphere"
                        position: Qt.vector3d(modelData[0], modelData[1], modelData[2])
                        scale: Qt.vector3d(0.08, 0.08, 0.08)
                        materials: PrincipledMaterial {
                            baseColor: Colors.accent
                            lighting: PrincipledMaterial.NoLighting
                        }
                    }
                }

                Model {
                    visible: root.measureComplete
                    source: "#Cylinder"
                    position: root.measureComplete
                        ? Qt.vector3d((root.measurePoints[0][0] + root.measurePoints[1][0]) / 2,
                                      (root.measurePoints[0][1] + root.measurePoints[1][1]) / 2,
                                      (root.measurePoints[0][2] + root.measurePoints[1][2]) / 2)
                        : Qt.vector3d(0, 0, 0)
                    rotation: root.measureComplete
                        ? root.segmentRotation(root.measurePoints[0], root.measurePoints[1])
                        : Qt.quaternion(1, 0, 0, 0)
                    scale: Qt.vector3d(0.006, Math.max(0.0001, root.measureDistance / 100), 0.006)
                    materials: PrincipledMaterial {
                        baseColor: Colors.accent
                        lighting: PrincipledMaterial.NoLighting
                    }
                }
            }

            // The plate's own chrome -- name, pencil, action column, plate number --
            // as quads in the bed plane instead of decals in the 2D scene. The look
            // is unchanged (each quad carries the texture of the very item the 2D
            // delegate lays out, and lands on the same rectangle), but the model now
            // stands in front of it: a part on the plate hides the buttons it
            // covers, which is the one thing a 2D composited overlay cannot do.
            Repeater3D {
                objectName: "plateChromeNode"

                model: plateDocument.plates

                delegate: Node {
                    id: chromePlate

                    required property int index

                    readonly property var entry: plateChrome.contents[index] ?? null
                    readonly property var rects: chromePlate.entry !== null ? chromePlate.entry.rects : null
                    readonly property var source: chromePlate.entry !== null ? chromePlate.entry.source : null

                    visible: root.mode !== "preview" && !root.paintActive

                    BedQuad {
                        sourceItem: chromePlate.source !== null ? chromePlate.source.name : null
                        x0: chromePlate.rects !== null ? chromePlate.rects.name.x : 0
                        y0: chromePlate.rects !== null ? chromePlate.rects.name.y : 0
                        x1: chromePlate.rects !== null ? chromePlate.rects.name.x
                                                         + chromePlate.rects.name.width : 0
                        y1: chromePlate.rects !== null ? chromePlate.rects.name.y
                                                         + chromePlate.rects.name.height : 0
                    }

                    BedQuad {
                        sourceItem: chromePlate.source !== null ? chromePlate.source.pencil : null
                        x0: chromePlate.rects !== null ? chromePlate.rects.pencil.x : 0
                        y0: chromePlate.rects !== null ? chromePlate.rects.pencil.y : 0
                        x1: chromePlate.rects !== null ? chromePlate.rects.pencil.x
                                                         + chromePlate.rects.pencil.width : 0
                        y1: chromePlate.rects !== null ? chromePlate.rects.pencil.y
                                                         + chromePlate.rects.pencil.height : 0
                    }

                    BedQuad {
                        sourceItem: chromePlate.source !== null ? chromePlate.source.number : null
                        x0: chromePlate.rects !== null ? chromePlate.rects.number.x : 0
                        y0: chromePlate.rects !== null ? chromePlate.rects.number.y : 0
                        x1: chromePlate.rects !== null ? chromePlate.rects.number.x
                                                         + chromePlate.rects.number.width : 0
                        y1: chromePlate.rects !== null ? chromePlate.rects.number.y
                                                         + chromePlate.rects.number.height : 0
                    }

                    Repeater3D {
                        model: 6

                        delegate: BedQuad {
                            required property int index

                            readonly property var rect:
                                chromePlate.rects !== null && index < chromePlate.rects.actions.length
                                    ? chromePlate.rects.actions[index] : null

                            sourceItem: chromePlate.source !== null
                                        && index < chromePlate.source.actions.length
                                        ? chromePlate.source.actions[index] : null
                            x0: rect !== null ? rect.x : 0
                            y0: rect !== null ? rect.y : 0
                            x1: rect !== null ? rect.x + rect.width : 0
                            y1: rect !== null ? rect.y + rect.height : 0
                        }
                    }
                }
            }
        }
    }


    // The brush bar. While a brush is armed the object panel is shut (it is 700
    // pixels of settings, and it sits over the part) and these five controls take
    // its place: the kind, the brush size, what the brush writes, the overhang
    // restriction, and the way out. It is a bar over the bed, not a popup, so it
    // cannot end up in front of what is being painted.
    Rectangle {
        id: brushBar
        objectName: "brushBar"
        // Above the viewport's own MouseArea, which fills the whole item and would
        // otherwise take the bar's clicks as brush strokes.
        z: 100
        visible: root.paintActive && root.mode !== "preview"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Metrics.controlHeight * 2 + Metrics.lg
        width: barRow.implicitWidth + Metrics.lg * 2
        height: barRow.implicitHeight + Metrics.sm * 2
        radius: Metrics.radiusMedium
        color: Colors.alpha(Colors.surfaceContainerLow, 0.94)
        border.width: Metrics.borderWidth
        border.color: Colors.outlineVariant

        RowLayout {
            id: barRow
            anchors.centerIn: parent
            spacing: Metrics.sm

            Button {
                text: "Support"
                primary: root.paintKind === "supports"
                onClicked: {
                    root.paintKind = "supports"
                    root.paintMode = "enforce"
                    root.paintSettingsChanged(root.paintKind, root.paintMode, root.paintRadius)
                }
            }
            Button {
                text: "Seam"
                primary: root.paintKind === "seam"
                onClicked: {
                    root.paintKind = "seam"
                    root.paintMode = "enforce"
                    root.paintSettingsChanged(root.paintKind, root.paintMode, root.paintRadius)
                }
            }
            Button {
                text: "Colour"
                primary: root.paintKind === "tool"
                onClicked: {
                    root.paintKind = "tool"
                    root.paintMode = "enforce"
                    root.paintSettingsChanged(root.paintKind, root.paintMode, root.paintRadius)
                }
            }

            Rectangle {
                width: Metrics.borderWidth
                Layout.preferredHeight: Metrics.controlHeight * 0.6
                color: Colors.outlineVariant
            }

            Text {
                text: "Brush size"
                font.family: Type.family
                font.pixelSize: Type.sizeCaption
                color: Colors.textMuted
            }

            Slider {
                Layout.preferredWidth: 130
                from: 0.5
                to: 20
                step: 0.5
                value: root.paintRadius
                onMoved: (value) => {
                    root.paintRadius = value
                    plateDocument.brushRadius = value
                    root.paintSettingsChanged(root.paintKind, root.paintMode, value)
                }
            }

            Text {
                text: root.paintRadius.toFixed(1) + " mm"
                font.family: Type.family
                font.pixelSize: Type.sizeCaption
                color: Colors.textSecondary
                Layout.preferredWidth: 56
            }

            Rectangle {
                width: Metrics.borderWidth
                Layout.preferredHeight: Metrics.controlHeight * 0.6
                color: Colors.outlineVariant
            }

            Button {
                text: root.paintKind === "seam" ? "Enforce seam"
                      : root.paintKind === "tool" ? "Paint" : "Enforce"
                primary: root.paintMode === "enforce"
                onClicked: {
                    root.paintMode = "enforce"
                    root.paintSettingsChanged(root.paintKind, root.paintMode, root.paintRadius)
                }
            }
            Button {
                visible: root.paintKind !== "tool"
                text: root.paintKind === "seam" ? "Block seam" : "Block"
                primary: root.paintMode === "block"
                onClicked: {
                    root.paintMode = "block"
                    root.paintSettingsChanged(root.paintKind, root.paintMode, root.paintRadius)
                }
            }
            Button {
                text: "Erase"
                primary: root.paintMode === "erase"
                onClicked: {
                    root.paintMode = "erase"
                    root.paintSettingsChanged(root.paintKind, root.paintMode, root.paintRadius)
                }
            }

            CheckBox {
                visible: root.paintKind === "supports"
                text: "Overhangs only"
                checked: plateDocument.brushOverhangOnly
                onToggled: plateDocument.brushOverhangOnly = checked
            }

            Item { Layout.preferredWidth: Metrics.xs }

            Button {
                text: "More"
                onClicked: root.paintMoreRequested()
            }

            Button {
                text: "Done"
                primary: true
                onClicked: root.paintFinished()
            }
        }
    }

    GcodeView {
        id: gcodeView
        anchors.fill: parent
        visible: toolpath.loaded && root.mode === "preview"

        cameraEye: perspectiveCamera.scenePosition
        cameraCenter: root.vAdd(root.focusPoint, root.panOffset)
        cameraUp: root.rotateByQuaternion(perspectiveCamera.sceneRotation, Qt.vector3d(0, 1, 0))
        fov: perspectiveCamera.fieldOfView

        Component.onCompleted: toolpath.attachView(gcodeView)
    }

    // Only a failed open says anything here now: the empty plate is what an empty
    // plate looks like, and a line of instruction painted across the bed reads as
    // part of the model.
    Text {
        anchors.centerIn: parent
        visible: !session.hasModel && session.error.length > 0
        text: session.error
        font.family: Type.family
        font.pixelSize: Type.sizeSmall
        color: Colors.error
        elide: Text.ElideMiddle
        width: Math.min(implicitWidth, parent.width - 80)
        horizontalAlignment: Text.AlignHCenter
    }

    Rectangle {
        visible: root.measureActive
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: Metrics.md
        width: readout.implicitWidth + Metrics.md * 2
        height: readout.implicitHeight + Metrics.md * 2
        radius: Metrics.radiusMedium
        color: Colors.alpha(Colors.surfaceContainer, 0.92)
        border.width: Metrics.borderWidth
        border.color: Colors.accent

        Column {
            id: readout
            anchors.centerIn: parent
            spacing: Metrics.xxs

            Text {
                text: root.measureComplete
                      ? "distance " + root.measureDistance.toFixed(2) + " mm"
                      : root.measurePoints.length === 0
                        ? "measure: click two points, on the model or on the bed"
                        : "measure: click the second point"
                font.family: Type.family
                font.pixelSize: Type.sizeCaption
                font.bold: true
                color: Colors.textPrimary
            }

            Text {
                visible: root.measureComplete
                text: "dx " + root.measureVector.x.toFixed(2)
                      + "  dy " + root.measureVector.y.toFixed(2)
                      + "  dz " + root.measureVector.z.toFixed(2) + " mm"
                font.family: Type.family
                font.pixelSize: Type.sizeCaption
                color: Colors.textSecondary
            }

            Text {
                visible: root.measureComplete
                text: "angle from vertical " + root.measureAngle.toFixed(1) + "°"
                font.family: Type.family
                font.pixelSize: Type.sizeCaption
                color: Colors.textSecondary
            }
        }
    }

    // The move commits through the engine, which writes a transformed STL and
    // reloads the mesh. Keep the drag offset applied until that new geometry
    // lands, so the object never snaps back to where it was dropped.


    Connections {
        target: meshGeometry
        function onBoundsChanged() { root.dragOffset = Qt.vector3d(0, 0, 0) }
    }

    // The camera cluster: a projection toggle, the six axis views plus the
    // isometric default, and a fit-to-plate button. Its own card in the corner so
    // the camera controls live with the viewport rather than in the settings chrome.
    Rectangle {
        id: cameraBar
        objectName: "cameraBar"
        z: 2
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: Metrics.md + root.rightInset
        anchors.bottomMargin: Metrics.md + root.bottomInset
        width: cameraRow.width + Metrics.sm * 2
        height: root.cameraBarHeight
        radius: Metrics.radiusSmall
        color: Colors.alpha(Colors.surfaceContainer, 0.92)
        border.width: Metrics.borderWidth
        border.color: Colors.edge

        Row {
            id: cameraRow
            anchors.centerIn: parent
            spacing: 0

            Repeater {
                model: [
                    { id: "iso", icon: "view_in_ar", hint: "Isometric (1)" },
                    { id: "top", icon: "vertical_align_top", hint: "Top view (2)" },
                    { id: "front", icon: "crop_square", hint: "Front view (4)" },
                    { id: "left", icon: "border_left", hint: "Left view (6)" },
                    { id: "right", icon: "border_right", hint: "Right view (7)" }
                ]

                delegate: ToolButton {
                    required property var modelData

                    width: Metrics.rowHeight - Metrics.sm
                    height: Metrics.rowHeight - Metrics.sm
                    icon: modelData.icon
                    hint: modelData.hint
                    active: root.viewName === modelData.id
                    onClicked: root.setView(modelData.id)
                }
            }

            Divider { vertical: true; anchors.verticalCenter: parent.verticalCenter }

            ToolButton {
                width: Metrics.rowHeight - Metrics.sm
                height: Metrics.rowHeight - Metrics.sm
                icon: "restart_alt"
                hint: "Reset view (R)"
                onClicked: root.resetView()
            }

            ToolButton {
                width: Metrics.rowHeight - Metrics.sm
                height: Metrics.rowHeight - Metrics.sm
                icon: "zoom_out_map"
                hint: "Fit plate (F)"
                onClicked: root.fitPlate()
            }

            ToolButton {
                width: Metrics.rowHeight - Metrics.sm
                height: Metrics.rowHeight - Metrics.sm
                icon: "camera"
                hint: root.orthographic ? "Perspective (O)" : "Orthographic (O)"
                active: root.orthographic
                onClicked: root.orthographic = !root.orthographic
            }
        }
    }

    // The selection rectangle, drawn over the viewport while Shift is held down.
    // Kept outside the input area so it never eats the drag it is visualising.
    Rectangle {
        id: band
        visible: false
        x: Math.min(band.startX, band.endX)
        y: Math.min(band.startY, band.endY)
        width: Math.abs(band.endX - band.startX)
        height: Math.abs(band.endY - band.startY)
        color: Colors.alpha(Colors.accent, 0.12)
        border.width: Metrics.borderWidth
        border.color: Colors.accent

        property real startX: 0
        property real startY: 0
        property real endX: 0
        property real endY: 0
    }

    MouseArea {
        id: input
        anchors.fill: parent
        focus: true
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
        cursorShape: root.measureActive || root.paintActive || root.layOnFaceActive
                   ? Qt.CrossCursor
                   : input.dragging ? Qt.ClosedHandCursor
                   : (root.moveEnabled || (root.dragSelectedEnabled && plateDocument.selectedCount > 0))
                     && input.containsMouse ? Qt.SizeAllCursor
                   : (pressed && input.panning) ? Qt.ClosedHandCursor
                   : root.hoveringObject ? Qt.PointingHandCursor
                   : Qt.ArrowCursor

        property real pointerX: 0
        property real pointerY: 0
        property real pressX: 0
        property real pressY: 0
        property bool panning: false
        property bool dragging: false
        property var dragStart: null
        property real dragPlaneZ: 0
        property int gizmoAxis: -1
        property var gizmoStart: null
        property vector3d gizmoDelta: Qt.vector3d(0, 0, 0)
        property bool banding: false
        /// Where the last dab landed, on screen and on the mesh. A drag paints
        /// again once the pointer leaves that facet or travels far enough: a
        /// repaint on every mouse report reserialises the whole selector and
        /// rebuilds the overlay for a dab that would land on the same triangles.
        property real paintX: 0
        property real paintY: 0
        property int paintFacet: -1

        onExited: {
            root.hoveringObject = false
            plateScene.clearBrushRing()
        }

        onPressed: (mouse) => {
            input.forceActiveFocus()
            input.pointerX = mouse.x
            input.pointerY = mouse.y
            input.pressX = mouse.x
            input.pressY = mouse.y
            input.panning = mouse.button === Qt.RightButton || mouse.button === Qt.MiddleButton
            input.dragging = false

            if (mouse.button !== Qt.LeftButton || root.measureActive || !session.hasModel)
                return

            if (root.paintActive) {
                input.dragging = true
                input.paintX = mouse.x
                input.paintY = mouse.y
                input.paintFacet = -1
                root.paintStroke(mouse.x, mouse.y)
                input.paintFacet = plateScene.pickedFacet
                root.updateBrushRing(mouse.x, mouse.y)
                return
            }

            if (root.layOnFaceActive) {
                const faceHit = root.hitAt(mouse.x, mouse.y)
                if (faceHit.w > 0) {
                    const facet = plateScene.pickedFacet
                    if (plateDocument.placeOnFace(plateScene.pickedId, facet))
                        root.placedOnFace(plateScene.pickedId, facet)
                }
                return
            }

            // Shift and drag is a selection rectangle, not a click: the click part is
            // decided on release, so Shift-click still adds one object and a Shift
            // drag picks up everything the rectangle covers.
            if ((mouse.modifiers & Qt.ShiftModifier) !== 0 && root.mode === "model") {
                input.banding = true
                band.startX = mouse.x
                band.startY = mouse.y
                band.endX = mouse.x
                band.endY = mouse.y
                band.visible = true
                return
            }

            // The sliced preview is a read-only view of the toolpaths. A left drag
            // there orbits the camera; letting it drag the object committed a move
            // and dropped the toolpath, which is exactly the "rotating the preview
            // throws the model away" report.
            if (root.mode !== "model")
                return

            // A handle that is under the pointer takes the drag, before the body
            // behind it does: dragging an arrow means move along it, not move
            // freely across the bed.
            const grabRay = root.rayAt(mouse.x, mouse.y)
            if (grabRay !== null && root.moveEnabled) {
                const axis = plateScene.pickGizmo(grabRay[0], grabRay[1])
                if (axis >= 0) {
                    input.gizmoAxis = axis
                    input.gizmoStart = plateScene.gizmoAxisPoint(axis, grabRay[0], grabRay[1])
                    return
                }
            }

            const hit = root.hitAt(mouse.x, mouse.y)
            if (hit.w > 0) {
                root.selected = true
                const id = plateScene.pickedId
                const additive = (mouse.modifiers & (Qt.ShiftModifier | Qt.ControlModifier)) !== 0
                if (additive)
                    plateDocument.selectObject(id, true)
                else if (!plateDocument.isSelected(id))
                    plateDocument.selectObject(id)
                if (!plateDocument.isSelected(id))
                    return
                if (!root.moveEnabled && !root.dragSelectedEnabled)
                    return
                input.dragging = true
                input.dragPlaneZ = hit.z
                input.dragStart = root.planePoint(mouse.x, mouse.y, hit.z)
            } else {
                root.selected = false
                plateDocument.selectObject(0)
            }
        }

        onReleased: (mouse) => {
            // The right button pans while it is dragged and opens the object's menu
            // when it is not -- the arrangement every slicer has, and the one that
            // stops a pan from being an accidental menu. A press that stays put is
            // a request for the menu of whatever is under it.
            if (mouse.button === Qt.RightButton) {
                const travelled = Math.abs(mouse.x - input.pressX) + Math.abs(mouse.y - input.pressY)
                input.panning = false
                if (travelled >= 4 || root.mode !== "model" || !session.hasModel)
                    return
                const hit = root.hitAt(mouse.x, mouse.y)
                if (hit.w <= 0)
                    return
                plateDocument.selectObject(plateScene.pickedId)
                const point = root.mapToItem(null, mouse.x, mouse.y)
                root.contextRequested(plateScene.pickedId, point.x, point.y)
                return
            }

            if (input.banding) {
                input.banding = false
                band.visible = false
                const travelled = Math.abs(mouse.x - input.pressX) + Math.abs(mouse.y - input.pressY)
                if (travelled < 4) {
                    // A Shift click is still a Shift click: it adds one object.
                    const hit = root.hitAt(mouse.x, mouse.y)
                    if (hit.w > 0)
                        plateDocument.selectObject(plateScene.pickedId, true)
                } else {
                    const count = root.selectInBand()
                    root.bandSelected(count)
                }
                return
            }

            if (input.gizmoAxis >= 0) {
                const moved = input.gizmoDelta
                input.gizmoAxis = -1
                input.gizmoStart = null
                input.gizmoDelta = Qt.vector3d(0, 0, 0)
                root.dragOffset = Qt.vector3d(0, 0, 0)
                if (Math.abs(moved.x) + Math.abs(moved.y) + Math.abs(moved.z) > 0.0005)
                    plateDocument.moveSelection(moved.x, moved.y, moved.z)
            }

            // A stroke ended: the press painted, the moves painted, and the release
            // is only the end of it. Painting here as well would put a second dab
            // wherever the pointer stopped, which is not what a stroke means.
            if (root.paintActive) {
                input.dragging = false
                return
            }

            if (input.dragging) {
                const point = root.planePoint(mouse.x, mouse.y, input.dragPlaneZ)
                input.dragging = false
                if (point === null || input.dragStart === null) {
                    root.dragOffset = Qt.vector3d(0, 0, 0)
                    return
                }
                const deltaX = point.x - input.dragStart.x
                const deltaY = point.y - input.dragStart.y
                if (Math.abs(deltaX) + Math.abs(deltaY) < 0.01) {
                    root.dragOffset = Qt.vector3d(0, 0, 0)
                    return
                }
                root.dragOffset = Qt.vector3d(0, 0, 0)
                plateDocument.moveSelection(deltaX, deltaY)
            }

            if (!root.measureActive)
                return
            const travelled = Math.abs(mouse.x - input.pressX) + Math.abs(mouse.y - input.pressY)
            if (travelled < 4)
                root.addMeasurePoint(mouse.x, mouse.y)
        }

        onDoubleClicked: (mouse) => {
            // Double clicking a plate makes it the active one, which is how a
            // multi-plate project is navigated with the mouse: the plate under the
            // pointer is the one being asked for, and the camera then goes there.
            if (mouse.button !== Qt.LeftButton || root.mode !== "model")
                return
            const point = root.planePoint(mouse.x, mouse.y, 0)
            if (point === null)
                return
            const plate = plateDocument.plateAt(point)
            if (plate >= 0 && plate !== plateDocument.activePlate)
                root.plateSelected(plate)
        }

        onPositionChanged: (mouse) => {
            if (input.banding) {
                band.endX = mouse.x
                band.endY = mouse.y
                return
            }

            if (root.paintActive && !input.panning) {
                root.updateBrushRing(mouse.x, mouse.y)
                if (input.pressed) {
                    // Dabs are spaced half a brush apart, the way a paint program
                    // lays down a stroke, and a fast drag is filled in between the
                    // two points it jumped over rather than leaving a dotted line.
                    // Each dab costs a selector round trip, so the fill is capped.
                    const spacing = Math.max(2.0, root.paintRadius
                                                   / Math.max(root.mmPerPixel, 0.0001) * 0.5)
                    const travelled = Math.abs(mouse.x - input.paintX)
                                    + Math.abs(mouse.y - input.paintY)
                    const facet = plateScene.pickedFacet
                    if (travelled >= spacing || (facet >= 0 && facet !== input.paintFacet)) {
                        const steps = Math.min(4, Math.max(1, Math.round(travelled / spacing)))
                        const fromX = input.paintX
                        const fromY = input.paintY
                        for (let step = 1; step <= steps; ++step) {
                            const t = step / steps
                            root.paintStroke(fromX + (mouse.x - fromX) * t,
                                             fromY + (mouse.y - fromY) * t)
                        }
                        input.paintX = mouse.x
                        input.paintY = mouse.y
                        input.paintFacet = plateScene.pickedFacet
                    }
                }
                return
            }

            // The cursor is a hand only over a model: the viewport is a canvas,
            // and a hand on the empty bed promises a click that does nothing.
            if (!input.dragging && !input.panning && root.mode === "model" && session.hasModel)
                root.hoveringObject = root.hitAt(mouse.x, mouse.y).w > 0
            else
                root.hoveringObject = false

            // With the lay-on-face tool armed, the face under the pointer is tinted
            // before the click: that is the difference between aiming and guessing.
            if (root.layOnFaceActive && !input.pressed) {
                const hoverHit = root.hitAt(mouse.x, mouse.y)
                if (hoverHit.w > 0)
                    plateScene.setHoverFacet(plateScene.pickedId, plateScene.pickedFacet)
                else
                    plateScene.setHoverFacet(0, -1)
            }

            if (input.pressed && input.gizmoAxis >= 0 && input.gizmoStart !== null) {
                const ray = root.rayAt(mouse.x, mouse.y)
                if (ray !== null) {
                    const point = plateScene.gizmoAxisPoint(input.gizmoAxis, ray[0], ray[1])
                    const delta = root.vSub(point, input.gizmoStart)
                    if (Math.abs(delta.x) + Math.abs(delta.y) + Math.abs(delta.z) > 0.0005) {
                        // Preview only: committing here ran an undo step and a plate
                        // rebuild for every mouse report, which is what made dragging
                        // a handle crawl on a busy plate. The move lands on release.
                        input.gizmoDelta = root.vAdd(input.gizmoDelta, delta)
                        root.dragOffset = Qt.vector3d(input.gizmoDelta.x,
                                                      input.gizmoDelta.y,
                                                      input.gizmoDelta.z)
                        input.gizmoStart = point
                    }
                }
                return
            }
            if (!input.pressed)
                return

            const dx = mouse.x - input.pointerX
            const dy = mouse.y - input.pointerY
            input.pointerX = mouse.x
            input.pointerY = mouse.y

            if (input.dragging) {
                const point = root.planePoint(mouse.x, mouse.y, input.dragPlaneZ)
                if (point !== null && input.dragStart !== null)
                    root.dragOffset = Qt.vector3d(point.x - input.dragStart.x,
                                                  point.y - input.dragStart.y, 0)
                return
            }

            if (input.panning) {
                const distance = root.fittedDistance * root.zoom
                const worldPerPixel = 2 * distance * Math.tan(root.fieldOfView * Math.PI / 360)
                                     / Math.max(1, view.height)
                const focus = root.vAdd(root.focusPoint, root.panOffset)
                const forward = root.vNormal(root.vSub(focus, perspectiveCamera.scenePosition))
                const right = root.vNormal(root.vCross(forward, Qt.vector3d(0, 1, 0)))
                const up = root.vNormal(root.vCross(right, forward))
                root.panOffset = root.vAdd(root.panOffset, root.vScale(right, -dx * worldPerPixel))
                root.panOffset = root.vAdd(root.panOffset, root.vScale(up, dy * worldPerPixel))
                return
            }

            root.yaw += dx * 0.4
            root.pitch = Math.max(-89, Math.min(89, root.pitch + dy * 0.4))
        }

        onWheel: (wheel) => {
            const steps = wheel.angleDelta.y / 120
            root.zoom = Math.max(root.minZoom,
                                 Math.min(root.maxZoom, root.zoom * Math.pow(0.88, steps)))
        }

        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_R || event.key === Qt.Key_Home) {
                root.resetView()
                event.accepted = true
            } else if (event.key === Qt.Key_F) {
                root.fitPlate()
                event.accepted = true
            } else if (event.key === Qt.Key_O) {
                root.orthographic = !root.orthographic
                event.accepted = true
            } else if (event.key >= Qt.Key_1 && event.key <= Qt.Key_7) {
                const names = ["iso", "top", "bottom", "front", "rear", "left", "right"]
                root.setView(names[event.key - Qt.Key_1])
                event.accepted = true
            } else if (event.key === Qt.Key_Up && toolpath.layerCount > 0) {
                toolpath.setLayer(toolpath.currentLayer + 1)
                event.accepted = true
            } else if (event.key === Qt.Key_Down && toolpath.layerCount > 0) {
                toolpath.setLayer(toolpath.currentLayer - 1)
                event.accepted = true
            } else if (event.key === Qt.Key_Escape && root.measureActive) {
                root.clearMeasure()
                event.accepted = true
            }
        }
    }

    // The plate chrome, ported from `PartPlate::calc_vertex_for_number`,
    // `calc_vertex_for_icons` and `calc_vertex_for_plate_name_edit_icon`. The engine
    // builds these as geometry lying in the plate's own plane and raycasts the
    // clicks; these are decals instead, but the
    // look that matters is that they are *on the bed*, foreshortening with it as the
    // camera tilts, so they are drawn here as decals: a rectangle in printer
    // coordinates, its four corners projected through the camera, and a projective
    // matrix that maps the item onto the resulting quad. The constants, in mm:
    // 16 for an icon, 9 for the pencil, 3 of gap to the left, 3 from the top and 5
    // between icons.
    Item {
        id: plateChrome
        anchors.fill: parent
        z: 20
        visible: root.mode !== "preview"

        // `PartPlate::icon_scale_factor()`: min(bed depth / 200, bed width * 0.2 / 22),
        // which for a 350 mm bed is 1.75 -- a 16 mm icon becomes 28 mm on the bed
        // and the 9 mm name height about 16 mm. Everything below scales with it.
        readonly property real iconScale: Math.max(
            0.7,
            Math.min((root.bedMax.y - root.bedMin.y) / 200,
                     (root.bedMax.x - root.bedMin.x) * 0.2 / 22))
        readonly property real iconSize: 16 * plateChrome.iconScale
        readonly property real pencilSize: 15 * plateChrome.iconScale
        readonly property real nameHeight: 9 * plateChrome.iconScale
        readonly property real gapLeft: 3 * plateChrome.iconScale
        readonly property real gapTop: 6 * plateChrome.iconScale
        readonly property real gapY: 5 * plateChrome.iconScale
        readonly property real decalLift: 0.25

        // Where a plate-plane point lands in the 2D scene. The chrome itself is
        // drawn as quads in the 3D scene (see `BedQuad`), because only a quad in
        // the scene is depth tested against the model -- an item in the 2D scene is
        // composited over it. What is left here is where to *click*: a hit area has
        // to be a 2D item, so it keeps the projective transform.
        //
        // `mapFrom3DScene` is a method, not a property, so the binding reads every
        // input the camera moves through -- without that it runs once and the chrome
        // never follows the view. A point behind the camera projects to nonsense
        // rather than to a wrong pixel: the projective terms of the transform blow up
        // as the plane approaches the eye, which is what smeared the chrome across the
        // model once the camera zoomed into the plate. Depth is tested first so the
        // quad is dropped instead.
        function project(printerX, printerY) {
            const world = Qt.vector3d(printerX, plateChrome.decalLift, -printerY)
            const eye = (root.orthographic ? orthoCamera : perspectiveCamera).scenePosition
            const forward = root.vNormal(root.vSub(root.vAdd(root.focusPoint, root.panOffset), eye))
            if (root.vDot(root.vSub(world, eye), forward) < root.clipNear * 2)
                return null
            return view.mapFrom3DScene(world)
        }

        // A decal more than half a viewport outside the frame is not clickable and
        // not worth a matrix: past that its projective terms are far enough from the
        // eye that the quad they describe is not the one on screen.
        function projectable(point) {
            return point !== null && isFinite(point.x) && isFinite(point.y)
                && Math.abs(point.x - plateChrome.width / 2) < plateChrome.width
                && Math.abs(point.y - plateChrome.height / 2) < plateChrome.height
        }

        // The rendered chrome, per plate, for the 3D quads. A repeater cannot read
        // another repeater's delegates, so each delegate publishes its own items and
        // rectangles here and the 3D tree indexes them. Reassigning the array is what
        // notifies the quads: a JS array mutated in place sends no change signal.
        property var contents: []

        function register(index, entry) {
            const next = contents.slice()
            next[index] = entry
            contents = next
        }

        /// The projective matrix that carries a `width` x `height` item onto the quad
        /// given by four screen points, in the order top-left, top-right,
        /// bottom-right, bottom-left. Heckbert's unit-square-to-quad homography,
        /// scaled from the unit square to the item's own size: `m14`/`m24` are the
        /// translation, `m41`/`m42` the perspective terms, applied by Qt as
        /// `(x, y, 1) * M` with the w divide.
        function quadMatrix(q0, q1, q2, q3, width, height) {
            const sx = q0.x - q1.x + q2.x - q3.x
            const sy = q0.y - q1.y + q2.y - q3.y
            let a, b, c, d, e, f, g, h
            if (Math.abs(sx) < 1e-6 && Math.abs(sy) < 1e-6) {
                a = q1.x - q0.x
                b = q3.x - q0.x
                c = q0.x
                d = q1.y - q0.y
                e = q3.y - q0.y
                f = q0.y
                g = 0
                h = 0
            } else {
                const dx1 = q1.x - q2.x
                const dx2 = q3.x - q2.x
                const dx3 = q0.x - q1.x + q2.x - q3.x
                const dy1 = q1.y - q2.y
                const dy2 = q3.y - q2.y
                const dy3 = q0.y - q1.y + q2.y - q3.y
                const det = dx1 * dy2 - dx2 * dy1
                if (Math.abs(det) < 1e-9)
                    return null
                g = (dx3 * dy2 - dx2 * dy3) / det
                h = (dx1 * dy3 - dx3 * dy1) / det
                a = q1.x - q0.x + g * q1.x
                b = q3.x - q0.x + h * q3.x
                c = q0.x
                d = q1.y - q0.y + g * q1.y
                e = q3.y - q0.y + h * q3.y
                f = q0.y
            }
            return Qt.matrix4x4(a / width, b / height, 0, c,
                                d / width, e / height, 0, f,
                                0, 0, 1, 0,
                                g / width, h / height, 0, 1)
        }

        Repeater {
            model: plateDocument.plates

            delegate: Item {
                id: plateItem

                required property var modelData
                required property int index

                readonly property bool current: plateItem.modelData.active === true
                readonly property bool locked: plateItem.modelData.locked === true

                // The icon column on the bed, back-right corner. The slot is carried
                // in the model rather than read from a Repeater's `index`: the outer
                // plate delegate declares its own `index`, which shadows the inner
                // one and stacked every icon on the same spot once already.
                readonly property var actions: [
                    { slot: 0, icon: "delete", hint: plateDocument.plateCount > 1
                                                 ? "Remove this plate"
                                                 : "The last plate cannot be removed",
                      enabled: plateDocument.plateCount > 1, danger: true, action: "remove" },
                    { slot: 1, icon: "autorenew", hint: "Auto-orient the objects on this plate",
                      enabled: true, danger: false, action: "orient" },
                    { slot: 2, icon: "grid_view", hint: "Arrange the objects on this plate",
                      enabled: true, danger: false, action: "arrange" },
                    { slot: 3, icon: plateItem.locked ? "lock" : "lock_open",
                      hint: plateItem.locked ? "Unlock this plate" : "Lock this plate",
                      enabled: true, danger: false, action: "lock" },
                    { slot: 4, icon: "more_vert", hint: "Plate settings: rename, duplicate, clear",
                      enabled: true, danger: false, action: "menu" },
                    { slot: 5, icon: "add", hint: "Add a plate after this one",
                      enabled: true, danger: false, action: "add" }
                ]

                // A rectangle of the bed, in printer coordinates, as a matrix for an
                // item whose local size is `w` x `h`. Null where any corner is behind
                // the camera, which is how the chrome disappears when the plate does.
                function decal(x0, y0, x1, y1, w, h) {
                    const q0 = plateChrome.project(plateItem.modelData.x + x0,
                                                   plateItem.modelData.y + y1)
                    const q1 = plateChrome.project(plateItem.modelData.x + x1,
                                                   plateItem.modelData.y + y1)
                    const q2 = plateChrome.project(plateItem.modelData.x + x1,
                                                   plateItem.modelData.y + y0)
                    const q3 = plateChrome.project(plateItem.modelData.x + x0,
                                                   plateItem.modelData.y + y0)
                    if (!plateChrome.projectable(q0) || !plateChrome.projectable(q1)
                            || !plateChrome.projectable(q2) || !plateChrome.projectable(q3))
                        return null
                    return plateChrome.quadMatrix(q0, q1, q2, q3, w, h)
                }

                // The plate's name sits above the back edge, off the bed, in the
                // 9 mm name height; the pencil follows it on the same row. The
                // decal's local box takes the name's own aspect so the text is not
                // stretched by the projection: a square box on a 40 x 16 mm quad
                // would shear the glyphs.
                readonly property real nameAspect: Math.max(1.2,
                                                            Math.min(6,
                                                                     nameMeasure.implicitWidth
                                                                     / Math.max(1, nameMeasure.implicitHeight)))
                readonly property real nameWidth: plateChrome.nameHeight * plateItem.nameAspect * 1.3
                readonly property real nameLocalHeight: 100 / plateItem.nameAspect
                readonly property real nameTop: root.bedMax.y + plateChrome.gapTop
                // The name and its pencil sit above the plate's back edge, starting at
                // its left corner and running rightwards: a caption on the plate, with
                // the space to its right left free for the action column.
                readonly property real nameLeft: root.bedMin.x + plateChrome.gapLeft
                readonly property var nameMatrix: plateItem.decal(
                    plateItem.nameLeft, plateItem.nameTop,
                    plateItem.nameLeft + plateItem.nameWidth,
                    plateItem.nameTop + plateChrome.nameHeight,
                    100, plateItem.nameLocalHeight)
                readonly property var pencilMatrix: plateItem.decal(
                    plateItem.nameLeft + plateItem.nameWidth + plateChrome.gapLeft,
                    plateItem.nameTop + (plateChrome.nameHeight - plateChrome.pencilSize) / 2,
                    plateItem.nameLeft + plateItem.nameWidth + plateChrome.gapLeft
                        + plateChrome.pencilSize,
                    plateItem.nameTop + (plateChrome.nameHeight + plateChrome.pencilSize) / 2,
                    100, 100)
                // The row's clickable quad, inflated around the name and the pencil.
                // Projected into the plate's plane at a shallow camera angle the name
                // is a strip a few pixels tall, and clicking text that small is a
                // game of pin the pixel; the label is decoration and this is the
                // target.
                readonly property real hitPad: plateChrome.nameHeight * 0.6
                readonly property var rowHitMatrix: plateItem.decal(
                    plateItem.nameLeft - plateItem.hitPad,
                    plateItem.nameTop
                        + (plateChrome.nameHeight - plateChrome.pencilSize) / 2 - plateItem.hitPad,
                    plateItem.nameLeft + plateItem.nameWidth + plateChrome.gapLeft
                        + plateChrome.pencilSize + plateItem.hitPad,
                    plateItem.nameTop + (plateChrome.nameHeight + plateChrome.pencilSize) / 2
                        + plateItem.hitPad,
                    100, 100)

                // Measures the name at a fixed reference size so the decal can take its
                // aspect; never drawn.
                Text {
                    id: nameMeasure
                    visible: false
                    text: plateItem.modelData.name
                    font.family: Type.family
                    font.bold: plateItem.current
                    font.pixelSize: 100
                }

                // The number sits at the front-right corner, drawn only once a
                // project has more than one plate.
                readonly property var numberMatrix: plateItem.decal(
                    root.bedMax.x - plateChrome.iconSize - plateChrome.gapLeft * 2,
                    root.bedMin.y + plateChrome.gapLeft * 2,
                    root.bedMax.x - plateChrome.gapLeft * 2,
                    root.bedMin.y + plateChrome.gapLeft * 2 + plateChrome.iconSize, 100, 100)

                // The same furniture in printer coordinates, which is what the 3D
                // quads are placed with. The matrices above project it; these say
                // where it lives on the bed.
                readonly property real plateX: plateItem.modelData.x ?? 0
                readonly property real plateY: plateItem.modelData.y ?? 0
                readonly property rect nameRect: Qt.rect(plateItem.plateX + plateItem.nameLeft,
                                                        plateItem.plateY + plateItem.nameTop,
                                                        plateItem.nameWidth,
                                                        plateChrome.nameHeight)
                readonly property rect pencilRect: Qt.rect(
                    plateItem.plateX + plateItem.nameLeft + plateItem.nameWidth
                        + plateChrome.gapLeft,
                    plateItem.plateY + plateItem.nameTop
                        + (plateChrome.nameHeight - plateChrome.pencilSize) / 2,
                    plateChrome.pencilSize, plateChrome.pencilSize)
                readonly property rect numberRect: Qt.rect(
                    plateItem.plateX + root.bedMax.x - plateChrome.iconSize
                        - plateChrome.gapLeft * 2,
                    plateItem.plateY + root.bedMin.y + plateChrome.gapLeft * 2,
                    plateChrome.iconSize, plateChrome.iconSize)
                // `icon_scale_factor()` again: the column hangs from the back-right
                // corner, one icon plus a gap per slot.
                readonly property var actionRects: {
                    const right = root.bedMax.x + plateChrome.gapLeft + plateChrome.iconSize
                    const base = root.bedMax.y - plateChrome.gapTop
                    const step = plateChrome.iconSize + plateChrome.gapY
                    const rects = []
                    for (let slot = 0; slot < plateItem.actions.length; ++slot)
                        rects.push(Qt.rect(plateItem.plateX + right - plateChrome.iconSize,
                                           plateItem.plateY + base - (slot + 1) * step,
                                           plateChrome.iconSize, plateChrome.iconSize))
                    return rects
                }

                // The label row is one target: name and pencil in a single inflated
                // quad that carries the hover state, the tooltip and the click. The
                // name and pencil below it are decoration, so the click does not
                // depend on hitting a glyph.
                Item {
                    id: nameRow

                    visible: plateItem.rowHitMatrix !== null
                    width: 100
                    height: 100
                    x: 0
                    y: 0
                    transformOrigin: Item.TopLeft
                    transform: Matrix4x4 { matrix: plateItem.rowHitMatrix ?? Qt.matrix4x4() }

                    MouseArea {
                        id: nameRowMouse
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: plateItem.current ? root.plateRenameRequested(plateItem.index)
                                                     : root.plateSelected(plateItem.index)
                    }

                    Tooltip {
                        target: nameRow
                        text: plateItem.current
                              ? "Rename " + plateItem.modelData.name
                              : "Switch to " + plateItem.modelData.name
                        shown: nameRowMouse.containsMouse
                        delay: 400
                    }
                }

                // The name, laid out for the quad's texture rather than drawn on
                // screen. It is laid out at three times the decal's own size and
                // parked outside the window: a 100-pixel layout sampled onto a quad
                // at close zoom reads as a smear, and an item parked on the screen
                // would be painted twice.
                Item {
                    id: nameContent

                    x: -20000
                    width: 300
                    height: 300 / plateItem.nameAspect

                    Item {
                        width: 100
                        height: plateItem.nameLocalHeight
                        transformOrigin: Item.TopLeft
                        scale: 3

                        Text {
                            anchors.fill: parent
                            anchors.margins: parent.height * 0.04
                            text: plateItem.modelData.name
                            verticalAlignment: Text.AlignVCenter
                            horizontalAlignment: Text.AlignLeft
                            elide: Text.ElideRight
                            font.family: Type.family
                            font.pixelSize: parent.height * 0.72
                            font.bold: plateItem.current
                            color: plateItem.current ? Colors.accent : Colors.textTertiary
                        }
                    }
                }

                // The pencil, right after the name on the same row.
                Item {
                    id: pencilContent

                    x: -20000
                    width: 300
                    height: 300

                    Item {
                        width: 100
                        height: 100
                        transformOrigin: Item.TopLeft
                        scale: 3

                        Icon {
                            anchors.centerIn: parent
                            name: plateItem.locked ? "lock" : "edit"
                            size: 76
                            color: plateItem.current ? Colors.accent : Colors.textTertiary
                        }
                    }
                }

                // The button column: six icons hanging down from the back-right
                // corner, each a 16 mm square standing on the bed. They came back
                // after being dropped for reading as loose glyphs: the plate is
                // what they act on, and reaching for a bar at the top of the window
                // to operate on the plate under the cursor is the wrong distance.
                // They carry a rounded plate behind the icon now, so they read as
                // buttons at any camera angle rather than as floating symbols.
                // They hang down the inside of the plate's right edge, where the
                // plate number already sits: the same place the reference slicer puts
                // them, and the reason is visible in one capture -- anchored outside
                // the bed they float on the background and leave the window as soon
                // as the camera is zoomed in, which makes the plate's own actions
                // unreachable exactly when the plate fills the view.
                Repeater {
                    model: plateItem.actions

                    delegate: Item {
                        id: actionDecal

                        required property var modelData

                        readonly property real buttonRight: root.bedMax.x + plateChrome.gapLeft
                                                            + plateChrome.iconSize
                        readonly property real buttonTop: root.bedMax.y - plateChrome.gapTop
                                                    - (actionDecal.modelData.slot + 1)
                                                    * (plateChrome.iconSize + plateChrome.gapY)
                        readonly property var matrix: plateItem.decal(
                            actionDecal.buttonRight - plateChrome.iconSize, actionDecal.buttonTop,
                            actionDecal.buttonRight,
                            actionDecal.buttonTop + plateChrome.iconSize, 100, 100)

                        // The click target, in the plate's plane and nothing else:
                        // the button the pointer lands on is this quad, and what it
                        // looks like is the one below it, drawn by the 3D scene.
                        Item {
                            id: actionHit

                            visible: actionDecal.matrix !== null
                            width: 100
                            height: 100
                            x: 0
                            y: 0
                            transformOrigin: Item.TopLeft
                            transform: Matrix4x4 { matrix: actionDecal.matrix ?? Qt.matrix4x4() }

                            MouseArea {
                                id: actionMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                enabled: actionDecal.modelData.enabled
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    const action = actionDecal.modelData.action
                                    if (action === "remove")
                                        root.plateRemoveRequested(plateItem.index)
                                    else if (action === "orient" || action === "arrange")
                                        root.actionRequested(action)
                                    else if (action === "lock")
                                        root.plateLockToggled(plateItem.index)
                                    else if (action === "menu") {
                                        const at = plateChrome.project(
                                            plateItem.modelData.x + root.bedMax.x
                                                + plateChrome.gapLeft + plateChrome.iconSize,
                                            plateItem.modelData.y + actionDecal.buttonTop)
                                        if (at !== null)
                                            root.plateMenuRequested(plateItem.index,
                                                                    Math.round(at.x),
                                                                    Math.round(at.y))
                                    }
                                    else if (action === "add")
                                        root.plateAddRequested()
                                }
                            }

                            Tooltip {
                                target: actionHit
                                text: actionDecal.modelData.hint
                                shown: actionMouse.containsMouse
                                delay: 500
                            }
                        }

                        Item {
                            id: actionContent

                            x: -20000
                            width: 300
                            height: 300

                            Item {
                                width: 100
                                height: 100
                                transformOrigin: Item.TopLeft
                                scale: 3

                                Rectangle {
                                    anchors.fill: parent
                                    anchors.margins: 3
                                    radius: 16
                                    color: !actionDecal.modelData.enabled
                                           ? Colors.alpha(Colors.viewportBackground, 0.55)
                                           : actionMouse.containsMouse
                                             ? Colors.alpha(Colors.accent, 0.30)
                                             : Colors.alpha(Colors.surfaceContainerHighest, 0.82)
                                    border.width: actionDecal.modelData.slot === 3 && plateItem.locked
                                                  ? 4 : 1
                                    border.color: actionDecal.modelData.slot === 3 && plateItem.locked
                                                  ? Colors.accent : Colors.alpha(Colors.edge, 0.9)
                                }

                                Icon {
                                    anchors.centerIn: parent
                                    name: actionDecal.modelData.icon
                                    size: 66
                                    filled: actionDecal.modelData.slot === 3 && plateItem.locked
                                    color: !actionDecal.modelData.enabled
                                           ? Colors.alpha(Colors.textSecondary, 0.4)
                                         : actionDecal.modelData.danger && actionMouse.containsMouse
                                           ? Colors.error
                                         : actionDecal.modelData.slot === 3 && plateItem.locked
                                           ? Colors.accent
                                         : Colors.textPrimary
                                }
                            }
                        }

                        Component.onCompleted:
                            plateItem.registerAction(actionDecal.modelData.slot, actionContent)
                    }
                }

                // The number, on the bed at the front-right corner.
                Item {
                    id: numberHit

                    visible: plateDocument.plateCount > 1 && plateItem.numberMatrix !== null
                    width: 100
                    height: 100
                    x: 0
                    y: 0
                    transformOrigin: Item.TopLeft
                    transform: Matrix4x4 { matrix: plateItem.numberMatrix ?? Qt.matrix4x4() }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.plateSelected(plateItem.index)
                    }

                    Tooltip {
                        target: numberHit
                        text: plateItem.modelData.count > 0
                              ? plateItem.modelData.name + " \u2014 "
                                + plateItem.modelData.count + " object(s)"
                              : plateItem.modelData.name + " \u2014 empty"
                        shown: numberHover.hovered
                        delay: 400
                    }
                    HoverHandler { id: numberHover }
                }

                Item {
                    id: numberContent

                    x: -20000
                    width: 300
                    height: 300

                    Item {
                        width: 100
                        height: 100
                        transformOrigin: Item.TopLeft
                        scale: 3

                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: 8
                            radius: 18
                            color: plateItem.current ? Colors.alpha(Colors.accent, 0.9)
                                                     : Colors.alpha(Colors.surfaceContainer, 0.85)
                            border.width: plateItem.current ? 0 : 4
                            border.color: Colors.alpha(Colors.outlineVariant, 0.9)
                        }

                        Text {
                            anchors.centerIn: parent
                            text: String(plateItem.index + 1)
                            font.family: Type.family
                            font.pixelSize: 62
                            font.bold: true
                            color: plateItem.current ? Colors.viewportBackground
                                                     : Colors.textSecondary
                        }
                    }
                }

                // What the 3D quads draw, and where: published rather than reached
                // for, because a repeater cannot see another repeater's delegates.
                readonly property QtObject chromeEntry: QtObject {
                    readonly property var source: ({
                        name: nameContent,
                        pencil: pencilContent,
                        number: plateDocument.plateCount > 1 ? numberContent : null,
                        actions: plateItem.actionContents
                    })
                    readonly property var rects: ({
                        name: plateItem.nameRect,
                        pencil: plateItem.pencilRect,
                        number: plateItem.numberRect,
                        actions: plateItem.actionRects
                    })
                }

                property var actionContents: []

                function registerAction(slot, item) {
                    const next = actionContents.slice()
                    next[slot] = item
                    actionContents = next
                }

                Component.onCompleted: plateChrome.register(plateItem.index, plateItem.chromeEntry)
            }
        }
    }
}
