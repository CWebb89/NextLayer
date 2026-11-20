#pragma once

#include <QCoreApplication>

namespace next_layer {

/// Leaves Qt's event loop, so the teardown this application registers runs.
///
/// Inline because the only C++ left in this crate is interop: the C++ side that
/// asks Rust for an update cannot be the side that owns Qt's exit path, and a Rust
/// thread cannot unwind Qt's own teardown.
inline void quit_application()
{
    QCoreApplication::quit();
}

}
