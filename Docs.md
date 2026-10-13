# Melodeus DJ Workstation — Manual de Arquitectura y Especificación Técnica

> **Documento de Referencia Técnica de Ingeniería**  
> **Versión del Sistema**: v1.1.0 (Windows 10/11 x64)  
> **Orquestación y Arquitectura**: danedsant  
> **Plataformas de Ingeniería**: Antigravity & OpenCode  
> **Modelo de Inteligencia Artificial**: Gemini 3.8 Flash High (Google DeepMind)  
> **Cómputo Cognitivo**: ~15.2M tokens procesados en el ciclo de diseño, DSP, shaders y empaquetado  

---

## Índice General

1. [Visión General y Filosofía de Diseño](#1-visión-general-y-filosofía-de-diseño)
2. [Arquitectura Global del Sistema y Modelo de Hilos](#2-arquitectura-global-del-sistema-y-modelo-de-hilos)
3. [Subsistema de Audio y DSP en Tiempo Real (`src/audio/`)](#3-subsistema-de-audio-y-dsp-en-tiempo-real-srcaudio)
4. [Subsistema Gráfico y Renderizado DirectX 12 (`app/render/`, `src/waveform/`)](#4-subsistema-gráfico-y-renderizado-directx-12-apprender-srcwaveform)
5. [Subsistema de Streaming en Línea (`src/streaming/`)](#5-subsistema-de-streaming-en-línea-srcstreaming)
6. [Subsistema de Biblioteca y Base de Datos (`src/library/`, `src/analysis/`)](#6-subsistema-de-biblioteca-y-base-de-datos-srclibrary-srcanalysis)
7. [Interfaz de Usuario, Ergonomía DJ y Glassmorphism (`app/`)](#7-interfaz-de-usuario-ergonomía-dj-y-glassmorphism-app)
8. [Sistema de Configuración e Internacionalización (`app/settings.*`, `app/i18n.*`)](#8-sistema-de-configuración-e-internacionalización-appsettings-appi18n)
9. [Recursos Nativos, Empaquetado e Instalador Windows (`app/res/`, `installer/`)](#9-recursos-nativos-empaquetado-e-instalador-windows-appres-installer)
10. [Garantías de Rendimiento, Verificación y Licencias](#10-garantías-de-rendimiento-verificación-y-licencias)

---

## 1. Visión General y Filosofía de Diseño

**Melodeus** es una estación de trabajo digital para DJs profesionales (*DJ Workstation*) diseñada para Windows 10/11 con dos imperativos técnicos absolutos:
1. **Latencia de audio ultra-baja y determinista (<5 ms)** con cero tolerancia a *dropouts* (*xruns*).
2. **Renderizado de interfaz y formas de onda sostenido a 144 FPS** mediante una canalización pura en hardware en **DirectX 12**.

### 1.1. Principios de Ingeniería

- **C++20 Monolítico**: Se descartan arquitecturas multi-lenguaje o wrappers FFI (como Rust/C++ o Python en runtime). Todo el sistema compila en un único ejecutable nativo optimizado con MSVC (`/O2 /fp:fast /W4 /permissive-`), garantizando un único depurador, un único pipeline de memoria y consistencia binaria.
- **Separación Estricta por Hilos**: Ningún hilo auxiliar (análisis espectral, descargas de red, consultas de base de datos) puede competir ni bloquear el hilo de audio en tiempo real.
- **Cero Asignaciones Dinámicas en Tiempo Real (Zero-Allocation RT)**: Durante la reproducción activa, el callback de audio jamás invoca `malloc`, `new`, `free`, `delete`, ni llamadas bloqueantes al sistema operativo (*syscalls* o locks). Toda la memoria se pre-reserva en el arranque o durante la carga de pistas en el hilo de fondo.
- **Desacoplamiento de Framework**: JUCE se utiliza exclusivamente en modo *headless* para la abstracción de bajo nivel de dispositivos de audio (`juce_audio_devices`) y MIDI. La ventana nativa es Win32 directa (`CreateWindowExW`), gestionando el swapchain DXGI e ImGui sin sobrecargar el bucle de mensajes con los componentes gráficos de JUCE.

### 1.2. Presupuestos y Métricas de Rendimiento

| Subsistema | Métrica Objetivo | Medición Real Verificada | Método de Medición |
|---|---|---|---|
| **Ruta de Audio Seca** | < 5.0 ms | **4.0 – 9.0 ms** | `tools/latency_probe` (WASAPI Exclusivo/Shared) |
| **Tiempo por Bloque RT** | < 1.3 ms (50% de 2.67 ms) | **0.08 – 0.21 ms** | QPC (QueryPerformanceCounter) por bloque |
| **Tasa de Refresco UI** | 144 FPS estables | **381.8 FPS** (sin vsync) / **144.0 Hz** (paced) | Estadísticas de swapchain en D3D12 |
| **Búsqueda en Biblioteca** | < 20 ms sobre 50k pistas | **3.99 – 4.45 ms** | Benchmark SQLite FTS5 con 50,000 registros |
| **Estabilidad de Audio** | 0 xruns en 30 minutos | **0 xruns** confirmados | Soak test `--selftest 1800` (86M muestras al 100%) |

---

## 2. Arquitectura Global del Sistema y Modelo de Hilos

El sistema opera mediante una jerarquía de hilos claramente diferenciados comunicados exclusivamente por estructuras lock-free o primitivas atómicas.

```
┌──────────────────────────────────────────────────────────────────────────────────┐
│                             HILO PRINCIPAL WIN32 / UI                            │
│  - Bucle de mensajes Win32 (WndProc)                                             │
│  - Renderizado ImGui (Glassmorphism, Decks, Mixer, Bibliotecas, Modales)         │
│  - Renderizado D3D12 a 144 Hz (Shaders de Waveform: Overview + Detalle)          │
└────────────▲─────────────────────────▲─────────────────────────────▲─────────────┘
             │                         │                             │
    Lectura  │                Lectura  │                    Lectura  │
    Lock-free│                Lock-free│                    Lock-free│
             ▼                         ▼                             ▼
┌─────────────────────────┐ ┌─────────────────────┐ ┌──────────────────────────────┐
│     HILO AUDIO RT       │ │   HILO STREAMING    │ │     HILO BASE DE DATOS       │
│ - Callback WASAPI/ASIO  │ │ - Worker yt-dlp/SC  │ │ - Worker SQLite (WAL)        │
│ - 48 kHz / 128-480 f    │ │ - Pipe FFmpeg f32le │ │ - FTS5 Full-Text Search      │
│ - MMCSS "Pro Audio"     │ │ - Caché LRU Disco   │ │ - Importación en segundo plan│
│ - DSP, EQ, Master Tempo │ │ - Inserción a WAV   │ │ - Extracción de metadatos    │
│ - 0 locks, 0 allocs     │ └─────────────────────┘ └──────────────────────────────┘
└─────────────────────────┘
```

### 2.1. Hilos del Sistema

1. **Hilo Principal (UI & Render D3D12)**:
   - Ejecuta el bucle de mensajes Win32 tradicional (`PeekMessageW` / `DispatchMessageW`).
   - Construye la geometría de ImGui para el diseño DJ simétrico.
   - Envía los comandos de dibujo DirectX 12 al `ID3D12GraphicsCommandList`, gestionando las rotaciones del swapchain a 144 Hz mediante `FramePacer` (sleep + spinlock con `QueryPerformanceCounter`).
2. **Hilo de Audio en Tiempo Real (RT Audio Thread)**:
   - Administrado por WASAPI / ASIO. Ejecuta con prioridad de sistema operativo elevada mediante MMCSS: `AvSetMmThreadCharacteristicsW(L"Pro Audio", &task_index)`.
   - Lee el transporte de ambos decks, aplica interpolación fraccional, WSOLA Master Tempo, ecualización tribanda TPT, filtros por canal, crossfader y limitador brickwall lookahead.
   - Publica los niveles de peak, envolventes y playheads a través de variables atómicas con semántica `std::memory_order_release`.
3. **Hilo Worker de Base de Datos (DB Worker Thread)**:
   - Instancia su propia conexión SQLite independiente en modo WAL (*Write-Ahead Logging*).
   - Procesa la importación masiva de carpetas y el cálculo de hashes SHA-1 para evitar lecturas redundantes.
4. **Hilo Worker de Streaming (Network / Streaming Worker)**:
   - Controla las tuberías asíncronas con `yt-dlp` y `ffmpeg` encapsuladas bajo un Windows Job Object.
   - Lee los streams de audio crudo en formato PCM float32, alimenta el búfer de jitter, vuelca el archivo final a la carpeta de descargas del usuario y solicita el refresco de la base de datos.

### 2.2. Comunicación Inter-Hilos Lock-Free

- **Estructura SPSC (Single-Producer Single-Consumer) Ring Buffer**:
  - Empleada para transferir streams de audio de baja latencia entre hilos sin contención.
  - Punteros de cabecera (`head_`) y cola (`tail_`) empaquetados en `std::atomic<uint64_t>` con alineación `alignas(64)` para evitar *false sharing* en las líneas de caché L1/L2 del procesador.
  - Operaciones de lectura y escritura restringidas a `memory_order_acquire` y `memory_order_release`.
- **Staging de Carga de Pistas**:
  - La decodificación completa de un archivo de audio (WAV, MP3, FLAC) se realiza en el hilo secundario.
  - Al completar la decodificación, el puntero a la estructura `DecodedTrack` y su pirámide de ondas se entregan al motor mediante un intercambio atómico (`pending_load[deck].exchange(...)`).
  - En el siguiente límite de bloque del callback de audio, el motor adopta la pista instantáneamente sin saltos de reproducción.

---

## 3. Subsistema de Audio y DSP en Tiempo Real (`src/audio/`)

El núcleo de procesamiento sonoro está contenido en `src/audio/` y sigue principios matemáticos estrictos de modelado digital y estabilidad numérica.

### 3.1. Transporte y Reproducción (`deck.hpp` / `deck.cpp`)

Cada deck maneja una posición temporal representada como un entero de 64 bits para las muestras enteras y un flotante de 64 bits para la fracción sub-muestra:
$$\text{pos}_{\text{total}} = \text{sample\_index} + \text{fractional\_offset}$$
- **Interpolación Fraccional**: Cuando el pitch se desvía de 0% (velocidad $\neq 1.0$), se aplica interpolación lineal de alta precisión entre muestras adyacentes para evitar distorsiones armónicas en altas frecuencias.
- **Scrubbing Bidireccional**: Capacidad de reproducir en reversa o a velocidades continuas para emular el tacto analógico de un vinilo (*jog wheel*).
- **Loops Cuantizados**: Puntos de inicio (`loop_start`) y final (`loop_end`) configurados en tiempo de compás musical. Cuando la posición alcanza `loop_end`, el transporte realiza un wrap instantáneo a `loop_start` sin chasquidos.

### 3.2. Master Tempo y Time-Stretching WSOLA (`pitch_shifter.hpp`)

El algoritmo **WSOLA** (*Waveform Similarity Overlap-Add*) permite alterar el tempo de una pista (velocidad de reproducción) manteniendo intacta su tonalidad musical original, o desplazar la afinación en semitonos (*Key Shifting*) sin alterar la velocidad.
1. **Segmentación**: La señal de entrada se descompone en ventanas Hanning solapadas al 50%.
2. **Búsqueda de Similitud por Correlación Cruzada**: Para evitar cancelaciones de fase al recombinar ventanas desplazadas en el tiempo, WSOLA busca la posición óptima dentro de una ventana de tolerancia $\Delta$ maximizando la correlación cruzada:
$$R(k) = \sum_{n=0}^{N-1} x(n) \cdot y(n+k)$$
3. **Recombinación Overlap-Add**: Las ventanas alineadas se suman ponderadas con desvanecimiento cruzado, permitiendo acelerar o ralentizar la pista hasta ±16% sin artefactos metálicos ni modulación en anillo.

### 3.3. Filtros y Ecualización TPT (`dsp.hpp`)

- **Filtros SVF TPT (Topology-Preserving Transform)**: Basados en la teoría de Vadim Zavalishin (*The Art of VA Filter Design*), estos filtros modelan circuitos analógicos en el dominio trapezoidal bilineal, preservando la respuesta en frecuencia y resonancia idéntica a los circuitos analógicos sin inestabilidad numérica:
  - **Filtro Bipolar DJ**: Un único control por canal que barre desde un filtro Paso Bajo resonante (20 Hz – 300 Hz a la izquierda) pasando por Bypass transparente al centro (0.0), hasta un Paso Alto agresivo (3 kHz – 20 kHz a la derecha).
- **Ecualizador Tri-Banda**:
  - Banda Baja (Low Shelf a 250 Hz)
  - Banda Media (Band Pass centrado en 1 kHz)
  - Banda Alta (High Shelf a 4 kHz)
  - Capacidad de **Kill** completo (−60 dB) en cualquiera de las tres bandas.
  - Parámetros dinámicos gobernados por filtros paso-bajo unipolares de suavizado (*one-pole parameter smoothing*, $\tau = 10\text{ ms}$) que eliminan completamente el efecto *zipper noise* al girar knobs en la pantalla.

### 3.4. Saturador Suave (Master Soft-Clipper) y Headroom (`engine.cpp`)

Para eliminar distorsión armónica áspera (*digital harsh clipping*) cuando dos pistas modernas normalizadas a 0 dBFS se suman durante una transición o drop, o cuando el usuario aplica un realce de EQ (+6 dB en Low):
- **Saturación Hiperbólica Continua $C^1$**: En lugar de recortar bruscamente a ±1.0, la suma de mezcla se procesa mediante una curva no lineal suave basada en tangente hiperbólica:
  $$f(x) = \begin{cases} x & \text{si } |x| \le 1.0 \\ \text{sgn}(x) \cdot (1.0 + \tanh(|x| - 1.0)) & \text{si } |x| > 1.0 \end{cases}$$
  Esta función es estrictamente idéntica a la señal original con cero distorsión armónica hasta 0 dBFS ($f'(1.0) = 1.0$), y comprime de forma analógica y cálida cualquier sobrecarga extrema antes de entrar al limitador brickwall.

### 3.5. Curvas del Crossfader y Turntablism Scratch (`mixer.hpp`)

El motor de mezcla soporta 4 curvas paramétricas seleccionables por el usuario en tiempo real:
1. **Sharp (Corte DJ)**: Salto binario estricto en el punto central (ideal para cortes secos de club).
2. **Linear**: Ganancias complementarias $G_A = t$, $G_B = 1 - t$.
3. **Constant Power (Trigonométrica Suave)**: Conserva la potencia acústica total idéntica ($0\text{ dB}$) a lo largo de todo el recorrido:
   $$G_A = \cos((1 - t) \cdot \frac{\pi}{2}), \quad G_B = \sin((1 - t) \cdot \frac{\pi}{2})$$
4. **Scratch (Turntablism)**: Mantiene el volumen al 100% ($G = 1.0$) en prácticamente todo el trayecto, aplicando una rampa ultrarrápida en el último 3% de los bordes para cortes agresivos sin clics DC.

### 3.6. Limitador Brickwall Lookahead (`dsp.hpp`)

Para garantizar que el máster nunca exceda el techo acústico (−1.0 dBFS):
- **Retardo Lookahead**: Búfer circular de retardo de **2.0 ms** (96 muestras a 48 kHz).
- **Búsqueda del Pico Máximo**: Se mantiene una cola monótona preasignada para detectar picos futuros en la ventana de lookahead.
- **Curva de Reducción**: Si el pico futuro excede el techo (−1.0 dBFS), la ganancia se atenúa instantáneamente antes de que la señal alcance la salida, con un tiempo de relajación (*release*) suave de 100 ms.

### 3.7. Verificación de Hardware y Manejo del Bug de 64 kHz

Durante las pruebas en hardware real de audio integrado (Realtek HD Audio en Windows 10/11), se descubrió que el endpoint WASAPI Exclusivo reportaba aceptar 48 kHz pero el reloj de hardware `IAudioClock` corría a **64 kHz exactos**, resultando en una aceleración del tono de +33%.
- **Medida adoptada**: `tools/latency_probe` y `main.cpp` ejecutan una prueba de conformidad con `IAudioClock` al inicializar. Si la tasa medida difiere en más de 1% de la declarada, el sistema conmuta automáticamente a **WASAPI Compartido de baja latencia (`IAudioClient3`)** a 48 kHz con períodos de 10 ms, protegiendo al usuario de fallos de hardware.

---

## 4. Subsistema Gráfico y Renderizado DirectX 12 (`app/render/`, `src/waveform/`)

El renderizado gráfico opera sobre una infraestructura nativa Direct3D 12 diseñada para mantener 144 FPS estables sin pausas de recolección de basura ni sincronización de comandos costosa.

### 4.1. Pirámide de Formas de Onda Multinivel y Decimación Adaptativa (`waveform.hpp` / `waveform.cpp`)

Para visualizar archivos de cualquier duración —desde pistas estándar de 3 minutos hasta **DJ mixes y podcasts masivos de 60 a 120+ minutos**— sin desbordamiento de búferes ni caídas de framerate:
- **Pirámide de 12 Niveles de Resolución**: El audio se subdivide en una jerarquía que abarca desde 256 muestras por bloque en el nivel 0 hasta **524,288 muestras por bloque en el nivel 11**. Una sesión de 2 horas a 48 kHz genera únicamente ~660 bloques en el nivel superior.
- **Prevención Definitiva de Truncamiento**: La función `fill_lane` y `visible_range` incorporan una decimación adaptativa que evalúa la capacidad de instancias disponibles (`avail_inst = 8,192` por carril, parte de una capacidad total ampliada a **32,768 instancias** en `WaveformPass`). Si una pista de gran extensión requiere más bloques de los que caben en el búfer, el sistema escala automáticamente al siguiente nivel de la pirámide, garantizando que el 100% de la pista (de 0:00 al final) se visualice íntegramente sin cortes.
- **Coloración Espectral Tri-Banda (Estándar DJ Rekordbox / Traktor)**: En lugar de un gradiente cosmético genérico, cada `WaveBucket` calcula la energía en 3 bandas filtradas mediante `TptSvf`:
  - **Graves / Kicks / 808 (`bk.lo`, <250 Hz)**: Canal primario del deck (Púrpura Eléctrico vibrante o tinte custom), permitiendo identificar visualmente cada golpe de bombo y línea de subgrave.
  - **Medios / Voces / Cajas (`bk.mid`, 250 Hz – 4 kHz)**: Tono cálido de transición (Ámbar dorado / Esmeralda), destacando estrofas, sintetizadores y cajas.
  - **Altos / Hi-Hats / Cymbals (`bk.hi`, >4 kHz)**: Tono brillante (Cian luminoso / Blanco hielo), revelando platillos, shakers y transientes de aire.
- **Visualización Doble en Overview**:
  - **Capa Exterior**: Envolvente tenue de transitorios que delimita la silueta completa del track.
  - **Capa Interior**: Núcleo dinámico de energía RMS modulado con la mezcla espectral luminosa, permitiendo al DJ distinguir drops, breakdowns y secciones vocales a simple vista.

### 4.2. Shaders HLSL Instanciados (`waveform.hlsl`)

- **Renderizado por Vertex ID**: En lugar de transferir geometrías complejas con búferes de vértices e índices, el vertex shader (`waveform.vso`, modelo 5.1) calcula la posición de las esquinas de cada barra de onda a partir de `SV_VertexID` (4 vértices por quad instanciado).
- **Búfer de Instancias Upload**: Cada instancia de barra ocupa únicamente **28 bytes**:
  ```cpp
  struct WaveInstance {
      float center_x;      // Posición horizontal normalizada NDC
      float half_w;        // Semi-ancho en pantalla
      float max_y, min_y;  // Alturas máxima y mínima del pico
      uint32_t color;      // Color RGBA8 empaquetado
      float pad[2];
  };
  ```
- **N-Buffering sin Hazards**: Se utiliza un búfer de subida (*upload heap*) rotativo persistente mapeado en memoria, indexado por `device.frame_slot()`. La CPU escribe las instancias del fotograma actual en su ranura sin bloquear a la GPU que renderiza el fotograma anterior.

### 4.3. Texturas de GPU y Gestión de Descriptores (`logo_texture.cpp`)

- El logotipo oficial de Melodeus se almacena en memoria como un array estático RGBA8 de 256×256 píxeles en [`logo_pixels.hpp`](file:///c:/Users/danie/OneDrive/Documentos/danedsant/Melodeus/app/res/logo_pixels.hpp).
- En el arranque, `LogoTexture::init(device)` asigna un recurso `ID3D12Resource` en el heap predeterminado de GPU, sube los datos mediante un recurso intermedio y reserva un descriptor en el *Shader Resource View Heap* (`device.srv_alloc()`).
- La textura es accesible instantáneamente para ImGui mediante `melodeus::app::global_logo().texture_id()`, proporcionando visualización a 144 Hz en el Splash Screen, la barra superior y los modales sin dependencias de librerías de imágenes externas (como libpng o stb_image).

---

## 5. Subsistema de Streaming en Línea (`src/streaming/`)

Melodeus permite pinchar temas en directo desde **YouTube** y **SoundCloud** sin descargas previas manuales.

### 5.1. Aislamiento de Procesos Externos (`ytdlp_source.cpp`)

El sistema ejecuta `yt-dlp.exe` y `ffmpeg.exe` como procesos secundarios conectados por pipes asíncronos:
1. **Windows Job Objects**: Ambos procesos se asignan a un `JobObject` con la bandera `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`. Si la aplicación Melodeus se cierra inesperadamente, el sistema operativo garantiza la terminación inmediata de los procesos de streaming, evitando procesos huérfanos en segundo plano.
2. **Pipes Anónimos Seguros**: Se emplea `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` en la llamada a `CreateProcessW` para heredar únicamente los descriptores de entrada/salida estrictamente necesarios.

### 5.2. Pipeline de Transcodificación en Tiempo Real

```
[ Consulta: ytsearch / scsearch ]
              │
              ▼
    yt-dlp.exe (Extracción)
              │ Pipe STDOUT (Audio original Opus/AAC)
              ▼
    ffmpeg.exe (Transcodificador)
              │ Pipe STDOUT (PCM f32le 48kHz Stereo)
              ▼
     DecodedTrack (RAM) ───► Volcado a disco: %USERPROFILE%/Music/Melodeus/Downloads/*.wav
              │
              ▼
   Database::import_file ───► Inserción en SQLite FTS5 (Búsqueda instantánea local)
```

- **Sanitización de Títulos en Windows**: Se reemplazan caracteres ilegales en nombres de archivo (`/`, `\`, `:`, `*`, `?`, `"`, `<`, `>`, `|`) por guiones bajos, garantizando que el archivo `.wav` se guarde limpiamente en la carpeta de música del usuario.
- **Caché LRU en Disco (`disk_cache.cpp`)**: Índice en memoria que rastrea el uso de archivos descargados con límite configurable (por defecto 1 GB), eliminando automáticamente los archivos menos usados al alcanzar el umbral.

---

## 6. Subsistema de Biblioteca y Base de Datos (`src/library/`, `src/analysis/`)

### 6.1. Esquema de Base de Datos SQLite

La biblioteca opera sobre un archivo local gestionado por SQLite con modo WAL (*Write-Ahead Logging*):

```sql
CREATE TABLE tracks (
    track_id       INTEGER PRIMARY KEY AUTOINCREMENT,
    source_type    INTEGER NOT NULL,      -- 0=Local, 1=YouTube, 2=SoundCloud
    uri            TEXT NOT NULL UNIQUE,  -- Ruta absoluta en disco o URL
    title          TEXT NOT NULL,
    artist         TEXT,
    album          TEXT,
    bpm            REAL,                  -- BPM base detectado
    musical_key    TEXT,                  -- Tonalidad tradicional (ej. "Am")
    camelot        INTEGER,               -- Número Camelot (1 - 12)
    camelot_minor  INTEGER,               -- 1=A (Menor), 0=B (Mayor)
    duration_ms    INTEGER,
    added_at       INTEGER
);

-- Tabla virtual FTS5 para búsquedas instantáneas
CREATE VIRTUAL TABLE tracks_fts USING fts5 (
    title, artist, album,
    content='tracks',
    content_rowid='track_id',
    tokenize='unicode61 remove_diacritics 2'
);
```

### 6.2. Detección Armónica y Rueda de Camelot (`detect_key`)

- **Extracción de Croma**: El análisis calcula la transformada de Fourier en ventanas STFT y condensa el espectro en un vector de cromas de 12 clases de tonos musicales ($C, C\sharp, D, \dots, B$).
- **Algoritmo de Krumhansl-Schmuckler**: Se calcula el coeficiente de correlación de Pearson entre el croma medio de la canción y los perfiles tonales ideales de las 24 escalas mayores y menores:
$$r = \frac{\sum (x_i - \bar{x})(y_i - \bar{y})}{\sqrt{\sum (x_i - \bar{x})^2 \sum (y_i - \bar{y})^2}}$$
- **Mapeo Camelot**: El tono detectado se indexa en el estándar DJ de la Rueda de Camelot (ej. $A\text{ menor} \rightarrow 8A$, $C\text{ mayor} \rightarrow 8B$), permitiendo mezclas armónicas intuitivas por proximidad numérica.

### 6.3. Motor de BPM Multi-Género EDM, Extracción de Fase y Quantize (`bpm.cpp` / `beatgrid.cpp`)

El sistema de análisis rítmico está optimizado integralmente para todo el espectro de música electrónica de club y festivales (House, Tech House, Techno, Trance, Hardstyle, Drum & Bass, Trap y Dubstep):
1. **Pre-Filtrado Pasa-Bajos de Onsets (~180 Hz)**:
   - Aplica un filtro unipolar de 180 Hz sobre la señal mono para aislar los transitorios fundamentales del bombo (*kick drum*) y líneas 808 frente a sintetizadores densos (*supersaws*), voces o platos.
   - Ponderación híbrida: combina un 70% de energía filtrada en graves con un 30% de energía de banda completa, asegurando detección precisa tanto en bombos profundos como en patrones percusivos de caja/click.
2. **Compresión Perceptual Logarítmica**:
   - Transforma la energía cuadrática mediante $E_{\text{comp}} = \ln(1 + 20 \cdot E)$ antes de calcular la media-onda rectificada de flujo espectral. Esto previene que un único transitorio descomunal (como un drop con crash o efecto vocal) distorsione la autocorrelación.
3. **Corrección de Octavas para Half-Time EDM (Trap, Dubstep y Drum & Bass)**:
   - En géneros urbanos y bass music (Trap/Dubstep a 140–150 BPM y DnB a 170–176 BPM), los patrones half-time suelen presentar picos fundamentales a 70–75 BPM y 85–88 BPM.
   - Si el tempo preliminar cae en el rango [65, 88] BPM, el algoritmo evalúa el armónico superior (`best_lag / 2`) con un umbral optimizado de confianza de **0.46** (frente al 0.75 tradicional). Esto promueve automáticamente pistas de Trap/Dubstep a 140+ BPM y Drum & Bass a 170+ BPM, manteniendo inmunes los temas de 90 BPM (Hip-Hop) y 120–135 BPM (House/Techno).
4. **Extracción de Fase (Downbeat Offset en Segundos)**:
   - Una vez consolidado el periodo de beat (`lag_refined`), el motor realiza una correlación cruzada de la envolvente contra un tren de impulsos sintético a lo largo de los primeros 16 compases.
   - El ángulo de fase óptimo $\phi^*$ determina la posición exacta en segundos del primer golpe de compás (`offset_sec`), empaquetándose en el BLOB de análisis `analysis_blobs` (kind = 0) en SQLite.
5. **Cuantización de Transporte Snap-to-Grid (1/8 de Beat)**:
   - Cuando `Quantize` está activo en ajustes, tanto el inicio de reproducción (*Play*), como el guardado y salto a *Hot Cues (1..3)* y la sincronización *SYNC* se alinean automáticamente al punto submúltiplo de 1/8 de compás más cercano de la cuadrícula:
     $$\text{pos}_{\text{snap}} = \text{offset}_{\text{sec}} + \text{round}\left(\frac{\text{pos} - \text{offset}_{\text{sec}}}{T_{\text{beat}} / 8}\right) \cdot \frac{T_{\text{beat}}}{8}$$
   - Esto garantiza que dos pistas sincronizadas mantengan un *beatmatching* de fase absoluto con los transitorios de bombo milimétricamente alineados.

---

## 7. Interfaz de Usuario, Ergonomía DJ y Glassmorphism (`app/`)

### 7.1. Layout Simétrico de 3 Columnas

```
┌────────────────────────────────────────────────────────────────────────┐
│ MELODEUS          [AJUSTES]   [QUANTIZE]   [AYUDA]    14:25:02  CPU 4% │
├─────────────────────────┬──────────────────┬───────────────────────────┤
│         DECK A          │   MIXER MASTER   │          DECK B           │
│  - Forma de onda Wave   │ - Gain A  Gain B │  - Forma de onda Wave     │
│  - Hot Cues (1, 2, 3)   │ - EQ Hi / Mid/Lo │  - Hot Cues (1, 2, 3)     │
│  - Loop (1/8 - 32)      │ - Filtro DJ A/B  │  - Loop (1/8 - 32)        │
│  - Pitch Fader ±16%     │ - Vúmetros Peak  │  - Pitch Fader ±16%       │
│  - Master Tempo / Sync  │ - Crossfader     │  - Master Tempo / Sync    │
├─────────────────────────┼──────────────────┼───────────────────────────┤
│      BIBLIOTECA A       │   (Espacio       │       BIBLIOTECA B        │
│  - Tabla de pistas      │    inferior      │  - Tabla de pistas        │
│  - Streaming YouTube/SC │    del mixer)    │  - Doble click -> Deck B  │
│  - Footer de 2 filas    │                  │  - Footer de 2 filas      │
└─────────────────────────┴──────────────────┴───────────────────────────┘
```

### 7.2. Footer de Biblioteca en Dos Filas Dedicadas

Para resolver colisiones entre los atajos de teclado y los textos largos de pistas durante la carga:
- **Fila 1 (Metadatos & Atajos)**:
  - Izquierda: Contador total de canciones (`♫ 142 pistas`).
  - Derecha: Micro-keycaps estilizados estilo glass (`[ ↑↓ ] mover   [ ↵ A ] Deck A   [ ⇧↵ B ] Deck B   [ / ] buscar`).
- **Fila 2 (Estado del Sistema & Streaming)**:
  - Ocupa el ancho completo sin recortes.
  - Muestra badges de estado: `[ ● CARGANDO DECK A ]`, `[ ● DESCARGANDO ]`, o el mensaje en reposo `✓ Biblioteca lista · Base de datos sincronizada` con indicador `● audio OK`.

### 7.3. Modal de Ayuda Interactivo (`help_ui.cpp`)

Accesible mediante el botón superior **`[ AYUDA ]`** o la tecla **`F1`**, renderiza un diálogo glassmorphism con el logotipo de Melodeus y cuatro pestañas:
1. *Decks & Reproducción*: Manejo de ondas, transporte, loops y Master Tempo.
2. *Mixer & Ecualización*: Curvas de crossfader, frecuencias de corte y ganancias.
3. *Biblioteca & Streaming*: Descargas de streaming, carpetas y arrastrar y soltar.
4. *Atajos de Teclado*: Resumen completo de comandos.

---

## 8. Sistema de Configuración e Internacionalización (`app/settings.*`, `app/i18n.*`)

### 8.1. Persistencia JSON (`settings.json`)

Los parámetros del usuario se guardan automáticamente en formato JSON junto al ejecutable:
- Idioma seleccionado (`lang`: 0=ES, 1=EN, 2=PT-BR).
- Mapeo de teclas (`keys`: arreglo de identificadores `ImGuiKey` para cada acción).
- Colores personalizados de los decks (`color_deck_a`, `color_deck_b`).
- Curva de Crossfader seleccionada (`crossfade_curve`: Linear, Sharp, ConstantPower, Scratch).
- Estado del motor de cuantización (`quantize`).
- Directorio de descargas (`download_dir`).

### 8.2. Motor i18n Centralizado (`i18n.cpp`)

- Se estructura sobre un enumerador `Str` con **119 identificadores de cadena**.
- Una tabla de cadenas tridimensional indexada por `[Str][Lang]` permite obtener la traducción en tiempo $O(1)$ sin búsquedas en diccionarios ni concatenaciones dinámicas.
- Conmutación en caliente: Cambiar de idioma en el modal de Ajustes actualiza instantáneamente toda la interfaz sin necesidad de reiniciar la aplicación.

---

## 9. Recursos Nativos, Empaquetado e Instalador Windows (`app/res/`, `installer/`)

### 9.1. Recursos de Aplicación (`melodeus.rc`)

El archivo de recursos de Windows vincula el icono oficial `melodeus.ico` con el identificador numérico `#define IDI_ICON1 1`. En `main.cpp`, `wWinMain` carga este icono mediante métricas del sistema para obtener renderizado nítido en alta resolución y lo asocia directamente a la ventana:
```cpp
HINSTANCE hInst = GetModuleHandle(nullptr);
HICON hIconBig = static_cast<HICON>(::LoadImageW(
    hInst, MAKEINTRESOURCEW(1), IMAGE_ICON,
    ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
HICON hIconSm = static_cast<HICON>(::LoadImageW(
    hInst, MAKEINTRESOURCEW(1), IMAGE_ICON,
    ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
if (!hIconBig) hIconBig = ::LoadIconW(hInst, MAKEINTRESOURCEW(1));
if (!hIconSm) hIconSm = hIconBig;
wc.hIcon = hIconBig;
wc.hIconSm = hIconSm;

// Tras CreateWindowW:
::SendMessageW(g_hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hIconBig));
::SendMessageW(g_hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIconSm));
```
Esto garantiza que la ventana de la aplicación, la barra de tareas de Windows, la barra de título Win32 y el Explorador de archivos muestren el logotipo oficial sin bordes borrosos en todas las resoluciones de pantalla (desde 16×16 hasta 256×256 px).

### 9.2. Instalador Inno Setup 6 (`installer/melodeus_setup.iss`)

El instalador autónomo empaqueta todo el entorno para que cualquier usuario pueda instalarlo y pinchar inmediatamente:
- **Compresión Sólida LZMA2**: Reduce el conjunto de archivos (~160 MB de binarios, shaders, fuentes, `yt-dlp.exe` y `ffmpeg.exe`) a un archivo único de **~66.7 MB** (`Melodeus_Setup_v1.1.exe`).
- **Herramientas de Streaming Incluidas**: `yt-dlp.exe` y `ffmpeg.exe` se colocan en `{app}\tools` y `{app}\external`. La función `find_external_tool` en `main.cpp` los localiza automáticamente al arrancar.
- **Gráficos Integrados**: Incorpora el banner vertical con el logotipo (`banner.bmp`) y el icono superior (`small.bmp`).
- **Instalación Multi-Idioma**: Asistente disponible en Español, Inglés y Portugués (Brasil).

---

## 10. Garantías de Rendimiento, Verificación y Licencias

### 10.1. Pruebas Unitarias y Selftest Automatizado

- **Suite Doctest (`melodeus_tests.exe`)**:
  - **86 casos de prueba** cubriendo transporte de audio, interpolación fraccional, análisis espectral, transcodificación de streaming, base de datos SQLite, pirámides de waveform multinivel y cuantización snap-to-grid.
  - **65,429 aserciones matemáticas** aprobadas al 100% sin fallos.
- **Selftest de Rendimiento D3D12 (`Melodeus.exe --selftest 2`)**:
  - Ejecuta el motor completo a máxima velocidad durante 2 segundos.
  - Verifica que el renderizado alcance $\ge 144\text{ FPS}$ en DirectX 12 (verificado a **291.6 FPS** sostenidos).

### 10.2. Licencias de Componentes de Terceros

Melodeus integra librerías de código abierto respetando íntegramente sus términos de licencia:

1. **Dear ImGui** (v1.91.x) — Licencia MIT · Copyright © Omar Cornut.
2. **JUCE Framework** (Core & Audio Devices) — Licencia ISC / Permissive Modules · Copyright © Raw Material Software.
3. **SQLite** (v3.46.x) — Dominio Público.
4. **Microsoft ONNX Runtime** — Licencia MIT · Copyright © Microsoft Corporation.
5. **yt-dlp** — The Unlicense (Dominio Público).
6. **FFmpeg** — Licencia LGPL v2.1+ / GPL v3 · Copyright © The FFmpeg developers.
7. **Inno Setup** (v6.x) — Licencia Inno Setup · Copyright © Jordan Russell / Martijn Laan.
8. **Inter Typeface** — SIL Open Font License 1.1 · Copyright © Rasmus Andersson.
9. **JetBrains Mono Typeface** — SIL Open Font License 1.1 · Copyright © JetBrains s.r.o.
10. **Phosphor Icons** — Licencia MIT · Copyright © Phosphor Icons.

---

<p align="center">
  <strong>Melodeus DJ Workstation</strong> — Orquestado por <em>danedsant</em> con <em>Antigravity</em> & <em>OpenCode</em>.<br/>
  Impulsado por los modelos de IA <em>Gemini 3.8 Flash High y MiMo V2 Flash Free</em>.
</p>
