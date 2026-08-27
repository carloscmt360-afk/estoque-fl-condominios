//! Adoção dos dados de uma instalação anterior.
//!
//! O app é portátil: banco, imagens e anexos ficam em pastas ao lado do
//! executável (ver portable_paths.cpp), não em AppData. Isso é ótimo pra rodar
//! de pendrive, mas cria um problema quando o nome do produto muda: o
//! instalador do Windows passa a usar OUTRA pasta, e a instalação nova nasce
//! vazia enquanto todos os dados continuam na pasta antiga. Para o usuário
//! parece que o sistema apagou tudo.
//!
//! Foi o que aconteceu ao renomear "Estoque FL Condomínios" para "Gestão de
//! Suprimentos - FL". Este módulo cobre essa passagem sozinho: na primeira vez
//! que a instalação nova sobe sem banco, procura a antiga nos lugares onde o
//! instalador poderia tê-la deixado e traz os dados.
//!
//! Duas decisões que valem ser explícitas:
//!
//! - **Copia, nunca move.** A instalação antiga fica intacta como backup. Se
//!   algo der errado aqui, o usuário não perdeu nada — basta abrir o programa
//!   antigo de novo.
//! - **O banco é o ÚLTIMO arquivo a aparecer no lugar final**, gravado com
//!   nome temporário e só então renomeado. Como a própria existência de
//!   `dados/estoque.db` é o que sinaliza "já migrei", uma cópia interrompida
//!   no meio (falta de espaço, queda de energia) não deixa um banco pela
//!   metade passando por completo: na próxima abertura a migração roda de novo.

use std::fs;
use std::path::{Path, PathBuf};

/// Nomes que o instalador já usou como pasta do produto, do mais recente para
/// o mais antigo. Ao renomear o produto de novo, acrescente o nome anterior
/// AQUI — é só isto que a migração precisa saber.
const NOMES_ANTERIORES: &[&str] = &["Estoque FL Condomínios"];

/// Pastas que compõem os dados do usuário, todas irmãs dentro da base
/// portátil. `dados` sai por último de propósito (ver nota do módulo).
const PASTAS_DE_DADOS: &[&str] = &["imagens", "anexos", "dados"];

const ARQUIVO_BANCO: &str = "estoque.db";

/// Resultado de uma migração bem-sucedida, só para o chamador poder registrar
/// no log de onde os dados vieram.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Migracao {
    pub origem: PathBuf,
    pub arquivos_copiados: usize,
}

/// Traz os dados de uma instalação anterior para `base_nova`, se houver uma e
/// se a nova ainda estiver vazia.
///
/// `base_nova` é a pasta do executável (a que contém `dados/`).
///
/// Devolve `Ok(None)` quando não havia nada a fazer — que é o caso comum, em
/// toda abertura depois da primeira. Erro aqui NUNCA deve impedir o app de
/// abrir: o chamador registra e segue com o banco vazio, porque os dados
/// antigos continuam onde sempre estiveram.
pub fn migrar_instalacao_anterior(base_nova: &Path) -> Result<Option<Migracao>, String> {
    // A instalação nova já tem banco: ou já migramos, ou o usuário já começou
    // a usar. Em nenhum dos dois casos podemos sobrescrever nada.
    if base_nova.join("dados").join(ARQUIVO_BANCO).is_file() {
        return Ok(None);
    }

    let origem = match encontrar_instalacao_anterior(base_nova) {
        Some(p) => p,
        None => return Ok(None),
    };

    let mut copiados = 0usize;
    for pasta in PASTAS_DE_DADOS {
        let de = origem.join(pasta);
        if !de.is_dir() {
            continue;
        }
        copiados += copiar_pasta(&de, &base_nova.join(pasta), pasta == &"dados")?;
    }

    Ok(Some(Migracao { origem, arquivos_copiados: copiados }))
}

/// Onde o instalador do Windows pode ter deixado a versão anterior:
/// ao lado da pasta atual (as duas instalações caem no mesmo lugar — o caso
/// normal, seja em %LOCALAPPDATA% ou em Arquivos de Programas), e nas raízes
/// conhecidas, para o caso de a instalação ter mudado de lugar entre as
/// versões.
fn encontrar_instalacao_anterior(base_nova: &Path) -> Option<PathBuf> {
    let mut raizes: Vec<PathBuf> = Vec::new();
    if let Some(pai) = base_nova.parent() {
        raizes.push(pai.to_path_buf());
    }
    for var in ["LOCALAPPDATA", "PROGRAMFILES", "ProgramFiles(x86)", "APPDATA"] {
        if let Ok(v) = std::env::var(var) {
            if !v.is_empty() {
                raizes.push(PathBuf::from(v));
            }
        }
    }

    for raiz in raizes {
        for nome in NOMES_ANTERIORES {
            let candidato = raiz.join(nome);
            // mesma pasta que a atual não é "instalação anterior" nenhuma
            if mesma_pasta(&candidato, base_nova) {
                continue;
            }
            if candidato.join("dados").join(ARQUIVO_BANCO).is_file() {
                return Some(candidato);
            }
        }
    }
    None
}

fn mesma_pasta(a: &Path, b: &Path) -> bool {
    match (fs::canonicalize(a), fs::canonicalize(b)) {
        (Ok(ca), Ok(cb)) => ca == cb,
        _ => a == b,
    }
}

/// Cópia recursiva. Nunca sobrescreve arquivo que já exista no destino: se a
/// instalação nova já tem alguma coisa, o que está lá é mais novo que o que
/// veio da antiga.
///
/// Com `banco_por_ultimo`, o `estoque.db` é gravado com sufixo temporário e só
/// renomeado no fim, depois que todo o resto da pasta já foi copiado (ver nota
/// do módulo).
fn copiar_pasta(de: &Path, para: &Path, banco_por_ultimo: bool) -> Result<usize, String> {
    fs::create_dir_all(para).map_err(|e| format!("criar '{}': {e}", para.display()))?;

    let mut copiados = 0usize;
    let entradas = fs::read_dir(de).map_err(|e| format!("ler '{}': {e}", de.display()))?;
    let mut banco: Option<PathBuf> = None;

    for entrada in entradas {
        let entrada = entrada.map_err(|e| format!("ler '{}': {e}", de.display()))?;
        let origem = entrada.path();
        let nome = entrada.file_name();
        let destino = para.join(&nome);

        if origem.is_dir() {
            copiados += copiar_pasta(&origem, &destino, false)?;
            continue;
        }
        if banco_por_ultimo && nome == ARQUIVO_BANCO {
            banco = Some(origem);
            continue;
        }
        if destino.exists() {
            continue;
        }
        fs::copy(&origem, &destino)
            .map_err(|e| format!("copiar '{}': {e}", origem.display()))?;
        copiados += 1;
    }

    if let Some(origem) = banco {
        let parcial = para.join(format!("{ARQUIVO_BANCO}.migrando"));
        let _ = fs::remove_file(&parcial); // sobra de uma tentativa interrompida
        fs::copy(&origem, &parcial)
            .map_err(|e| format!("copiar '{}': {e}", origem.display()))?;
        fs::rename(&parcial, para.join(ARQUIVO_BANCO))
            .map_err(|e| format!("concluir a cópia do banco: {e}"))?;
        copiados += 1;
    }

    Ok(copiados)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn tmp_dir(name: &str) -> PathBuf {
        let mut p = std::env::temp_dir();
        p.push(format!("estoque_migracao_test_{name}_{}", std::process::id()));
        let _ = fs::remove_dir_all(&p);
        fs::create_dir_all(&p).unwrap();
        p
    }

    /// Monta uma instalação com banco, uma imagem e um anexo.
    fn instalacao_antiga(raiz: &Path, nome: &str) -> PathBuf {
        let base = raiz.join(nome);
        fs::create_dir_all(base.join("dados")).unwrap();
        fs::create_dir_all(base.join("imagens/produtos/CMT000001")).unwrap();
        fs::create_dir_all(base.join("anexos/aquisicoes/aq_001")).unwrap();
        fs::write(base.join("dados").join(ARQUIVO_BANCO), b"banco antigo").unwrap();
        fs::write(base.join("imagens/produtos/CMT000001/1.webp"), b"imagem").unwrap();
        fs::write(base.join("anexos/aquisicoes/aq_001/nota.pdf"), b"%PDF").unwrap();
        base
    }

    #[test]
    fn traz_banco_imagens_e_anexos_da_instalacao_anterior() {
        let raiz = tmp_dir("adota");
        instalacao_antiga(&raiz, "Estoque FL Condomínios");
        let nova = raiz.join("Gestão de Suprimentos - FL");
        fs::create_dir_all(nova.join("dados")).unwrap();

        let r = migrar_instalacao_anterior(&nova).unwrap().expect("devia migrar");
        assert!(r.origem.ends_with("Estoque FL Condomínios"));
        assert_eq!(r.arquivos_copiados, 3);

        assert_eq!(fs::read(nova.join("dados").join(ARQUIVO_BANCO)).unwrap(), b"banco antigo");
        assert_eq!(fs::read(nova.join("imagens/produtos/CMT000001/1.webp")).unwrap(), b"imagem");
        assert_eq!(fs::read(nova.join("anexos/aquisicoes/aq_001/nota.pdf")).unwrap(), b"%PDF");
    }

    #[test]
    fn a_instalacao_antiga_continua_intacta_depois_de_migrar() {
        let raiz = tmp_dir("naomove");
        let antiga = instalacao_antiga(&raiz, "Estoque FL Condomínios");
        let nova = raiz.join("Gestão de Suprimentos - FL");
        fs::create_dir_all(nova.join("dados")).unwrap();

        migrar_instalacao_anterior(&nova).unwrap().unwrap();

        assert!(antiga.join("dados").join(ARQUIVO_BANCO).is_file(), "o banco antigo é backup");
        assert!(antiga.join("imagens/produtos/CMT000001/1.webp").is_file());
    }

    #[test]
    fn nao_faz_nada_se_a_instalacao_nova_ja_tem_banco() {
        let raiz = tmp_dir("jatem");
        instalacao_antiga(&raiz, "Estoque FL Condomínios");
        let nova = raiz.join("Gestão de Suprimentos - FL");
        fs::create_dir_all(nova.join("dados")).unwrap();
        fs::write(nova.join("dados").join(ARQUIVO_BANCO), b"banco EM USO").unwrap();

        assert_eq!(migrar_instalacao_anterior(&nova).unwrap(), None);
        // o banco em uso não pode ter sido tocado
        assert_eq!(fs::read(nova.join("dados").join(ARQUIVO_BANCO)).unwrap(), b"banco EM USO");
    }

    #[test]
    fn nao_faz_nada_quando_nao_existe_instalacao_anterior() {
        let raiz = tmp_dir("primeira");
        let nova = raiz.join("Gestão de Suprimentos - FL");
        fs::create_dir_all(nova.join("dados")).unwrap();

        assert_eq!(migrar_instalacao_anterior(&nova).unwrap(), None);
        assert!(!nova.join("dados").join(ARQUIVO_BANCO).exists());
    }

    /// Pasta antiga existindo mas SEM banco não é instalação a migrar — é
    /// resto de desinstalação, e adotá-la criaria uma pasta "dados" vazia que
    /// faria a migração se dar por concluída sem nunca ter trazido nada.
    #[test]
    fn ignora_pasta_antiga_sem_banco() {
        let raiz = tmp_dir("vazia");
        fs::create_dir_all(raiz.join("Estoque FL Condomínios/dados")).unwrap();
        let nova = raiz.join("Gestão de Suprimentos - FL");
        fs::create_dir_all(nova.join("dados")).unwrap();

        assert_eq!(migrar_instalacao_anterior(&nova).unwrap(), None);
    }

    #[test]
    fn nao_sobrescreve_arquivo_que_ja_existe_na_nova() {
        let raiz = tmp_dir("naosobrescreve");
        instalacao_antiga(&raiz, "Estoque FL Condomínios");
        let nova = raiz.join("Gestão de Suprimentos - FL");
        fs::create_dir_all(nova.join("imagens/produtos/CMT000001")).unwrap();
        fs::write(nova.join("imagens/produtos/CMT000001/1.webp"), b"ja estava aqui").unwrap();

        migrar_instalacao_anterior(&nova).unwrap().unwrap();
        assert_eq!(fs::read(nova.join("imagens/produtos/CMT000001/1.webp")).unwrap(),
                   b"ja estava aqui");
    }

    /// A base nova sendo a MESMA pasta da antiga (usuário que só trocou o exe
    /// de lugar, sem instalador) não pode disparar uma cópia de si mesma.
    #[test]
    fn nao_migra_de_si_mesma() {
        let raiz = tmp_dir("mesma");
        let base = instalacao_antiga(&raiz, "Estoque FL Condomínios");
        // remove o banco pra passar do primeiro guarda e chegar na busca
        fs::remove_file(base.join("dados").join(ARQUIVO_BANCO)).unwrap();
        assert_eq!(migrar_instalacao_anterior(&base).unwrap(), None);
    }
}
