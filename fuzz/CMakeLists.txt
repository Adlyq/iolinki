# libFuzzer harnesses for the three parsers that consume untrusted bytes.
#
# The fuzzer needs coverage feedback from the code under test, so the library is
# built a second time here with sanitizer instrumentation. It gets its own
# target on purpose: instrumenting `iolinki` itself would leave every other
# target in the same tree (examples, a nested BUILD_TESTING=ON) linking an
# instrumented library without the sanitizer runtime.
if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "IOLINK_BUILD_FUZZERS needs clang: the harnesses use libFuzzer")
endif()
if(CMAKE_VERSION VERSION_LESS 3.13)
    message(FATAL_ERROR "IOLINK_BUILD_FUZZERS needs CMake 3.13+ (target_link_options)")
endif()

set(IOLINK_FUZZ_FLAGS
    -fsanitize=fuzzer-no-link,address,undefined
    -fno-sanitize-recover=all
    -fno-omit-frame-pointer
    -O1
    -g)

add_library(iolinki_fuzz STATIC ${IOLINK_SOURCES})
target_include_directories(iolinki_fuzz PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_compile_options(iolinki_fuzz PRIVATE ${IOLINK_FUZZ_FLAGS})

set(IOLINK_FUZZ_TARGETS fuzz_frame fuzz_isdu fuzz_od)
foreach(_fuzz_target IN LISTS IOLINK_FUZZ_TARGETS)
    add_executable(${_fuzz_target} ${_fuzz_target}.c)
    target_link_libraries(${_fuzz_target} PRIVATE iolinki_fuzz)
    target_compile_options(${_fuzz_target} PRIVATE
        -fsanitize=fuzzer,address,undefined
        -fno-sanitize-recover=all
        -fno-omit-frame-pointer
        -O1
        -g)
    target_link_options(${_fuzz_target} PRIVATE -fsanitize=fuzzer,address,undefined)
endforeach()
