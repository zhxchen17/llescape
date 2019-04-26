package main;

type Point struct {
	X int
	Y int
}

type Pair struct {
	P *Point
	Z int
}
func foo() int {
	a := &Point{1, 2}
	b := Pair{a, 0}
	return b.Z
}

func main() {
	a := foo()
	println(a)
}
