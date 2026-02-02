# Explorer instrumentation module for DOSBox-Staging
# Provides coverage tracking, edge logging, data access monitoring,
# headless mode, input injection, memory/register access, state save/restore,
# and optional LibTorch RL policy integration

target_sources(libdosboxcommon PRIVATE
    explorer.cpp
    explorer_log.cpp
    explorer_data.cpp
    explorer_nn.cpp
    explorer_api.cpp
    explorer_headless.cpp
    explorer_input.cpp
    explorer_cpu.cpp
    explorer_memory.cpp
    explorer_trace.cpp
    explorer_state.cpp
)

target_compile_definitions(libdosboxcommon PUBLIC EXPLORER_ENABLED)

# Optional LibTorch support for neural network policies
if (OPT_EXPLORER_LIBTORCH AND Torch_FOUND)
    target_link_libraries(libdosboxcommon PRIVATE ${TORCH_LIBRARIES})
    target_compile_definitions(libdosboxcommon PUBLIC EXPLORER_ENABLE_LIBTORCH)
    target_include_directories(libdosboxcommon PRIVATE ${TORCH_INCLUDE_DIRS})
endif()
