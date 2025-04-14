subdir('net')
subdir('net/tcp')
subdir('net/enc')

plink_client_files = files(
  'peer-linker-client.cpp',
) + netprotocol_files \
  + netprotocol_tcp_client_files \
  + netprotocol_enc_client_files

plink_client_deps = crypto_utils_deps + netprotocol_deps + netprotocol_tcp_deps + netprotocol_enc_deps

chub_client_files = files(
  'channel-hub-client.cpp',
) + netprotocol_files \
  + netprotocol_tcp_client_files \
  + netprotocol_enc_client_files

chub_client_deps = crypto_utils_deps + netprotocol_deps + netprotocol_tcp_deps + netprotocol_enc_deps
