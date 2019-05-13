# llescape

## Build Project

Run `build.sh` should build the whole project.
```
$ ./build.sh
```

## Run Analysis

### Test Files

Test files are under the directory `./tests`.

### Intraprocedual Analysis

Run `./analyze.sh [test file name (without extension name)]` should execute the intraprocedual analysis. For exmaple:

```
$ ./analyze.sh local
```

### Interprocedual Analysis

Run `./analyze.sh [test file name (without extension name)] -module` should execute the interprocedual analysis. For example:

```
$ ./analyze.sh global -module
```
