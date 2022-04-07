# uFork (Actor Virtual Machine)

The key idea behind this Actor Virtual Machine is
interleaved execution of threaded instruction streams.

## Representation

The primary data-structure in **uFork** consists of four machine-native integers.

Name | Description
-----|------------
 t   | code/type
 x   | first/car
 y   | rest/cdr
 z   | link/next

## Inspiration

 * [SectorLISP](http://justine.lol/sectorlisp2/)
 * [Ribbit](https://github.com/udem-dlteam/ribbit)
   * [A Small Scheme VM, Compiler and REPL in 4K](https://www.youtube.com/watch?v=A3r0cYRwrSs)
 * [From Folklore to Fact: Comparing Implementations of Stacks and Continuations](https://par.nsf.gov/servlets/purl/10201136)
 * [Schism](https://github.com/schism-lang/schism)
 * [A Simple Scheme Compiler](https://www.cs.rpi.edu/academics/courses/fall00/ai/scheme/reference/schintro-v14/schintro_142.html#SEC271)
