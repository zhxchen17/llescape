// errorcheck -0 -N -m -l -newescape=true

// Copyright 2010 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Test, using compiler diagnostic flags, that the escape analysis is working.
// Compiles but does not run.  Inlining is disabled.
// Registerization is disabled too (-N), which should
// have no effect on escape analysis.
package main;

type Bar struct {
	i  int
	ii *int
}

func NewBar() *Bar {
	return &Bar{42, nil} // ERROR "&Bar literal escapes to heap$"
}

func NewBarp(x *int) *Bar { // ERROR "leaking param: x$"
	return &Bar{42, x} // ERROR "&Bar literal escapes to heap$"
}

func NewBarp2(x *int) *Bar { // ERROR "NewBarp2 x does not escape$"
	return &Bar{*x, nil} // ERROR "&Bar literal escapes to heap$"
}

func foo11() int {
	x, y := 0, 42
	xx := &x
	yy := &y
	*xx = *yy
	return x
}

// assigning to a struct field  is like assigning to the struct
func foo61(i *int) *int { // ERROR "leaking param: i to result ~r1 level=0$"
	type S struct {
		a, b *int
	}
	var s S
	s.a = i
	return s.b
}

// assigning to a struct field is like assigning to the struct but
// here this subtlety is lost, since s.a counts as an assignment to a
// track-losing dereference.
func foo62(i *int) *int { // ERROR "leaking param: i$"
	type S struct {
		a, b *int
	}
	s := new(S) // ERROR "foo62 new\(S\) does not escape$"
	s.a = i
	return nil // s.b
}


func foo61a(i *int) *int { // ERROR "foo61a i does not escape$"
	type S struct {
		a, b *int
	}
	var s S
	s.a = i
	return nil
}

// assigning to an array element is like assigning to the array
func foo60(i *int) *int { // ERROR "leaking param: i to result ~r1 level=0$"
	var a [12]*int
	a[0] = i
	return a[1]
}

func foo60a(i *int) *int { // ERROR "foo60a i does not escape$"
	var a [12]*int
	a[0] = i
	return nil
}

func foo72a() {
	var y [10]*int
	for i := 0; i < 10; i++ {
		// escapes its scope
		x := i    // ERROR "moved to heap: x$"
		y[i] = &x
	}
	return
}

func foo80() *int {
	var z *int
	for {
		// Really just escapes its scope but we don't distinguish
		z = new(int) // ERROR "new\(int\) escapes to heap$"
	}
	_ = z
	return nil
}

func foo138() *byte {
	type T struct {
		x [1]byte
	}
	t := new(T)    // ERROR "new\(T\) escapes to heap$"
	return &t.x[0]
}


type Lit struct {
	p *int
}

func ptrlitNoescape() {
	// Both literal and element do not escape.
	i := 0
	x := &Lit{&i} // ERROR "ptrlitNoescape &Lit literal does not escape$"
	_ = x
}

func main() {
	a := 0
	println(foo11())
	println(foo61(&a))
	println(foo61a(&a))
	println(foo62(&a))
	println(foo60(&a))
	println(foo60a(&a))
	foo72a()
	println(foo80())
	println(foo138())
	ptrlitNoescape()
}
