//! The application backend, in Rust, behind the QML front end.
//!
//! QML keeps its own spelling for the objects it reads; every object it reaches
//! is declared here as a QML singleton in the `NextLayer` module. The module's
//! types are registered by `register_qml_types`, which the entry point calls
//! once before the engine loads anything.

#[cxx_qt::bridge(auto_cxx_name = "camel_case")]
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

        #[qobject]
        #[qml_element = "Updater"]
        #[qml_singleton]
        #[qproperty(QString, status)]
        #[qproperty(QString, availableVersion)]
        #[qproperty(QString, notes)]
        #[qproperty(bool, busy)]
        #[qproperty(bool, downloaded)]
        #[qproperty(bool, canInstall)]
        #[qproperty(QString, channel)]
        type Updater = super::UpdaterRust;

        #[qinvokable]
        fn check(self: Pin<&mut Updater>);
        #[qinvokable]
        fn download(self: Pin<&mut Updater>);
        #[qinvokable]
        fn install(self: Pin<&mut Updater>);
        #[qinvokable]
        fn relaunch(self: Pin<&mut Updater>);
    }

    impl cxx_qt::Initialize for Runtime {}
    impl cxx_qt::Initialize for Updater {}
    impl cxx_qt::Threading for Updater {}
}

use core::pin::Pin;
use cxx_qt_lib::QString;

/// The resources tree this build falls back to when none is found at runtime.
const BAKED_RESOURCES_DIR: &str = env!("NEXT_LAYER_RESOURCES_DIR");

/// The version this build reports: the application's, not the crate's.
const BAKED_VERSION: &str = env!("NEXT_LAYER_VERSION");

pub use crate::Updater::UpdaterRust;

#[allow(non_snake_case)]
pub mod ForeignSettings;
#[allow(non_snake_case)]
pub mod GcodeBrand;
#[allow(non_snake_case)]
pub mod MeshGeometry;
#[allow(non_snake_case)]
pub mod Palette;
#[allow(non_snake_case)]
pub mod PlateThumbnail;
#[allow(non_snake_case)]
pub mod ProjectFile;
#[allow(non_snake_case)]
pub mod Resources;
#[allow(non_snake_case)]
pub mod ThreeMfProject;
#[allow(non_snake_case)]
pub mod Scratch;
#[allow(non_snake_case)]
pub mod Updater;
pub mod base64;
pub mod bridge;
pub mod hfw;
pub mod mesh;

#[derive(Default)]
pub struct RuntimeRust {
    version: QString,
}

impl cxx_qt::Initialize for qobject::Runtime {
    fn initialize(mut self: Pin<&mut Self>) {
        self.as_mut().set_version(QString::from(BAKED_VERSION));
    }
}

impl cxx_qt::Initialize for qobject::Updater {
    fn initialize(mut self: Pin<&mut Self>) {
        self.as_mut()
            .set_channel(QString::from(Updater::channel_display()));
    }
}
