TEMP="temp"
mkdir -p $TEMP
rm -f $TEMP/*
./llgo_baseline -S -emit-llvm tests/$1.go -o $TEMP/$1.ll
./build/bin/llvm-as $TEMP/$1.ll -o $TEMP/$1.bc
./build/bin/opt -S -load ./build/lib/LLVMEscape.so -mem2reg -instnamer -basicaa -globals-aa -cfl-anders-aa -scev-aa -escape$2 < $TEMP/$1.bc > $TEMP/$1.out.ll
