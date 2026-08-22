// Processamento e armazenamento local do anexo de NF de uma Aquisição
// (Compras > Aquisições FL).
//
// Mesmo critério de bridge/src/images.rs (fotos de produto): módulo puro
// Rust, o C++ só grava o CAMINHO relativo que este módulo devolve, nunca
// decodifica/gera bytes de imagem ou PDF. A diferença para images.rs é o que
// o usuário pode anexar aqui — não só foto, mas também PDF (a NF em si,
// digitalizada ou exportada do sistema do fornecedor) — e o tamanho de
// gravação: uma foto de produto é para tela (≤1000px), um anexo de NF é para
// IMPRESSÃO, então a imagem é redimensionada para caber numa folha A4, nunca
// recortada (mesmo "nunca aumenta, só encaixa" de resize_capped).
use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};

use image::imageops::FilterType;
use image::DynamicImage;

// A4 a 150dpi (210×297mm): nítido para impressão sem gerar arquivo enorme —
// uma foto de celular (12MP+) run através disto cai para uma fração do
// tamanho original sem perder legibilidade de texto.
pub const A4_MAX_W: u32 = 1240;
pub const A4_MAX_H: u32 = 1754;
const IMAGE_QUALITY: f32 = 84.0;
// Mesmo limite de images.rs — um upload de dezenas de MB não é NF, é engano.
const MAX_INPUT_BYTES: usize = 20 * 1024 * 1024;

#[derive(Debug)]
pub struct SavedAttachment {
    pub path: String,
    /// "imagem" | "pdf" — a tela usa isto pra saber se mostra <img> ou um
    /// link/ícone de PDF, sem precisar inspecionar a extensão do arquivo.
    pub kind: String,
}

pub fn attachments_root(data_dir: &Path) -> PathBuf {
    match data_dir.parent() {
        Some(base) => base.join("anexos").join("aquisicoes"),
        None => PathBuf::from("anexos").join("aquisicoes"),
    }
}

fn app_base_dir(data_dir: &Path) -> &Path {
    data_dir.parent().unwrap_or(data_dir)
}

// Mesmo saneamento de sanitize_sku em images.rs — o id de uma Aquisição é
// gerado pelo próprio app (uid('aq_') no frontend), mas um caminho de
// arquivo em disco nunca deve confiar cegamente numa string.
fn sanitize_id(id: &str) -> String {
    let cleaned: String = id
        .chars()
        .filter(|c| c.is_ascii_alphanumeric() || *c == '_' || *c == '-')
        .collect();
    if cleaned.is_empty() {
        "_sem_id".to_string()
    } else {
        cleaned
    }
}

fn aquisicao_dir(root: &Path, aquisicao_id: &str) -> PathBuf {
    root.join(sanitize_id(aquisicao_id))
}

fn to_relative(base: &Path, path: &Path) -> String {
    path.strip_prefix(base)
        .unwrap_or(path)
        .to_string_lossy()
        .replace('\\', "/")
}

/// Nunca AUMENTA a imagem — se já cabe dentro da folha A4 nos dois eixos,
/// devolve como está. `DynamicImage::resize` encaixa DENTRO da caixa
/// preservando a proporção (não recorta, não distorce).
fn resize_to_a4(img: &DynamicImage) -> DynamicImage {
    use image::GenericImageView;
    let (w, h) = img.dimensions();
    if w <= A4_MAX_W && h <= A4_MAX_H {
        return img.clone();
    }
    img.resize(A4_MAX_W, A4_MAX_H, FilterType::Lanczos3)
}

fn write_webp_atomic(img: &DynamicImage, dest: &Path, quality: f32) -> Result<(), String> {
    let rgba = img.to_rgba8();
    let (w, h) = image::GenericImageView::dimensions(img);
    let encoded = webp::Encoder::from_rgba(&rgba, w, h).encode(quality);

    let tmp = dest.with_extension("webp.tmp");
    {
        let mut f = fs::File::create(&tmp).map_err(|e| format!("falha ao gravar anexo: {e}"))?;
        f.write_all(&encoded).map_err(|e| format!("falha ao gravar anexo: {e}"))?;
    }
    fs::rename(&tmp, dest).map_err(|e| format!("falha ao finalizar gravação do anexo: {e}"))
}

fn write_bytes_atomic(bytes: &[u8], dest: &Path) -> Result<(), String> {
    let tmp = dest.with_extension("pdf.tmp");
    {
        let mut f = fs::File::create(&tmp).map_err(|e| format!("falha ao gravar anexo: {e}"))?;
        f.write_all(bytes).map_err(|e| format!("falha ao gravar anexo: {e}"))?;
    }
    fs::rename(&tmp, dest).map_err(|e| format!("falha ao finalizar gravação do anexo: {e}"))
}

fn is_pdf(bytes: &[u8]) -> bool {
    bytes.starts_with(b"%PDF")
}

/// Processa e grava o anexo de NF de uma Aquisição: PDF é gravado como veio
/// (já é um documento pronto pra impressão, redimensionar não faz sentido);
/// imagem é redimensionada para caber numa folha A4 e convertida para WebP.
/// Sobrescreve o que já existia (troca de anexo) — a pasta só tem um
/// arquivo, nunca acumula versões antigas.
pub fn save_aquisicao_attachment(
    data_dir: &Path,
    aquisicao_id: &str,
    bytes: &[u8],
) -> Result<SavedAttachment, String> {
    if bytes.is_empty() {
        return Err("arquivo vazio.".to_string());
    }
    if bytes.len() > MAX_INPUT_BYTES {
        return Err(format!(
            "arquivo maior que {} MB — escolha um arquivo menor.",
            MAX_INPUT_BYTES / (1024 * 1024)
        ));
    }

    let dir = aquisicao_dir(&attachments_root(data_dir), aquisicao_id);
    fs::create_dir_all(&dir).map_err(|e| format!("não foi possível criar a pasta de anexos: {e}"))?;
    // Troca de anexo: remove o que existia antes de gravar o novo, pra nunca
    // sobrar um nota_fiscal.pdf de um upload antigo ao lado do .webp novo
    // (ou vice-versa) — mesmo critério de "nunca deixa arquivo órfão" de
    // images.rs.
    let _ = fs::remove_dir_all(&dir);
    fs::create_dir_all(&dir).map_err(|e| format!("não foi possível criar a pasta de anexos: {e}"))?;

    let base = app_base_dir(data_dir);
    if is_pdf(bytes) {
        let dest = dir.join("nota_fiscal.pdf");
        write_bytes_atomic(bytes, &dest)?;
        return Ok(SavedAttachment { path: to_relative(base, &dest), kind: "pdf".to_string() });
    }

    let img = image::load_from_memory(bytes)
        .map_err(|_| "arquivo não é uma imagem válida nem um PDF (use JPG, PNG, WebP ou PDF).".to_string())?;
    let dest = dir.join("nota_fiscal.webp");
    write_webp_atomic(&resize_to_a4(&img), &dest, IMAGE_QUALITY)?;
    Ok(SavedAttachment { path: to_relative(base, &dest), kind: "imagem".to_string() })
}

/// Remove a pasta inteira do anexo de uma Aquisição — usado ao trocar/
/// remover o anexo e na exclusão definitiva da Aquisição. Idempotente:
/// pasta inexistente não é erro.
pub fn delete_aquisicao_attachment(data_dir: &Path, aquisicao_id: &str) -> Result<(), String> {
    let dir = aquisicao_dir(&attachments_root(data_dir), aquisicao_id);
    if dir.exists() {
        fs::remove_dir_all(&dir).map_err(|e| format!("falha ao remover anexo antigo: {e}"))?;
    }
    Ok(())
}

/// Lê os bytes de um anexo já salvo, a partir do caminho RELATIVO gravado em
/// compras_aquisicoes.anexo_path. Mesma cautela de read_image_bytes:
/// resolve, canonicaliza e confirma que o resultado continua DENTRO de
/// anexos/aquisicoes antes de ler.
pub fn read_attachment_bytes(data_dir: &Path, relative_path: &str) -> Result<Vec<u8>, String> {
    let root = attachments_root(data_dir);
    let candidate = app_base_dir(data_dir).join(relative_path);

    let canon_root = fs::canonicalize(&root).map_err(|_| "pasta de anexos indisponível.".to_string())?;
    let canon_candidate = fs::canonicalize(&candidate).map_err(|_| "anexo não encontrado.".to_string())?;
    if !canon_candidate.starts_with(&canon_root) {
        return Err("caminho de anexo inválido.".to_string());
    }
    fs::read(&canon_candidate).map_err(|e| format!("falha ao ler anexo: {e}"))
}

// ------------------------------------------------------- propostas de orçamento
//
// Mesmo processamento (PDF como veio, imagem redimensionada pra A4 em WebP)
// das funções acima, mas com um nível a mais de pasta: uma Ordem tem N
// Propostas (uma por empresa), então o id sozinho não basta para não colidir
// — é `anexos/orcamentos/<ordem_id>/<proposta_id>/proposta.*`, e não
// `anexos/orcamentos/<proposta_id>/...` como aquisicao_dir faria.

pub fn orcamento_propostas_root(data_dir: &Path) -> PathBuf {
    match data_dir.parent() {
        Some(base) => base.join("anexos").join("orcamentos"),
        None => PathBuf::from("anexos").join("orcamentos"),
    }
}

fn proposta_dir(root: &Path, ordem_id: &str, proposta_id: &str) -> PathBuf {
    root.join(sanitize_id(ordem_id)).join(sanitize_id(proposta_id))
}

/// Processa e grava o anexo (PDF da proposta recebida, ou foto dela) de uma
/// Proposta de orçamento — mesma regra de save_aquisicao_attachment (troca
/// sobrescreve, nunca acumula versão antiga).
pub fn save_proposta_attachment(
    data_dir: &Path,
    ordem_id: &str,
    proposta_id: &str,
    bytes: &[u8],
) -> Result<SavedAttachment, String> {
    if bytes.is_empty() {
        return Err("arquivo vazio.".to_string());
    }
    if bytes.len() > MAX_INPUT_BYTES {
        return Err(format!(
            "arquivo maior que {} MB — escolha um arquivo menor.",
            MAX_INPUT_BYTES / (1024 * 1024)
        ));
    }

    let dir = proposta_dir(&orcamento_propostas_root(data_dir), ordem_id, proposta_id);
    let _ = fs::remove_dir_all(&dir);
    fs::create_dir_all(&dir).map_err(|e| format!("não foi possível criar a pasta de anexos: {e}"))?;

    let base = app_base_dir(data_dir);
    if is_pdf(bytes) {
        let dest = dir.join("proposta.pdf");
        write_bytes_atomic(bytes, &dest)?;
        return Ok(SavedAttachment { path: to_relative(base, &dest), kind: "pdf".to_string() });
    }

    let img = image::load_from_memory(bytes)
        .map_err(|_| "arquivo não é uma imagem válida nem um PDF (use JPG, PNG, WebP ou PDF).".to_string())?;
    let dest = dir.join("proposta.webp");
    write_webp_atomic(&resize_to_a4(&img), &dest, IMAGE_QUALITY)?;
    Ok(SavedAttachment { path: to_relative(base, &dest), kind: "imagem".to_string() })
}

/// Idempotente — mesma regra de delete_aquisicao_attachment.
pub fn delete_proposta_attachment(data_dir: &Path, ordem_id: &str, proposta_id: &str) -> Result<(), String> {
    let dir = proposta_dir(&orcamento_propostas_root(data_dir), ordem_id, proposta_id);
    if dir.exists() {
        fs::remove_dir_all(&dir).map_err(|e| format!("falha ao remover anexo antigo: {e}"))?;
    }
    Ok(())
}

/// Mesma cautela de read_attachment_bytes (canonicaliza e confirma que o
/// resultado continua dentro de anexos/orcamentos antes de ler) — usado
/// tanto pra mostrar a proposta na tela quanto pra anexar no e-mail
/// "Enviar para o cliente" (ver bridge/src/mailer.rs).
pub fn read_proposta_attachment_bytes(data_dir: &Path, relative_path: &str) -> Result<Vec<u8>, String> {
    let root = orcamento_propostas_root(data_dir);
    let candidate = app_base_dir(data_dir).join(relative_path);

    let canon_root = fs::canonicalize(&root).map_err(|_| "pasta de anexos indisponível.".to_string())?;
    let canon_candidate = fs::canonicalize(&candidate).map_err(|_| "anexo não encontrado.".to_string())?;
    if !canon_candidate.starts_with(&canon_root) {
        return Err("caminho de anexo inválido.".to_string());
    }
    fs::read(&canon_candidate).map_err(|e| format!("falha ao ler anexo: {e}"))
}

#[cfg(test)]
mod tests {
    use super::*;
    use image::{ImageEncoder, Rgb, RgbImage};
    use std::io::Cursor;

    fn tmp_dir(name: &str) -> PathBuf {
        let mut p = std::env::temp_dir();
        p.push(format!("estoque_attachments_test_{name}_{}", std::process::id()));
        let _ = fs::remove_dir_all(&p);
        fs::create_dir_all(&p).unwrap();
        p
    }

    fn fake_jpeg(w: u32, h: u32) -> Vec<u8> {
        let mut img = RgbImage::new(w, h);
        for (x, y, px) in img.enumerate_pixels_mut() {
            *px = Rgb([(x % 256) as u8, (y % 256) as u8, 128]);
        }
        let mut bytes = Vec::new();
        image::codecs::jpeg::JpegEncoder::new(&mut Cursor::new(&mut bytes))
            .write_image(&img, w, h, image::ExtendedColorType::Rgb8)
            .unwrap();
        bytes
    }

    fn fake_pdf() -> Vec<u8> {
        b"%PDF-1.4\n%%EOF".to_vec()
    }

    #[test]
    fn salva_imagem_grande_redimensionada_para_a4_em_webp() {
        let base = tmp_dir("img");
        let data_dir = base.join("dados");
        fs::create_dir_all(&data_dir).unwrap();

        let jpeg = fake_jpeg(3000, 2000); // maior que a caixa A4 nos dois eixos
        let saved = save_aquisicao_attachment(&data_dir, "aq_001", &jpeg).unwrap();
        assert_eq!(saved.kind, "imagem");
        assert_eq!(saved.path, "anexos/aquisicoes/aq_001/nota_fiscal.webp");

        let full = base.join(&saved.path);
        assert!(full.is_file());
        let img = image::open(&full).unwrap();
        assert!(image::GenericImageView::dimensions(&img).0 <= A4_MAX_W);
        assert!(image::GenericImageView::dimensions(&img).1 <= A4_MAX_H);
    }

    #[test]
    fn nao_aumenta_imagem_menor_que_a4() {
        let base = tmp_dir("small");
        let data_dir = base.join("dados");
        fs::create_dir_all(&data_dir).unwrap();

        let jpeg = fake_jpeg(200, 150);
        let saved = save_aquisicao_attachment(&data_dir, "aq_002", &jpeg).unwrap();
        let img = image::open(base.join(&saved.path)).unwrap();
        assert_eq!(image::GenericImageView::dimensions(&img), (200, 150));
    }

    #[test]
    fn salva_pdf_como_veio_sem_reprocessar() {
        let base = tmp_dir("pdf");
        let data_dir = base.join("dados");
        fs::create_dir_all(&data_dir).unwrap();

        let pdf = fake_pdf();
        let saved = save_aquisicao_attachment(&data_dir, "aq_003", &pdf).unwrap();
        assert_eq!(saved.kind, "pdf");
        assert_eq!(saved.path, "anexos/aquisicoes/aq_003/nota_fiscal.pdf");
        assert_eq!(fs::read(base.join(&saved.path)).unwrap(), pdf);
    }

    #[test]
    fn trocar_anexo_nao_deixa_arquivo_antigo_de_outro_formato() {
        let base = tmp_dir("swap");
        let data_dir = base.join("dados");
        fs::create_dir_all(&data_dir).unwrap();

        save_aquisicao_attachment(&data_dir, "aq_004", &fake_pdf()).unwrap();
        let dir = base.join("anexos/aquisicoes/aq_004");
        assert!(dir.join("nota_fiscal.pdf").is_file());

        save_aquisicao_attachment(&data_dir, "aq_004", &fake_jpeg(100, 100)).unwrap();
        assert!(!dir.join("nota_fiscal.pdf").exists());
        assert!(dir.join("nota_fiscal.webp").is_file());
        assert_eq!(fs::read_dir(&dir).unwrap().count(), 1);
    }

    #[test]
    fn delete_aquisicao_attachment_remove_a_pasta_e_e_idempotente() {
        let base = tmp_dir("delete");
        let data_dir = base.join("dados");
        fs::create_dir_all(&data_dir).unwrap();

        save_aquisicao_attachment(&data_dir, "aq_005", &fake_pdf()).unwrap();
        let dir = base.join("anexos/aquisicoes/aq_005");
        assert!(dir.exists());

        delete_aquisicao_attachment(&data_dir, "aq_005").unwrap();
        assert!(!dir.exists());
        delete_aquisicao_attachment(&data_dir, "aq_005").unwrap();
    }

    #[test]
    fn recusa_arquivo_que_nao_e_imagem_nem_pdf() {
        let base = tmp_dir("invalid");
        let data_dir = base.join("dados");
        fs::create_dir_all(&data_dir).unwrap();

        let err = save_aquisicao_attachment(&data_dir, "aq_006", b"isto nao e nada valido").unwrap_err();
        assert!(err.contains("imagem válida") || err.contains("PDF"));
    }

    #[test]
    fn read_attachment_bytes_le_o_que_foi_gravado() {
        let base = tmp_dir("read");
        let data_dir = base.join("dados");
        fs::create_dir_all(&data_dir).unwrap();

        let saved = save_aquisicao_attachment(&data_dir, "aq_007", &fake_pdf()).unwrap();
        let bytes = read_attachment_bytes(&data_dir, &saved.path).unwrap();
        assert_eq!(bytes, fake_pdf());
    }

    #[test]
    fn id_com_caracteres_de_travessia_de_caminho_e_higienizado() {
        let base = tmp_dir("traversal");
        let data_dir = base.join("dados");
        fs::create_dir_all(&data_dir).unwrap();

        let saved = save_aquisicao_attachment(&data_dir, "../../etc/aq_008", &fake_pdf()).unwrap();
        assert!(saved.path.starts_with("anexos/aquisicoes/"));
        assert!(!saved.path.contains(".."));
    }

    #[test]
    fn proposta_attachment_usa_dois_niveis_de_pasta_ordem_e_proposta() {
        let base = tmp_dir("proposta");
        let data_dir = base.join("dados");
        fs::create_dir_all(&data_dir).unwrap();

        let saved = save_proposta_attachment(&data_dir, "ord_1", "prop_1", &fake_pdf()).unwrap();
        assert_eq!(saved.path, "anexos/orcamentos/ord_1/prop_1/proposta.pdf");

        // Duas propostas da MESMA ordem não colidem.
        let outra = save_proposta_attachment(&data_dir, "ord_1", "prop_2", &fake_jpeg(50, 50)).unwrap();
        assert_eq!(outra.path, "anexos/orcamentos/ord_1/prop_2/proposta.webp");
        assert!(base.join(&saved.path).is_file());
        assert!(base.join(&outra.path).is_file());

        let bytes = read_proposta_attachment_bytes(&data_dir, &saved.path).unwrap();
        assert_eq!(bytes, fake_pdf());

        delete_proposta_attachment(&data_dir, "ord_1", "prop_1").unwrap();
        assert!(!base.join(&saved.path).exists());
        assert!(base.join(&outra.path).exists());  // a outra proposta não é afetada
        delete_proposta_attachment(&data_dir, "ord_1", "prop_1").unwrap();  // idempotente
    }
}
