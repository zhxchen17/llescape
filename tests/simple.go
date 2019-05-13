package main;

func alloc() *int {
	x := 0
	return &x
}

func add(a, b int) int {
	return a + b
}

func main() {
	x := alloc()
	println(x)
}
