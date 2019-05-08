package main;

func inner(x *int) {
	*x += 1
}

func foo() int {
	a := 0
	x := &a
	inner(x)
	return *x
}

func main() {
	a := foo()
	println(a)
}
