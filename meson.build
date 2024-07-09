project('app', 'cpp', version: '1.0.0', default_options : ['warning_level=3', 'werror=false', 'cpp_std=c++20'])
add_project_arguments('-Wfatal-errors', language: 'cpp')
add_project_arguments('-Wno-missing-field-initializers', language: 'cpp')

subdir('src')

peer_linker_files = files('src/peer-linker.cpp') + ws_files + ws_server_files

client_files = files(
  'example/client.cpp',
) + p2p_files


executable('peer-linker', peer_linker_files, dependencies : ws_deps)
executable('client', client_files, dependencies : p2p_deps)
