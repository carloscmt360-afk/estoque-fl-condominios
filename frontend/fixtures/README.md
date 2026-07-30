# Fixtures de desenvolvimento

Esta pasta fica **vazia no repositório** — os arquivos que o mock de
desenvolvimento (`js/devMock.js`) consome contêm dados reais do usuário
(custos, nomes de funcionários) e nunca devem ser versionados nem
distribuídos com o app.

Para regenerar localmente (necessário só para revisar o frontend num
navegador comum, sem compilar Tauri):

```bash
cd estoque-fl-app
cargo run -p core-cli -- --dump-fixtures frontend/fixtures
```

Isso grava `backup.json`, `report_2026_06.json`, `products.json` e
`departments.json` nesta pasta, a partir da ponte cxx real.
