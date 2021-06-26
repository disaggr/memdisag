#!/bin/bash
set -e
echo "Resolving gRPC dependency"
git clone --recurse-submodules -b v1.38.0 https://github.com/grpc/grpc
export MY_INSTALL_DIR=$PWD/.local
mkdir -p $MY_INSTALL_DIR
export PATH="$MY_INSTALL_DIR/bin:$PATH"
cd grpc
mkdir -p cmake/build
pushd cmake/build
echo "Installing gRPC"
cmake -DgRPC_INSTALL=ON \
      -DgRPC_BUILD_TESTS=OFF \
      -DCMAKE_INSTALL_PREFIX=$MY_INSTALL_DIR \
      ../..
make -j$(nproc)
make install
popd
mkdir -p third_party/abseil-cpp/cmake/build
pushd third_party/abseil-cpp/cmake/build
cmake -DCMAKE_INSTALL_PREFIX=$MY_INSTALL_DIR \
      -DCMAKE_POSITION_INDEPENDENT_CODE=TRUE \
      ../..
make -j$(nproc)
make install
popd
cd ..
echo "gRPC installed"

echo "Cloning Apache Arrow repository"
mkdir arrow_build
cd arrow_build
echo "Installing Apache Arrow with Plasma"
cmake ../arrow/cpp -DARROW_PLASMA=ON -DCMAKE_INSTALL_PREFIX=$MY_INSTALL_DIR -DDEP_DIR=$MY_INSTALL_DIR
make -j$(nproc)
make install
cd $MY_INSTALL_DIR/..
