# 0.8.0-rc.2 — instalação limpa e mapa fiel à flash

- Novo mapa padrão sem reservas de dados de aplicativos: removidos `nvs`, `aurora_cfg`, `vfs` e `spiffs`. Permanecem apenas `phy_init`, `otadata`, `launch_req`, `launcher` e `coredump`, necessários à infraestrutura atual.
- O gráfico nativo e a API/WebGUI deixaram de apresentar 0x5f0000 bytes fixos como “Dados”. Consultam as partições reais na região protegida e mostram o restante como “Sem partição”. Espaço sem partição não é tratado como dado ocupado.
- A fronteira validada do alocador de aplicativos continua em 0xa10000. Apagar as reservas de dados não amplia automaticamente esse banco; a região posterior sem partições é exibida como tal, e não prometida como capacidade atual de instalação. Ampliação do alocador exige mudança e validação próprias.
- Nova regressão cobre tabelas vazias, mapa limpo, mapa legado, lacunas e limites de endereço; teste LVGL verifica o rótulo sem partição. Build, limites de memória, suíte host e sintaxe JavaScript aprovados.

O usuário autorizou apagar toda a flash, sem aplicativos instalados. Foi executado apagamento integral antes da instalação limpa. Uma leitura integral das antigas regiões de dados confirmou 6.160.384 bytes apagados, além de 24.576 bytes do antigo NVS. O microSD e o pendrive não foram formatados; a atualização da WebGUI substitui apenas seu arquivo app.js oficial.

Permanece **Release Candidate**, destinado a uso prolongado antes da versão final, sem data definida. Ensaios prolongados e cenários específicos de recuperação continuam pendentes. Registro final em BUILD_0.8.0-rc.2.json.
