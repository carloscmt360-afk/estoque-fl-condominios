// Valida a ponte inteira (Rust -> cxx -> C++ -> SQLite) rodando de verdade
// neste ambiente Linux, sem tocar em Tauri/WebKitGTK — importa o backup real
// do usuário e confere que o relatório reproduz os números já validados
// manualmente contra a planilha original (R$ 56.664,37 em estoque,
// R$ 10.790,36 de consumo em junho/2026, 67 pedidos).
//
// `--dump-fixtures <pasta>`: além do teste, grava backup/relatório/produtos/
// departamentos em JSON na pasta indicada — usado para regenerar
// frontend/fixtures/ (dados REAIS, nunca versionados — ver fixtures/README.md)
// quando for iterar no mock de desenvolvimento do frontend.
use bridge::ffi;
use std::fs;
use std::path::PathBuf;

const BACKUP_PATH: &str = "/home/cruz/Área de trabalho/Projetos/Carlos FL/carga_inicial_ref_jun26.json";

fn main() {
    let dump_dir = std::env::args()
        .collect::<Vec<_>>()
        .windows(2)
        .find(|w| w[0] == "--dump-fixtures")
        .map(|w| w[1].clone());

    if let Err(e) = run(dump_dir.as_deref()) {
        eprintln!("[core-cli] FALHOU — {e}");
        std::process::exit(1);
    }
}

fn run(dump_dir: Option<&str>) -> Result<(), String> {
    // Armazenamento portátil: dados/ criado numa pasta temporária que simula
    // "ao lado do executável" (o resolvedor de produção usa o diretório real
    // do binário — testado via resolveDataDir(exeDir) em core-cpp/tests).
    let mut data_dir = std::env::temp_dir();
    data_dir.push("estoque_core_cli_demo");
    fs::create_dir_all(&data_dir).map_err(|e| e.to_string())?;
    let db_path: PathBuf = data_dir.join("estoque.db");
    let _ = fs::remove_file(&db_path); // roda limpo a cada chamada

    println!("[core-cli] banco: {}", db_path.display());
    let mut session = ffi::open_session(db_path.to_str().unwrap()).map_err(|e| e.to_string())?;
    println!("[core-cli] sessão aberta (Rust -> cxx -> C++ -> SQLite) OK");

    let payload = fs::read_to_string(BACKUP_PATH).map_err(|e| format!("lendo backup: {e}"))?;
    session.as_mut().unwrap().restore_from_json(&payload).map_err(|e| e.to_string())?;
    println!("[core-cli] backup importado: {BACKUP_PATH}");

    let report_str = session
        .as_mut()
        .unwrap()
        .compute_report_json(2026, 5, "", 6, "2026-07-30T12:00:00.000Z")
        .map_err(|e| e.to_string())?;
    let report: serde_json::Value = serde_json::from_str(&report_str).map_err(|e| e.to_string())?;

    let valor_estoque = report["valorTotal"].as_f64().unwrap_or(f64::NAN);
    let consumo = report["kpi"]["consumo"].as_f64().unwrap_or(f64::NAN);
    let pedidos = report["kpi"]["pedidos"].as_i64().unwrap_or(-1);

    println!("[core-cli] valor em estoque : R$ {valor_estoque:.2}   (esperado R$ 56664.37)");
    println!("[core-cli] consumo de junho : R$ {consumo:.2}   (esperado R$ 10790.36)");
    println!("[core-cli] pedidos          : {pedidos}   (esperado 67)");

    let close = |a: f64, b: f64| (a - b).abs() < 0.01;
    if !close(valor_estoque, 56664.37) || !close(consumo, 10790.36) || pedidos != 67 {
        return Err("os números NÃO batem com o relatório oficial já validado".to_string());
    }
    println!("[core-cli] validado — ponte completa confere com dados reais do usuário");

    if let Some(dir) = dump_dir {
        fs::create_dir_all(dir).map_err(|e| e.to_string())?;
        fs::write(format!("{dir}/backup.json"), &payload).map_err(|e| e.to_string())?;
        fs::write(format!("{dir}/report_2026_06.json"), &report_str).map_err(|e| e.to_string())?;
        let products = session.as_mut().unwrap().list_products_json().map_err(|e| e.to_string())?;
        fs::write(format!("{dir}/products.json"), &products).map_err(|e| e.to_string())?;
        let depts = session.as_mut().unwrap().list_departments_json().map_err(|e| e.to_string())?;
        fs::write(format!("{dir}/departments.json"), &depts).map_err(|e| e.to_string())?;
        println!("[core-cli] fixtures gravadas em {dir} (contêm dados reais — nunca versionar)");
    }

    Ok(())
}
