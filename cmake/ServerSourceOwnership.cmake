include_guard(GLOBAL)

# MSBuild projects already own each production translation unit. Read those
# explicit entries instead of maintaining a second Linux source inventory.
function(chat_read_project_sources project output)
    file(READ "${project}" project_xml)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${project}")
    get_filename_component(project_directory "${project}" DIRECTORY)
    if(project_xml MATCHES "<ItemGroup[^>]*Condition=" OR project_xml MATCHES "<ClCompile[^>]*(Remove|Update)=")
        message(FATAL_ERROR "Conditional source groups require explicit cross-platform handling: ${project}")
    endif()
    string(REGEX MATCHALL "<ClCompile[^>]*Include=[^>]*>" entries "${project_xml}")
    set(sources "")
    foreach(entry IN LISTS entries)
        if(NOT entry MATCHES "^<ClCompile[ \t\r\n]+Include=\"[^\"]+\"[ \t\r\n]*/>$")
            message(FATAL_ERROR "Conditional/metadata source entry requires explicit handling: ${entry}")
        endif()
        string(REGEX REPLACE ".*Include=\"([^\"]+)\".*" "\\1" source "${entry}")
        # Canonical protobuf is generated and compiled once by chat_protocol_cpp.
        if(source MATCHES "^\\$\\(ProtocolGeneratedDir\\)")
            continue()
        endif()
        if(source MATCHES "\\$")
            message(FATAL_ERROR "Unsupported source property in ${project}: ${source}")
        endif()
        string(REPLACE "\\" "/" source "${source}")
        cmake_path(ABSOLUTE_PATH source BASE_DIRECTORY "${project_directory}" NORMALIZE)
        if(NOT EXISTS "${source}")
            message(FATAL_ERROR "Missing production source: ${source}")
        endif()
        if(source IN_LIST sources)
            message(FATAL_ERROR "Duplicate production source in ${project}: ${source}")
        endif()
        list(APPEND sources "${source}")
    endforeach()
    if(NOT sources)
        message(FATAL_ERROR "Empty production source owner: ${project}")
    endif()
    set(${output} "${sources}" PARENT_SCOPE)
endfunction()

set(CHAT_SERVER_SOURCE_OWNERS
    GateServer/GateServer/GateTransport
    GateServer/GateServer/GateRequest
    GateServer/GateServer/GateGrpcClients
    GateServer/GateServer/GateServer
    StatusServer/StatusServer/StatusRouting
    StatusServer/StatusServer/StatusTransport
    StatusServer/StatusServer/StatusServer
    ChatServer/ChatServer/ChatTransport
    ChatServer/ChatServer/ChatSessionState
    ChatServer/ChatServer/LogicDispatcher
    ChatServer/ChatServer/ChatGrpcClients
    ChatServer/ChatServer/ChatServer)

function(chat_check_source_ownership root)
    set(all_sources "")
    foreach(owner IN LISTS CHAT_SERVER_SOURCE_OWNERS)
        chat_read_project_sources("${root}/${owner}.vcxproj" sources)
        file(READ "${root}/${owner}.vcxproj" owner_xml)
        string(REGEX MATCHALL "<ProjectReference[^>]*Include=\"[^\"]+\"" references "${owner_xml}")
        get_filename_component(owner_directory "${root}/${owner}" DIRECTORY)
        foreach(reference IN LISTS references)
            string(REGEX REPLACE ".*Include=\"([^\"]+)\"" "\\1" referenced_project "${reference}")
            string(REPLACE "\\" "/" referenced_project "${referenced_project}")
            cmake_path(ABSOLUTE_PATH referenced_project BASE_DIRECTORY "${owner_directory}" NORMALIZE)
            file(RELATIVE_PATH referenced_owner "${root}" "${referenced_project}")
            string(REGEX REPLACE "\\.vcxproj$" "" referenced_owner "${referenced_owner}")
            if(NOT referenced_owner IN_LIST CHAT_SERVER_SOURCE_OWNERS)
                message(FATAL_ERROR "Unregistered production project reference: ${referenced_project}")
            endif()
        endforeach()
        foreach(source IN LISTS sources)
            if(source IN_LIST all_sources)
                message(FATAL_ERROR "Production source has multiple owners: ${source}")
            endif()
            list(APPEND all_sources "${source}")
        endforeach()
    endforeach()
    list(LENGTH all_sources count)
    message(STATUS "Windows/Linux production ownership: ${count} explicit translation units")
endfunction()

function(chat_add_project_library target project)
    chat_read_project_sources("${CMAKE_SOURCE_DIR}/${project}.vcxproj" sources)
    if(ARGC GREATER 2)
        list(REMOVE_ITEM sources "${CMAKE_SOURCE_DIR}/${ARGV2}")
    endif()
    add_library(${target} STATIC ${sources})
    get_filename_component(source_root "${project}" DIRECTORY)
    chat_configure_server_target(${target} "${source_root}")
endfunction()
