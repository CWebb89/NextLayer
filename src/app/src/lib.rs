//! The application backend, in Rust, behind the QML front end.
//!
//! QML keeps its own spelling for the objects it reads; every object it reaches
//! is declared here as a QML singleton in the `NextLayer` module. The module's
//! types are registered by `register_qml_types`, which the entry point calls
//! once before the engine loads anything.

#[cxx_qt::bridge]
pub mod qobject {
    unsafe extern "C++" {
        include!("cxx-qt-lib/qstring.h");
        type QString = cxx_qt_lib::QString;
    }

    unsafe extern "RustQt" {
        #[qobject]
        #[qml_element = "Runtime"]
        #[qml_singleton]
        #[qproperty(QString, version)]
        type Runtime = super::RuntimeRust;
    }

    impl cxx_qt::Initialize for Runtime {}
}

use core::pin::Pin;
use cxx_qt_lib::QString;

/// The resources tree this build falls back to when none is found at runtime.
const BAKED_RESOURCES_DIR: &str = env!("NEXT_LAYER_RESOURCES_DIR");

/// The version this build reports: the application's, not the crate's.
const BAKED_VERSION: &str = env!("NEXT_LAYER_VERSION");

#[allow(non_snake_case)]
pub mod GcodeBrand;
#[allow(non_snake_case)]
pub mod Resources;
#[allow(non_snake_case)]
pub mod Scratch;
pub mod bridge;
pub mod hfw;

#[derive(Default)]
pub struct RuntimeRust {
    version: QString,
}

impl cxx_qt::Initialize for qobject::Runtime {
    fn initialize(mut self: Pin<&mut Self>) {
        self.as_mut().set_version(QString::from(BAKED_VERSION));
    }
}
