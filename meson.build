project('peer-linker', 'cpp', version : '1.0.0', default_options : ['warning_level=3', 'werror=false', 'cpp_std=c++23'])
add_project_arguments('-Wfatal-errors', language : 'cpp')
add_project_arguments('-Wno-missing-field-initializers', language : 'cpp')

if get_option('client')
  subdir('src')
else
  subdir('src/ws')
endif
subdir('src/crypto')
subdir('src/spawn')

session_key_files = files(
  'src/crypto/base64.cpp',
  'src/crypto/hmac.cpp',
  'src/session-key.cpp',
)

server_files = files(
  'src/server.cpp',
) + session_key_files + ws_files + ws_server_files + process_spawn_files

executable('peer-linker',
  files(
    'src/peer-linker.cpp',
  ) + server_files,
  dependencies : ws_deps + crypto_utils_deps,
)

executable('channel-hub',
  files(
    'src/channel-hub.cpp',
  ) + server_files,
  dependencies : ws_deps + crypto_utils_deps,
)

executable('session-key-util',
  files(
  'src/session-key-util.cpp',
  ) + session_key_files,
  dependencies : crypto_utils_deps,
)

if get_option('client')
  executable('ice-test',
    files(
      'example/ice-test.cpp',
    ) + p2p_client_ice_files,
    dependencies : p2p_client_ice_deps,
  )

  executable('channel-hub-test',
    files(
      'example/channel-hub-test.cpp',
    ) + p2p_client_chub_files,
    dependencies : p2p_client_chub_deps,
  )
endif
