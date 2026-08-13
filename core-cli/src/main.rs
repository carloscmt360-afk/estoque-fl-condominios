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
//
// `--db <caminho>`: cria/atualiza o banco SQLite exatamente nesse caminho em
// vez da pasta temporária padrão — usado para popular de verdade uma pasta
// dados/ de produção (ex.: ao lado de onde o .exe vai rodar).
//
// `--import-history <arquivo.json>`: importa o histórico de custo por
// departamento (aba RESTROSPECTO / CUSTO POR DEPTO da planilha) para o banco
// indicado em --db e SAI, sem tocar em produtos/movimentações. É de propósito
// um modo exclusivo: o fluxo normal deste binário começa apagando tudo com
// restore_from_json, o que destruiria um banco de produção.
//
// `--retrospect <ano>`: imprime os totais do retrospecto do ano no banco de
// --db e sai — usado para conferir a importação contra a planilha. Combinado
// com `--dump-fixtures <pasta>`, grava também a fixture do retrospecto (que
// precisa vir de um banco COM histórico importado, e não do banco temporário
// do modo padrão).
use bridge::ffi;
use std::fs;
use std::path::PathBuf;

const BACKUP_PATH: &str = "/home/cruz/Área de trabalho/Projetos/Carlos FL/carga_inicial_ref_jun26.json";

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let dump_dir = args.windows(2).find(|w| w[0] == "--dump-fixtures").map(|w| w[1].clone());
    let db_override = args.windows(2).find(|w| w[0] == "--db").map(|w| w[1].clone());
    let import_history = args.windows(2).find(|w| w[0] == "--import-history").map(|w| w[1].clone());
    let retrospect = args.windows(2).find(|w| w[0] == "--retrospect").map(|w| w[1].clone());

    let result = match (import_history, retrospect) {
        (Some(path), _) => import_history_only(db_override.as_deref(), &path),
        (None, Some(ano)) => retrospect_only(db_override.as_deref(), &ano, dump_dir.as_deref()),
        (None, None) => run(dump_dir.as_deref(), db_override.as_deref()),
    };
    if let Err(e) = result {
        eprintln!("[core-cli] FALHOU — {e}");
        std::process::exit(1);
    }
}

fn require_db(db_override: Option<&str>) -> Result<PathBuf, String> {
    db_override
        .map(PathBuf::from)
        .ok_or_else(|| "este modo exige --db <caminho do estoque.db>".to_string())
}

fn import_history_only(db_override: Option<&str>, payload_path: &str) -> Result<(), String> {
    let db_path = require_db(db_override)?;
    println!("[core-cli] banco: {}", db_path.display());
    let mut session = ffi::open_session(db_path.to_str().unwrap()).map_err(|e| e.to_string())?;
    // Sessão de serviço: este binário opera direto no arquivo do banco, onde
    // autenticação não protege nada (quem tem o arquivo já tem tudo) e exigir
    // senha só criaria uma credencial embutida no código. Ver api.hpp.
    session.as_mut().unwrap().login_as_service("core-cli").map_err(|e| e.to_string())?;
    let payload = fs::read_to_string(payload_path).map_err(|e| format!("lendo histórico: {e}"))?;
    let gravadas = session
        .as_mut()
        .unwrap()
        .import_dept_cost_history(&payload)
        .map_err(|e| e.to_string())?;
    println!("[core-cli] histórico importado: {gravadas} linha(s) de {payload_path}");
    Ok(())
}

fn retrospect_only(db_override: Option<&str>, ano: &str, dump_dir: Option<&str>) -> Result<(), String> {
    let db_path = require_db(db_override)?;
    let year: i32 = ano.parse().map_err(|_| format!("ano inválido: {ano}"))?;
    let mut session = ffi::open_session(db_path.to_str().unwrap()).map_err(|e| e.to_string())?;
    // Sessão de serviço: este binário opera direto no arquivo do banco, onde
    // autenticação não protege nada (quem tem o arquivo já tem tudo) e exigir
    // senha só criaria uma credencial embutida no código. Ver api.hpp.
    session.as_mut().unwrap().login_as_service("core-cli").map_err(|e| e.to_string())?;
    let raw = session
        .as_mut()
        .unwrap()
        .compute_retrospect_json(year, "auto", "2026-08-06T12:00:00.000Z")
        .map_err(|e| e.to_string())?;
    let r: serde_json::Value = serde_json::from_str(&raw).map_err(|e| e.to_string())?;

    println!("[core-cli] retrospecto {year} (banco: {})", db_path.display());
    let meses = ["JAN", "FEV", "MAR", "ABR", "MAI", "JUN", "JUL", "AGO", "SET", "OUT", "NOV", "DEZ"];
    for (i, m) in meses.iter().enumerate() {
        let v = r["ref"]["totaisMes"][i].as_f64().unwrap_or(0.0);
        let origem = r["ref"]["origem"][i].as_str().unwrap_or("");
        if !origem.is_empty() {
            println!("  {m} {v:>12.2}   ({origem})");
        }
    }
    println!("  TOTAL {:>10.2}", r["ref"]["total"].as_f64().unwrap_or(0.0));
    println!("  ano anterior ({}): {:.2}", r["prevYear"], r["ant"]["total"].as_f64().unwrap_or(0.0));
    for l in r["ref"]["linhas"].as_array().map(|v| v.as_slice()).unwrap_or(&[]) {
        println!("    {:<24} {:>12.2}", l["name"].as_str().unwrap_or(""), l["total"].as_f64().unwrap_or(0.0));
    }

    if let Some(dir) = dump_dir {
        fs::create_dir_all(dir).map_err(|e| e.to_string())?;
        let destino = format!("{dir}/retrospect_{year}.json");
        fs::write(&destino, &raw).map_err(|e| e.to_string())?;
        println!("[core-cli] fixture gravada em {destino} (dados reais — nunca versionar)");
    }
    Ok(())
}

fn run(dump_dir: Option<&str>, db_override: Option<&str>) -> Result<(), String> {
    // Armazenamento portátil: por padrão, dados/ numa pasta temporária que
    // simula "ao lado do executável" (o resolvedor de produção usa o
    // diretório real do binário — testado via resolveDataDir(exeDir) em
    // core-cpp/tests). --db permite apontar para um caminho real.
    let db_path: PathBuf = match db_override {
        Some(p) => PathBuf::from(p),
        None => {
            let mut data_dir = std::env::temp_dir();
            data_dir.push("estoque_core_cli_demo");
            fs::create_dir_all(&data_dir).map_err(|e| e.to_string())?;
            let p = data_dir.join("estoque.db");
            let _ = fs::remove_file(&p); // roda limpo a cada chamada
            p
        }
    };
    if let Some(parent) = db_path.parent() {
        fs::create_dir_all(parent).map_err(|e| e.to_string())?;
    }

    println!("[core-cli] banco: {}", db_path.display());
    let mut session = ffi::open_session(db_path.to_str().unwrap()).map_err(|e| e.to_string())?;
    // Sessão de serviço: este binário opera direto no arquivo do banco, onde
    // autenticação não protege nada (quem tem o arquivo já tem tudo) e exigir
    // senha só criaria uma credencial embutida no código. Ver api.hpp.
    session.as_mut().unwrap().login_as_service("core-cli").map_err(|e| e.to_string())?;
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
        // A fixture do retrospecto NÃO sai daqui: este banco é temporário e não
        // tem o histórico da planilha importado, então sairia vazia e daria a
        // impressão errada de que a tela não funciona. Gere-a com
        // `--db <banco real> --retrospect <ano> --dump-fixtures <pasta>`.
        println!("[core-cli] fixtures gravadas em {dir} (contêm dados reais — nunca versionar)");
    }

    Ok(())
}
