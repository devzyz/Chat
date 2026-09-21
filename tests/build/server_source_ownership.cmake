cmake_minimum_required(VERSION 3.24)
get_filename_component(repo_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${repo_root}/cmake/ServerSourceOwnership.cmake")
chat_check_source_ownership("${repo_root}")
