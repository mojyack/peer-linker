subdir('ws')

p2p_deps = [dependency('libjuice')] + ws_deps
p2p_files = files(
  'websocket-session.cpp',
  'ice-session.cpp',
  'event-manager.cpp',
) + ws_files + ws_client_files
