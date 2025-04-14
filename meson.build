project('peer-linker', 'cpp', version : '2.0.0', default_options : ['warning_level=3', 'werror=false', 'cpp_std=c++23'], meson_version : '>=1.1')
add_project_arguments('-Wno-missing-field-initializers', language : 'cpp')
add_project_arguments('-Wfatal-errors', language : 'cpp')

subdir('src')
subdir('src/crypto')
subdir('src/spawn')

session_key_files = files(
  'src/crypto/base64.cpp',
  'src/crypto/hmac.cpp',
  'src/session-key.cpp',
)

server_files = files(
  'src/server.cpp',
) + session_key_files \
  + netprotocol_files \
  + netprotocol_tcp_server_files \
  + netprotocol_enc_server_files \
  + process_spawn_files

server_deps = crypto_utils_deps + netprotocol_deps + netprotocol_tcp_deps + netprotocol_enc_deps

executable('peer-linker',
  files(
    'src/peer-linker.cpp',
  ) + server_files,
  dependencies : server_deps,
)

executable('channel-hub',
  files(
    'src/channel-hub.cpp',
  ) + server_files,
  dependencies : server_deps,
)

executable('session-key-util',
  files(
  'src/session-key-util.cpp',
  ) + session_key_files,
  dependencies : crypto_utils_deps,
)

if get_option('test')
  subdir('tests')
endif
