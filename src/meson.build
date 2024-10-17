subdir('ws')

p2p_client_common_files = files(
  'event-manager.cpp',
  'peer-linker-session.cpp',
  'websocket-session.cpp',
) + ws_files + ws_client_files
p2p_client_common_deps = ws_deps

p2p_client_ice_files = files(
  'ice-session.cpp',
) + p2p_client_common_files
p2p_client_ice_deps = [dependency('libjuice')] + p2p_client_common_deps

p2p_client_chub_files = files(
  'channel-hub-client.cpp',
) + p2p_client_common_files
p2p_client_chub_deps = p2p_client_common_deps
