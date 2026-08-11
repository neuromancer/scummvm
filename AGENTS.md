
# Istruzioni per l'agente OpenCode

## Lingua
- L'utente parla italiano come lingua madre: rispondi e commenta in italiano
  salvo diversa richiesta esplicita.

## Contesto del progetto
Fork personale di ScummVM. Focus attuale: porting di ulteriori plugin del
motore AGS embedded (verso l'upstream v3.6.2.21, su master) per sbloccare i
giochi ancora elencati come UNSUPPORTED. Completati finora i plugin ags_fire,
OtherRoom, ags_CCS e agslua (che sblocca 3 giochi: allthewaydown,
barelyfloating, salt); il plugin agslua è verificato solo a livello di
compilazione/link, la validazione runtime contro i file di gioco reali è
ancora da fare. Restano 14 voci UNSUPPORTED: 8 usano plugin vecchi builtin, 2
usano il plugin Theora, più i limiti del motore (zak2 per le room animations,
kq1agdi per il formato 4.2). C'è anche una pipeline GitHub Actions per
generare AppImage e pacchetto Arch/CachyOS .pkg.tar.zst.
Supporto dei giochi compilati con ags 3.6.2.
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
  l'upstream v3.6.2.21, non risolverli silenziosamente.
- Non fare refactor speculativi oltre lo scope del commit che si sta
  portando.

## Compilazione per test
- Esegui sempre `make -j1` (single core), senza parallelismo, per avere
  log di errore lineari e non intrecciati.
- Compila solo il motore AGS: esegui la configure con
  `--disable-all-engines --enable-engine=ags` prima del make.
- Sequenza tipica:
  `./configure --disable-all-engines --enable-engine=ags && make -j1`
- Se la build directory ha già una configurazione diversa, ri-esegui
  `make clean` o cancella la build dir e riconfigura da zero.

## CI/CD (GitHub Actions)
- Quando modifichi i workflow, spiega il motivo del cambiamento (es. nome
  pacchetto, flag di configure, conflitto plugin linuxdeploy) in un
  commento nel diff.
- Preferisci modifiche minime e verificabili ai job esistenti piuttosto che
  riscritture complete del workflow.

## Cosa NON fare
- Non eseguire comandi bash distruttivi (rm -rf, force-push) senza
  conferma esplicita.
- Non committare o pushare automaticamente: prepara solo le modifiche. Chiedi conferma prima del push.   
- 
