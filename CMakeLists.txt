cmake_minimum_required(VERSION 3.7)
project(netvid)

set(CMAKE_CXX_STANDARD 14)
set(Boost_USE_MULTITHREADED ON)

find_package(Boost REQUIRED COMPONENTS program_options system)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_library(netvid
        check.h
        framebuffer.cpp
        framebuffer.h
        linux_framebuffer.cpp
        linux_framebuffer.h
        net.cpp
        net.h
        protocol.h)
target_include_directories(netvid PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../)

add_executable(netvid_test test.cpp)
target_link_libraries(netvid_test ${Boost_LIBRARIES} Threads::Threads netvid)

add_executable(netvid_play netvid_play.cpp)
target_link_libraries(netvid_play ${Boost_LIBRARIES} Threads::Threads netvid)

add_executable(netvid_record netvid_record.cpp)
target_link_libraries(netvid_record ${Boost_LIBRARIES} Threads::Threads netvid)

add_executable(netvid_slice netvid_slice.cpp)
target_link_libraries(netvid_slice ${Boost_LIBRARIES} Threads::Threads netvid)

configure_file(xz_slice.sh xz_slice.sh COPYONLY)
configure_file(xz_record.sh xz_record.sh COPYONLY)
configure_file(xz_play.sh xz_play.sh COPYONLY)
