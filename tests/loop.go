package main;

func loop() *int {
	a := 0
	b := 1
	ptr_b := &b
	pp := &ptr_b
	var ptr_a *int
	for i:=0; i < 5; i++ {
		ptr_a = &a
		*pp = ptr_a
	}
	return ptr_a
}

func main() {
	x := loop()
	println(*x)
}
