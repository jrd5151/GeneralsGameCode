set(MILES_SRC
    mss/mss.h
    MilesLoader.h
)

if(RTS_BUILD_OPTION_MINIAUDIO)
    list(APPEND MILES_SRC MilesMiniaudio.cpp)
else()
    list(APPEND MILES_SRC MilesLoader.cpp)
endif()

add_library(deps_miles STATIC)

target_sources(deps_miles PRIVATE ${MILES_SRC})

target_include_directories(deps_miles PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/mss
)

target_link_libraries(deps_miles PRIVATE
    depsi_always # Gives force included headers. Link early.
    deps_utility
)

if(RTS_BUILD_OPTION_MINIAUDIO)
    target_link_libraries(deps_miles PRIVATE deps_miniaudio)
endif()
