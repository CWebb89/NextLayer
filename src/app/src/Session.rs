// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

//! What the viewport shows of the open document, decided once.
//!
//! `Session` is a view over `PlateDocument` and owns no model state: it answers
//! which object the viewport is looking at, what the window calls it, and where the
//! camera frames it. Only the last is arithmetic — and it is the arithmetic that
//! goes wrong quietly, because Qt Quick 3D's world is Y-up and the printer's is
//! Z-up, so every camera figure here negates Y.

use crate::bridge::SessionView;

/// Decides the view for the active plate.
///
/// `document_empty` is the whole document's emptiness and is what `has_model`
/// reports, verbatim: an empty active plate in a two-plate document still reads
/// `hasModel == true` today, and QML gates its pointer handling on that. `ids` is the
/// active plate's objects in insertion order, `count` is that plate's object count
/// and `names` are their display names, already resolved from name and label by the
/// model.
///
/// The empty-plate case is the one deliberate difference: `displayName()` in C++
/// reaches `ids().first()` on an empty vector when the document is non-empty and the
/// active plate is empty, which is undefined. There is no faithful answer to
/// reproduce — the only alternatives are a crash or a name — so this returns
/// "no model" and says so.
pub fn session_view(
    document_empty: bool,
    ids: &[i64],
    count: i32,
    selected: i64,
    paths: &[String],
    names: &[String],
    bounds_min: [f32; 3],
    bounds_max: [f32; 3],
    error: String,
) -> SessionView {
    if ids.is_empty() {
        return SessionView {
            has_model: !document_empty,
            object: 0,
            path: String::new(),
            display_name: "no model".to_owned(),
            error,
            focus: [bounds_min[0], 0.0, -bounds_min[1]],
            radius: 0.0,
        };
    }

    let index = ids.iter().position(|id| *id == selected).unwrap_or(0);
    let object = ids[index];
    let display_name = if count > 1 {
        format!("{} objects", count)
    } else {
        names.get(index).cloned().unwrap_or_else(|| "no model".to_owned())
    };

    let extent = [
        bounds_max[0] - bounds_min[0],
        bounds_max[1] - bounds_min[1],
        bounds_max[2] - bounds_min[2],
    ];
    let square = extent[0] * extent[0] + extent[1] * extent[1] + extent[2] * extent[2];

    SessionView {
        has_model: true,
        object,
        path: paths.get(index).cloned().unwrap_or_default(),
        display_name,
        error,
        focus: [
            (bounds_min[0] + bounds_max[0]) * 0.5,
            (bounds_min[2] + bounds_max[2]) * 0.5,
            -(bounds_min[1] + bounds_max[1]) * 0.5,
        ],
        radius: 0.5 * f64::from(square).sqrt(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn paths(values: &[&str]) -> Vec<String> {
        values.iter().map(|value| (*value).to_owned()).collect()
    }

    #[test]
    fn an_empty_plate_reads_no_model_but_reports_the_documents_emptiness() {
        let view = session_view(
            false,
            &[],
            0,
            0,
            &[],
            &[],
            [10.0, 20.0, 0.0],
            [230.0, 240.0, 210.0],
            String::new(),
        );
        assert!(view.has_model);
        assert_eq!(view.object, 0);
        assert_eq!(view.display_name, "no model");
        assert_eq!(view.focus, [10.0, 0.0, -20.0]);
        assert_eq!(view.radius, 0.0);
    }

    #[test]
    fn an_empty_document_reports_no_model() {
        let view = session_view(
            true,
            &[],
            0,
            0,
            &[],
            &[],
            [0.0; 3],
            [10.0; 3],
            String::new(),
        );
        assert!(!view.has_model);
        assert_eq!(view.display_name, "no model");
    }

    #[test]
    fn the_focus_negates_y_and_averages_z_the_printer_way() {
        let view = session_view(
            false,
            &[7],
            1,
            7,
            &paths(&["/models/cube.stl"]),
            &paths(&["cube"]),
            [0.0, 0.0, 0.0],
            [20.0, 40.0, 10.0],
            String::new(),
        );
        assert_eq!(view.focus, [10.0, 5.0, -20.0]);
        assert!((view.radius - 0.5 * 2100.0f64.sqrt()).abs() < 1e-9);
    }

    #[test]
    fn several_objects_read_as_a_count_and_the_selected_one_names_the_path() {
        let view = session_view(
            false,
            &[3, 9],
            2,
            9,
            &paths(&["/models/a.stl", "/models/b.stl"]),
            &paths(&["a", "b"]),
            [0.0; 3],
            [10.0; 3],
            String::new(),
        );
        assert_eq!(view.object, 9);
        assert_eq!(view.path, "/models/b.stl");
        assert_eq!(view.display_name, "2 objects");
    }

    #[test]
    fn a_selection_on_another_plate_falls_back_to_the_first_object() {
        let view = session_view(
            false,
            &[4, 5],
            2,
            99,
            &paths(&["/models/a.stl", "/models/b.stl"]),
            &paths(&["a", "b"]),
            [0.0; 3],
            [1.0; 3],
            String::new(),
        );
        assert_eq!(view.object, 4);
        assert_eq!(view.path, "/models/a.stl");
    }
}
