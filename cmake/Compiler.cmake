# Compilación: avisos estrictos y utilidades comunes.

option(MELODEUS_WERROR "Tratar avisos como errores" OFF)

function(melodeus_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /Zc:strictStrings
            /utf-8
            /MP
            /w14242 /w14254 /w14263 /w14265 /w14287
            /w14296 /w14311 /w14545 /w14546 /w14547 /w14549
            /w14555 /w14640 /w14826 /w14905 /w14906 /w14928
            # C4324: "estructura rellenada por especificador de alineacion":
            # es intencionado (padding a línea de caché en estructuras RT).
            /wd4324
        )
        if(MELODEUS_WERROR)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic
            -Wshadow -Wconversion -Wsign-conversion
            -Wnon-virtual-dtor -Woverloaded-virtual -Wcast-qual
        )
        if(MELODEUS_WERROR)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

# Opciones agresivas para el camino de audio (sin cambios de semántica peligrosos:
# /fp:fast se usa sólo donde el DSP lo justifica y está documentado).
function(melodeus_set_audio_options target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            $<$<CONFIG:Release>:/fp:fast /Oi /Gy>
            $<$<CONFIG:RelWithDebInfo>:/fp:fast /Oi>
        )
    else()
        target_compile_options(${target} PRIVATE
            $<$<CONFIG:Release>:-ffast-math -funroll-loops>
        )
    endif()
endfunction()
