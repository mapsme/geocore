# MAPS.ME Development

## Coding Style

See [CPP_STYLE.md](CPP_STYLE.md). Use `clang-format` when in doubt.

## Pull Requests

All contributions to MAPS.ME source code should be submitted via github pull requests.
Each pull request is reviewed by MAPS.ME employees, to ensure consistent code style
and quality. Sometimes the review process even for smallest commits can be
very thorough.

To contribute you must sign the [license agreement](CLA.md): the same one you
sign for Google or Facebook open-source projects.

## Directories

### Core

* `base` - some base things, like macros, logging, caches etc.
* `coding` - I/O classes and data processing.
* `generator` - map building tool.
* `geocoder` -
* `geometry` - geometry primitives we use.
* `indexer` - processor for map files, classificator, styles.
* `platform` - platform abstraction classes: file paths, http requests, location services.
* `std` - standard headers wrappers, for Boost, STL, C-rt.

### Other

Some of these contain their own README files.

* `3party` - external libraries, sometimes modified.
* `cmake` - CMake helper files.
* `data` - data files for the application: maps, styles, country borders.
* `testing` - common interfaces for tests.
