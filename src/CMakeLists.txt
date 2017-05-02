## Settings ##

## Paths ##

set( incl ${PROJECT_SOURCE_DIR}/include )
set( h_dir ${incl}/mkvreader )


## What to build ##

set( sources
    matroska_parser.cpp
)

file( GLOB headers
    LIST_DIRECTORIES false
    ${h_dir}/*
)

add_library( mkvreader ${sources} )

target_link_libraries( mkvreader
    ${EBML_LIBRARY}
    ${Matroska_LIBRARY}
    Boost::filesystem
)


## How to build it ##

include_directories(
    ${incl}
    ${Boost_INCLUDE_DIRS}
    ${EBML_INCLUDE_DIRS}
    ${Matroska_INCLUDE_DIRS}
)


## Where to install it ##

install(
    TARGETS mkvreader
    DESTINATION lib )

install(
    FILES ${headers}
    DESTINATION include/mkvreader )

