cmake_minimum_required(VERSION 3.5 FATAL_ERROR)
project(zopfli LANGUAGES C)

add_library(zopfli
	blocksplitter.c
	deflate.c
	katajainen.c
	lz77.c
	squeeze.c
	util.c
	zlib_container.c
	gzip_container.c
	zopfli_lib.c
	LzFind.c
	
	blocksplitter.h
	deflate.h
	katajainen.h
	lz77.h
	match.h
	squeeze.h
	util.h
	symbols.h
	zlib_container.h
	gzip_container.h
	zopfli.h
	LzFind.h)

add_library(zopfli::zopfli ALIAS zopfli)

target_link_libraries(zopfli m)
