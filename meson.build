project('app', 'cpp', version: '1.0.0', default_options : ['warning_level=3', 'werror=false', 'cpp_std=c++20'])
add_project_arguments('-Wfatal-errors', language: 'cpp')
add_project_arguments('-Wno-missing-field-initializers', language: 'cpp')

subdir('src')

server_files = files('src/server.cpp') + ws_files + ws_server_files

client_files = files(
  'example/client.cpp',
) + p2p_files


executable('server', server_files, dependencies : p2p_deps)
executable('client', client_files, dependencies : p2p_deps)
