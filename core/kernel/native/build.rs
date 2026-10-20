// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
fn main() {
    println!("cargo::rerun-if-env-changed=NF_NATIVE_LIB_DIR");
    let dir = std::env::var("NF_NATIVE_LIB_DIR")
        .expect("Build the CMake native adapter first, then set NF_NATIVE_LIB_DIR (see README.md)");
    let name = match std::env::var("CARGO_CFG_TARGET_OS").unwrap().as_str() {
        "macos" => "libnf_native_adapter.dylib",
        "linux" => "libnf_native_adapter.so",
        other => panic!("unsupported native adapter target: {other}"),
    };
    let library = std::path::Path::new(&dir).join(name);
    assert!(
        library.is_file(),
        "native adapter missing: {}",
        library.display()
    );
    println!("cargo::rerun-if-changed={}", library.display());
    println!("cargo::rustc-link-search=native={dir}");
    println!("cargo::rustc-link-lib=dylib=nf_native_adapter");
}
