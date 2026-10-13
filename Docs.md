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
  > build sin warnings. ✅ Fix: el motor de audio ahora arranca **siempre** (antes
  > sólo en `--selftest`; en interactivo `audio_spike_load` devolvía false → doble
  > clic "no hacía nada") y `audio_spike_finish()` se llama al salir en ambos modos
  > (si no, `std::terminate` por el thread joinable). **Pendiente F3.5:** scrub/jog (heredado de F1), learn de
  > filtros con botones de preset, feedback de drop durante el arrastre (highlight
  > más marcado).

- **F3.6 — UI moderna (glassmorphism).** Tema central + tipografía propias, paneles con
  look "glass", controles restaurados como widgets reales e iconos vectoriales.
  **DoD:** paleta/tema en un solo sitio, fuentes correctas, look moderno sin regresión
  (72/72 tests, selftest paced 144 fps / 0 xruns).
  > **Estado: F3.6 completo.** ✅ **M0 fuentes** en `app/fonts/` (versionadas con el repo;
  > Clash Display Medium/Semibold para UI/títulos, JetBrains Mono Regular/Medium para
  > números, Phosphor 2.1.1 regular para iconos; licencias OFL/Future en `LICENSE-*.txt`)
  > copiadas junto al exe con POST_BUILD en CMake. ✅ **M1 fuentes en ImGui** (`imgui_layer`): 3 fuentes
  > (`imgui_font_ui` 16 + Phosphor 14 merged, `imgui_font_title` 19, `imgui_font_mono` 15),
  > rangos acentos+iconos PUA, fallback a default si faltan TTF. ⚠️ Ojo con ImGui 1.92:
  > `ImFont::FontSize` → `LegacySize`, `AddGlyph` → `AddChar`. ✅ **M2 tema central**
  > (`app/theme.hpp`): paleta glass (fill/border/highlight), acentos A=azul 86,148,255
  > / B=naranja 255,150,86, `theme::apply()` tematiza ~30 `ImGuiCol` (fondos transparentes,
  > el backdrop plano lo aporta el clear), `theme::glass(dl, …)` dibuja panel glass falso
  > (sombra + hairline + highlight top) y `theme::icons::utf8()` para los iconos. ✅ **M3-M5
  > paneles reescritos**: biblioteca glass con títulos/mono/iconos (lupa, folder-plus) y A/B
  > teñidos por acento; decks glass, PLAY/CUE como `ImGui::Button()` con iconos play/pause/
  > stop-circle, BPM gigante en mono, badge Camelot glass, carriles glass como drop targets;
  > mixer glass con knobs (valor dB en mono dentro), VU y detente del crossfader; barra
  > superior/inferior glass con wordmark en title y fps en mono. ✅ Tests 72/72 (60012
  > aserciones), selftest paced 143.9 fps / 142.2 min1s / 0 xruns. **Nota:** si Clash
  > Display resulta poco legible en listas, fallback a Clash Grotesk (misma familia).
  > **Pendiente:** blur real de fondos (M6), scrub/jog, learn de filtros.

- **F3.6.x — Layout DJ simétrico.** Reorganiza la UI al patrón Serato/Rekordbox: deck A a la
  izquierda y deck B a la derecha (zona superior), mixer como columna central de altura
  completa, y dos bibliotecas gemelas (browse) debajo de cada deck. El VU vertical y el
  espectro FFT de la barra inferior se eliminan; el peak del master se muestra como
  mini-barra horizontal en la top-right.
  **DoD:** elementos alineados en grid, waveforms en cada deck, peak visible, sin regresión
  (72/72 tests, selftest paced 144 fps / 0 xruns).
  > **Estado: completo.** ✅ **Deck panel** reordenado en 3 bloques: display glass (BPM
  > gigante mono, key Camelot, tiempo, título) → waveform (2 carriles drop-target) →
  > controles glass (PLAY/CUE botones + pitch fader vertical). ✅ **Mixer** reescrito como
  > columna central vertical: FILTRO+EQ (HI/MID/LOW) de ambos canales arriba, gains+crossfader
  > al centro, master abajo; **VUs eliminados**. ✅ **Dos bibliotecas gemelas** (`lib_state`
  > izq con URL+import, `lib_state_r` der sólo browse) que comparten DB; toggle de lado
  > eliminado; atajos operan sobre la izquierda. ✅ **Barra de peak del master** en top-right:
  > degradado navy→purple (`kPeakNavy`→`kPeakPurple`) con pico de retención y lectura dB en
  > mono. ✅ **Footer inferior** reducido a solo texto de estado (audio OK | db N pistas);
  > espectro FFT y `kSpectrumBars` eliminados. ✅ Layout grid con gutters compartidos
  > (deck ~58% / biblio ~42% del alto). ✅ Tests 72/72, selftest paced 144.0 fps / 143.5
  > min1s / 0 xruns.

- **F3.6.1 — Fix mixer + waveform seek.** Corrige los 3 defectos reportados del layout F3.6.x:
  mixer desalineado, onda sin avance visible y sin interacción de seek/scrub.
  **DoD:** tiras de canal alineadas, playhead visible en overview, click/arrastre hacen seek,
  sin regresión (72/72 tests).
  > **Estado: completo.** ✅ **Mixer** (`mixer_panel.cpp`): tiras de canal verticales centradas
  > por canal — `GAIN (knob, doble-click=0.8, muestra %) → FILTRO → HI → MID → LOW` — con
  > crossfader y master centrados y anclados al pie (sin hueco muerto); ganancias ahora son
  > knobs (no faders) para alinear toda la tira a un mismo eje; corregidas etiquetas A/B que
  > estaban invertidas (gain A mostraba "B" en azul y viceversa). `knob()` acepta `KnobDisp`
  > {None, Db, Pct}; helper `knob_at(cx,...)` coloca knobs centrados y devuelve altura.
  > ✅ **Waveform seek/scrub** (`deck_panel.hpp/.cpp`, `main.cpp`): `DeckPanelOut` gana
  > `seek_requested`/`seek_seconds`; `DeckUiState` gana `detail_span_sec`. En ambos carriles,
  > click = seek y arrastre mantenido = scrub continuo (overview: `frac*duración`; detail:
  > `posición + (frac-0.5)*span`), cableado a `audio_spike_seek` (aplica en pausa también).
  > ✅ **Playhead** de acento del deck en overview dibujado por D3D12 en `fill_lane` (encima de
  > la onda, ya que el pass D3D12 renderiza tras ImGui); firma de `fill_lane` añade
  > `ph_r/ph_g/ph_b`. ⚠️ Nota: selftest con la máquina cargada (Google Drive/Discord/Spotify/
  > VS Code) dio 142.8 fps / min1s 126 / 0 xruns; el shortfall vs 143.0 es por contención de
  > fondo (pico de frame ~66 ms por descheduling), no por regresión — re-verificar con la
  > máquina limpia. Pendiente de commit.

- **F3.6.2 — Fix waveform D3D12 + tema purple + solapes de layout.** Corrige la causa raíz
  del waveform invisible/basura y completa la migración de acentos al medidor del top bar.
  **DoD:** onda dibujada con geometría correcta, acentos purple en toda la UI, sin solapes
  de texto, 72/72 tests.
  > **Estado: completo.** ✅ **Waveform** (`waveform_pass.cpp`): `IASetVertexBuffers` usaba
  > slot 1 pero `kLayout` declara las instancias en **slot 0** → el IA leía de slot 0 sin
  > buffer → geometría basura (banda plana). Corregido a slot 0. Verificado con marcadores
  > de prueba (overview 34px: 8/4/9/34 px; detail 96px: 24/10/24/96 px). ✅ **Layout**
  > (`main.cpp`): `draw_deck_column` y mixer se llamaban con `y=0.0f` dentro de `##body`
  > (y=body_top=28) → columnas solapaban el top bar. Cambiado a `body_top`. ✅ **Onda
  > centrada y coloreada** (`waveform.cpp`): `fill_wave_instances` con `ndc_y_center=0`
  > (antes -1) y degradado 3 paradas navy `(18,32,92)` → purple `(178,60,240)` → hot
  > `(236,130,250)` por amplitud. ✅ **Tema purple** (`theme.hpp`, `library_panel.cpp`,
  > `main.cpp`): `kAccentA/B` = family purple; `kPeakNavy/Purple/Hot` = medidor top-right;
  > playheads deck A/B en purple; headers/grab_hov/botones A/B de biblioteca migrados.
  > ✅ **Solapes**: fps/label dB separados (`right_limit` en `draw_chrome`); "BPM pitched"
  > movido a la línea de la etiqueta medido (`bpm_w`). Test `fill_wave_instances` actualizado
  > (r/g/b ∈ [0,1]). PSO blend deshabilitado (opaco).

- **F3.6.3 — Layout de referencia (deck reestructurado) + hot cues + quitar A/B de la
  biblioteca.** Reorganiza el deck al patrón de la maqueta HTML del usuario, añade hot cues
  funcionales y elimina los botones A/B de la biblioteca (carga por teclado/drag/drop).
  **DoD:** display con divider vertical + pills, timestamps bajo el waveform, hot cues
  1/2/3 operativos, biblioteca sin columna A/B, 72/72 tests.
  > **Estado: completo.** ✅ **Deck** (`deck_panel.cpp/.hpp`): panel 1 (glass) ahora contiene
  > la fila de info — BPM grande a la izquierda con **divider vertical**, y a la derecha
  > **título arriba + pills abajo** (badge DECK, badge key camelot, nombre tonalidad) con
  > **divider horizontal** inferior — seguido de los 2 carriles de onda y los **timestamps**
  > debajo del detail (transcurrido izq / restante der, mono). Panel 2 (glass): PLAY, CUE,
  > **divider vertical + hot cues 1/2/3**, y **pitch knob circular** a la derecha (reemplaza
  > al fader vertical; ±8%, doble-click=0). ✅ **Hot cues** (`deck_panel.hpp`, `main.cpp`):
  > `DeckUiState` gana `hot_cues[3]` (-1=vacío); `DeckPanelOut` gana `hot_cue_slot`/
  > `hot_cue_set`/`hot_cue_clear`. Click en slot vacío = guardar posición actual; click en
  > slot lleno = saltar; click der = borrar. Se limpian al cargar otra pista. Cableado a
  > `audio_spike_seek`. ✅ **Biblioteca** (`library_panel.cpp`): columna A/B eliminada
  > (5→4 columnas: Título/BPM/Key/Time); la carga es por doble-click, teclado (1/2) o
  > drag&drop a un deck.

- **F3.6.4 — Tipografía Inter + layout 1:1 con la maqueta HTML.** Migra la UI de Clash
  Display a Inter (la familia de la maqueta) y ajusta métricas, fuentes y disposición a
  valores exactos de Tailwind (×scale).
  **DoD:** tipografía Inter en toda la UI, BPM en Inter bold, métricas 1:1 (padding 12,
  gaps 12, knobs 44, botones 40×32/28×28, mixer 220), 72/72 tests.
  > **Estado: completo.** ✅ **Fuentes** (`imgui_layer.cpp/.hpp`, `app/fonts/`): descargada
  > Inter 4.0 (OFL) — Regular/Bold/ExtraBold TTF + `LICENSE-Inter.txt`; `load_fonts` carga
  > Regular→`g_font_ui`, Bold→`g_font_bold` (nuevo getter), ExtraBold 19px→`g_font_title`,
  > JetBrainsMono 15px→`g_font_mono`; Phosphor merged en `g_font_ui`. CMake copia la carpeta
  > `fonts/` completa junto al exe. ✅ **Deck** (`deck_panel.cpp`): BPM grande en Inter Bold
  > 30px (text-3xl, ya no mono); label "B P M" text-xs; título text-sm; pills text-[10px]
  > bold; timestamps mono text-[11px]; botones PLAY/CUE 40×32 (w-10 h-8); hot cues 28×28
  > (w-7 h-7); pitch knob 32px (w-8); paddings p-3 (12), gaps gap-3 (12), pb-3 (12).
  > Nueva `deck_panel_height(scale)` exportada; Panel 2 posicionado en coordenadas absolutas
  > desde `origin` (los Dummy/InvisibleButton acumulan ItemSpacing y desplazan el cursor —
  > causa por la que el Panel 2 quedaba recortado/invisible). ✅ **Mixer** (`mixer_panel.cpp`):
  > knobs 44px (w-11, radio 22); grid 2 columnas (gap-x-2=8, gap-y-8=32) con filas
  > GAIN/FILTRO/HI/MID/LOW; título "MIXER" centrado bold tracking-widest; crossfader y master
  > en cajas oscuras (bg-dark rounded p-3) ancladas al pie. ✅ **Biblioteca**
  > (`library_panel.cpp`): columnas 45/40/45px; fila seleccionada con highlight morado
  > (accent 28%); key verde normal, morado si seleccionada. ✅ **Layout global** (`main.cpp`):
  > gutter/margin 12 (gap-3/p-3); mixer 220px (w-[220px]); `deck_h = deck_panel_height(scale)`
  > (altura por contenido, ya no 58% fijo); children con `WindowPadding=0` (el padding por
  > defecto desbordaba el contenido del child). ✅ **Verificado**: app compila, corre y la
  > captura muestra los 3 paneles por deck (info+ondas+timestamps / controles / biblioteca),
  > mixer con knobs 44 y cajas CROSSFADER/MASTER, tipografía Inter. Tests 72/72.

- **F3.7 — Knobs corregidos, hot cues persistentes, loop ajustable + marcadores de onda.**
  Arregla la visualización de los knobs del mixer, el bug por el que los hot cues se
  reseteaban cada frame, y añade loop con longitud ajustable (2 flechas junto a los hot
  cues) reflejado en el waveform junto a los hot cues.
  **DoD:** EQ/filtro/pitch con detente a las 12:00, hot cues persistentes entre frames y
  limpiados al cargar pista, loop activo con wrap de audio + UI ‹ ›, marcadores de cues y
  loop en ambas ondas, 75/75 tests.
  > **Estado: completo.** ✅ **Knobs** (`mixer_panel.cpp`, `deck_panel.cpp`): causa raíz —
  > `a0=0.75π` con y-flip apuntaba arriba-izquierda; 0 dB del EQ caía hacia abajo. Fix:
  > `a0=1.25π` (frac 0→inferior-izq 225°, 0.5→arriba, 1→inferior-der) + mapeo piecewise
  > para knobs con centro (`centered=true`): el valor central (0 dB/bypass/0%) va a las
  > 12:00 y ±135° hacia los lados; el arco va del detente al valor actual. GAIN sigue
  > lineal sin centro. `pitch_knob` (deck) con el mismo tratamiento centrado. ✅ **Hot
  > cues** (`main.cpp`): causa raíz — `double hot_cues[2][3]` declarado DENTRO del bucle de
  > frames (main.cpp:607) se reseteaba a {-1,-1,-1} cada frame; izado junto a `MixerState`
  > (~línea 488). Limpieza centralizada en `clear_deck_marks(deck)` (cues + loop) invocada
  > desde `load_row`, drag-drop de Explorer y carga por URL. ✅ **Loop de audio**
  > (`src/audio/deck.hpp`, `spike_winsnd`): `Deck::set_loop(start,len)/clear_loop/looping()`
  > con wrap en `render()` (`pos_ = loop_start + fmod(overshoot, loop_len)`); clamp contra
  > la duración de la pista; `load/clear` desactivan el loop. Puente UI→audio con atomics
  > `ui_loop_start/ui_loop_len` (patrón de seek) y `audio_spike_set_loop(deck,start,len)`,
  > aplicado al inicio del callback. ✅ **UI loop** (`deck_panel`): botón LOOP (icono "L",
  > acento cuando activo) + flechas ‹ › con la longitud entre medias (beats si hay BPM,
  > si no segundos); escalera 1/2/4/8/16 beats (default 4); `DeckUiState` gana
  > `loop_on/loop_len_sec`, `DeckPanelOut` gana `loop_toggled/loop_len_delta`. Con el loop
  > activo, ‹ › re-aplican manteniendo el inicio. ✅ **Marcadores de onda** (`main.cpp`
  > `fill_lane`): `LaneMarkers` (cues + loop) pasado a los 4 carriles; hot cues = línea
  > vertical fina + flag superior en el acento del deck; loop = bracket (2 líneas de borde
  > + barra fina superior) en verde `kSuccess`; playhead encima. Reserva de 12 instancias
  > por carril. ✅ **Tests**: 3 casos nuevos de loop en `test_deck.cpp` (wrap sin terminar,
  > clear_loop reanuda al final, len<=0 desactiva). 75/75. ✅ **Verificado**: screenshot
  > muestra knobs con indicadores correctos (GAIN a las 2h, EQ/filtro a las 12:00) y
  > controles LOOP + ‹ 4 › junto a los hot cues en ambos decks.

- **F3.8 — Biblioteca: context menu, sort por columna, elipsis, tooltips, staging.**
  Mejoras de usabilidad de la biblioteca (sin badges A/B, descartado): menú contextual
  con click derecho, ordenación por cabecera, título con "…", tooltip de artista/álbum,
  indicador de carga, contador filtrado y estado vacío, cabeceras al estilo de la
  maqueta, y hints de atajos en el panel izquierdo.
  **DoD:** click derecho → "Cargar en Deck A/B", cabeceras TÍTULO/BPM/KEY/TIME
  ordenables (click = asc/desc; BPM/key sin dato al final), títulos largos con "…",
  hover de fila marcado, tooltip artista/álbum/formato, puntos animados en la fila
  cargada (~1 s), "(12/35)" cuando hay búsqueda, estado vacío con mensaje, hints
  "↑↓ mover · Enter A · ⇧Enter B · / buscar", 75/75 tests.
  > **Estado: completo.** ✅ **Context menu** (`library_panel.cpp`):
  > `BeginPopupContextItem` tras el Selectable (dentro de `PushID(i)`); items "Cargar en
  > Deck A/B" que reutilizan `LibraryAction::LoadDeckA/LoadDeckB` (el click derecho ya
  > pone `state.selected = i`). ✅ **Sort por columna**: `ImGuiTableFlags_Sortable` +
  > `TableGetSortSpecs()`; `LibraryUiState` gana `sort_col/sort_asc` y `apply_sort()`
  > (comparador de 3 vías: título case-insensitive + artista de desempate, BPM/key con
  > vacíos al final en ambos sentidos, duración numérica); `refresh()` aplica el orden
  > tras cada búsqueda; flip de args (`c<0` vs `c>0`) para descending — nunca `!cmp`
  > (violación de orden estricto). ✅ **Elipsis + hover** (`library_panel.cpp`):
  > `ellipsize()` con búsqueda binaria sobre `CalcTextSize` (O(log n)); ancho disponible
  > = `inner_w - 130*scale - 36*scale` (columnas fijas + scrollbar + bordes/cell-padding);
  > hover más marcado empujando `ImGuiCol_HeaderHovered/Active` (accent 50/60%) sólo
  > dentro de la tabla. ✅ **Tooltip**: hover de fila → `BeginTooltip` con artista,
  > álbum (`TextDisabled`) y "formato · bitrate kbps". ✅ **Staging** (`main.cpp`,
  > `spike_winsnd`): `LibraryUiState` gana `loading_row/loading_until` (1 s);
  > `load_row` los pone al aceptar la carga; el panel dibuja 3 puntos animados en la
  > columna Time (hueco que se desplaza con `GetTime()*3 % 3`); limpieza perezosa.
  > Puente audio: `EngineCallback` gana `load_gen[2]` (atómico, incrementado al aplicar
  > `pending_load`, reset en `aboutToStart`), expuesto en `AudioSpikeView` (por si la UI
  > quiere sincronizar en el futuro). ✅ **Contador + vacío**: cabecera muestra
  > "(12/35)" con búsqueda activa; footer "12 de 35 pistas"; estado vacío centrado
  > ("biblioteca vacía — pulsa + carpeta" / "sin resultados para \"…\""). ✅ **Cabeceras
  > + hints**: `TableHeadersRow` con `ImGuiCol_Text` dim (uppercase TÍTULO/BPM/KEY/TIME);
  > hints de atajos alineados a la derecha del footer, sólo en panel izquierdo (los
  > atajos operan sobre `lib_state`). ✅ **Verificado**: screenshot — elipsis en títulos
  > largos ("House - ..."), sort ▲ en cabeceras, hints visibles, "(35)" en cabecera.

- **F3.9 — Sync BPM entre decks + BPM efectivo visible + pitch ±16%.**
  Botón SYNC por deck que ajusta el pitch para igualar el BPM efectivo del otro
  deck (BPM-matching, el núcleo del beatmatching digital). El número grande de
  BPM muestra ahora el efectivo (con pitch); el base aparece en el label cuando
  hay desviación. Rango del knob de pitch ampliado a ±16% (cubre 120↔140 BPM).
  **DoD:** botón SYNC en cada deck (acentuado mientras activo + pill "SYNC" en
  la info), al pulsar iguala el BPM del otro deck y lo sigue en vivo, click
  > de nuevo o mover el pitch lo desactiva, sólo un sync activo a la vez, atajos
  S (deck A) y F (deck B), display muestra BPM efectivo, 75/75 tests.
  > **Estado: completo.** ✅ **Modelo**: BPM-matching puro (sin fase de beat —
  > requiere beat grids con offset real, pendiente con detección de onsets).
  > SYNC activo = este deck sigue al otro; el otro es el "master" implícito.
  > Sólo un sync a la vez (activar en un deck desactiva el otro) para evitar
  > bucles de realimentación. ✅ **UI** (`deck_panel.cpp/.hpp`):
  > `DeckUiState` gana `sync_on`, `DeckPanelOut` gana `sync_pressed`. Botón
  > "SYNC" 44×28 tras el bloque LOOP (divider + acento cuando activo, estilo
  > del botón LOOP). Pill "SYNC" en la fila de info junto a DECK A/B.
  > `pitch_knob` ampliado a ±16% (clamp y mapeo de ángulo). Display de BPM:
  > número grande = efectivo (`bpm_pitched`); label "B P M" muestra el base
  > (`B P M 128.0`) cuando `|pitch| > 0.05`. ✅ **Lógica** (`main.cpp`):
  > `bool sync_on[2]` izado junto a hot_cues/loop; limpiado en
  > `clear_deck_marks` (al cargar pista). `draw_deck_column`: `sync_pressed`
  > alterna (y apaga el otro); `pitch_changed` (knob o teclado) desactiva
  > `sync_on[d]` (pitch manual = control manual). Follow en vivo: cada frame
  > con sync activo recalcula `pitch = (bpm_efectivo_otro / bpm_base - 1)×100`
  > clamp ±16 y llama `audio_spike_set_pitch`. ✅ **Atajos**: S = sync deck A,
  > F = sync deck B (mutuamente excluyentes). ✅ **Verificado**: screenshot —
  > botones SYNC visibles en ambos decks entre LOOP y PITCH; 75/75 tests.

- **F3.10 — Configuraciones: idioma (ES/EN/PT-BR) + atajos rebindables.**
  Botón "AJUSTES" en la top bar (junto al wordmark) abre un modal con pestañas
  General (selector de idioma) y Atajos (rebind de teclas); persistencia en
  `settings.json` junto al exe. Toda la UI visible responde al idioma.
  **DoD:** botón AJUSTES en la top bar, modal con las dos pestañas, cambio de
  idioma aplica de inmediato a biblioteca/mixers/decks/footer, atajos
  rebindables y persistentes, 75/75 tests.
  > **Estado: completo.** ✅ **Modelo** (`settings.hpp/.cpp`): `Settings`
  > con `lang` (enum 0=ES,1=EN,2=PT-BR) y `keys[Action]` (ImGuiKey), load/save
  > clave=valor plano en `settings_path()` (junto al exe), defaults al no existir;
  > `global_settings()` singleton cargado en startup antes de `ShowWindow`.
  > ✅ **i18n** (`i18n.hpp/.cpp`): enum `Str` (~45 entradas) + tabla estática
  > [ES, EN, PT-BR] en orden idéntico al enum; `tr(Str)` lee `global_settings().lang`.
  > Aplicado a `library_panel`, `deck_panel`, `mixer_panel` y footer de `main.cpp`
  > (títulos, tooltips, hints, estados vacíos, labels de mixer, SYNC/LOOP/CUE, etc.).
  > ✅ **UI** (`settings_ui.hpp/.cpp`): `draw_settings_modal(bool*)` — modal
  > centrado con `DisplaySize*0.5` (pivot 0.5), fondo opaco vía
  > `PushStyleColor(ImGuiCol_WindowBg)` (se limpia tras `End()`); pestañas
  > General (radio ES/EN/PT-BR) y Atajos (filas por acción, click → espera de
  > tecla → `ImGuiKey_None` check + `SetKeyboardFocusHere`); cerrar con Close o X.
  > Dibujado al FINAL del frame (tras el footer) para quedar encima; el pase de
  > waveforms D3D12 se omite (`!open_settings`) mientras está abierto.
  > ✅ **Integración** (`main.cpp`): botón "AJUSTES" (texto, estilo transparente
  > con hover, tooltip) en `draw_chrome`; handler de teclado con `kdown` lambda
  > sobre `global_settings().key(Action::X)` (Enter/Shift+Enter hardcodeados como
  > fallback LoadA/LoadB); CMakeLists con `i18n.cpp`, `settings.cpp`,
  > `settings_ui.cpp`. ✅ **Icono descartado**: el engranaje Phosphor (0xE270)
  > era inutilizable — Inter-Regular carga primero y sus 764 glifos PUA eclipsan
  > los del engranaje; sin duplicados libres → botón de texto por decisión del
  > usuario (gear eliminado de `theme.hpp` + `kIcons`). ✅ **Verificado**:
  > screenshots — AJUSTES visible, modal centrado/opaco, cambio a EN traduce
  > toda la UI visible, Close cierra; 75/75 tests.
  > ✅ **Correcciones post-F3.10 (3 bugs reportados)**:
  > (1) *z-order* — un clic en `##body` enfocaba esa ventana y ImGui la mandaba
  > al frente de la lista de dibujo, tapando el modal; ahora `draw_settings_modal`
  > llama `ImGui::BringWindowToDisplayFront(GetCurrentWindow())` cada frame tras
  > `Begin` (sólo re-ordena, no roba foco) — `settings_ui.cpp`.
  > (2) *arranque con 2 sonidos* — `audioDeviceAboutToStart` hacía `play()` en
  > ambos decks con los sines de prueba; quitado, arrancan en pausa.
  > (3) *load auto-play* — el swap de `pending_load` hacía `deck.play()` tras
  > `load()`; ahora preserva el estado previo (`was_playing`), una carga no
  > arranca sola. Además los atajos de teclado se bloquean con el modal abierto
  > (el rebind no dispara la acción a la vez). 75/75 tests; runtime verificado
  > con screenshots (modal encima tras clic en deck/biblioteca, arranque en
  > pausa, carga de "Emotion Drive" en pausa).

- **F3.11 — Detección Tonal, Camelot Wheel y Notación Musical.**
  Integración de análisis armónico y notación Camelot (1A-12B) junto con nombres
  tradicionales (Cm, G#m, etc.). Persistencia en SQLite y badge armónico visible
  en los displays de deck y la biblioteca.
  > **Estado: completo.** ✅ Notación armónica Camelot y tradicional calculada
  > e integrada en la base de datos de pistas; coloreado armónico en UI.

- **F3.12 — Master Tempo (Pitch Lock) y Time-Stretching WSOLA.**
  Preservación de la afinación original al alterar el pitch (tempo) del deck mediante
  algoritmo WSOLA de baja latencia. Key shifting independiente de ±12 semitonos.
  > **Estado: completo.** ✅ Pitch lock operativo con botón MT en la UI, key shift
  > en tiempo real y transiciones sin artefactos sonoros.

- **F3.13 — Beatgrid Alignment, Cuantización y Beat Phase Matching.**
  Detección de fase de compás y alineación de transientes. Modo Quantize [Q]
  conmutable desde la barra superior y persistente en configuración.
  > **Estado: completo.** ✅ Rejilla de compases visible sobre la onda a 144 Hz,
  > saltos de Cue y Loop cuantizados a tiempo musical.

- **F3.14 — Footer de Biblioteca en Dos Filas y 100% de Traducciones.**
  Rediseño del footer eliminando espacios muertos, separando los atajos de teclado
  del visor de estado de pista completa. Tabla i18n ampliada a 119 cadenas en
  Español, Inglés y Portugués (Brasil).
  > **Estado: completo.** ✅ Doble fila en footer con badges coloreados y
  > micro-keycaps; traducción total sin textos hardcodeados.

- **F4 — Streaming en Línea Nativo: YouTube & SoundCloud.**
  Integración de fuentes de audio en línea mediante `yt-dlp` y `ffmpeg` por tuberías
  asíncronas. Búsqueda en vivo (`ytsearch`, `scsearch`), preescucha, transcodificación
  automática a WAV flotante de 32 bits, persistencia local y auto-importación a SQLite.
  > **Estado: completo.** ✅ Búsqueda instantánea en YouTube y SoundCloud,
  > cola de descarga en segundo plano y reproducción inmediata sin xruns.

- **F4.5 — Identidad Visual, Logotipo Oficial Colosseum y Splash Screen.**
  Icono de aplicación multirresolución (`app/res/melodeus.ico`, 16x16 a 256x256 px),
  recurso nativo `melodeus.rc`, textura D3D12 en descriptor SRV (`logo_texture.cpp`),
  Splash Screen animado al inicio con fade-out a 1.8 s, micro-logo en barra superior,
  ventana maximizada por defecto y modal interactivo de Ayuda (`F1` / `[ AYUDA ]`).
  > **Estado: completo.** ✅ Gráficos integrados con cero dependencias externas
  > en tiempo de ejecución.

- **F4.6 — Pestaña "Acerca de", Términos y Reconocimiento de Licencias.**
  Pestaña dedicada en Ajustes y archivo de términos de licencia legal con créditos
  de orquestación a danedsant con Antigravity y OpenCode, métricas de desarrollo
  con Gemini 3.8 Flash High (Google DeepMind), ~14.5M tokens procesados y aviso legal
  de los 10 componentes de terceros.
  > **Estado: completo.** ✅ Sección completa en `settings_ui.cpp` y `license.txt`.

- **F4.7 — Instalador Autónomo para Windows (`Melodeus_Setup_v1.0.exe`).**
  Script de Inno Setup 6 (`installer/melodeus_setup.iss`) con compresión sólida LZMA2
  (reduce ~155 MB a 63.5 MB), gráficos del logo en el asistente (`banner.bmp`, `small.bmp`),
  empaquetado de `yt-dlp.exe` y `ffmpeg.exe` en `{app}\tools` y `{app}\external` para
  streaming out-of-the-box sin descargas adicionales, y accesos directos con icono oficial.
  > **Estado: completo.** ✅ Instalador probado y generado en `installer/Output/Melodeus_Setup_v1.0.exe`.

- **F5 — Stems ≤ 500 ms (Próxima fase).**
  Inferencia con modelo ONNX y DirectML para separación de fuentes en vivo.

- **F6 — MIDI / HID y Controladores Físicos (Próxima fase).**
  Mapeo MIDI Learn y soporte para controladoras DJ USB.

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

---

## 10. Créditos, Tecnologías de Ingeniería y Métricas de Desarrollo

- **Diseño y Orquestación de Arquitectura**: `danedsant`
- **Plataformas de Ingeniería**: `Antigravity` & `OpenCode`
- **Modelo de Inteligencia Artificial**: `Gemini 3.8 Flash High (Google DeepMind)`
- **Métricas de Cómputo Cognitivo**:
  - `~14.5M tokens procesados` a través de las fases F0 a F4.7.
  - Alcance: Shaders HLSL a 144 Hz, DSP de tiempo real (<5ms), decodificadores sin bloqueo, layout DJ simétrico en ImGui/Win32, streaming de YouTube/SoundCloud, base de datos SQLite con FTS5, internacionalización y empaquetado autónomo con Inno Setup.

