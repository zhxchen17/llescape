# Milestone Updates
My project is about performing escape analysis at LLVM IR level. The language
frontend I chose is llgo, which can compile go source code into LLVM modules.
The work is currently hosted at https://github.com/zhxchen17/llescape .

The first thing I did was building two different llgo executables. The first
one is the original llgo compiler on the master branch, and the second one is
my tweaked version. The reason for having two executables is llgo already does
a simple escape analysis for its own high level IR, and I want to have an llgo
version that doesn't do escape analysis so that we can compare the result 
produced by our approach and the current official approach (as a baseline).

For the analysis part, so far I tried one prototype based on the concept of "
pointer capturing", which simply does a forward analysis, tracking whether
each IR register might contain the address of an allocated heap object. When
a register gets returned, and it potentially contain the reference to a heap
object, it is "captured". This approach is simple to implement, but there is
also a severe flaw. The reason is that a pointer is also considered "captured"
when it is spilled to memory by a "store" instruction. This is reasonable
because a pointer can be temporarily stored to memory and gets loaded again
later on, to simplify the information maintained, LLVM won't track further into
the memory, so I abandoned this method.

Another thing I looked into is a classical literature called "Escape Analysis
for Java" (Choi, 1999). The paper discussed the concept of "connection graph"
as a core thing in escape analysis. The idea seems feasiable, but we still 
need to have a notion to represent the pointers that are stored in memory, to
get more precise results. In addition, Go is based on "value semantics", which
means not everything is passed as references as Java does. Therefore we must
solve the above two problems first.

Currently I'm implementing a more precise graph model, which is an extension to
the "connection graph" proposed by Choi. The code can be found at my personal 
Github repository https://github.com/zhxchen17/llescape, the main work is placed
under llvm/lib/Transforms/Escape. I have to say the work is kind of undergoing 
so currently there's no well-formed output. My plan now is to finish the 
intraprocedual analysis part (the original plan) before Apr. 28th, and I hope 
I can catch up with the progress in several days.
