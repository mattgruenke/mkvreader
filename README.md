# MKV Reader Library #

A simple library for reading MKV files.


## Background ##

This is a heavily-modified fork of
https://github.com/Matroska-Org/foo_input_matroska.git, which had no
commits since being transitioned from another repo.  It seemed to be a MKV
reader plugin for the FooBar 2000 audio program.  I've simply used it as a
starting point for reading MKV files in my own apps.

I've tried to expunge everything not relevant to my purposes, in the interest
of simplicity, portability, and comprehensibility.  To that end, I've replaced
the old buildsystem with CMake.  I've only built it for Linux & Android NDK,
though I welcome contributions from anyone interested in using it on other
platforms.

## Important ##

The classes currently used in the interface of this library are very fragile.
Perhaps they weren't intended to be used in such a manner, but this means you're
advised to statically link this library or risk breakage upon some future update
to the shared library.

## License ##

Thanks to the original author, Jory Stone, this library is now maintained under
the MIT license.  See LICENSE.txt, for details.


## Releases ##

### v0.1.0 - 2017-04-29 ###

Initial release.

* Added CMake buildsystem.
* Removed everything extraneous
* Other modifications too numerous to mention.
  MatroskaParser is still largely intact, sans any FooBar2k stuff.
* Added this README.md
* Restructured sources into src/ and include/
* Added simple example program that will eventually demux JPG frames from
  a .mkv file.
* Re-licensed under MIT license, with permission from copyright holder.


## To Do ##

* Fix support for seekable files.
* Add `mkvreader` namespace
* Remov `using namespace` from public headers
* Improve API stability, by converting interface classes into abstract base
  classes.
* Improve conformance with later versions of the MKV specification.

