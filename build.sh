if [[ -f "./build/build.ninja" ]]; then
    cd build
    ninja
else
    mkdir -p build
    cd build
    CC=clang CXX=clang++ cmake -G Ninja ../llvm -DLLVM_USE_SPLIT_DWARF=TRUE -DBUILD_SHARED_LIBS=true -DLLVM_TARGETS_TO_BUILD=X86 -DLLVM_ENABLE_PROJECTS='llvm' -DCMAKE_INSTALL_PREFIX=~/llvm -DLLVM_USE_LINKER=gold
    ninja
fi
