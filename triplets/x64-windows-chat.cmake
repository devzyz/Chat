set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)

# mysql-connector-cpp's JDBC feature is static-only in the pinned registry.
# Keep this exception narrow; every other dependency remains dynamically linked.
if(PORT STREQUAL "mysql-connector-cpp" OR PORT STREQUAL "libmysql")
    set(VCPKG_LIBRARY_LINKAGE static)
endif()
