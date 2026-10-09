# Flatpak (Flathub)

O Slayer Editor roda num sandbox Flatpak e executa as ferramentas do usuário
(shell, compiladores, `gdb`, `debugpy`, servidores LSP) **no sistema do
hospedeiro** via `flatpak-spawn --host`. A detecção (`Sandbox::inFlatpak()`)
é automática: fora do Flatpak, tudo roda direto, sem mudança.

## Testar localmente

Requer `flatpak` e `flatpak-builder`, mais o runtime do KDE:

```bash
flatpak install -y flathub org.kde.Platform//6.8 org.kde.Sdk//6.8
```

Para compilar a partir desta árvore (em vez do git), troque, no manifesto, o
módulo `slayer-editor` por:

```yaml
    sources:
      - type: dir
        path: ..
```

E então:

```bash
flatpak-builder --user --install --force-clean build-flatpak \
    flatpak/io.github.wyllianbs.SlayerEditor.yaml
flatpak run io.github.wyllianbs.SlayerEditor
```

## Publicar no Flathub

Já pronto neste repositório:

- [x] Manifesto com dependências e `slayer-editor` fixado em `tag: v0.3.0` +
      `commit:` (reprodutível). Build verificado localmente (x86_64).
- [x] `metainfo` válido (`appstreamcli validate`), com 6 screenshots `https`
      em `screenshots/` (servidas via `raw.githubusercontent.com`).
- [x] `.desktop` e ícones nomeados pelo app-id `io.github.wyllianbs.SlayerEditor`.
- [x] `LICENSE` (GPL-3.0) na raiz; `project_license` declarado no `metainfo`.

Falta (decisões/ações suas):

1. **Tornar o repositório público.** O Flathub compila a partir da URL git
   pública e as screenshots do `metainfo` só resolvem com o repo público.
2. **Confirmar a licença GPL-3.0-or-later** como pública (o binário liga o
   QTermWidget, GPL-2.0-or-later, então o conjunto é GPL).
3. **Abrir o PR no `flathub/flathub`:**
   - Faça fork de `github.com/flathub/flathub` e crie um branch chamado
     `io.github.wyllianbs.SlayerEditor` (o mesmo app-id).
   - Adicione o arquivo `io.github.wyllianbs.SlayerEditor.yaml` (este manifesto,
     com `tag`+`commit`) na raiz do branch e abra o PR.
   - O Flathub compila para `x86_64` e `aarch64` e um revisor avalia o PR.

> Atenção na revisão: o app usa `--filesystem=host` e
> `--talk-name=org.freedesktop.Flatpak` (para rodar as ferramentas do
> hospedeiro via `flatpak-spawn --host`). São permissões amplas e os revisores
> do Flathub costumam questioná-las — vale explicar no PR que o editor é um
> front-end para o toolchain do usuário (compiladores, `gdb`, LSP, shell), que
> não faz sentido embutir no sandbox.
