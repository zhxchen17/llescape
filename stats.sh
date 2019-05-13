./compare.sh $1
./analyze.sh $1 2>&1 >/dev/null | grep "ly escapes." | wc -l
