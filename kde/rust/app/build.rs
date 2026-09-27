use cxx_qt_build::CxxQtBuilder;
use std::process::Command;

fn main() {
    // Build Go protocol c-archive first
    let protocol_dir = std::path::PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .unwrap()
        .parent()
        .unwrap()
        .parent()
        .unwrap()
        .join("protocol")
        .join("cgo");

    let output = std::path::PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .unwrap()
        .join("target")
        .join("libflowgrid_protocol.a");

    if !output.exists() {
        let status = Command::new("go")
            .args([
                "build",
                "-buildmode=c-archive",
                "-o",
                output.to_str().unwrap(),
            ])
            .current_dir(&protocol_dir)
            .status()
            .expect("Failed to build Go protocol");
        assert!(status.success(), "Go protocol build failed");
    }

    // Build cxx-qt bridge
    CxxQtBuilder::new()
        .file("../cxx-qt-bridge/src/lib.rs")
        .build();
}
