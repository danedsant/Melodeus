# Melodeus — Blueprint Técnico v2 (Windows 10/11)

> Replanteamiento de la v1. Cambios principales: **C++20 monolítico** (se elimina Rust/FFI),
> presupuestos de latencia medibles en vez de "latencia cero", stems con latencia declarada
> (≤500ms), fuentes de streaming realistas, y fases con criterios de aceptación.

---

## 1. Visión General

Software de DJ para Windows 10/11 con dos prioridades absolutas: **latencia de audio medible**
y **calidad de render a 144 FPS**. Arquitectura de un solo lenguaje (C++20) con separación
estricta por hilos y reglas de tiempo real verificables por build.

### Objetivos Clave (y cómo se miden)
| Objetivo | Métrica | Herramienta |
|---|---|---|
| Ruta de audio seca | < 5 ms end-to-end (WASAPI Exclusivo 48k/128) | `tools/latency_probe` |
| Procesamiento en callback | < 1.3 ms por bloque (50% del periodo), 0 xruns/30min | contadores QPC + soak test |
| Render | 144 FPS estables con 4 waveforms + espectro | HUD de frame-time en debug |
| Stems (GPU) | ≤ 500 ms de latencia declarada, mostrada en UI | timestamp chunk-in → out |
| Biblioteca | búsqueda < 20 ms sobre 50k pistas | test de rendimiento en F3 |

---

## 2. Pila Tecnológica

| Área | Elección | Licencia |
|---|---|---|
| Build | CMake ≥ 3.24 + MSVC (VS2022/BuildTools) | — |
| Audio I/O | JUCE `juce_audio_devices` (WASAPI Exclusivo + ASIO) | AGPLv3 (JUCE doble licencia; **decisión D1**: Melodeus se distribuye como AGPLv3, ver `LICENSE`) |
| ASIO | Steinberg ASIO SDK (`JUCE_USE_ASIO=1`) | Gratis con registro |
| Ventana/Render | Win32 + **Dear ImGui** (backend DX12) + D3D12 propio | MIT |
| DSP | Propio (TPT biquad, SVF, limiter) + `pffft`/`juce::dsp::FFT` | — |
| Elastic audio | WSOLA propio (Master Tempo) | — |
| Stems | ONNX Runtime C++ + **DirectML EP** | MIT |
| DB | SQLite amalgamación + FTS5 (`-DSQLITE_ENABLE_FTS5`) | Dominio público |
| Decode local | `minimp3`, `dr_flac`, `dr_wav` (header-only) | CC0 |
| Streaming | `yt-dlp` + `ffmpeg` como **procesos externos** por pipe | Unlicense |
| MIDI | JUCE `MidiInput/MidiOutput` | (incluido) |
| HID/LEDs | `hidapi` | LGPL/BSD dinámico |

**Decisiones respecto a la v1:**
- Sin Rust, sin `cxx`, sin `cargo` en CMake: un solo toolchain, un solo depurador.
- El SPSC lock-free se mantiene **dentro de C++** (audio → UI/analizador).
- JUCE corre **headless** (devices + MIDI); la ventana es Win32 propia para poder
  manejar el swapchain DXGI sin pelear con el repaint de JUCE.
- FFmpeg/YT-DLP se ejecutan como procesos externos (pipe PCM f32): cero linking,
  cero obligaciones LGPL en el binario.

---

## 3. Arquitectura de Hilos

```
[Hilo principal Win32]      bucle de mensajes JUCE + WndProc → ImGui; render D3D12 @144Hz
        ▲ lee (lock-free)
[Audio RT - callback JUCE]  48kHz / 128 muestras (2.67ms)
        │ escribe peaks/RMS/bandas → SPSC ring pre-asignado
[Analyzer]                  FFT beatgrid, espectro, onsets (baja prioridad)
[Stems]                     ORT/DML, chunker overlap-add → ring con timestamp
[DB Writer]                 cola de comandos, único escritor SQLite (WAL)
[Stream/Network]            yt-dlp/ffmpeg, jitter buffer, caché LRU en disco
[MIDI]                      FSM de mapping → cola SPSC de eventos
[Device Watcher]            IMMNotificationClient → FSM de failover
```

### Reglas de Tiempo Real (Hard Constraints)
1. **Prohibido** `new`/`delete` en el callback: un hook global de heap con flag
   thread-local aborta el proceso en debug si el hilo RT aloja.
2. **Prohibido** mutex en RT: sólo atomics y SPSC con `memory_order_acquire/release`.
3. Cero asignaciones, syscalls, logging con locks o excepciones dentro del callback.
4. Parámetros de UI (knobs) con *smoothing* one-pole por bloque → sin zipper.
5. Toda la memoria de decks/rings/buffers se pre-asigna al cargar la pista.
6. El hilo RT usa `AvSetMmThreadCharacteristics("Pro Audio")` y prioridad alta.
7. Hilos de análisis/red/DB jamás tocan el estado del motor sin pasar por colas
   lock-free o comandos atómicos.

### SPSC Lock-Free Ring
- Productor único (hilo de audio) / consumidor único (UI o analizador).
- Head/tail `std::atomic<uint64_t>` con padding a línea de caché (falso sharing = 0).
- Capacidad potencia de 2, búfer pre-allocado una vez, nunca redimensionado.

---

## 4. Presupuesto de Latencia

| Ruta | Objetivo |
|---|---|
| WASAPI Exclusivo 48k/128 (salida) | 2.7 – 5.5 ms |
| Compartido `IAudioClient3` low-latency | 3 – 10 ms (fallback en HW no conforme) |
| Procesamiento dentro del callback | < 1.3 ms |
| Ruta seca total (sin stems) | **< 5 ms** |
| Ruta con stems activos | **≤ 500 ms** (delay de alineación fijo 450 ms + hop 100 ms, crossfade 30 ms) |
| xruns en soak (4 decks + stems + UI) | 0 en 30 min |

> **F0 — hallazgo de hardware (Realtek integrado de prueba):** el modo exclusivo ejecuta
> el endpoint a **64 kHz exactos** aunque el formato declarado sea 48 kHz (medido con
> `IAudioClock`: playback = 64000 fps vs `GetFrequency` = 48000; intervalo de evento =
> buffer/64000 para cualquier tamaño) → +33 % de tono. Solo acepta PCM16/24 @44.1/48 kHz
> (ni float32 ni 64/96/192 kHz) y devuelve `AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED` (144 f →
> alineado 192 f). Sin MMCSS, el hilo de emisión acumula stalls de 8–36 ms y señales
> dobles. **Política resultante:** (1) *conformance check* al abrir: posición
> `IAudioClock` ~1 s vs tasa declarada, desviación > 1 % → rechazar exclusivo;
> (2) por defecto compartido `IAudioClient3` de baja latencia, exclusivo solo en
> dispositivos conformes; (3) MMCSS "Pro Audio" obligatorio en el hilo RT (§3 regla 6).

Los stems **nunca** corren dentro del callback: se pre-separan con anticipación en el hilo
de GPU y el path seco se retarda el mismo tiempo para mantener la sincronía.

---

## 5. Estructura del Repositorio

```
melodeus/
├─ CMakeLists.txt            raíz; targets: melodeus, tests, tools
├─ cmake/                    toolchain, warnings, presets, FindONNXRuntime
├─ external/                 juce, imgui, sqlite, hidapi, pffft, minimp3, dr_*
├─ src/
│  ├─ app/                   main, ventana Win32, AppState
│  ├─ core/                  spsc_ring, rt_guard, prealloc_pool, log_rt
│  ├─ audio/                 Engine, Deck, Mixer, EqTpt, Filter, Crossfader,
│  │                         Limiter, Meter, Transport, FailoverFsm, LatencyProbe
│  ├─ render/                DeviceD3D12, SwapChain, WaveformRenderer,
│  │                         ImGuiLayer, shaders/*.hlsl
│  ├─ analysis/              Beatgrid, Onset, Key, WaveformPyramid, BlobWriter
│  ├─ library/               Database(FTS5), Importer, Search, Migrations
│  ├─ streaming/             IStreamSource, YtDlpSource, JitterBuffer, DiskCache
│  ├─ stems/                 OrtSession, DmlContext, Chunker, AlignedDelay, StemEq
│  └─ controllers/           MidiEngine, MappingSchema(JSON), ControlFsm, HidFeedback
├─ assets/                   models/*.onnx, icons/, fonts/, mappings/*.json
├─ tests/                    unit/, golden/, soak/
├─ tools/                    latency_probe/, blobdump/
└─ Docs.md                   este documento
```

---

## 6. Fases con Definition of Done

- **F0 — Spike de integración y toolchain.** CMake + JUCE headless + ventana Win32 con
  ImGui/DX12 en el mismo bucle de mensajes; ORT con DirectML cargando un modelo trivial;
  `yt-dlp` por pipe; CI (`windows-msvc` con tests + formato + `dxc`).
  **DoD:** ventana a 144fps y tono por WASAPI exclusivo con latencia medida impresa.
  > **Estado: F0 completo.** ✅ toolchain CMake/MSVC + `core` RT (spsc/pool/rt_guard/log)
  > con 19 tests en verde; ✅ `latency_probe` (exclusivo pcm24@48k a **4 ms**, exit 0);
  > ✅ ventana Win32+ImGui/DX12 (`melodeus --selftest N`: **~2000 fps** vs objetivo 144,
  > HUD con frame-time, `SELFTEST=0`); ✅ JUCE 9.0.3 headless: tono 440 Hz en **WASAPI
  > exclusivo** verificado por tipo de dispositivo, buffer **144 f**, latencia **9.00 ms**,
  > muestras al 100 % del reloj; ✅ `ort_probe`: ONNX Runtime 1.24.4 + **DirectML**,
  > modelo trivial desde memoria, **0.33 ms/inf**, salida exacta; ✅ `stream_probe`:
  > `yt-dlp` 2026.08.19 + `ffmpeg` n9.0 por **pipes** (roundtrip lavfi→aac→f32le
  > ~4 MB/s y red vía Wikimedia), exit limpio y sin huérfanos (job objects +
  > herencia selectiva de handles); ✅ CI `windows-msvc` (deps, build, tests,
  > formato, sanity dxc).
  > **Aprendizajes JUCE 9:** `initialise()` repite la selección de tipo
  > (`pickCurrentDeviceTypeWithDevices`) → usar `setCurrentAudioDeviceType` +
  > `setAudioDeviceSetup` (exige nombre de dispositivo); `runDispatchLoopUntil` requiere
  > `JUCE_MODAL_LOOPS_PERMITTED=1`; el hilo de audio JUCE activa solo MMCSS "Pro Audio".
  > **Aprendizajes pipes:** `bInheritHandles=TRUE` hereda todos los handles
  > heredables (p. ej. los pipes del job de PowerShell) y los retiene → EOF nunca
  > llega; usar `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` + job object
  > (`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`); un handle de NUL en la lista da
  > `ERROR_INVALID_PARAMETER`; el build LGPL de ffmpeg no incluye libmp3lame →
  > codificar con AAC nativo.

- **F1 — Motor de audio.** 2 decks, decode local, transporte/scrub, gain, EQ 3-band TPT,
  filtro HP/LP, crossfader (sharp/linear/const-power), limiter lookahead, metering,
  pools pre-asignados, `rt_guard`.
  **DoD:** mezcla de 2 tracks < 5 ms, 0 xruns en 30 min, tests unitarios de DSP en verde.
  > **Estado (núcleo F1 listo):** ✅ `src/audio/`: `Deck` (interpolador lineal único =
  > resample de tasa + pitch ±8 %, transporte/seek/gain/**scrub** a velocidad fija),
  > `crossfade_gains` (sharp/linear/const-power), `Engine` (2 decks + master, scratch
  > pre-asignados, `rt_guard::RtContext` dentro de `process`, peaks atómicos),
  > decoder de archivo completo wav/flac/mp3 (dr_wav/dr_flac/minimp3,
  > `mp3dec_load_buf`) + encoder WAV PCM16 para roundtrip;
  > ✅ DSP (`dsp.hpp`): SVF **TPT** (Zavalishin), **EQ 3-bandas** por deck
  > (LP 250 Hz + HP 4 kHz + residual medio, kill −60 dB, suavizado anti-zipper
  > 10 ms), **limiter lookahead** brickwall exacto (2 ms, techo −1 dBFS, release
  > 100 ms, **mínimo deslizante** con deque monótono preasignado — el release
  > one-pote solo violaba el techo; el deque lo garantiza) y **PeakMeter** con
  > caída τ=0.4 s (`master_env`, `limiter_gain` para GR);
  > ✅ 24 tests nuevos (43/43, **5155 aserciones**) incl. respuesta SVF, transparencia
  > del EQ, brickwall con seno de 2.0, release, scrub 1/8, y no-alloc en `process`;
  > ✅ wiring en el spike: callback WASAPI → motor real con DSP
  > (`avg=0.087 ms`, `max=0.407 ms/bloque` vs presupuesto 5 ms; exclusivo 48k/144 f
  > a **9.00 ms**, muestras al 100 %, `SELFTEST=0`).
  > ✅ **estrés 30 min** (`--selftest 1800`, render + motor a la vez): **0 xruns**
  > (detector de gap entre callbacks > 2.5× periodo = 7.5 ms + errores de
  > dispositivo), 86.3 M muestras al **100.0 %** del reloj, max-gap 4.26 ms,
  > motor avg 0.212 / max 0.813 ms — el hilo de audio (MMCSS "Pro Audio") no se
  > resintió ni con ráfagas de 860 ms del render. **DoD de F1 cumplido.**
  > **Nota render 30 min:** la iGPU Intel UHD 620 degrada térmicamente sostenida
  > a fps máximos (avg 390, min1s 11, frame max 860 ms) → vigilar en F2
  > (limitar a 144 fps; el waitable timer de alta resolución no expira en este
  > entorno — ni siquiera el timer clásico —, así que F2 usa sleep+spin por QPC).
  > **Pendiente F1:** scrub con UI de jog (el primitivo de velocidad ya está).

- **F2 — Render de waveform.** Pirámide multiresolución (min/max por nivel), waveform
  scrolling + bandas RGB (rojo=bajos, azul=agudos) en D3D12, layout ImGui base.
  **DoD:** 144fps sostenidos con 4 ondas + espectro, sin allocations en el hilo de render.
  > **Estado: F2 completo.** ✅ `src/waveform/`: `WaveformPyramid` (niveles 256–32768
  > muestras/bucket, fusionadas 2 hijos, bandas lo/mid/hi), `pick_level`/`visible_range`,
  > `fill_wave_instances` (NDC, 28 B/instancia), `FftScratch` radix-2 sin allocs.
  > ✅ Shaders `waveform.hlsl` (quads instanciados por SV_VertexID) compilados en build
  > con fxc (vs_5_1/ps_5_1) y copiados junto al exe. ✅ `WaveformPass` D3D12: PSO root-sig
  > vacío, upload-heap N-buffered persistently mapped (sin hazard: slot = frame_slot()).
  > ✅ Layout ImGui: overview+detail por deck, mixer/browser placeholder (F3/F4),
  > espectro 48 barras log del tap SPSC 1024 f del master. ✅ Pirámides construidas en
  > `aboutToStart` desde pistas de 30 s; vista `audio_spike_view()` para el hilo de render.
  > ✅ Pacing `--paced` (sleep+spin QPC a 144 Hz): avg 144.0 / min1s 143.8 / frame 6.94 ms
  > con el payload completo en la UHD 620; sin paced, avg 620 fps. DoD cumplido
  > (49/49 tests, 6872 aserciones; selftest 5 s OK; 0 xruns; motor avg 0.02–0.03 ms).
  > **Pendiente F2:** scrub con UI de jog (heredado de F1).

- **F3 — Biblioteca y análisis.** SQLite FTS5 (`unicode61 remove_diacritics 2`), import de
  carpetas, beatgrid/BPM/key en background, BLOBs versionados (`format_version + audio_sha1`),
  HUD "Just-In-Time".
  **DoD:** 50k pistas, búsqueda < 20 ms con audio reproduciéndose sin dropouts.
  > **Estado: F3 completo (núcleo + UI + pendientes menores).** ✅ `src/analysis/`:
  > `detect_bpm` (envolvente de onset + autocorrelación 70–180 con corrección de octava;
  > click 128 → 127.8), `detect_key` (croma STFT + Krumhansl-Schmuckler → Camelot A/B +
  > tradicional; acorde Am → 8A/Am), `BeatGrid` mínimo. ✅ `src/library/`: SQLite 3.46
  > amalgamation (external/sqlite, fetch_deps grupo f3), `Database` (WAL, FTS5 con triggers,
  > migraciones user_version 1→2), `import_file` (decode dr_*/minimp3 + análisis + BLOB
  > beatgrid con SHA-1 de samples), `ImportWorker` (hilo con conexión propia WAL).
  > Tests: roundtrip, diacríticos, import WAV, **50k filas → peor búsqueda 4.45 ms** (<20 ms
  > DoD), blob roundtrip, SHA-1 vectores (65/65). ✅ UI replanteada (3 columnas): biblioteca
  > a izq/der con toggle (tabla Title/BPM/Key/Time, botones A/B, doble-click carga,
  > **[+ carpeta] con picker COM → import en background con progreso en el footer**), decks
  > al centro con display BPM (+pitch) y key Camelot(tradicional), tiempo y título; mixer
  > mínimo a un lado (crossfader, faders, master, VU). ✅ Spike: DB demo en %TEMP% (click 128
  > + acorde Am), carga con staging seguro, mixer remoto por atomics. ✅ Selftest: 144.0 fps
  > paced con UI completa, carga staged verificada, 0 xruns. **Pendiente F3:** lectura del
  > BLOB beatgrid al cargar decks (sync/quantize), BLOB de waveform (`kind=1`), scrub jog
  > (heredado de F1).

- **F4 — Streaming y caché.** `YtDlpSource` aislado tras `IStreamSource` + disclaimer de ToS,
  jitter buffer de red, anillo de descarga alrededor del icono, caché LRU por peso.
  **DoD:** pista de YT/SC a play sin glitch con red estable; reintento automático.
  > **Estado: F4 completo (transporte + caché + UI).** ✅ `src/streaming/`: `IStreamSource`
  > (interface estable; la app no ve yt-dlp/ffmpeg), `JitterBuffer` (SPSC f32, prefill 1 s,
  > drenado de clips cortos vía finish(), contadores underrun/overflow), `YtDlpSource`
  > (hilo propio: yt-dlp `--print title/duration` + pipeline yt-dlp→ffmpeg f32le 48k
  > estéreo por pipes con herencia selectiva de handles + job object; **reintento
  > automático ×3 con backoff** si el stream muere antes del 80% de la duración esperada;
  > fichero local vía sólo-ffmpeg para tests offline), `DiskCache` (LRU por peso, default
  > 1 GB, índice en memoria, persistencia al reabrir), `load_url_blocking` (caché →
  > descarga → DecodedTrack → commit). ✅ Spike: `audio_spike_load_samples` (staging con
  > samples ya decodificados). ✅ UI: fila URL en la biblioteca (input + botón "cargar");
  > carga en hilo detached con estado en el footer ("descargando..." → "cargado: Título
  > (Ns) [cache]"); disclaimer de ToS en el commit de F0. ✅ Tests: prefill/drenado/
  > overflow del jitter, LRU por peso + persistencia de caché, smoke offline ffmpeg
  > (WAV local → f32le ≈1 s), loader con hit de caché (71/71, 60008 aserciones). ✅
  > Selftest paced 144.0 fps con la UI de URL, 0 xruns. **Pendiente F4:** prueba con red
  > real YT/SC (manual; requiere cookies/ToS), descarga incremental al deck (hoy es
  > full-preload; el jitter buffer ya soporta pull live), anillo de descarga visual
  > (icono con progreso), scrub sobre streams.

- **F3.5 — UX DJ (pasada de usabilidad).** Arregla el bug de filas invisibles de la
  biblioteca y completa los controles básicos de un set: carga/transporte/pitch/EQ/filtro,
  drag & drop y atajos de teclado.
  **DoD:** biblioteca usable con ratón y teclado, controles DJ operativos, sin regresión
  de rendimiento (72/72 tests, selftest paced 144 fps / 0 xruns).
  > **Estado: F3.5 completo.** ✅ **Biblioteca reescrita con widgets ImGui reales**
  > (`BeginTable` 5 columnas + `Selectable` con SpanAllColumns): el bug raíz eran filas
  > dibujadas con rects manuales dentro de un `BeginChild` cuyo fondo opaco (`##libtable`)
  > las tapaba → "no puedo cargar". Ahora hay hover, selección visible, scroll-into-view
  > con ↑/↓, doble-click carga a A, botones A/B por fila y **drag & drop fila→deck**
  > (payload `MD_TRACK`). ✅ **Decks**: PLAY gigante + botón CUE (seek 0), pitch fader
  > vertical ±8 % (doble-click = reset), BPM gigante + pitched, key Camelot(tradicional),
  > tiempo/restante; carriles de onda como **drop targets**. ✅ **Mixer**: knobs de EQ
  > 3 bandas (LOW/MID/HI, −26..+6 dB, doble-click = 0) y **filtro DJ por canal** (−1 LP
  > 300 Hz … 0 bypass … +1 HP 3 kHz) con SVF TPT + suavizado one-pole anti-zipper en
  > `Engine` (test: corta 5 kHz con LP y 100 Hz con HP, bypass transparente). ✅ **Explorer
  > DnD**: `WM_DROPFILES` → 1º archivo al deck A, 2º al B, resto importa a la biblioteca
  > (hilo con conexión propia WAL). ✅ **Atajos** (inactivos al escribir en un campo):
  > ↑/↓ navegar, Enter/Shift+Enter o 1/2 cargar A/B, Q/W play-pause, Z/X y ,/. pitch
  > (Shift = fino 0.1 %), C/N CUE, Space stop, F mover biblioteca, / buscar, Esc limpiar.
  > ✅ **Feedback de error visible** en el footer (`audio_spike_last_error`), no sólo
  > printf. ✅ Tests 72/72 (60012 aserciones), selftest paced 144.0 fps / 0 xruns,
  > build sin warnings. **Pendiente F3.5:** scrub/jog (heredado de F1), learn de
  > filtros con botones de preset, feedback de drop durante el arrastre (highlight
  > más marcado).

- **F5 — Stems ≤ 500 ms.** Export ONNX de `demucs` (MIT), sesión DML con warm-up, chunker
  hop=100ms/overlap 50% con crossfade, `AlignedDelay` de 450ms en el path seco, Stem-EQ de
  4 faders, degradación elegante (GPU → CPU → off con aviso).
  **DoD:** latencia medida ≤ 500 ms, toggle sin clics, latencia visible en UI.

- **F6 — MIDI/HID.** FSM (Normal/Turn/Shift/Touch) + MIDI learn, mappings JSON versionados,
  hot-plug, feedback de LEDs.
  **DoD:** controlador genérico mapeable desde la UI, LEDs reactivos, plug/unplug sin crash.

- **F7 — Hardening y entrega.** Failover de dispositivo (ramp-down 10 ms → reconectar →
  ramp-up, estado en RAM + snapshot), profiling, empaquetado (NSIS/MSIX, DLLs de ORT),
  docs de usuario.
  **DoD:** desconectar el dispositivo de audio en plena reproducción no mata la app.

---

## 7. Módulos Críticos — Contratos Internos

### 7.1. Motor (`audio/`)
- `Engine::process(AudioBuffer&)` es lo único que el callback invoca. Sin estado
  compartido mutable con otros hilos salvo vía SPSC y comandos atómicos.
- Parámetros: `SmoothedValue` (one-pole, tiempo ~10 ms) para gain/EQ/filtro/crossfader.
- `Deck`: transporte con posición en muestras `int64` + fracción, pre-decode a pool.

### 7.2. Biblioteca (`library/`)
```sql
CREATE TABLE tracks(
  track_id INTEGER PRIMARY KEY,
  source_type INTEGER NOT NULL,      -- 0=local 1=youtube 2=soundcloud 3=spotify(meta)
  uri TEXT NOT NULL, title TEXT, artist TEXT, album TEXT,
  bpm REAL, musical_key TEXT, duration_ms INTEGER,
  bitrate INTEGER, format TEXT, audio_sha1 BLOB,
  file_size INTEGER, added_at INTEGER, last_played_at INTEGER
);
CREATE VIRTUAL TABLE tracks_fts USING fts5(
  title, artist, album, content='tracks', content_rowid='track_id',
  tokenize = "unicode61 remove_diacritics 2"
);
CREATE TABLE analysis_blobs(
  track_id INTEGER PRIMARY KEY,
  format_version INTEGER NOT NULL,
  audio_sha1 BLOB NOT NULL,
  kind INTEGER NOT NULL,            -- 0=beatgrid 1=waveform 2=cues
  data BLOB NOT NULL
);
```
- WAL activo; un único hilo escritor; lectores concurrentes.
- Los BLOBs se invalidan si cambia `audio_sha1` o `format_version`.

### 7.3. Stems (`stems/`)
1. `Deck` llena un ring de "feed" con anticipación (look-ahead ≥ 500 ms).
2. Hilo de stems toma chunks (hop 100 ms, ventana 450 ms), inferencia DML, overlap-add.
3. `AlignedDelay` retarda el path seco 450 ms (delay fijo presente mientras Stems está armado).
4. Crossfade de 30 ms al alternar stems para evitar clics.
5. Salida: 4 buses (`vocals/drums/bass/melody`) → Stem-EQ (4 faders con smoothing).

### 7.4. MIDI (`controllers/`)
```json
{
  "schema_version": 1,
  "device": { "name": "*", "manufacturer": "*" },
  "controls": [
    { "id": "deck_a_pitch", "type": "cc", "channel": 0, "number": 1,
      "range": [0, 16383], "action": "deck.a.pitch" },
    { "id": "deck_a_play",  "type": "note", "channel": 0, "number": 0x0B,
      "action": "deck.a.play", "feedback": { "type": "note", "number": 0x0B } }
  ]
}
```
- FSM por control: `Idle → Touched → Turned → Released` (para skirts de 14-bit y LEDs).

---

## 8. Riesgos

| Riesgo | Impacto | Mitigación |
|---|---|---|
| Export ONNX de HTDemucs sin calidad/latencia | Alto | Spike al inicio de F5; plan B: modelo de 2 fuentes |
| GPU sin DirectML / VRAM insuficiente | Alto | Fallback CPU con aviso; stems desactivables |
| Endpoint exclusivo no conforme (Realtek a 64 kHz con 48k declarado) | Alto | Conformance check `IAudioClock` al abrir; fallback compartido `IAudioClient3` |
| Exclusivo limitado a PCM16/24 @44.1/48k (sin float32) en HW integrado | Medio | Motor trabaja en float interno; acepta el formato nativo del dispositivo |
| JUCE AGPLv3/comercial | Económico/legal | **Resuelto (D1):** Melodeus es AGPLv3 (`LICENSE`); JUCE se usa bajo su licencia AGPLv3. Comercial solo si el modelo cambia |
| `yt-dlp` cambia sus argumentos | Medio | Aislado tras `IStreamSource`, tests de humo |
| JUCE MessageManager + ventana Win32 propia | Medio | Validado en F0 antes de invertir en más código |
| Latencia de stems > 500 ms en GPUs débiles | Medio | Ajustar hop/ventana y mostrar latencia real en UI |

---

## 9. Convenciones de Código

- C++20, `clang-format` con estilo LLVM base (`IndentWidth 4`, `ColumnLimit 100`,
  `BreakBeforeBraces: Attach`); config en `.clang-format`.
- Compilación en `/W4 /permissive- /fp:fast`, sin excepciones en el path RT
  (las excepciones se permiten fuera de RT: DB, import, red).
- Sin `using namespace` en headers. Nombres en inglés; documentos en español.
- Todo código RT lleva el prefijo mental de "¿qué pasa si el driver nos llama ya?".
- Tests: `tests/unit` (uno por módulo de `core`/`audio`), `tests/golden` (WAVs de
  referencia), `tests/soak` (30 min sin xruns).
