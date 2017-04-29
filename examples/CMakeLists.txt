add_subdirectory( mjpgdemuxer )

add_custom_target( examples )
add_dependencies( examples mjpgdemuxer )
