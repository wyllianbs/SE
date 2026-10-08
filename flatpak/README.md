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

1. Repositório público e licença GPL-3.0.
2. `metainfo` com ao menos uma screenshot hospedada (URL `https`).
3. No manifesto, o módulo `slayer-editor` usa `type: git` com `tag` e `commit`
   fixos.
4. Pull request para `flathub/flathub` com este manifesto. O Flathub compila
   para x86_64 e aarch64.
