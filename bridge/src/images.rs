// Processamento e armazenamento local de fotos de produto.
//
// Módulo isolado de propósito: puro Rust, nunca cruza a ponte cxx (foto não
// é domínio de negócio do core-cpp — o C++ só grava o CAMINHO relativo que
// este módulo devolve, nunca decodifica/gera bytes de imagem). É a "camada
// responsável pelo gerenciamento das imagens" pedida — o resto do app nunca
// monta um caminho de arquivo de imagem na mão, sempre chama uma função
// daqui, o que permite trocar armazenamento local por nuvem no futuro
// mexendo só neste arquivo.
use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};

use image::imageops::FilterType;
use image::{DynamicImage, GenericImageView};

pub const MAIN_MAX_SIDE: u32 = 1000;
pub const THUMB_MAX_SIDE: u32 = 200;
const MAIN_QUALITY: f32 = 82.0;
const THUMB_QUALITY: f32 = 78.0;
// Recusa antes mesmo de tentar decodificar — um upload de dezenas de MB não
// é foto de produto, é engano ou arquivo corrompido; barato de checar antes
// de gastar CPU decodificando.
const MAX_INPUT_BYTES: usize = 20 * 1024 * 1024;

/// Caminhos RELATIVOS (item 16 do pedido — nunca absolutos, pra sobreviver
/// a mover a pasta do app pra outro PC) prontos pra gravar em
/// products.image_path / products.thumbnail_path.
#[derive(Debug)]
pub struct SavedImage {
    pub image_path: String,
    pub thumbnail_path: String,
}

/// Pasta raiz das imagens — sempre IRMÃ de "dados" (mesma base portátil,
/// ao lado do executável, nunca AppData; ver portable_paths.cpp).
/// `data_dir` é o que resolve_data_dir_default() já devolve (".../dados");
/// isto só troca o último componente, nunca recalcula do zero — um único
/// lugar decide onde fica a base portátil do app inteiro.
pub fn images_root(data_dir: &Path) -> PathBuf {
    match data_dir.parent() {
        Some(base) => base.join("imagens").join("produtos"),
        None => PathBuf::from("imagens").join("produtos"),
    }
}

fn app_base_dir(data_dir: &Path) -> &Path {
    data_dir.parent().unwrap_or(data_dir)
}

// SKU já sai do backend no formato CMT###### (nunca texto livre do
// usuário — ver inventory_engine::nextSku), mas um caminho de arquivo em
// disco nunca deve confiar cegamente numa string: mantém só
// [A-Za-z0-9_-], então nem um SKU legado/corrompido escapa da pasta
// imagens/produtos via "../" ou separador de caminho (item 19: segurança
// contra imagem associada ao produto errado).
fn sanitize_sku(sku: &str) -> String {
    let cleaned: String = sku
        .chars()
        .filter(|c| c.is_ascii_alphanumeric() || *c == '_' || *c == '-')
        .collect();
    if cleaned.is_empty() {
        "_sem_sku".to_string()
    } else {
        cleaned
    }
}

fn product_dir(root: &Path, sku: &str) -> PathBuf {
    root.join(sanitize_sku(sku))
}

fn to_relative(base: &Path, path: &Path) -> String {
    path.strip_prefix(base)
        .unwrap_or(path)
        .to_string_lossy()
        .replace('\\', "/")
}

/// Nunca AUMENTA a imagem (item 5 do pedido) — se já é menor ou igual ao
/// alvo nos dois eixos, devolve como está, sem reamostrar.
fn resize_capped(img: &DynamicImage, max_side: u32) -> DynamicImage {
    let (w, h) = img.dimensions();
    if w <= max_side && h <= max_side {
        return img.clone();
    }
    img.resize(max_side, max_side, FilterType::Lanczos3)
}

fn write_webp_atomic(img: &DynamicImage, dest: &Path, quality: f32) -> Result<(), String> {
    // to_rgba8() (do crate `image`) normaliza QUALQUER variante decodificada
    // — grayscale, paleta, 16 bits por canal, CMYK etc. — pra um buffer RGBA
    // de 8 bits, formato que webp::Encoder::from_rgba aceita incondicional.
    // webp::Encoder::from_image, usado antes aqui, só cobre um subconjunto
    // de DynamicImage e falhava com "Unimplemented" pra fotos que decodificam
    // fora dele (caso real: uma foto de grampeador quebrava exatamente nisso).
    let rgba = img.to_rgba8();
    let (w, h) = rgba.dimensions();
    let encoded = webp::Encoder::from_rgba(&rgba, w, h).encode(quality);

    // Grava num arquivo temporário e troca por rename (atômico no mesmo
    // volume) — nunca deixa foto.webp pela metade se o processo cair no
    // meio da escrita (item 11: "não deixar arquivos órfãos").
    let tmp = dest.with_extension("webp.tmp");
    {
        let mut f = fs::File::create(&tmp).map_err(|e| format!("falha ao gravar imagem: {e}"))?;
        f.write_all(&encoded).map_err(|e| format!("falha ao gravar imagem: {e}"))?;
    }
    fs::rename(&tmp, dest).map_err(|e| format!("falha ao finalizar gravação da imagem: {e}"))
}

/// Processa e grava a foto principal (≤1000×1000) e a miniatura (≤200×200)
/// de um produto, convertendo sempre para WebP. `bytes` é o arquivo bruto
/// selecionado pelo usuário (JPG, PNG ou WebP — validado aqui, não confia
/// na extensão do arquivo). Sobrescreve o que já existia (troca de foto).
pub fn save_product_image(data_dir: &Path, sku: &str, bytes: &[u8]) -> Result<SavedImage, String> {
    if bytes.is_empty() {
        return Err("arquivo vazio.".to_string());
    }
    if bytes.len() > MAX_INPUT_BYTES {
        return Err(format!(
            "imagem maior que {} MB — escolha um arquivo menor.",
            MAX_INPUT_BYTES / (1024 * 1024)
        ));
    }
    let img = image::load_from_memory(bytes)
        .map_err(|_| "arquivo não é uma imagem válida (use JPG, PNG ou WebP).".to_string())?;

    let dir = product_dir(&images_root(data_dir), sku);
    fs::create_dir_all(&dir).map_err(|e| format!("não foi possível criar a pasta de imagens: {e}"))?;

    let main_path = dir.join("foto.webp");
    let thumb_path = dir.join("thumb.webp");
    write_webp_atomic(&resize_capped(&img, MAIN_MAX_SIDE), &main_path, MAIN_QUALITY)?;
    write_webp_atomic(&resize_capped(&img, THUMB_MAX_SIDE), &thumb_path, THUMB_QUALITY)?;

    let base = app_base_dir(data_dir);
    Ok(SavedImage {
        image_path: to_relative(base, &main_path),
        thumbnail_path: to_relative(base, &thumb_path),
    })
}

/// Remove a pasta inteira de imagens de um produto (foto.webp + thumb.webp
/// + a própria pasta) — usado na exclusão definitiva do produto (item 12)
/// e antes de gravar uma foto nova no lugar da antiga (item 11: nunca
/// acumula arquivo órfão). Idempotente: pasta inexistente não é erro.
pub fn delete_product_images(data_dir: &Path, sku: &str) -> Result<(), String> {
    let dir = product_dir(&images_root(data_dir), sku);
    if dir.exists() {
        fs::remove_dir_all(&dir).map_err(|e| format!("falha ao remover imagens antigas: {e}"))?;
    }
    Ok(())
}

// Logo da FL — uma única foto global (não por registro), mostrada sempre na
// barra superior dos relatórios impressos. Pasta e formato à parte de
// imagens/produtos (não é imagem de produto), mas mesmo pipeline de
// decodificar/redimensionar/gravar em WebP.
const LOGO_MAX_SIDE: u32 = 300;

fn app_images_root(data_dir: &Path) -> PathBuf {
    match data_dir.parent() {
        Some(base) => base.join("imagens").join("app"),
        None => PathBuf::from("imagens").join("app"),
    }
}

fn app_logo_path(data_dir: &Path) -> PathBuf {
    app_images_root(data_dir).join("logo.webp")
}

/// Processa e grava a logo (≤300×300, WebP), sobrescrevendo a anterior se
/// houver. Devolve os bytes já prontos pra mostrar (a UI atualiza a prévia
/// na hora, sem precisar de uma segunda chamada pra reler do disco).
pub fn save_app_logo(data_dir: &Path, bytes: &[u8]) -> Result<Vec<u8>, String> {
    if bytes.is_empty() {
        return Err("arquivo vazio.".to_string());
    }
    if bytes.len() > MAX_INPUT_BYTES {
        return Err(format!(
            "imagem maior que {} MB — escolha um arquivo menor.",
            MAX_INPUT_BYTES / (1024 * 1024)
        ));
    }
    let img = image::load_from_memory(bytes)
        .map_err(|_| "arquivo não é uma imagem válida (use JPG, PNG ou WebP).".to_string())?;

    let dir = app_images_root(data_dir);
    fs::create_dir_all(&dir).map_err(|e| format!("não foi possível criar a pasta da logo: {e}"))?;
    let dest = app_logo_path(data_dir);
    write_webp_atomic(&resize_capped(&img, LOGO_MAX_SIDE), &dest, MAIN_QUALITY)?;
    fs::read(&dest).map_err(|e| format!("falha ao ler a logo recém-gravada: {e}"))
}

/// Remove a logo (volta ao estado "sem logo" — relatórios impressos sem
/// imagem no cabeçalho). Idempotente: sem logo já não é erro.
pub fn delete_app_logo(data_dir: &Path) -> Result<(), String> {
    let path = app_logo_path(data_dir);
    if path.exists() {
        fs::remove_file(&path).map_err(|e| format!("falha ao remover a logo: {e}"))?;
    }
    Ok(())
}

/// Lê a logo salva, se houver — `None` é "sem logo" (não erro), porque é o
/// estado normal antes do usuário escolher uma foto.
pub fn read_app_logo_bytes(data_dir: &Path) -> Result<Option<Vec<u8>>, String> {
    let path = app_logo_path(data_dir);
    if !path.exists() {
        return Ok(None);
    }
    fs::read(&path).map(Some).map_err(|e| format!("falha ao ler a logo: {e}"))
}

/// Lê os bytes de uma imagem já salva, a partir do caminho RELATIVO gravado
/// em products.image_path/thumbnail_path. `relative_path` normalmente só
/// pode ter vindo do nosso próprio backend (nunca digitado por usuário),
/// mas nunca confia cegamente num caminho de arquivo: resolve, canonicaliza
/// e confirma que o resultado continua DENTRO de imagens/produtos antes de
/// ler — nem ".." nem um caminho absoluto trocado escapam dessa pasta
/// (mesma cautela de sanitize_sku, item 19 do pedido).
pub fn read_image_bytes(data_dir: &Path, relative_path: &str) -> Result<Vec<u8>, String> {
    let root = images_root(data_dir);
    let candidate = app_base_dir(data_dir).join(relative_path);

    let canon_root = fs::canonicalize(&root).map_err(|_| "pasta de imagens indisponível.".to_string())?;
    let canon_candidate = fs::canonicalize(&candidate).map_err(|_| "imagem não encontrada.".to_string())?;
    if !canon_candidate.starts_with(&canon_root) {
        return Err("caminho de imagem inválido.".to_string());
    }
    fs::read(&canon_candidate).map_err(|e| format!("falha ao ler imagem: {e}"))
}

/// Decodifica o arquivo que o usuário selecionou (a UI manda base64 pela
/// ponte Tauri — invoke() não carrega binário bruto).
pub fn decode_base64(data: &str) -> Result<Vec<u8>, String> {
    use base64::Engine;
    base64::engine::general_purpose::STANDARD
        .decode(data)
        .map_err(|e| format!("arquivo inválido: {e}"))
}

/// Codifica bytes de imagem já lidos do disco para mandar de volta pra UI
/// como `data:` URL — evita configurar o protocolo de asset do Tauri para
/// um caminho portátil que só existe em tempo de execução.
pub fn encode_base64(bytes: &[u8]) -> String {
    use base64::Engine;
    base64::engine::general_purpose::STANDARD.encode(bytes)
}

#[cfg(test)]
mod tests {
    use super::*;
    use image::{GrayImage, ImageEncoder, Luma, Rgb, RgbImage};
    use std::io::Cursor;

    fn tmp_dir(name: &str) -> PathBuf {
        let mut p = std::env::temp_dir();
        p.push(format!("estoque_images_test_{name}_{}", std::process::id()));
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

    // Muitas fotos reais (scanner, print em P&B, alguns celulares) decodificam
    // como grayscale, não RGB — era exatamente esse caso que quebrava com
    // "falha ao preparar imagem para WebP: Unimplemented" antes da correção
    // (webp::Encoder::from_image só cobre um subconjunto de DynamicImage).
    fn fake_jpeg_grayscale(w: u32, h: u32) -> Vec<u8> {
        let mut img = GrayImage::new(w, h);
        for (x, y, px) in img.enumerate_pixels_mut() {
            *px = Luma([((x + y) % 256) as u8]);
        }
        let mut bytes = Vec::new();
        image::codecs::jpeg::JpegEncoder::new(&mut Cursor::new(&mut bytes))
            .write_image(&img, w, h, image::ExtendedColorType::L8)
            .unwrap();
        bytes
    }

    #[test]
    fn salva_foto_e_thumb_em_webp_dentro_da_pasta_do_sku() {
        let base = tmp_dir("save");
        let data_dir = base.join("dados");
        fs::create_dir_all(&data_dir).unwrap();

        let jpeg = fake_jpeg(1200, 900); // maior que 1000 -> deve reamostrar
        let saved = save_product_image(&data_dir, "CMT000001", &jpeg).unwrap();

        assert_eq!(saved.image_path, "imagens/produtos/CMT000001/foto.webp");
        assert_eq!(saved.thumbnail_path, "imagens/produtos/CMT000001/thumb.webp");

        let main_full = base.join(&saved.image_path);
        let thumb_full = base.join(&saved.thumbnail_path);
        assert!(main_full.is_file());
        assert!(thumb_full.is_file());

        let main_img = image::open(&main_full).unwrap();
        assert!(main_img.width() <= MAIN_MAX_SIDE && main_img.height() <= MAIN_MAX_SIDE);
        let thumb_img = image::open(&thumb_full).unwrap();
        assert!(thumb_img.width() <= THUMB_MAX_SIDE && thumb_img.height() <= THUMB_MAX_SIDE);

        // WebP reduz bem uma foto sintética repetitiva — confirma que não
        // está só copiando o JPEG original disfarçado de .webp.
        assert!(fs::metadata(&main_full).unwrap().len() < jpeg.len() as u64);
    }

    #[test]
    fn nao_aumenta_imagem_menor_que_o_alvo() {
        let base = tmp_dir("noupscale");
        let data_dir = base.join("dados");
        fs::create_dir_all(&data_dir).unwrap();

        let jpeg = fake_jpeg(100, 80); // menor que 1000 e que 200
        let saved = save_product_image(&data_dir, "CMT000002", &jpeg).unwrap();

        let main_img = image::open(base.join(&saved.image_path)).unwrap();
        assert_eq!((main_img.width(), main_img.height()), (100, 80));
        let thumb_img = image::open(base.join(&saved.thumbnail_path)).unwrap();
        assert_eq!((thumb_img.width(), thumb_img.height()), (100, 80));
    }

    #[test]
    fn troca_de_foto_sobrescreve_sem_deixar_arquivo_antigo_diferente() {
        let base = tmp_dir("replace");
        let data_dir = base.join("dados");
        fs::create_dir_all(&data_dir).unwrap();

        save_product_image(&data_dir, "CMT000003", &fake_jpeg(300, 300)).unwrap();
        let dir = base.join("imagens/produtos/CMT000003");
        assert_eq!(fs::read_dir(&dir).unwrap().count(), 2); // foto.webp + thumb.webp

        save_product_image(&data_dir, "CMT000003", &fake_jpeg(150, 150)).unwrap();
        assert_eq!(fs::read_dir(&dir).unwrap().count(), 2); // continua só 2, nada órfão

        let main_img = image::open(dir.join("foto.webp")).unwrap();
        assert_eq!((main_img.width(), main_img.height()), (150, 150));
    }

    #[test]
    fn delete_product_images_remove_a_pasta_inteira_e_e_idempotente() {
        let base = tmp_dir("delete");
        let data_dir = base.join("dados");
        fs::create_dir_all(&data_dir).unwrap();

        save_product_image(&data_dir, "CMT000004", &fake_jpeg(200, 200)).unwrap();
        let dir = base.join("imagens/produtos/CMT000004");
        assert!(dir.exists());

        delete_product_images(&data_dir, "CMT000004").unwrap();
        assert!(!dir.exists());

        // idempotente: chamar de novo numa pasta que já não existe não é erro
        delete_product_images(&data_dir, "CMT000004").unwrap();
    }

    #[test]
    fn recusa_arquivo_que_nao_e_imagem() {
        let base = tmp_dir("invalid");
        let data_dir = base.join("dados");
        fs::create_dir_all(&data_dir).unwrap();

        let err = save_product_image(&data_dir, "CMT000005", b"isto nao e uma imagem").unwrap_err();
        assert!(err.contains("imagem válida"));
    }

    #[test]
    fn salva_foto_grayscale_sem_erro_unimplemented() {
        // Regressão: uma foto real (grampeador) que decodificava como
        // grayscale quebrava com "falha ao preparar imagem para WebP:
        // Unimplemented" — webp::Encoder::from_image não cobre esse
        // DynamicImage; from_rgba (via to_rgba8()) cobre qualquer um.
        let base = tmp_dir("grayscale");
        let data_dir = base.join("dados");
        fs::create_dir_all(&data_dir).unwrap();

        let saved = save_product_image(&data_dir, "CMT000009", &fake_jpeg_grayscale(300, 200)).unwrap();
        let main_img = image::open(base.join(&saved.image_path)).unwrap();
        assert_eq!((main_img.width(), main_img.height()), (300, 200));
    }

    #[test]
    fn sku_com_caracteres_de_travessia_de_caminho_e_higienizado() {
        let base = tmp_dir("traversal");
        let data_dir = base.join("dados");
        fs::create_dir_all(&data_dir).unwrap();

        let saved = save_product_image(&data_dir, "../../etc/CMT000006", &fake_jpeg(50, 50)).unwrap();
        // o ".." e "/" saem do saneamento — nunca escreve fora de imagens/produtos/
        assert!(saved.image_path.starts_with("imagens/produtos/"));
        assert!(!saved.image_path.contains(".."));
        let full = base.join(&saved.image_path);
        assert!(full.starts_with(base.join("imagens/produtos")));
    }

    #[test]
    fn read_image_bytes_le_o_que_save_product_image_gravou() {
        let base = tmp_dir("read");
        let data_dir = base.join("dados");
        fs::create_dir_all(&data_dir).unwrap();

        let saved = save_product_image(&data_dir, "CMT000007", &fake_jpeg(300, 300)).unwrap();
        let bytes = read_image_bytes(&data_dir, &saved.thumbnail_path).unwrap();
        assert!(!bytes.is_empty());
        assert_eq!(bytes, fs::read(base.join(&saved.thumbnail_path)).unwrap());
    }

    #[test]
    fn read_image_bytes_recusa_caminho_fora_da_pasta_de_imagens() {
        let base = tmp_dir("read_traversal");
        let data_dir = base.join("dados");
        fs::create_dir_all(&data_dir).unwrap();
        save_product_image(&data_dir, "CMT000008", &fake_jpeg(50, 50)).unwrap();

        // um segredo qualquer fora de imagens/produtos, tentando escapar via "..".
        fs::write(base.join("segredo.txt"), b"nao deveria ser legivel por aqui").unwrap();
        let err = read_image_bytes(&data_dir, "../segredo.txt").unwrap_err();
        assert!(err.contains("inválido") || err.contains("não encontrada"));
    }

    #[test]
    fn base64_roundtrip() {
        let original = fake_jpeg(40, 40);
        let encoded = encode_base64(&original);
        let decoded = decode_base64(&encoded).unwrap();
        assert_eq!(original, decoded);
    }

    #[test]
    fn sem_logo_devolve_none_nao_erro() {
        let base = tmp_dir("logo_none");
        let data_dir = base.join("dados");
        fs::create_dir_all(&data_dir).unwrap();
        assert_eq!(read_app_logo_bytes(&data_dir).unwrap(), None);
    }

    #[test]
    fn salva_le_e_apaga_a_logo() {
        let base = tmp_dir("logo_cycle");
        let data_dir = base.join("dados");
        fs::create_dir_all(&data_dir).unwrap();

        let bytes = save_app_logo(&data_dir, &fake_jpeg(1000, 700)).unwrap();
        assert!(!bytes.is_empty());
        let img = image::load_from_memory(&bytes).unwrap();
        assert!(image::GenericImageView::dimensions(&img).0 <= LOGO_MAX_SIDE);
        assert!(image::GenericImageView::dimensions(&img).1 <= LOGO_MAX_SIDE);

        let relida = read_app_logo_bytes(&data_dir).unwrap();
        assert_eq!(relida, Some(bytes));

        delete_app_logo(&data_dir).unwrap();
        assert_eq!(read_app_logo_bytes(&data_dir).unwrap(), None);
        // idempotente: apagar de novo não é erro.
        delete_app_logo(&data_dir).unwrap();
    }

    #[test]
    fn trocar_a_logo_sobrescreve_sem_deixar_arquivo_antigo() {
        let base = tmp_dir("logo_swap");
        let data_dir = base.join("dados");
        fs::create_dir_all(&data_dir).unwrap();

        save_app_logo(&data_dir, &fake_jpeg(300, 300)).unwrap();
        let dir = base.join("imagens/app");
        assert_eq!(fs::read_dir(&dir).unwrap().count(), 1);

        save_app_logo(&data_dir, &fake_jpeg(150, 150)).unwrap();
        assert_eq!(fs::read_dir(&dir).unwrap().count(), 1);
    }
}
