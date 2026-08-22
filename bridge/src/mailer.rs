// Envio de e-mail (SMTP) para o fluxo de cotação de Orçamentos — a única
// coisa neste workspace que faz I/O de rede. O C++ nunca chama isto direto:
// ele só COMPÕE o conteúdo (destinatário/assunto/corpo/anexos, ver
// Api::solicitarOrcamentoParaEmpresas em core-cpp/src/api.cpp) e devolve num
// array "emails" dentro do JSON de resposta; quem chama send_email por item
// é o comando Tauri (src-tauri/src/commands.rs), que também é quem busca as
// credenciais salvas (getEmailConfigJson) e os bytes dos anexos (por caminho
// relativo, via attachments::read_proposta_attachment_bytes).
//
// Transporte SÍNCRONO de propósito: o resto da ponte cxx é bloqueante (cada
// comando Tauri já roda fora da thread da UI), então não há motivo pra
// introduzir um runtime async só pra isto — SmtpTransport::send() bloqueia a
// thread da chamada, não a janela do app.
use std::path::Path;

use lettre::message::{header::ContentType, Attachment, Mailbox, Message, MultiPart, SinglePart};
use lettre::transport::smtp::authentication::Credentials;
use lettre::{SmtpTransport, Transport};

use crate::attachments::read_proposta_attachment_bytes;

pub struct SmtpConfig {
    pub host: String,
    pub port: u16,
    pub username: String,
    pub password: String,
    pub from_email: String,
    pub from_name: String,
    // true = STARTTLS (porta 587, o normal em Gmail/Outlook/SMTP
    // corporativo); false = sem criptografia (só p/ testar contra um SMTP
    // local, nunca use em produção — por isso não tem opção de TLS
    // implícito/porta 465 aqui, pra não multiplicar combinações que
    // ninguém vai testar de verdade neste sandbox).
    pub use_tls: bool,
}

pub struct EmailToSend {
    /// Um ou mais endereços separados por vírgula (mesmo formato de
    /// Empresa::emails no cadastro).
    pub to: String,
    pub subject: String,
    pub body_html: String,
    /// Caminhos relativos (compras_propostas_orcamento.anexo_path) — lidos
    /// daqui, nunca recebidos como bytes prontos, pra não duplicar o PDF
    /// inteiro na memória entre o C++ e o Rust à toa.
    pub attachment_paths: Vec<String>,
}

fn parse_mailboxes(csv: &str) -> Result<Vec<Mailbox>, String> {
    let mut out = Vec::new();
    for part in csv.split(',') {
        let addr = part.trim();
        if addr.is_empty() {
            continue;
        }
        out.push(addr.parse::<Mailbox>().map_err(|e| format!("e-mail inválido '{addr}': {e}"))?);
    }
    if out.is_empty() {
        return Err("nenhum destinatário válido".to_string());
    }
    Ok(out)
}

fn attachment_content_type(path: &str) -> ContentType {
    if path.ends_with(".pdf") {
        ContentType::parse("application/pdf").unwrap_or_else(|_| ContentType::TEXT_PLAIN)
    } else {
        ContentType::parse("image/webp").unwrap_or_else(|_| ContentType::TEXT_PLAIN)
    }
}

fn file_name_of(path: &str) -> String {
    Path::new(path).file_name().map(|f| f.to_string_lossy().to_string()).unwrap_or_else(|| "anexo".to_string())
}

/// Monta e envia UM e-mail. `data_dir` é o mesmo caminho portátil usado em
/// todo o resto da ponte (ver resolve_data_dir em src-tauri/src/main.rs) —
/// só pra resolver os anexos, o envio em si não toca no banco.
pub fn send_email(config: &SmtpConfig, data_dir: &Path, email: &EmailToSend) -> Result<(), String> {
    if config.host.trim().is_empty() {
        return Err("configure o servidor de e-mail em Configurações antes de enviar.".to_string());
    }

    let from_addr = format!("{} <{}>", config.from_name, config.from_email);
    let from: Mailbox = from_addr.parse().map_err(|e| format!("remetente inválido: {e}"))?;

    let mut builder = Message::builder().from(from).subject(email.subject.clone());
    for to in parse_mailboxes(&email.to)? {
        builder = builder.to(to);
    }

    let mut multipart = MultiPart::mixed()
        .singlepart(SinglePart::builder().header(ContentType::TEXT_HTML).body(email.body_html.clone()));
    for rel_path in &email.attachment_paths {
        let bytes = read_proposta_attachment_bytes(data_dir, rel_path)?;
        multipart = multipart
            .singlepart(Attachment::new(file_name_of(rel_path)).body(bytes, attachment_content_type(rel_path)));
    }

    let message = builder.multipart(multipart).map_err(|e| format!("falha ao montar e-mail: {e}"))?;

    // Porta 465 é TLS IMPLÍCITO (a conexão já nasce criptografada) — porta
    // 587 (ou qualquer outra) com "usar TLS" marcado é STARTTLS (conexão
    // começa em texto puro e SÓ DEPOIS sobe pra TLS). São protocolos
    // diferentes: usar starttls_relay() numa porta 465 manda um EHLO em
    // texto puro pra um servidor que já espera TLS desde o primeiro byte, o
    // servidor derruba a conexão, e nada é enviado — foi exatamente o que
    // aconteceu com smtp.office365.com:465 (Office 365 também aceita STARTTLS
    // na 587, mas não os dois protocolos na mesma porta).
    let mut transport_builder = if config.use_tls {
        if config.port == 465 {
            SmtpTransport::relay(&config.host).map_err(|e| format!("servidor SMTP inválido: {e}"))?
        } else {
            SmtpTransport::starttls_relay(&config.host).map_err(|e| format!("servidor SMTP inválido: {e}"))?
        }
    } else {
        SmtpTransport::builder_dangerous(&config.host)
    };
    transport_builder = transport_builder.port(config.port);
    if !config.username.trim().is_empty() {
        transport_builder =
            transport_builder.credentials(Credentials::new(config.username.clone(), config.password.clone()));
    }

    transport_builder.build().send(&message).map_err(|e| format!("falha ao enviar e-mail: {e}"))?;
    Ok(())
}

#[cfg(test)]
mod tests {
    // Nada aqui abre socket — send_email real precisa de um servidor SMTP
    // (não há um disponível neste ambiente de build), então os testes cobrem
    // só o que é determinístico e não depende de rede: montagem de
    // destinatários e a mensagem em si (headers/anexos), até o ponto ANTES
    // do transporte tentar conectar.
    use super::*;
    use std::fs;
    use std::path::PathBuf;

    fn tmp_data_dir(name: &str) -> PathBuf {
        let mut base = std::env::temp_dir();
        base.push(format!("estoque_mailer_test_{name}_{}", std::process::id()));
        let _ = fs::remove_dir_all(&base);
        let data_dir = base.join("dados");
        fs::create_dir_all(&data_dir).unwrap();
        data_dir
    }

    #[test]
    fn parse_mailboxes_aceita_varios_enderecos_e_ignora_espacos() {
        let out = parse_mailboxes("a@x.com, b@y.com.br ,  c@z.com").unwrap();
        assert_eq!(out.len(), 3);
    }

    #[test]
    fn parse_mailboxes_recusa_string_vazia_ou_so_com_virgulas() {
        assert!(parse_mailboxes("").is_err());
        assert!(parse_mailboxes(" , , ").is_err());
    }

    #[test]
    fn parse_mailboxes_recusa_endereco_invalido() {
        assert!(parse_mailboxes("nao-e-email").is_err());
    }

    #[test]
    fn send_email_recusa_sem_host_configurado_antes_de_tentar_conectar() {
        let data_dir = tmp_data_dir("sem_host");
        let config = SmtpConfig {
            host: "".to_string(),
            port: 587,
            username: "".to_string(),
            password: "".to_string(),
            from_email: "fl@flcondominios.com.br".to_string(),
            from_name: "FL Condomínios".to_string(),
            use_tls: true,
        };
        let email = EmailToSend {
            to: "empresa@x.com".to_string(),
            subject: "Solicitação de orçamento".to_string(),
            body_html: "<p>Olá</p>".to_string(),
            attachment_paths: vec![],
        };
        let err = send_email(&config, &data_dir, &email).unwrap_err();
        assert!(err.contains("Configurações"), "mensagem inesperada: {err}");
    }

    #[test]
    fn attachment_content_type_distingue_pdf_de_imagem() {
        assert_eq!(attachment_content_type("ord/prop/proposta.pdf"), ContentType::parse("application/pdf").unwrap());
        assert_eq!(attachment_content_type("ord/prop/proposta.webp"), ContentType::parse("image/webp").unwrap());
    }

    #[test]
    fn file_name_of_extrai_so_o_nome_do_arquivo() {
        assert_eq!(file_name_of("anexos/orcamentos/ord_1/prop_1/proposta.pdf"), "proposta.pdf");
    }
}
