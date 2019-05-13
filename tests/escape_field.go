// errorcheck -0 -m -l -newescape=true

// Copyright 2015 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Test escape analysis with respect to field assignments.

package main

var sink interface{}

type X struct {
	p1 *int
	p2 *int
	a  [2]*int
}

type Y struct {
	x X
}

func field0() {
	i := 0 // ERROR "moved to heap: i$"
	var x X
	x.p1 = &i
	sink = x.p1 // ERROR "x\.p1 escapes to heap"
}

func field1() {
	i := 0 // ERROR "moved to heap: i$"
	var x X
	// BAD: &i should not escape
	x.p1 = &i
	sink = x.p2 // ERROR "x\.p2 escapes to heap"
}

func field3() {
	i := 0 // ERROR "moved to heap: i$"
	var x X
	x.p1 = &i
	sink = x  // ERROR "x escapes to heap"
}

func field4() {
	i := 0 // ERROR "moved to heap: i$"
	var y Y
	y.x.p1 = &i
	x := y.x
	sink = x // ERROR "x escapes to heap"
}

func field5() {
	i := 0 // ERROR "moved to heap: i$"
	var x X
	// BAD: &i should not escape here
	x.a[0] = &i
	sink = x.a[1] // ERROR "x\.a\[1\] escapes to heap"
}

// BAD: we are not leaking param x, only x.p2
func field6(x *X) { // ERROR "leaking param content: x$"
	sink = x.p2 // ERROR "x\.p2 escapes to heap"
}

func field6a() {
	i := 0 // ERROR "moved to heap: i$"
	var x X
	// BAD: &i should not escape
	x.p1 = &i
	field6(&x)
}

func field7() {
	i := 0
	var y Y
	y.x.p1 = &i
	x := y.x
	var y1 Y
	y1.x = x
	_ = y1.x.p1
}

func field8() {
	i := 0 // ERROR "moved to heap: i$"
	var y Y
	y.x.p1 = &i
	x := y.x
	var y1 Y
	y1.x = x
	sink = y1.x.p1 // ERROR "y1\.x\.p1 escapes to heap"
}

func field9() {
	i := 0 // ERROR "moved to heap: i$"
	var y Y
	y.x.p1 = &i
	x := y.x
	var y1 Y
	y1.x = x
	sink = y1.x // ERROR "y1\.x escapes to heap"
}

func main() {
	field0()
	field1()
	field3()
	field4()
	field5()
	field6a()
	field7()
	field8()
	field9()
}
