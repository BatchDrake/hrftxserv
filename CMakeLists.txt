cmake_minimum_required(VERSION 3.8)
project(HRFTXServ)

include(FindPkgConfig)

pkg_check_modules(SIGUTILS REQUIRED sigutils>=0.1)


string(REPLACE ";" " " SIGUTILS_SPC_CFLAGS "${SIGUTILS_CFLAGS}")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall ${SIGUTILS_SPC_CFLAGS}")

add_executable(
  hrftxserv
  src/hrftxserv.c
  src/list.c
  src/main.c
  include/list.h
  include/hrftxserv.h)

target_include_directories(hrftxserv PUBLIC include ${SIGUTILS_INCLUDE_DIRS})

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(hrftxserv PRIVATE Threads::Threads ${SIGUTILS_LIBRARIES})