project('app', 'cpp', version: '1.0.0', default_options : ['warning_level=3', 'werror=false', 'cpp_std=c++20'])
add_project_arguments('-Wfatal-errors', language: 'cpp')
add_project_arguments('-Wno-missing-field-initializers', language: 'cpp')

subdir('src')

peer_linker_files = files('src/peer-linker.cpp') + ws_files + ws_server_files
channel_hub_files = files('src/channel-hub.cpp') + ws_files + ws_server_files

client_files = files(
  'example/peer-linker-test.cpp',
) + p2p_client_ice_files


chub_client_test_files = files(
  'example/channel-hub-client-test.cpp',
) + p2p_client_chub_files

executable('peer-linker', peer_linker_files, dependencies : ws_deps)
executable('channel-hub', channel_hub_files, dependencies : ws_deps)
executable('peer-linker-test', client_files, dependencies : p2p_client_ice_deps)
executable('channel-hub-client-test', chub_client_test_files, dependencies : p2p_client_chub_deps)
