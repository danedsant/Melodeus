#include "i18n.hpp"

namespace melodeus::app {

namespace {

// Tabla de traducciones [ES, EN, PT-BR]. El orden debe coincidir con Str.
const char* kStrings[static_cast<int>(Str::Count)][3] = {
    // SettingsTooltip
    {"Configuraci\xC3\xB3n", "Settings", "Configura\xC3\xA7\xC3\xA3o"},
    // SettingsButton
    {"AJUSTES", "SETTINGS", "AJUSTES"},
    // QuantizeButton
    {"QUANTIZE", "QUANTIZE", "QUANTIZE"},
    // QuantizeTooltip
    {"Ajustar beats y cues al comp\xC3\xA1s (Q)", "Snap beats and cues to grid (Q)",
     "Ajustar batidas e cues ao compasso (Q)"},
    // SettingsTitle
    {"CONFIGURACI\xC3\x93N", "SETTINGS", "CONFIGURA\xC3\x87\xC3\x83O"},
    // TabGeneral
    {"General", "General", "Geral"},
    // TabAppearance
    {"Apariencia", "Appearance", "Apar\xC3\xAncia"},
    // TabShortcuts
    {"Atajos", "Shortcuts", "Atalhos"},
    // Language
    {"Idioma", "Language", "Idioma"},
    // LangEs
    {"Espa\xC3\xB1ol", "Spanish", "Espanhol"},
    // LangEn
    {"Ingl\xC3\xA9s", "English", "Ingl\xC3\xAAs"},
    // LangPtBr
    {"Portugu\xC3\xA9s (Brasil)", "Portuguese (Brazil)", "Portugu\xC3\xAAs (Brasil)"},
    // ShortcutsHint
    {"Pulsa una tecla para reasignar. Esc = cancelar.", "Press a key to rebind. Esc = cancel.",
     "Pressione uma tecla para reatribuir. Esc = cancelar."},
    // ResetDefaults
    {"Restablecer", "Reset", "Restaurar"},
    // Close
    {"Cerrar", "Close", "Fechar"},
    // DjPlayback
    {"DJ & Reproducci\xC3\xB3n", "DJ & Playback", "DJ & Reprodu\xC3\xA7\xC3\xA3o"},
    // QuantizeSnap
    {"Quantize (snap beats & cues)", "Quantize (snap beats & cues)",
     "Quantize (snap batidas e cues)"},
    // ShowBeatgrid
    {"Mostrar Beatgrid en Waveform", "Show Beatgrid on Waveform",
     "Mostrar Beatgrid na Waveform"},
    // PitchRangeSetting
    {"Rango Pitch", "Pitch Range", "Faixa de Pitch"},
    // CrossfadeCurveSetting
    {"Curva Crossfader", "Crossfader Curve", "Curva Crossfader"},
    // StreamingDownloads
    {"Descargas de Streaming (YouTube / SoundCloud)", "Streaming Downloads (YouTube / SoundCloud)",
     "Downloads de Streaming (YouTube / SoundCloud)"},
    // DefaultDownloadDir
    {"(Por defecto: carpeta 'downloads' junto a la base de datos)",
     "(Default: 'downloads' folder next to database)",
     "(Padr\xC3\xA3o: pasta 'downloads' junto ao banco de dados)"},
    // BrowseFolder
    {"Examinar carpeta...", "Browse folder...", "Procurar pasta..."},
    // ResetDefaultFolder
    {"Restablecer por defecto", "Reset to default", "Restaurar padr\xC3\xA3o"},
    // OpenInExplorer
    {"Abrir en Explorador", "Open in Explorer", "Abrir no Explorer"},
    // DeckColors
    {"Colores de los Decks", "Deck Colors", "Cores dos Decks"},
    // ColorDeckA
    {"Color Deck A", "Deck A Color", "Cor Deck A"},
    // ColorDeckB
    {"Color Deck B", "Deck B Color", "Cor Deck B"},
    // SkinPresets
    {"Presets de color (Skins)", "Color presets (Skins)",
     "Predefini\xC3\xA7\xC3\xB5" "es de cores (Skins)"},
    // CustomMode
    {"Modo: Personalizado", "Mode: Custom", "Modo: Personalizado"},
    // DeckA
    {"DECK A", "DECK A", "DECK A"},
    // DeckB
    {"DECK B", "DECK B", "DECK B"},
    // LoadingBadge
    {"CARGANDO...", "LOADING...", "CARREGANDO..."},
    // KeyResetTooltip
    {"Click para resetear afinaci\xC3\xB3n a 0 semitonos",
     "Click to reset key to 0 semitones",
     "Clique para redefinir afina\xC3\xA7\xC3\xA3o para 0 semitons"},
    // KeySemiDownTooltip
    {"Bajar 1 semitono (sin modificar BPM)", "Down 1 semitone (without changing BPM)",
     "Diminuir 1 semitom (sem alterar BPM)"},
    // KeySemiUpTooltip
    {"Subir 1 semitono (sin modificar BPM)", "Up 1 semitone (without changing BPM)",
     "Aumentar 1 semitom (sem alterar BPM)"},
    // PlayTooltip
    {"Play / Pausa (Espacio)", "Play / Pause (Space)", "Reproduzir / Pausar (Espa\xC3\xA7o)"},
    // Loop
    {"LOOP", "LOOP", "LOOP"},
    // LoopTooltip
    {"LOOP \xE2\x80\x94 click: activar/desactivar en la posici\xC3\xB3n actual",
     "LOOP \xE2\x80\x94 click: toggle at current position",
     "LOOP \xE2\x80\x94 clique: ativar/desativar na posi\xC3\xA7\xC3\xA3o atual"},
    // LoopShorten
    {"Acortar loop", "Shorten loop", "Encurtar loop"},
    // LoopLengthen
    {"Alargar loop", "Lengthen loop", "Alargar loop"},
    // Cue
    {"CUE", "CUE", "CUE"},
    // CueTooltip
    {"CUE \xE2\x80\x94 volver al inicio", "CUE \xE2\x80\x94 back to start",
     "CUE \xE2\x80\x94 voltar ao in\xC3\xAD" "cio"},
    // HotCueSet
    {"Hot cue %d \xE2\x80\x94 click: guardar posici\xC3\xB3n",
     "Hot cue %d \xE2\x80\x94 click: set position",
     "Hot cue %d \xE2\x80\x94 clique: salvar posi\xC3\xA7\xC3\xA3o"},
    // HotCueJump
    {"Hot cue %d \xE2\x80\x94 click: saltar | click der: borrar",
     "Hot cue %d \xE2\x80\x94 click: jump | right-click: clear",
     "Hot cue %d \xE2\x80\x94 clique: pular | clique dir: apagar"},
    // Sync
    {"SYNC", "SYNC", "SYNC"},
    // SyncTooltip
    {"SYNC \xE2\x80\x94 ajusta el pitch de este deck para igualar el BPM del otro deck\n"
     "(click de nuevo o mover el pitch lo desactiva)",
     "SYNC \xE2\x80\x94 adjusts this deck's pitch to match the other deck's BPM\n"
     "(click again or move the pitch to disable)",
     "SYNC \xE2\x80\x94 ajusta o pitch deste deck para igualar o BPM do outro deck\n"
     "(clique de novo ou mova o pitch para desativar)"},
    // MasterTempoTooltip
    {"Master Tempo / Key Lock (Mantiene la afinaci\xC3\xB3n al variar el BPM)",
     "Master Tempo / Key Lock (Preserves pitch when changing BPM)",
     "Master Tempo / Key Lock (Mant\xC3\xA9m a afina\xC3\xA7\xC3\xA3o ao mudar o BPM)"},
    // PitchRangeTooltip
    {"Rango de Pitch (Click para alternar: \xC2\xB1" "8%, \xC2\xB1" "16%, \xC2\xB1" "50%, \xC2\xB1" "100%)",
     "Pitch Range (Click to toggle: \xC2\xB1" "8%, \xC2\xB1" "16%, \xC2\xB1" "50%, \xC2\xB1" "100%)",
     "Faixa de Pitch (Clique para alternar: \xC2\xB1" "8%, \xC2\xB1" "16%, \xC2\xB1" "50%, \xC2\xB1" "100%)"},
    // Pitch
    {"PITCH", "PITCH", "PITCH"},
    // Bpm
    {"B P M", "B P M", "B P M"},
    // Mixer
    {"MIXER", "MIXER", "MIXER"},
    // Gain
    {"GAIN", "GAIN", "GAIN"},
    // Filter
    {"FILTRO", "FILTER", "FILTRO"},
    // EqHi
    {"HI", "HI", "AGUDO"},
    // EqMid
    {"MID", "MID", "M\xC3\x89" "DIO"},
    // EqLow
    {"LOW", "LOW", "GRAVE"},
    // Crossfader
    {"CROSSFADER", "CROSSFADER", "CROSSFADER"},
    // Master
    {"MASTER", "MASTER", "MASTER"},
    // Library
    {"BIBLIOTECA", "LIBRARY", "BIBLIOTECA"},
    // SearchHint
    {"buscar (titulo, artista, album)...", "search (title, artist, album)...",
     "buscar (t\xC3\xADtulo, artista, \xC3\xA1lbum)..."},
    // UrlHint
    {"https://... (YouTube/SoundCloud)", "https://... (YouTube/SoundCloud)",
     "https://... (YouTube/SoundCloud)"},
    // Load
    {"cargar", "load", "carregar"},
    // Folder
    {"carpeta", "folder", "pasta"},
    // Tracks
    {"%d pistas", "%d tracks", "%d faixas"},
    // OfTracks
    {"%d de %d pistas", "%d of %d tracks", "%d de %d faixas"},
    // NoResults
    {"sin resultados para \"%.80s\"", "no results for \"%.80s\"", "sem resultados para \"%.80s\""},
    // NoTracks
    {"sin pistas", "no tracks", "sem faixas"},
    // EmptyLibrary
    {"biblioteca vac\xC3\xAD" "a \xE2\x80\x94 pulsa \"+ carpeta\" para importar",
     "empty library \xE2\x80\x94 click \"+ folder\" to import",
     "biblioteca vazia \xE2\x80\x94 clique \"+ pasta\" para importar"},
    // Title
    {"T\xC3\xADTULO", "TITLE", "T\xC3\x8DTULO"},
    // Artist
    {"ARTISTA", "ARTIST", "ARTISTA"},
    // Album
    {"\xC3\x81LBUM", "ALBUM", "\xC3\x81LBUM"},
    // Key
    {"KEY", "KEY", "KEY"},
    // Time
    {"TIEMPO", "TIME", "TEMPO"},
    // InLibrary
    {"En Biblioteca", "In Library", "Na Biblioteca"},
    // OnlineResults
    {"Resultados en L\xC3\xADnea", "Online Results", "Resultados Online"},
    // SearchingOnline
    {"Buscando...", "Searching...", "Pesquisando..."},
    // SearchButton
    {"Buscar", "Search", "Buscar"},
    // SearchYtHint
    {"Buscar en YouTube o pegar enlace...", "Search YouTube or paste link...",
     "Buscar no YouTube ou colar link..."},
    // SearchScHint
    {"Buscar en SoundCloud o pegar enlace...", "Search SoundCloud or paste link...",
     "Buscar no SoundCloud ou colar link..."},
    // AllFolders
    {"Todas las carpetas", "All folders", "Todas as pastas"},
    // AddFolderTooltip
    {"Importar carpeta a la biblioteca", "Import folder to library",
     "Importar pasta para a biblioteca"},
    // FilterFolderTooltip
    {"Filtrar pistas por carpeta", "Filter tracks by folder", "Filtrar faixas por pasta"},
    // ChannelArtist
    {"Canal / Artista", "Channel / Artist", "Canal / Artista"},
    // Status
    {"Estado", "Status", "Status"},
    // EmptyOnlineHint
    {"Escribe un t\xC3\xA9rmino de b\xC3\xBAsqueda y pulsa Enter o Buscar",
     "Type a search term and press Enter or Search",
     "Digite um termo de busca e pressione Enter ou Buscar"},
    // LoadDeckA
    {"Cargar en Deck A", "Load to Deck A", "Carregar no Deck A"},
    // LoadDeckB
    {"Cargar en Deck B", "Load to Deck B", "Carregar no Deck B"},
    // DeleteTrack
    {"Eliminar de la biblioteca", "Delete from library", "Remover da biblioteca"},
    // DownloadTrack
    {"Descargar a la biblioteca", "Download to library", "Baixar para biblioteca"},
    // HintsNav
    {"\u2191\u2193 mover \u00B7 Enter A \u00B7 \u21E7Enter B \u00B7 / buscar",
     "\u2191\u2193 move \u00B7 Enter A \u00B7 \u21E7Enter B \u00B7 / search",
     "\u2191\u2193 mover \u00B7 Enter A \u00B7 \u21E7Enter B \u00B7 / buscar"},
    // NavMove
    {"mover", "move", "mover"},
    // NavDeckA
    {"Deck A", "Deck A", "Deck A"},
    // NavDeckB
    {"Deck B", "Deck B", "Deck B"},
    // NavSearch
    {"buscar", "search", "buscar"},
    // StatusReady
    {"\xE2\x9C\x93 Biblioteca lista \xC2\xB7 Base de datos sincronizada",
     "\xE2\x9C\x93 Library ready \xC2\xB7 Database synchronized",
     "\xE2\x9C\x93 Biblioteca pronta \xC2\xB7 Banco de dados sincronizado"},
    // StatusLoadingDeckA
    {"CARGANDO DECK A", "LOADING DECK A", "CARREGANDO DECK A"},
    // StatusLoadingDeckB
    {"CARGANDO DECK B", "LOADING DECK B", "CARREGANDO DECK B"},
    // StatusImporting
    {"IMPORTANDO", "IMPORTING", "IMPORTANDO"},
    // StatusDownloading
    {"DESCARGANDO", "DOWNLOADING", "BAIXANDO"},
    // AudioOk
    {"audio OK", "audio OK", "audio OK"},
    // AudioOff
    {"audio --", "audio --", "audio --"},
    // DbTracks
    {"db %d pistas", "db %d tracks", "db %d faixas"},
    // ActionPlayA
    {"Play/Pausa Deck A", "Play/Pause Deck A", "Play/Pausa Deck A"},
    // ActionPlayB
    {"Play/Pausa Deck B", "Play/Pause Deck B", "Play/Pausa Deck B"},
    // ActionPitchADown
    {"Pitch Deck A -", "Pitch Deck A -", "Pitch Deck A -"},
    // ActionPitchAUp
    {"Pitch Deck A +", "Pitch Deck A +", "Pitch Deck A +"},
    // ActionPitchBDown
    {"Pitch Deck B -", "Pitch Deck B -", "Pitch Deck B -"},
    // ActionPitchBUp
    {"Pitch Deck B +", "Pitch Deck B +", "Pitch Deck B +"},
    // ActionCueA
    {"Cue Deck A", "Cue Deck A", "Cue Deck A"},
    // ActionCueB
    {"Cue Deck B", "Cue Deck B", "Cue Deck B"},
    // ActionSyncA
    {"Sync Deck A", "Sync Deck A", "Sync Deck A"},
    // ActionSyncB
    {"Sync Deck B", "Sync Deck B", "Sync Deck B"},
    // ActionLoadA
    {"Cargar en Deck A", "Load to Deck A", "Carregar no Deck A"},
    // ActionLoadB
    {"Cargar en Deck B", "Load to Deck B", "Carregar no Deck B"},
    // ActionSearch
    {"Foco en b\xC3\xBAsqueda", "Focus search", "Foco na busca"},
    // ActionClearSearch
    {"Limpiar b\xC3\xBAsqueda", "Clear search", "Limpar busca"},
    // ActionStopBoth
    {"Detener ambos Decks", "Stop both decks", "Parar ambos os Decks"},
    // ActionNavUp
    {"Biblioteca arriba", "Library up", "Biblioteca para cima"},
    // ActionNavDown
    {"Biblioteca abajo", "Library down", "Biblioteca para baixo"},
    // HelpButton
    {"AYUDA", "HELP", "AJUDA"},
    // HelpTooltip
    {"Manual y gu\xC3\xAD" "a de funcionamiento", "User manual and quick guide",
     "Manual e guia de opera\xC3\xA7\xC3\xA3o"},
    // HelpTitle
    {"GU\xC3\x8D" "A DE FUNCIONAMIENTO", "USER GUIDE & MANUAL", "GUIA DE OPERA\xC3\x87\xC3\x83O"},
    // TabHelpDecks
    {"Decks & Reproducci\xC3\xB3n", "Decks & Playback", "Decks & Reprodu\xC3\xA7\xC3\xA3o"},
    // TabHelpMixer
    {"Mixer & Ecualizaci\xC3\xB3n", "Mixer & EQ", "Mixer & Equaliza\xC3\xA7\xC3\xA3o"},
    // TabHelpLibrary
    {"Biblioteca & Streaming", "Library & Streaming", "Biblioteca & Streaming"},
    // TabHelpShortcuts
    {"Atajos R\xC3\xA1pidos", "Quick Shortcuts", "Atalhos R\xC3\xA1pidos"},
    // TabAbout
    {"Acerca de", "About", "Sobre"},
    // AboutOrchestrated
    {"Orquestada por danedsant con Antigravity y OpenCode",
     "Orchestrated by danedsant with Antigravity and OpenCode",
     "Orquestrada por danedsant com Antigravity e OpenCode"},
    // AboutAiModel
    {"Modelo de IA: Gemini 3.8 Flash High (Google DeepMind)",
     "AI Model: Gemini 3.8 Flash High (Google DeepMind)",
     "Modelo de IA: Gemini 3.8 Flash High (Google DeepMind)"},
    // AboutAiPlatform
    {"Plataforma: Antigravity Agentic Coding", "Platform: Antigravity Agentic Coding",
     "Plataforma: Antigravity Agentic Coding"},
    // AboutTokens
    {"Tokens procesados en desarrollo: ~14.5M tokens",
     "Development tokens processed: ~14.5M tokens",
     "Tokens processados no desenvolvimento: ~14.5M tokens"},
    // AboutLicensesHeading
    {"Licencias de Componentes y C\xC3\xB3" "digo Abierto", "Component & Open Source Licenses",
     "Licen\xC3\xA7" "as de Componentes e C\xC3\xB3" "digo Aberto"},
};

} // namespace

const char* tr(Str id) {
    const int i = static_cast<int>(id);
    const int l = static_cast<int>(global_settings().lang);
    if (i < 0 || i >= static_cast<int>(Str::Count) || l < 0 || l >= 3) {
        return "";
    }
    return kStrings[i][l];
}

} // namespace melodeus::app
