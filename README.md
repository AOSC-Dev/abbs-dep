# abbs-dep

Compile:

```
meson builddir
cd builddir/
ninja
src/abbs-dep
```

```
Usage:
  abbs-dep [OPTION…] package...

Resolve dependencies for abbs trees.

This tool is intended for use with abbs.db database file 
generated from a `abbs-meta` local scan and `dpkgrepo.py`
sync with appropriate sources.list.

Exit status 2 indicates that there is a dependency loop.

Help Options:
  -h, --help            Show help options

Application Options:
  --version             Show program version
  -a, --arch            Set architecture to look up, default 'amd64'
  -n, --no-builddep     Don't include BUILDDEP
  -v, --verbose         Show progress
  -d, --dbfile          abbs-meta database file
```
