cmake_minimum_required(VERSION 2.8.12)
set (TARGET_NAME libvorbis)


# Install options
option(INSTALL_DOCS "Install documentation" OFF)
option(INSTALL_PKG_CONFIG_MODULE "Install ogg.pc file" OFF)
option(INSTALL_CMAKE_PACKAGE_MODULE "Install CMake package configiguration module" ON)

# Extract project version from configure.ac
file(READ configure.ac CONFIGURE_AC_CONTENTS)
string(REGEX MATCH "AC_INIT//(//[libogg//],//[([0-9]*).([0-9]*).([0-9]*)" DUMMY ${CONFIGURE_AC_CONTENTS})
set(PROJECT_VERSION_MAJOR ${CMAKE_MATCH_1})
set(PROJECT_VERSION_MINOR ${CMAKE_MATCH_2})
set(PROJECT_VERSION_PATCH ${CMAKE_MATCH_3})
set(PROJECT_VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH})

# Extract library version from configure.ac
string(REGEX MATCH "LIB_CURRENT=([0-9]*)" DUMMY ${CONFIGURE_AC_CONTENTS})
set(LIB_CURRENT ${CMAKE_MATCH_1})

string(REGEX MATCH "LIB_AGE=([0-9]*)" DUMMY ${CONFIGURE_AC_CONTENTS})
set(LIB_AGE ${CMAKE_MATCH_1})

string(REGEX MATCH "LIB_REVISION=([0-9]*)" DUMMY ${CONFIGURE_AC_CONTENTS})
set(LIB_REVISION ${CMAKE_MATCH_1})

math(EXPR LIB_SOVERSION "${LIB_CURRENT} - ${LIB_AGE}")
set(LIB_VERSION "${LIB_SOVERSION}.${LIB_AGE}.${LIB_REVISION}")



# Configure config_type.h
check_include_files(inttypes.h INCLUDE_INTTYPES_H)
check_include_files(stdint.h INCLUDE_STDINT_H)
check_include_files(sys/types.h INCLUDE_SYS_TYPES_H)


set(VORBIS_HEADERS
    lib/backends.h
    lib/bitrate.h
    lib/codebook.h
    include/vorbis/codec.
    lib/codec_internal.h
    lib/envelope.h
    lib/modes/floor_all.h
    lib/books/floor/floor_books.h
    lib/highlevel.h
    lib/lookup.h
    lib/lookup_data.h
    lib/lpc.h
    lib/lsp.h
    lib/masking.h
    lib/mdct.h
    lib/misc.h
    lib/os.h
    lib/psy.h
    lib/modes/psych_11.h
    lib/modes/psych_16.h
    lib/modes/psych_44.h
    lib/modes/psych_8.h
    lib/registry.h
    lib/books/coupled/res_books_stereo.h
    lib/books/uncoupled/res_books_uncoupled.h
    lib/modes/residue_16.h
    lib/modes/residue_44.h
    lib/modes/residue_44u.h
    lib/modes/residue_8.h
    lib/scales.h
    lib/modes/setup_11.h
    lib/modes/setup_16.h
    lib/modes/setup_22.h
    lib/modes/setup_32.h
    lib/modes/setup_44.h
    lib/modes/setup_44u.h
    lib/modes/setup_8.h
    lib/modes/setup_X.h
    lib/smallft.h
    include/vorbis/vorbisenc.h
    include/vorbis/vorbisfile.h
    lib/window.h
)

set(VORBIS_SOURCES
    lib/analysis.c
    lib/bitrate.c
    lib/block.c
    lib/codebook.c
    lib/envelope.c
    lib/floor0.c
    lib/floor1.c
    lib/info.c
    lib/lookup.c
    lib/lpc.c
    lib/lsp.c
    lib/mapping0.c
    lib/mdct.c
    lib/psy.c
    lib/registry.c
    lib/res0.c
    lib/sharedbook.c
    lib/smallft.c
    lib/synthesis.c
    lib/vorbisenc.c
    lib/window.c
)

file (GLOB CPP_FILES ${VORBIS_SOURCES} )
file (GLOB H_FILES ${VORBIS_HEADERS} )

set (SOURCE_FILES ${CPP_FILES} ${H_FILES})
set (INCLUDE_DIRS include lib ${CMAKE_BINARY_DIR}/${DEST_INCLUDE_DIR}/ThirdParty/libogg)

# Setup target
setup_library ()

# Install headers for building and using the Urho3D library
install_header_files (DIRECTORY include/vorbis DESTINATION ${DEST_INCLUDE_DIR}/ThirdParty FILES_MATCHING PATTERN *.h BUILD_TREE_ONLY)

