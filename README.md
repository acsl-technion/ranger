# Introduction

The official source code of *libranger* - an open source library for
nucleotide string indexing using *range matching*.
libranger implements the *Ranger* index data-structure for sequence
alignment tools.

# Making Ranger

1. Make sure you use a Linux operating-system or WSL in Windows. Your CPU must support the AVX2 and POPCNT extensions.
2. Make sure you have g++ and git utilities.
3. Git clone the project.
4. Cd into ranger directory.
5. Run ``make``. The *bin* directory should be generated with Ranger library.

You can do all of these steps using a single line of code. Paste the following into your terminal:

```bash
(( $(cat /proc/cpuinfo | grep avx2 | grep popcnt | wc -l) != 0 )) && \
(( $(which g++ | wc -l) + $(which git | wc -l) == 2 )) && \
git clone https://github.com/acsl-technion/ranger && \
cd ranger && \
make
```

You should see a *bin* directory with the *libranger.so* library.

# How to use Ranger with Minimap2

1. Make the project
2. Run ``./build-minimap2.sh``. The command will git clone Minimap2 repository
and patch it with Ranger code. The Ranger-enhanced Minimap2 will be available
under *minimap2* directory.
3. In order to build a Ranger index, use the regular Minimap2 flags for
building an index together with the flag ``--libranger-create-index``.
For example:

``
./minimap2 [-x preset] --libranger-create-index -d target-index.mmi target.fa
``

4. The Ranger-enhanced Minimap2 is able to use both the original indexing
method or Ranger. This is automatically encoded within the reference genome
index file, and no further action is required. Usage example:

``
./minimap2 -a [-x preset] target-index.mmi query.fa > output.sam
``

# Integration with other software

1. Copy [libranger.h](lib/libranger.h) to your source directory.
2. In your code, include [libranger.h](lib/libranger.h) for using Ranger API.
The documentation to Ragner API is available within [libranger.h](lib/libranger.h)
in [Doxygen](https://www.doxygen.nl/) format.
3. Make Ranger. The *bin* directory should be generated with two shared
objects: *libranger.so* and *libnuevomatchup.so*.
4. Copy the two libraries to the same directory as your Makefile.
5. Add the following to your linker flags:
`` -lranger `-Wl,-rpath,'$$ORIGIN/' -Wl,-z,origin -L. ``
6. When distributing your app, don't forget to include *libranger.so* and
*libnuevomatchup.so* in your executable directory. These libraries will be
dynamically loaded at runtime when your application starts.

# Contributing

Bug fixes and other contributions are welcome.

# License

MIT license.

# Citing

If used as part of an academic paper, please cite *Nucleotide String Indexing
using Range Matching*.
