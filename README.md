# mkjbf

## Introduction

mkjbf is a program which creates jbf browse files compatible with Jasc
Paint Shop Pro 7.

Typical use case is to run the program in a directory of images to
create the jbf browse file for those images. Only jpg and png images
are supported at this time. There is no support for updating an
already existing browse file either - a new one will be created from
scratch every time the program is run.

The advantage of using `mkjbf` compared to generating the browse files
with psp itself is that this program allows creating higher quality
thumbnail images. The default setting is 100% quality, which gives the
best looking thumbnails, but also the largest file size.

## Using mkjbf

mkjbf operates in the current working directory, or in a supplied path,
always putting the `pspbrwse.jbf` file in the current working directory.

The following command line options can be used to tune this program:

Option  | Argument  | Description
:---    | :---      | :---
-s      |           | sort by...
&nbsp;  | 0         | &emsp;no sorting
&nbsp;  | n         | &emsp;file name
&nbsp;  | g         | &emsp;file name general numeric (default)
&nbsp;  | d         | &emsp;file date
&nbsp;  | f         | &emsp;file size
&nbsp;  | w         | &emsp;image width
&nbsp;  | h         | &emsp;image height
&nbsp;  | x         | &emsp;image size in pixels
-r      |           | reverse sort order
-c      |           | ignore case when sorting
-z      | size      | thumbnail size, default 150
-q      | quality   | thumbnail quality, default 100
-h      |           | show help
path    |           | working directory, default .

## Building jbf2html

The included Makefile compiles all source files and links the
binary. Invoke `make` to build mkjbf. The project depends on
MagickWand which needs to be provided by the environment. The
program has been compiled and used in Cygwin only.
