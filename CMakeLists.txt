# =============================================================================
# tcxOpenStreetMap - slippy-map tile renderer
# =============================================================================
# Has its own CMakeLists (rather than letting auto-collect handle the addon)
# because it depends on tcxCurl for HTTP and needs to link against that
# addon's interface target.
# =============================================================================

cmake_minimum_required(VERSION 3.16)

set(ADDON_NAME tcxOpenStreetMap)

file(GLOB ADDON_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
file(GLOB ADDON_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)

add_library(${ADDON_NAME} STATIC ${ADDON_SOURCES} ${ADDON_HEADERS})
target_include_directories(${ADDON_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(${ADDON_NAME} PUBLIC TrussC tcxCurl)
