## Finds libmatroska
#
# Sets:
#   Matroska_INCLUDE_DIRS - must be added to include path to use libmatroska.
#   Matroska_LIBRARY - libmatroska.
#   Matroska_FOUND - Set if the dependencies were found.

find_path( Matroska_INCLUDE_DIRS matroska/KaxTypes.h )

find_library( Matroska_LIBRARY matroska )

if( EXISTS ${Matroska_INCLUDE_DIRS} AND EXISTS ${Matroska_LIBRARY} )
    set( Matroska_FOUND TRUE )
endif()

