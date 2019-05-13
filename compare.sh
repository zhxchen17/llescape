TEMP="temp"
mkdir -p $TEMP
rm -f $TEMP/*
./llgo_baseline -S -emit-llvm tests/$1.go -o $TEMP/$1.ll
grep -o "call i8\* @__go_new" $TEMP/$1.ll | wc -l
./llgo -S -emit-llvm tests/$1.go -o $TEMP/$1.ll
grep -o "call i8\* @__go_new" $TEMP/$1.ll | wc -l
