TEMP="tmp"
mkdir -p $TEMP
rm -f $TEMP/*
./llgo -S -emit-llvm tests/escape.go -o $TEMP/escape.ll
./build/bin/llvm-as $TEMP/escape.ll -o $TEMP/escape.bc
./build/bin/opt -load ./build/lib/LLVMEscape.so -escape < $TEMP/escape.bc > /dev/null
