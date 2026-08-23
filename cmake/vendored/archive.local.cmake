# This file is part of the Diffractor photo and video organizer
# Copyright 2026  Zac Walker
#
# Purpose: What the libarchive .vcxproj could not describe. The Windows disk and process sources
# have POSIX siblings that were already vendored and simply never referenced, and the checked-in
# config.h describes Windows. See archive-linux/config_linux.h for the configuration itself.

if (NOT WIN32)
    set(_archive_src "${CMAKE_SOURCE_DIR}/third-party/libarchive/libarchive")

    foreach (_windows_only IN ITEMS
            archive_windows.c
            archive_read_disk_windows.c
            archive_write_disk_windows.c
            archive_entry_copy_bhfi.c
            filter_fork_windows.c)
        set_source_files_properties("${_archive_src}/${_windows_only}"
                TARGET_DIRECTORY diffractor_archive PROPERTIES HEADER_FILE_ONLY ON)
    endforeach ()

    target_sources(diffractor_archive PRIVATE "${_archive_src}/filter_fork_posix.c")

    target_compile_definitions(diffractor_archive PRIVATE
            PLATFORM_CONFIG_H="config_linux.h")
    target_include_directories(diffractor_archive PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/archive-linux")

    target_link_libraries(diffractor_archive PRIVATE crypto)
endif ()
