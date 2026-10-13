#include "settings.hpp"
#include "i18n.hpp"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace melodeus::app {

namespace {

/// Nombre corto de la tecla para el archivo (invertible con from_name).
const char* key_name(ImGuiKey k) {
    switch (k) {
    case ImGuiKey_Q: return "Q";
    case ImGuiKey_W: return "W";
    case ImGuiKey_E: return "E";
    case ImGuiKey_R: return "R";
    case ImGuiKey_T: return "T";
    case ImGuiKey_Y: return "Y";
    case ImGuiKey_U: return "U";
    case ImGuiKey_I: return "I";
    case ImGuiKey_O: return "O";
    case ImGuiKey_P: return "P";
    case ImGuiKey_A: return "A";
    case ImGuiKey_S: return "S";
    case ImGuiKey_D: return "D";
    case ImGuiKey_F: return "F";
    case ImGuiKey_G: return "G";
    case ImGuiKey_H: return "H";
    case ImGuiKey_J: return "J";
    case ImGuiKey_K: return "K";
    case ImGuiKey_L: return "L";
    case ImGuiKey_Z: return "Z";
    case ImGuiKey_X: return "X";
    case ImGuiKey_C: return "C";
    case ImGuiKey_V: return "V";
    case ImGuiKey_B: return "B";
    case ImGuiKey_N: return "N";
    case ImGuiKey_M: return "M";
    case ImGuiKey_1: return "1";
    case ImGuiKey_2: return "2";
    case ImGuiKey_3: return "3";
    case ImGuiKey_4: return "4";
    case ImGuiKey_5: return "5";
    case ImGuiKey_6: return "6";
    case ImGuiKey_7: return "7";
    case ImGuiKey_8: return "8";
    case ImGuiKey_9: return "9";
    case ImGuiKey_0: return "0";
    case ImGuiKey_Comma: return ",";
    case ImGuiKey_Period: return ".";
    case ImGuiKey_Slash: return "/";
    case ImGuiKey_Space: return "Space";
    case ImGuiKey_Enter: return "Enter";
    case ImGuiKey_KeypadEnter: return "Enter";
    case ImGuiKey_Escape: return "Esc";
    case ImGuiKey_UpArrow: return "Up";
    case ImGuiKey_DownArrow: return "Down";
    case ImGuiKey_LeftArrow: return "Left";
    case ImGuiKey_RightArrow: return "Right";
    case ImGuiKey_Tab: return "Tab";
    case ImGuiKey_Backspace: return "Backspace";
    default: return "?";
    }
}

/// Nombre → tecla (para load). ImGuiKey_None si no se reconoce.
ImGuiKey key_from_name(const char* s) {
    if (s == nullptr || s[0] == '\0') {
        return ImGuiKey_None;
    }
    if (s[1] == '\0') {
        const char c = s[0];
        if (c >= 'A' && c <= 'Z') {
            return static_cast<ImGuiKey>(ImGuiKey_A + (c - 'A'));
        }
        if (c >= 'a' && c <= 'z') {
            return static_cast<ImGuiKey>(ImGuiKey_A + (c - 'a'));
        }
        if (c >= '1' && c <= '9') {
            return static_cast<ImGuiKey>(ImGuiKey_1 + (c - '1'));
        }
        if (c == '0') {
            return ImGuiKey_0;
        }
        if (c == ',') {
            return ImGuiKey_Comma;
        }
        if (c == '.') {
            return ImGuiKey_Period;
        }
        if (c == '/') {
            return ImGuiKey_Slash;
        }
    }
    if (std::strcmp(s, "Space") == 0) {
        return ImGuiKey_Space;
    }
    if (std::strcmp(s, "Enter") == 0) {
        return ImGuiKey_Enter;
    }
    if (std::strcmp(s, "Esc") == 0) {
        return ImGuiKey_Escape;
    }
    if (std::strcmp(s, "Up") == 0) {
        return ImGuiKey_UpArrow;
    }
    if (std::strcmp(s, "Down") == 0) {
        return ImGuiKey_DownArrow;
    }
    if (std::strcmp(s, "Left") == 0) {
        return ImGuiKey_LeftArrow;
    }
    if (std::strcmp(s, "Right") == 0) {
        return ImGuiKey_RightArrow;
    }
    if (std::strcmp(s, "Tab") == 0) {
        return ImGuiKey_Tab;
    }
    if (std::strcmp(s, "Backspace") == 0) {
        return ImGuiKey_Backspace;
    }
    return ImGuiKey_None;
}

/// Clave de archivo para una acción ("play_a", "pitch_a_down", ...).
const char* action_key(Action a) {
    switch (a) {
    case Action::PlayA: return "play_a";
    case Action::PlayB: return "play_b";
    case Action::PitchADown: return "pitch_a_down";
    case Action::PitchAUp: return "pitch_a_up";
    case Action::PitchBDown: return "pitch_b_down";
    case Action::PitchBUp: return "pitch_b_up";
    case Action::CueA: return "cue_a";
    case Action::CueB: return "cue_b";
    case Action::SyncA: return "sync_a";
    case Action::SyncB: return "sync_b";
    case Action::LoadA: return "load_a";
    case Action::LoadB: return "load_b";
    case Action::Search: return "search";
    case Action::ClearSearch: return "clear_search";
    case Action::StopBoth: return "stop_both";
    case Action::NavUp: return "nav_up";
    case Action::NavDown: return "nav_down";
    default: return "?";
    }
}

/// Acción desde clave de archivo; Count si no se reconoce.
Action action_from_key(const char* k) {
    for (int i = 0; i < static_cast<int>(Action::Count); ++i) {
        if (std::strcmp(k, action_key(static_cast<Action>(i))) == 0) {
            return static_cast<Action>(i);
        }
    }
    return Action::Count;
}

} // namespace

Settings Settings::defaults() {
    Settings s;
    s.lang = Language::Es;
    s.keys.fill(ImGuiKey_None);
    s.keys[static_cast<std::size_t>(Action::PlayA)] = ImGuiKey_Q;
    s.keys[static_cast<std::size_t>(Action::PlayB)] = ImGuiKey_W;
    s.keys[static_cast<std::size_t>(Action::PitchADown)] = ImGuiKey_Z;
    s.keys[static_cast<std::size_t>(Action::PitchAUp)] = ImGuiKey_X;
    s.keys[static_cast<std::size_t>(Action::PitchBDown)] = ImGuiKey_Comma;
    s.keys[static_cast<std::size_t>(Action::PitchBUp)] = ImGuiKey_Period;
    s.keys[static_cast<std::size_t>(Action::CueA)] = ImGuiKey_C;
    s.keys[static_cast<std::size_t>(Action::CueB)] = ImGuiKey_N;
    s.keys[static_cast<std::size_t>(Action::SyncA)] = ImGuiKey_S;
    s.keys[static_cast<std::size_t>(Action::SyncB)] = ImGuiKey_F;
    s.keys[static_cast<std::size_t>(Action::LoadA)] = ImGuiKey_1;
    s.keys[static_cast<std::size_t>(Action::LoadB)] = ImGuiKey_2;
    s.keys[static_cast<std::size_t>(Action::Search)] = ImGuiKey_Slash;
    s.keys[static_cast<std::size_t>(Action::ClearSearch)] = ImGuiKey_Escape;
    s.keys[static_cast<std::size_t>(Action::StopBoth)] = ImGuiKey_Space;
    s.keys[static_cast<std::size_t>(Action::NavUp)] = ImGuiKey_UpArrow;
    s.keys[static_cast<std::size_t>(Action::NavDown)] = ImGuiKey_DownArrow;
    return s;
}

bool Settings::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        return false;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);
        if (key == "lang") {
            if (val == "es") {
                lang = Language::Es;
            } else if (val == "en") {
                lang = Language::En;
            } else if (val == "pt_br") {
                lang = Language::PtBr;
            }
        } else if (key == "color.deck_a") {
            sscanf_s(val.c_str(), "%f,%f,%f", &color_deck_a[0], &color_deck_a[1], &color_deck_a[2]);
        } else if (key == "color.deck_b") {
            sscanf_s(val.c_str(), "%f,%f,%f", &color_deck_b[0], &color_deck_b[1], &color_deck_b[2]);
        } else if (key == "pitch.range") {
            pitch_range = std::atoi(val.c_str());
            if (pitch_range != 8 && pitch_range != 16 && pitch_range != 50 && pitch_range != 100) {
                pitch_range = 16;
            }
        } else if (key == "crossfader_curve") {
            crossfader_curve = std::clamp(std::atoi(val.c_str()), 0, 3);
        } else if (key == "quantize") {
            quantize = (val == "1" || val == "true");
        } else if (key == "show_beatgrid") {
            show_beatgrid = (val == "1" || val == "true");
        } else if (key == "theme.preset") {
            theme_preset = std::atoi(val.c_str());
        } else if (key == "download_dir") {
            download_dir = val;
        } else if (key.rfind("key.", 0) == 0) {
            const Action a = action_from_key(key.c_str() + 4);
            if (a != Action::Count) {
                set_key(a, key_from_name(val.c_str()));
            }
        }
    }
    return true;
}

bool Settings::save(const std::string& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        return false;
    }
    out << "# Melodeus settings (auto-generated)\n";
    const char* lang_s = "es";
    if (lang == Language::En) {
        lang_s = "en";
    } else if (lang == Language::PtBr) {
        lang_s = "pt_br";
    }
    out << "lang=" << lang_s << "\n";
    char cbuf[64];
    std::snprintf(cbuf, sizeof(cbuf), "%.3f,%.3f,%.3f", static_cast<double>(color_deck_a[0]),
                  static_cast<double>(color_deck_a[1]), static_cast<double>(color_deck_a[2]));
    out << "color.deck_a=" << cbuf << "\n";
    std::snprintf(cbuf, sizeof(cbuf), "%.3f,%.3f,%.3f", static_cast<double>(color_deck_b[0]),
                  static_cast<double>(color_deck_b[1]), static_cast<double>(color_deck_b[2]));
    out << "color.deck_b=" << cbuf << "\n";
    out << "pitch.range=" << pitch_range << "\n";
    out << "crossfader_curve=" << crossfader_curve << "\n";
    out << "quantize=" << (quantize ? "1" : "0") << "\n";
    out << "show_beatgrid=" << (show_beatgrid ? "1" : "0") << "\n";
    out << "theme.preset=" << theme_preset << "\n";
    out << "download_dir=" << download_dir << "\n";
    for (int i = 0; i < static_cast<int>(Action::Count); ++i) {
        const Action a = static_cast<Action>(i);
        out << "key." << action_key(a) << "=" << key_name(key(a)) << "\n";
    }
    return true;
}

ImGuiKey Settings::key(Action a) const {
    const auto i = static_cast<std::size_t>(a);
    return (i < keys.size()) ? keys[i] : ImGuiKey_None;
}

void Settings::set_key(Action a, ImGuiKey k) {
    const auto i = static_cast<std::size_t>(a);
    if (i < keys.size()) {
        keys[i] = k;
    }
}

const char* Settings::action_label(Action a) {
    switch (a) {
    case Action::PlayA: return tr(Str::ActionPlayA);
    case Action::PlayB: return tr(Str::ActionPlayB);
    case Action::PitchADown: return tr(Str::ActionPitchADown);
    case Action::PitchAUp: return tr(Str::ActionPitchAUp);
    case Action::PitchBDown: return tr(Str::ActionPitchBDown);
    case Action::PitchBUp: return tr(Str::ActionPitchBUp);
    case Action::CueA: return tr(Str::ActionCueA);
    case Action::CueB: return tr(Str::ActionCueB);
    case Action::SyncA: return tr(Str::ActionSyncA);
    case Action::SyncB: return tr(Str::ActionSyncB);
    case Action::LoadA: return tr(Str::ActionLoadA);
    case Action::LoadB: return tr(Str::ActionLoadB);
    case Action::Search: return tr(Str::ActionSearch);
    case Action::ClearSearch: return tr(Str::ActionClearSearch);
    case Action::StopBoth: return tr(Str::ActionStopBoth);
    case Action::NavUp: return tr(Str::ActionNavUp);
    case Action::NavDown: return tr(Str::ActionNavDown);
    default: return "?";
    }
}

const char* Settings::key_label(ImGuiKey k) {
    return key_name(k);
}

Settings& global_settings() {
    static Settings s = Settings::defaults();
    return s;
}

std::string settings_path() {
    wchar_t buf[MAX_PATH]{};
    const DWORD len = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        std::filesystem::path exe(buf);
        return (exe.parent_path() / "settings.json").string();
    }
    return "settings.json";
}

} // namespace melodeus::app
