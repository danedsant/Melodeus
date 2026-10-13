#pragma once

// UI del modal de configuraciones (F3.10): pestañas General (idioma) y
// Atajos (rebind de teclas). Se dibuja con ImGui::OpenPopup desde main.

#include <string>

namespace melodeus::app {

/// Abre el diálogo nativo de Windows para seleccionar una carpeta.
/// Devuelve cadena vacía si se cancela.
std::string pick_folder_dialog(void* hwnd_parent = nullptr);

/// Dibuja el modal de configuraciones. Llamar solo cuando open es true
/// (main hace OpenPopup una vez y luego llama esto cada frame).
/// Devuelve true si el usuario lo cerró.
bool draw_settings_modal(bool* open);

} // namespace melodeus::app
