# Explorer instrumentation module

libexplorer_sources = [
    'explorer.cpp',
    'explorer_api.cpp',
    'explorer_cpu.cpp',
    'explorer_data.cpp',
    'explorer_headless.cpp',
    'explorer_input.cpp',
    'explorer_log.cpp',
    'explorer_memory.cpp',
    'explorer_nn.cpp',
    'explorer_shared.cpp',
    'explorer_state.cpp',
    'explorer_trace.cpp',
    'explorer_training.cpp',
]

# Check for LibTorch
libtorch_path = get_option('explorer_libtorch_path')
libtorch_found = false
libtorch_dep = disabler()

if libtorch_path != ''
    # Manual configuration with explicit path
    cxx = meson.get_compiler('cpp')
    libtorch_inc = include_directories(libtorch_path / 'include', 
                                       libtorch_path / 'include' / 'torch' / 'csrc' / 'api' / 'include')
    torch_lib = cxx.find_library('torch', dirs: libtorch_path / 'lib', required: false)
    torch_cpu_lib = cxx.find_library('torch_cpu', dirs: libtorch_path / 'lib', required: false)
    c10_lib = cxx.find_library('c10', dirs: libtorch_path / 'lib', required: false)
    
    if torch_lib.found() and torch_cpu_lib.found() and c10_lib.found()
        # Link order matters: torch -> torch_cpu -> c10
        # Also add rpath for runtime
        libtorch_dep = declare_dependency(
            include_directories: libtorch_inc,
            dependencies: [torch_lib, torch_cpu_lib, c10_lib],
            link_args: ['-Wl,-rpath,' + libtorch_path / 'lib'],
        )
        libtorch_found = true
    endif
else
    # Try pkg-config or cmake
    libtorch_dep = dependency('Torch', required: false)
    if not libtorch_dep.found()
        libtorch_dep = dependency('torch', 
                                  method: 'cmake',
                                  modules: ['Torch::torch', 'Torch::torch_cpu'],
                                  required: false)
    endif
    libtorch_found = libtorch_dep.found()
endif

# Real-time library for shm_open (shared memory)
rt_dep = cxx.find_library('rt', required: false)

explorer_deps = [
    sdl2_dep,
    threads_dep,
]

# Add RT library if found (needed for shared memory on Linux)
if rt_dep.found()
    explorer_deps += rt_dep
endif

explorer_cpp_args = ['-DEXPLORER_ENABLED']

# Suppress certain warnings for LibTorch headers
explorer_warning_suppressions = [
    '-Wno-effc++',
    '-Wno-extra-semi',
    '-Wno-suggest-override',
]

if libtorch_found
    explorer_deps += libtorch_dep
    explorer_cpp_args += '-DEXPLORER_ENABLE_LIBTORCH'
    # ABI flag is now set globally in main meson.build
    explorer_cpp_args += explorer_warning_suppressions
    summary('LibTorch support', true)
else
    summary('LibTorch support', false)
endif

libexplorer = static_library(
    'explorer',
    libexplorer_sources,
    include_directories: incdir,
    dependencies: explorer_deps,
    cpp_args: warnings + explorer_cpp_args,
)

libexplorer_dep = declare_dependency(
    link_with: libexplorer,
    compile_args: explorer_cpp_args,
    dependencies: explorer_deps,
)
