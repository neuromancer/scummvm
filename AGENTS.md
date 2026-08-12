
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
- Indentazione: TAB (hard tab), larghezza 4, come da `.editorconfig`.
- Spazi: MAI spazi in parentesi vuote, spazi solo prima di parentesi
  di control-statements (if, for, while), come da `.clang-format`.
- Puntatori: asterisco attaccato al tipo (`int *ptr`), non alla variabile.
- Header guard: formato `AGS_PATH_TO_FILE_H` (es. `AGS_AGS_H`,
  `AGS_PLUGINS_PLUGIN_BASE_H`, `AGS_ENGINE_AC_ROUTE_FINDER_JPS_H`).
  MAI `#pragma once`. Verificare che OGNI header abbia la guardia.
- Copyright: OGNI file `.h` e `.cpp` deve avere l'header GPLv3+ standard
  ScummVM come prima riga del file (nessuna riga vuota prima).
  Testo corretto della terza clausola:
  `* the Free Software Foundation, either version 3 of the License, or`
  `* (at your option) any later version.`
  (NON duplicare "of the License, or").
- Final newline: ogni file deve terminare con un newline (`\n`).
- No trailing whitespace: nessuno spazio o tab a fine riga.
- Linee: evitare linee >120 caratteri; limite massimo assoluto 200.
- Macro: sempre UPPER_CASE. Nomi sufficientemente specifici da non
  collidere con altri engine (prefisso `AGS_`).
- Non introdurre dipendenze esterne non già presenti in \`configure\`.
- Mantieni la compatibilità con le build SSE4.2 e AVX2 (nessun intrinsic
  non condizionato da un check di feature).
- Nessun simbolo di debug nelle build di release.

## API e simboli vietati
- VIETATO: `atoi`, `rand`/`random`, `sprintf`/`vsprintf`, `strcpy`/`strcat`,
  `strtok`, `getenv`, `FILE*`/`fopen`/`fclose`/`fread`/`fwrite`, `sscanf`,
  `exit`/`abort`. Usare equivalenti ScummVM (`Common::`, `g_system`, etc.).
  Se indispensabile per compatibilità AGS upstream, usare
  `FORBIDDEN_SYMBOL_EXCEPTION_nome` e documentare il motivo.
- VIETATO: `malloc`/`free`/`calloc`/`realloc`. Usare allocatori ScummVM.
- VIETATO: `assert()` nativo C. Usare `debug()` checks di ScummVM.
- `std::vector` → `Common::Array`; `std::map` → `Common::HashMap`;
  `std::string` → `Common::String` o `AGS::Shared::String` (nel core engine).
  Preferire `Common::SharedPtr` a `std::shared_ptr`.
- Niente eccezioni C++ (`try`/`catch`/`throw`), tranne dove già usato in
  minima parte per bridge con API AGS (documentare).
- Niente `using namespace` in header, nemmeno in funzioni inline.
  Qualificare esplicitamente i namespace (es. `AGS::Shared::FlagToFlag`).
- Niente `using` alias con nomi generici (es. `using string = ...`).
- Include standard C++: usare `"common/std/..."` per header che hanno un
  wrapper in `common/std/` (es. `"common/std/vector"`, `"common/std/memory"`).
  Per header non wrappati (es. `<cstdarg>`), usare la forma standard `<>`.
- `NULL` → `nullptr` per nuovo codice.
- C-style cast: preferire `static_cast<>` e `reinterpret_cast<>`.

## Namespace e isolamento
- Struttura namespace: `AGS3::AGS::Shared` (codice condiviso),
  `AGS3::AGS::Engine` (runtime), `AGS3::AGS::Engine::Plugins` (plugin).
- Layer di bridge (file top-level `ags.cpp`, `metaengine.cpp`, `detection.cpp`
  e `plugins/ags_plugin.cpp`): usa `Common::String`.
- Core engine (sotto `engine/`, `shared/`, `lib/`): usa `AGS::Shared::String`
  e tipi AGS interni. Conversione via `operator Common::String()` di
  `AGS::Shared::String`.
- Macro con nomi generici in header pubblici vanno evitate (es. MAI
  `#define texWidth 64` o `#define SCREEN_WIDTH 320` senza prefisso `AGS_`).
- `g_system` accessibile solo in `.cpp`, MAI in header.
- SIMD: rilevamento runtime via `g_system->hasFeature()`; niente `#ifdef`
  specifici di architettura nel codice engine.

## Code Review Checklist

Quando fai code review dell'engine AGS (o nuovo codice), verifica
**nell'ordine** i punti seguenti. La checklist è stratificata per priorità.

### Livello 1 — Bloccanti (ogni violazione blocca la PR)
- [ ] **Include guard**: OGNI `.h` ha `#ifndef AGS_*_H` / `#define` / `#endif`.
      Controllare anche file come `sys_main.h` e `route_finder_jps.h`
      (storicamente problematici).
- [ ] **Copyright**: ogni `.h` e `.cpp` ha header GPLv3+ ScummVM come prima
      riga (nessuna riga vuota, nessuna decorazione prima). Verificare terza
      clausola: `(at your option) any later version` (NON duplicata).
- [ ] **Final newline**: `\n` a fine file (100% dei file).
- [ ] **`#include` duplicati**: nessun `#include` identico consecutivo.
- [ ] **Build pulita**: `./configure --disable-all-engines --enable-engine=ags &&
      make -j1` senza errori né warning.

### Livello 2 — API vietate (da eliminare o documentare)
- [ ] `atoi` (15 occorrenze storiche) → funzioni di parsing sicure
- [ ] `sscanf` (12 occorrenze storiche) → parsing manuale o `Common::`
- [ ] `malloc`/`free`/`calloc`/`realloc` fuori da `lib/` → allocatori ScummVM
- [ ] `sprintf`/`vsprintf` (anche se in `lib/alfont`) → `Common::String::format`
- [ ] `strcpy`/`strcat` → se indispensabile, `FORBIDDEN_SYMBOL_EXCEPTION_nome`
      **con commento** che spiega perché
- [ ] `assert()` nativo → `debug()` checks ScummVM (141 occorrenze storiche)
- [ ] `exit()`/`abort()` → meccanismo `quit()` di AGS

### Livello 3 — Container e smart pointer
- [ ] `std::vector` → `Common::Array` (329 occorrenze storiche)
- [ ] `std::map` → `Common::HashMap` (14)
- [ ] `std::set` → `Common::HashSet` (14)
- [ ] `std::list`/`std::deque`/`std::queue` → equivalenti `Common::`
- [ ] `std::shared_ptr`/`std::unique_ptr` → `Common::SharedPtr` ove possibile
- [ ] `std::string` → `Common::String` (bridge) o `AGS::Shared::String` (core)

### Livello 4 — Macro e namespace
- [ ] Macro **UPPER_CASE** e con prefisso `AGS_` (es. `AGS_SCREEN_WIDTH`,
      `AGS_GFX_DRIVER`, `AGS_SHOULD_QUIT`). MAI macro lowercase.
- [ ] Macro generiche in header vanno evitate: `texWidth`, `screenWidth`,
      `SCREEN_WIDTH`, `SCREEN_HEIGHT` senza prefisso `AGS_` (collidono
      con ~12+ altri engine).
- [ ] `#undef` a tappeto di costanti standard (`INT_MIN`, `INT_MAX`,
      `SIZE_MAX`, `TRUE`/`FALSE`) in `shared/core/types.h` — da rimuovere
      o giustificare esplicitamente.
- [ ] `typedef int64 intptr_t` / `typedef uint64 uintptr_t` in `types.h` —
      forzato a 64-bit, errato su architetture 32-bit.
- [ ] `using namespace` MAI in header (nemmeno in funzioni inline).
      Qualificare esplicitamente: `AGS::Shared::FlagToFlag`.
- [ ] `using` alias senza nomi generici (MAI `using string = ...`).

### Livello 5 — Stile e formattazione
- [ ] Trailing whitespace: zero tolleranza (`sed -i 's/[[:space:]]*$//'`)
- [ ] Prima riga vuota prima del copyright (es. `console.h`, `rotate.h`)
- [ ] Indentazione TAB hard, larghezza 4, consistente
- [ ] `NULL` → `nullptr` per nuovo codice (il codice legacy `lib/` è escluso)
- [ ] C-style cast: preferire `static_cast<>` e `reinterpret_cast<>`
      (rapporto storico 2.15:1 C/C++ cast, da invertire nel nuovo codice)
- [ ] Linee: max 120 caratteri, limite assoluto 200
- [ ] `#include <cstdarg>` vs `"common/std/..."` — usare `<>` solo per
      header C++ standard senza wrapper in `common/std/`

### Livello 6 — Plugin-specifico
- [ ] Plugin portati (`ags_fire`, `ags_ccs`, `ags_lua`, `ags_otherroom`)
      devono includere controllo `_engine->version < 13`
- [ ] Plugin che salvano stato devono implementare `AGSE_SAVEGAME`/
      `AGSE_RESTOREGAME` con `Serializer` (o persistenza custom per
      `ags_lua`)
- [ ] `SaveMagic` univoco per plugin (no collisioni come `0xCAFE0000`
      condiviso tra `ags_parallax` e `ags_snow_rain`)
- [ ] Plugin stub (es. `ags_nickenstien_gfx` con 79 TODO) → o completare o
      rimuovere dagli enabled games

### Livello 7 — Portabilità e sicurezza
- [ ] Nessun `#ifdef _WIN32`/`__WINDOWS__` nel codice engine (usare
      `AGS_PLATFORM_OS_*`)
- [ ] API Windows (`WideCharToMultiByte`, `_spawnl`) protette da guard
      condizionali o in file separati
- [ ] `g_system` accessibile SOLO in `.cpp`, MAI in header
- [ ] Metodi `OSystem` chiamati solo via `g_system->...`
- [ ] `error("TODO: ...")` in codice raggiungibile → sostituire con
      implementazione o `quit()`
- [ ] `Common::String` vs `AGS::Shared::String`: layer di bridge usa
      `Common::`, core engine usa `AGS::Shared::`
- [ ] `"scummvm.exe"` hardcodato → `"scummvm"` senza estensione

### Livello 8 — Stilistiche LOW (non bloccanti, ma desiderabili)
- [ ] Magic number → costanti nominate (`static const int`) o enum. Pattern
      sentinella (`99999`, `-9999`, `999999999`) ripetuti vanno consolidati.
- [ ] `#ifdef TODO` / `#if 0` blocchi di codice disabilitato: o completare o
      rimuovere (12 `#ifdef TODO`, 1 `#if 0` in `ags_waves/sound.cpp`)
- [ ] `FORBIDDEN_SYMBOL_EXCEPTION_*` deve avere un commento che spiega
      il motivo (es. `// Necessario per compatibilità plugin AGS`)
- [ ] Densità eccessiva di `#define` in header: oltre ~100 macro nello stesso
      header (es. `script_api.h` 125, `cc_internal.h` 96) — valutare refactor
- [ ] Commenti placeholder incompleti: `/* Synced up to upstream: --- */`
      vanno completati con il riferimento al commit upstream
- [ ] `// TODO` e `// FIXME`: >10 nello stesso file va segnalato.
      Storicamente critici: `ags_nickenstien_gfx.cpp` (79), `cc_instance.cpp`
      (19), `draw.cpp` (19), `sys_main.cpp` (13)
- [ ] Variabili statiche locali in funzioni (es. `game_run.cpp:698`)
      → refactor in membri di classe
- [ ] `#pragma region` specifico MSVC: innocuo ma inutile su GCC/Clang
- [ ] `NULL` nel codice `lib/` (legacy importato) è accettato, ma nuovo
      codice engine deve usare `nullptr`

### Comandi rapidi per la review
```bash
# Trova GPL corrotto
grep -rn 'of the License, or(at your option)' engines/ags/

# Trova include duplicati (righe consecutive identiche)
find engines/ags -name "*.h" -o -name "*.cpp" | xargs awk 'NR>1 && $0==p && /^#include/ {print FILENAME":"NR":"$0} {p=$0}'

# Trova header senza include guard
find engines/ags -name "*.h" -exec sh -c 'grep -L "^#ifndef AGS_" "$1"' _ {} \;

# Conta trailing whitespace
grep -rn '[[:space:]]$' engines/ags/ --include="*.cpp" --include="*.h" | grep -v '/tests/'

# Verifica simboli vietati (da eseguire dopo la build)
grep -rn '\batoi\b\|\bsscanf\b\|\bmalloc\b\|\bfree\b' engines/ags/ \
  --include="*.cpp" --include="*.h" | grep -v '/tests/' | grep -v '/lib/' | grep -v FORBIDDEN
```

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
