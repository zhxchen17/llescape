TEMP="temp"
mkdir -p $TEMP
rm -f $TEMP/*
./llgo -S -emit-llvm tests/local.go -o $TEMP/local.ll
./build/bin/llvm-as $TEMP/local.ll -o $TEMP/local.bc
./build/bin/opt -S -load ./build/lib/LLVMEscape.so -mem2reg -instnamer -escape < $TEMP/local.bc > $TEMP/local.out.ll
