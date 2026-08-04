include_directories("${CMAKE_CURRENT_BINARY_DIR}/../")

add_executable(darktable-test-variables variables.c)
target_link_libraries(darktable-test-variables lib_darktable)

if(WIN32)
  # This tester sets up a darktable instance (of sorts). Hence it expects libraries at ../lib/darktable
  # Easiest way to comply with this on Windows: Put tester executable in same directory as darktable executable
  set_target_properties(darktable-test-variables PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${DARKTABLE_BINDIR}
  )
endif(WIN32)

add_subdirectory(unittests)

add_cmocka_test(test_pdf_finalize
                SOURCES
                  "${CMAKE_SOURCE_DIR}/tools/windows-print/test-pdf-finalize.c"
                  "${CMAKE_SOURCE_DIR}/src/common/pdf.c"
                LINK_LIBRARIES
                  ${Glib_LIBRARIES}
                  ZLIB::ZLIB)
target_include_directories(test_pdf_finalize PRIVATE
  "${CMAKE_SOURCE_DIR}/src"
  ${Glib_INCLUDE_DIRS})
if(WIN32)
  target_link_libraries(test_pdf_finalize PRIVATE intl)
endif()

if(BUILD_PRINT AND PRINT_BACKEND_ENABLED)
  add_cmocka_test(test_print_backend_utils
                  SOURCES
                    "${CMAKE_SOURCE_DIR}/tools/windows-print/test-print-backend-utils.c"
                    "${CMAKE_SOURCE_DIR}/src/common/print_backend_utils.c"
                  LINK_LIBRARIES
                    ${Glib_LIBRARIES})
  target_include_directories(test_print_backend_utils PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    ${Glib_INCLUDE_DIRS})

  add_cmocka_test(test_print_buffer_ownership
                  SOURCES
                    "${CMAKE_SOURCE_DIR}/tools/windows-print/test-print-buffer-ownership.c"
                    "${CMAKE_SOURCE_DIR}/src/common/printing.c"
                  LINK_LIBRARIES
                    ${Glib_LIBRARIES})
  target_include_directories(test_print_buffer_ownership PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    ${Glib_INCLUDE_DIRS})
  target_compile_definitions(test_print_buffer_ownership PRIVATE
    DT_PRINT_OWNERSHIP_STANDALONE)

  if(NOT WIN32)
    target_link_libraries(test_print_buffer_ownership PRIVATE m)
  endif()

  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_print_buffer_ownership PRIVATE
      -ffunction-sections -fdata-sections)
    if(APPLE)
      target_link_options(test_print_buffer_ownership PRIVATE -Wl,-dead_strip)
    else()
      target_link_options(test_print_buffer_ownership PRIVATE -Wl,--gc-sections)
    endif()
  endif()

  if(WIN32)
    set_target_properties(test_print_buffer_ownership PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY ${DARKTABLE_BINDIR})
  endif()
endif()

if(WIN32 AND BUILD_PRINT AND PRINT_BACKEND_ENABLED)
  add_cmocka_test(test_win_print_utils
                  SOURCES
                    "${CMAKE_SOURCE_DIR}/tools/windows-print/test-win-print-utils.c"
                    "${CMAKE_SOURCE_DIR}/src/common/win_print_utils.c"
                  LINK_LIBRARIES
                    ${Glib_LIBRARIES}
                    ${LCMS2_LIBRARIES})
  target_include_directories(test_win_print_utils PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    ${Glib_INCLUDE_DIRS}
    ${LCMS2_INCLUDE_DIRS})
endif()

if(NOT WIN32 AND BUILD_PRINT AND PRINT_BACKEND_ENABLED AND CUPS_FOUND)
  add_cmocka_test(test_cups_print_utils
                  SOURCES
                    "${CMAKE_SOURCE_DIR}/tools/windows-print/test-cups-print-utils.c"
                    "${CMAKE_SOURCE_DIR}/src/common/cups_print_utils.c"
                  LINK_LIBRARIES
                    ${CUPS_LIBRARIES}
                    ${Glib_LIBRARIES})
  target_include_directories(test_cups_print_utils PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    ${CUPS_INCLUDE_DIR}
    ${Glib_INCLUDE_DIRS})
endif()
