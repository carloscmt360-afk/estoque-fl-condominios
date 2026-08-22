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

    // A lista é explícita (e não um glob) para que entrar um arquivo novo em
    // core-cpp/src seja uma decisão consciente. O preço disso é esquecer de
    // acrescentar um: como o núcleo vira uma biblioteca ESTÁTICA, a falta só
    // aparece no LINK do binário final — e, no MSVC, muitas vezes só no CI,
    // depois de dez minutos de build (foi o que aconteceu com dates_engine.cpp).
    // A conferência abaixo transforma esse erro tardio e obscuro num erro de
    // build imediato e explícito.
    const CORE_SOURCES: &[&str] = &[
        "models.cpp",
        "db.cpp",
        "time_utils.cpp",
        "crypto.cpp",
        "inventory_engine.cpp",
        "report_engine.cpp",
        "retrospect_engine.cpp",
        "auth_engine.cpp",
        "request_engine.cpp",
        "dates_engine.cpp",
        "companies_engine.cpp",
        "managers_engine.cpp",
        "commissions_engine.cpp",
        "purchases_engine.cpp",
        "suprimentos_engine.cpp",
        "portable_paths.cpp",
        "api.cpp",
    ];

    let src_dir = core_cpp.join("src");
    let mut nao_listados: Vec<String> = std::fs::read_dir(&src_dir)
        .expect("não foi possível ler core-cpp/src")
        .filter_map(|e| e.ok())
        .filter_map(|e| e.file_name().into_string().ok())
        .filter(|n| n.ends_with(".cpp") && !CORE_SOURCES.contains(&n.as_str()))
        .collect();
    if !nao_listados.is_empty() {
        nao_listados.sort();
        panic!(
            "core-cpp/src tem arquivo(s) .cpp fora da lista CORE_SOURCES de bridge/build.rs: {}.\n\
             Acrescente-o(s) ali — senão o símbolo some no link do executável final \
             (LNK1120/undefined reference), e não aqui.",
            nao_listados.join(", ")
        );
    }

    for src in CORE_SOURCES {
        bridge_build.file(src_dir.join(src));
    }
    // "-std=c++17" é sintaxe GCC/Clang; o MSVC (cl.exe) não reconhece essa
    // flag e a descarta silenciosamente via flag_if_supported, compilando em
    // modo pré-C++17 (o que quebra `namespace a::b { ... }` no shim — MSVC
    // exige "/std:c++17" explicitamente para nested-namespace-definition).
    let target_env = std::env::var("CARGO_CFG_TARGET_ENV").unwrap_or_default();
    if target_env == "msvc" {
        bridge_build.flag("/std:c++17");
    } else {
        bridge_build.flag_if_supported("-std=c++17");
    }
    bridge_build.warnings(true);
    bridge_build.compile("estoque_bridge");

    println!("cargo:rerun-if-changed={}", cpp_dir.display());

    // pthread/dl são bibliotecas do mundo Unix — não existem no MSVC (LNK1181
    // "cannot open input file 'pthread.lib'"). No Windows o runtime C++ já
    // embute as primitivas de thread; nada equivalente a linkar.
    if target_env != "msvc" {
        println!("cargo:rustc-link-lib=pthread");
        println!("cargo:rustc-link-lib=dl");
    }

    println!("cargo:rerun-if-changed=src/lib.rs");
    println!("cargo:rerun-if-changed={}", core_cpp.join("src").display());
    println!("cargo:rerun-if-changed={}", include_dir.display());
}
