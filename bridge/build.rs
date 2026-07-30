use std::path::PathBuf;

fn main() {
    let manifest_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    let core_cpp = manifest_dir.join("..").join("core-cpp");
    let include_dir = core_cpp.join("include");
    let third_party_dir = core_cpp.join("third_party");  // nlohmann/json.hpp, doctest.h
    let sqlite_dir = third_party_dir.join("sqlite");

    // O amálgama do SQLite usa idiomas válidos em C mas ilegais em C++ (ex.:
    // nome de struct declarado só dentro de uma função, usado como tipo antes
    // da definição completa) — precisa ser compilado como C, nunca como C++.
    // Confirmado por smoke test manual antes de automatizar isso aqui.
    cc::Build::new()
        .file(sqlite_dir.join("sqlite3.c"))
        .include(&sqlite_dir)
        .flag_if_supported("-w") // amálgama de terceiros: não é nosso código para lintar
        .define("SQLITE_THREADSAFE", "1")
        .define("SQLITE_ENABLE_JSON1", None)
        .opt_level(2)
        .warnings(false)
        .compile("sqlite3_vendored");

    // Ponte cxx: o shim (bridge/cpp/) + o núcleo C++ do domínio (core-cpp/),
    // compilados juntos como C++17.
    let cpp_dir = manifest_dir.join("cpp");
    let mut bridge_build = cxx_build::bridge("src/lib.rs");
    bridge_build
        .include(&cpp_dir)
        .include(&include_dir)
        .include(&third_party_dir)
        .include(&sqlite_dir)
        .file(cpp_dir.join("shim.cpp"));
    for src in [
        "models.cpp",
        "db.cpp",
        "time_utils.cpp",
        "inventory_engine.cpp",
        "report_engine.cpp",
        "portable_paths.cpp",
        "api.cpp",
    ] {
        bridge_build.file(core_cpp.join("src").join(src));
    }
    bridge_build
        .flag_if_supported("-std=c++17")
        .warnings(true);
    bridge_build.compile("estoque_bridge");

    println!("cargo:rerun-if-changed={}", cpp_dir.display());

    println!("cargo:rustc-link-lib=pthread");
    println!("cargo:rustc-link-lib=dl");

    println!("cargo:rerun-if-changed=src/lib.rs");
    println!("cargo:rerun-if-changed={}", core_cpp.join("src").display());
    println!("cargo:rerun-if-changed={}", include_dir.display());
}
