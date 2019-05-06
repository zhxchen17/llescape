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

func fooEscape() *Point {
	a := &Point{1, 2}
	b := Pair{a, 0}
	return b.P
}

type Rec struct {
	R *Rec
	X int
}

func foo2() int {
	a := Rec{}
	a.R = &a
	a.X = 0
	return a.X
}

func fooIfEscape() *Point {
	a := true
	var p Pair
	b := &Point{1, 2}
	if (a) {
		p = Pair{b, 1}
	} else {
		p = Pair{b, 2}
	}
	return p.P
}

func fooIf() int {
	a := true
	var p Pair
	b := &Point{1, 2}
	if (a) {
		p = Pair{b, 1}
	} else {
		p = Pair{b, 2}
	}
	return p.Z
}

type Pair2 struct {
	Z int
	P *Point
}

var global Pair2
func fooGlobal() {
	a := &Point{1, 2}
	i := 0
	var p *Pair2
	for i < 1 {
		p = &global
		i += 1
	}
	p.P = a
}

func main() {
	a := foo()
	println(a)
	b := fooEscape()
	println(b)
	c := foo2()
	println(c)
	d := fooIfEscape()
	println(d)
	e := fooIf()
	println(e)
	fooGlobal()
}
