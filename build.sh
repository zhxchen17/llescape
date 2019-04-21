if mkdir build > /dev/null 2>&1 ; then
    cd build
    CC=clang CXX=clang++ cmake -G Ninja ../llvm -DLLVM_USE_SPLIT_DWARF=TRUE -DBUILD_SHARED_LIBS=true -DLLVM_TARGETS_TO_BUILD=X86 -DLLVM_ENABLE_PROJECTS='llvm' -DCMAKE_INSTALL_PREFIX=~/llvm -DLLVM_USE_LINKER=gold
else
    cd build
fi
ninja
