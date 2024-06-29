project('app', 'cpp', version: '1.0.0', default_options : ['warning_level=3', 'werror=false', 'cpp_std=c++20'])
add_project_arguments('-Wfatal-errors', language: 'cpp')
add_project_arguments('-Wno-missing-field-initializers', language: 'cpp')

subdir('src/ws')

deps = [
  dependency('libjuice'),
] + ws_deps

libws_dep = dependency('libwebsockets')

common_files = files(
)

server_files = files(
  'src/server.cpp',
) + common_files + ws_files + ws_server_files

client_files = files(
  'example/client.cpp',
  'src/ice.cpp',
) + common_files + ws_files + ws_client_files


executable('server', server_files, dependencies : deps)
executable('client', client_files, dependencies : deps)
