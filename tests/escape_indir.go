// errorcheck -0 -m -l

// Copyright 2015 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Test escape analysis when assigning to indirections.

package main

var sink interface{}

type ConstPtr struct {
	p *int
	c ConstPtr2
	x **ConstPtr
}

type ConstPtr2 struct {
	p *int
	i int
}

func constptr0() {
	i := 0           // ERROR "moved to heap: i"
	x := &ConstPtr{} // ERROR "&ConstPtr literal does not escape"
	// BAD: i should not escape here
	x.p = &i
	_ = x
}

func constptr01() *ConstPtr {
	i := 0           // ERROR "moved to heap: i"
	x := &ConstPtr{} // ERROR "&ConstPtr literal escapes to heap"
	x.p = &i
	return x
}

func constptr02() ConstPtr {
	i := 0           // ERROR "moved to heap: i"
	x := &ConstPtr{} // ERROR "&ConstPtr literal does not escape"
	x.p = &i
	return *x
}

func constptr03() **ConstPtr {
	i := 0           // ERROR "moved to heap: i"
	x := &ConstPtr{} // ERROR "&ConstPtr literal escapes to heap" "moved to heap: x"
	x.p = &i
	return &x
}

func constptr1() {
	i := 0           // ERROR "moved to heap: i"
	x := &ConstPtr{} // ERROR "&ConstPtr literal escapes to heap"
	x.p = &i
	sink = x         // ERROR "x escapes to heap"
}

func constptr2() {
	i := 0           // ERROR "moved to heap: i"
	x := &ConstPtr{} // ERROR "&ConstPtr literal does not escape"
	x.p = &i
	sink = *x        // ERROR "\*x escapes to heap"
}

func constptr4() *ConstPtr {
	p := new(ConstPtr) // ERROR "new\(ConstPtr\) escapes to heap"
	*p = *&ConstPtr{}  // ERROR "&ConstPtr literal does not escape"
	return p
}

func constptr5() *ConstPtr {
	p := new(ConstPtr) // ERROR "new\(ConstPtr\) escapes to heap"
	p1 := &ConstPtr{}  // ERROR "&ConstPtr literal does not escape"
	*p = *p1
	return p
}

func constptr8() *ConstPtr {
	p := new(ConstPtr) // ERROR "new\(ConstPtr\) escapes to heap"
	var tmp ConstPtr2
	p.c = *&tmp
	return p
}

func constptr9() ConstPtr {
	p := new(ConstPtr) // ERROR "new\(ConstPtr\) does not escape"
	var p1 ConstPtr2
	i := 0    // ERROR "moved to heap: i"
	p1.p = &i
	p.c = p1
	return *p
}

func main() {
	constptr0()
	constptr01()
	constptr02()
	constptr1()
	constptr2()
	constptr4()
	constptr5()
	constptr8()
	constptr9()
}
