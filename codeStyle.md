## Code Style

### Command-Line
We generally follow the [Google Cpp Style Guide](https://google.github.io/styleguide/cppguide.html#Formatting). 
There is a [.clang-format](.clang-format) file in the root directory that is derived from this style.
It can be used with the `clang-format` tool to reformat the source files, e.g.,

```
$ clang-format -style=file libs/include/psandbox.h
```

This will use the `.clang-format` file to re-format the source file and print it to the console. 
To re-format the file in-place, add the `-i` flag.

```
$ clang-format -i -style=file libs/include/psandbox.h
$ clang-format -i -style=file lib/*/*.h
```

### Make targets
We defined several make targets in the CMakeFiles to run `clang-format`.

* Check the format of *changed* source files:

```
make format-check
```

* Check the format of all source files:

```
make format-check-all
```

* **Format the changed source files (in place)**:

```
make format
```

* Format all source files:

```
make format-all
```

### IDE
If you are using Clion, the IDE supports `.clang-format` style. Go to `Settings/Preferences | Editor | Code Style`, 
check the box `Enable ClangFormat with clangd server`. 

### Vim
`clang-format` can also be integrated with vim [doc](http://clang.llvm.org/docs/ClangFormat.html#vim-integration).
