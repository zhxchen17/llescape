package main;

func inner(x string) {
	x += "1"
}

func foo() string {
	x := ""
	inner(x)
	return x
}

func main() {
	a := foo()
	println(a)
}
