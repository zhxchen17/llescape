# llescape

## Prerequisites

1. CMake
2. Ninja build system

## Build Project

Run `build.sh` should build the whole project.
```
$ ./build.sh
```
Depending on the machine, it could take 10 - 40 mintues to compile the project (including LLVM).

## Implementation

The source code for escape analysis is placed under the directory `llvm/lib/Transforms/Escape/`.

## Run Analysis

### Test Files

Test files are under the directory `./tests`.

### Intraprocedural Analysis

Run `./analyze.sh [test file name (without extension name)]` should execute the intraprocedual analysis. For exmaple:

```
$ ./analyze.sh local
```

### Interprocedural Analysis

Run `./analyze.sh [test file name (without extension name)] -module` should execute the interprocedual analysis. For example:

```
$ ./analyze.sh global -module
```
