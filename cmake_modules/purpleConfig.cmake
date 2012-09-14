FIND_LIBRARY(PURPLE_LIBRARY NAMES purple)
FIND_PATH(PURPLE_INCLUDE_DIR NAMES "purple.h" PATH_SUFFIXES libpurple )

if(WIN32 AND PURPLE_INCLUDE_DIR )
	if (NOT PURPLE_NOT_RUNTIME)
		set(PURPLE_LIBRARY "")
	endif(NOT PURPLE_NOT_RUNTIME)
	set(PURPLE_FOUND 1)
	message(STATUS "Using purple: ${PURPLE_INCLUDE_DIR} ${PURPLE_LIBRARY}")
else()

if( PURPLE_LIBRARY AND PURPLE_INCLUDE_DIR )
    message( STATUS "Found libpurple: ${PURPLE_LIBRARY}, ${PURPLE_INCLUDE_DIR}")
    set( PURPLE_FOUND 1 )
else( PURPLE_LIBRARY AND PURPLE_INCLUDE_DIR )
    message( STATUS "Could NOT find libpurple" )
endif( PURPLE_LIBRARY AND PURPLE_INCLUDE_DIR )

endif()
