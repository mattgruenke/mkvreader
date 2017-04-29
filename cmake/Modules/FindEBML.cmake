## Finds libebml
#
# Sets:
#   EBML_INCLUDE_DIRS - must be added to include path to use libebml.
#   EBML_LIBRARY - libebml.
#   EBML_FOUND - Set if the dependencies were found.

find_path( EBML_INCLUDE_DIRS ebml/EbmlTypes.h )

find_library( EBML_LIBRARY ebml )

if( EXISTS ${EBML_INCLUDE_DIRS} AND EXISTS ${EBML_LIBRARY} )
    set( EBML_FOUND TRUE )
endif()

