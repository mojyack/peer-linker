# build
```
# if juice is not installed,
PREFIX=$PWD/rootfs
mkdir repos
git clone --depth=1 -b v1.5.1 https://github.com/paullouisageneau/libjuice.git repos/libjuice
mkdir repos/libjuice/build
pushd repos/libjuice/build
cmake .. -DCMAKE_INSTALL_PREFIX=$PREFIX -DCMAKE_INSTALL_LIBDIR=lib
make -j8 install
popd
# build example
git clone git@github.com/mojyack/p2p-signaling-server.git
# libjuice does not provide pkg-config
mkdir -p $PREFIX/lib/pkgconfig
cd p2p-signaling-server
cp files/libjuice.pc $PREFIX/lib/pkgconfig
PKG_CONFIG_PATH=$PREFIX/lib/pkgconfig meson setup debug
ninja -C debug
# run
export LD_LIBRARY_PATH=$PREFIX/lib
debug/client
```
