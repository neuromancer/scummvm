'
# Istruzioni per l'agente OpenCode

## Contesto del progetto
Fork personale di ScummVM. Focus attuale: porting incrementale del motore
AGS embedded verso il tag upstream v3.6.2.10 (branch
ags-sync-3.6.2.10-savegame), e pipeline GitHub Actions per generare
AppImage e pacchetto Arch/CachyOS .pkg.tar.zst.

## Convenzioni di stile
- Segui lo stile di codice esistente di ScummVM (indentazione, naming,
  header guard) anche nei file dell'engine AGS embedded.
- Non introdurre dipendenze esterne non già presenti in configure.
- Mantieni la compatibilità con le build SSE4.2 e AVX2 (nessun intrinsic
  non condizionato da un check di feature).
- Nessun simbolo di debug nelle build di release.

## Porting AGS
- Confronta sempre il commit upstream originale con la versione embedded
  prima di applicare una patch: ScummVM spesso adatta il codice per il
  proprio sistema di build e per l'astrazione IO/audio.
- Segnala esplicitamente conflitti di API tra la versione embedded e
  l'upstream v3.6.2.10, non risolverli silenziosamente.
- Non fare refactor speculativi oltre lo scope del commit che si sta
  portando.

## CI/CD (GitHub Actions)
- Quando modifichi i workflow, spiega il motivo del cambiamento (es. nome
  pacchetto, flag di configure, conflitto plugin linuxdeploy) in un
  commento nel diff.
- Preferisci modifiche minime e verificabili ai job esistenti piuttosto che
  riscritture complete del workflow.

## Cosa NON fare
- Non eseguire comandi bash distruttivi (rm -rf, force-push) senza
  conferma esplicita.
- Non committare o pushare automaticamente: prepara solo le modifiche.
