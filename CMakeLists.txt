cmake_minimum_required(VERSION 3.16)
project(void_arcade LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(SDL2_MIXER REQUIRED SDL2_mixer)
find_package(OpenGL REQUIRED)

file(GLOB_RECURSE APP_SRC CONFIGURE_DEPENDS src/*.cpp)

add_executable(void_arcade ${APP_SRC})

target_include_directories(void_arcade PRIVATE
  src
  third_party
  ${SDL2_INCLUDE_DIRS}
  ${SDL2_MIXER_INCLUDE_DIRS}
)

target_link_libraries(void_arcade PRIVATE
  ${SDL2_LIBRARIES}
  ${SDL2_MIXER_LIBRARIES}
  OpenGL::GL
)

target_link_directories(void_arcade PRIVATE
  ${SDL2_LIBRARY_DIRS}
  ${SDL2_MIXER_LIBRARY_DIRS}
)

# Copy runtime assets next to the binary so SDL_GetBasePath() finds them.
add_custom_command(TARGET void_arcade POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:void_arcade>/assets
)

# Install rules (used by packaging, e.g. the snap). The binary and its assets go
# under <prefix>/bin so SDL_GetBasePath() finds <exe-dir>/assets at runtime.
install(TARGETS void_arcade RUNTIME DESTINATION bin)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/assets DESTINATION bin)
