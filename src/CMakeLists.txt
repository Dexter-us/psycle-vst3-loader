set(PSYCLE_LOADER_SOURCES
    loader/loader_controller.cpp
    loader/loader_controller.h
    loader/loader_editor.cpp
    loader/loader_editor.h
    loader/loader_messages.h
    loader/loader_processor.cpp
    loader/loader_processor.h
    loader/loader_state.cpp
    loader/loader_state.h
    loader/psycle_machine_loader.cpp
    loader/psycle_machine_loader.h
    loader/windows_dependency_diagnostics.cpp
    loader/windows_dependency_diagnostics.h
)

function(configure_psycle_plugin target)
    target_compile_features(${target} PRIVATE cxx_std_17)
    target_compile_definitions(${target} PRIVATE
        PSYCLE_LOADER_VERSION="${PROJECT_VERSION}"
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(${target} PRIVATE sdk)
    if(WIN32)
        target_link_libraries(${target} PRIVATE comdlg32)
    endif()
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    endif()
endfunction()

smtg_add_vst3plugin(PsycleSynthLoader
    synth/synth_entry.cpp
    ${PSYCLE_LOADER_SOURCES}
)
configure_psycle_plugin(PsycleSynthLoader)
smtg_target_set_bundle(PsycleSynthLoader
    BUNDLE_IDENTIFIER com.psycle.loader.synth
    COMPANY_NAME "Dexter U.S."
)

smtg_add_vst3plugin(PsycleEffectLoader
    effect/effect_entry.cpp
    ${PSYCLE_LOADER_SOURCES}
)
configure_psycle_plugin(PsycleEffectLoader)
smtg_target_set_bundle(PsycleEffectLoader
    BUNDLE_IDENTIFIER com.psycle.loader.effect
    COMPANY_NAME "Dexter U.S."
)