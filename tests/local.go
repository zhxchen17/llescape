package main;

type Point struct {
  X int
  Y int
}

func main() {
  a := &Point{1, 2}
  println(a.X)
}
