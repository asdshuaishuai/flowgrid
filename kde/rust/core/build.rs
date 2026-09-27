fn main() {
    // Point to the Go cgo c-archive in the protocol directory
    // CARGO_MANIFEST_DIR is kde/rust/core
    let protocol_dir = std::path::PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .unwrap() // kde/rust
        .parent()
        .unwrap() // kde
        .parent()
        .unwrap() // flowgrid repo root
        .join("protocol")
        .join("cgo");
    println!(
        "cargo:rustc-link-search=native={}",
        protocol_dir.display()
    );
}
