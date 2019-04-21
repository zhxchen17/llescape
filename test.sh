mkdir -p temp
rm -f temp/*
./llgo -S -emit-llvm tests/test.go -o temp/test.ll
./build/bin/llvm-as temp/test.ll -o temp/test.bc
./build/bin/opt -load ./build/lib/LLVMEscape.so -escape < temp/test.bc > /dev/null
