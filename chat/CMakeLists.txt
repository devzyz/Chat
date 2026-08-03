cmake_minimum_required(VERSION 3.16)

project(chat VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network)

set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "Build spdlog as a static library" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "Disable spdlog examples" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "Disable spdlog tests" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "Disable spdlog benchmarks" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "Disable spdlog install target" FORCE)
set(SPDLOG_FMT_EXTERNAL OFF CACHE BOOL "Use spdlog bundled fmt" FORCE)
add_subdirectory(packages/spdlog EXCLUDE_FROM_ALL)

set(PROJECT_SOURCES
        main.cpp
        mainwindow.cpp
        mainwindow.h
        mainwindow.ui
)

if(WIN32)
    list(APPEND PROJECT_SOURCES app.rc)
endif()

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(chat
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
        logindialog.h logindialog.cpp logindialog.ui
        rc.qrc
        registerdialog.h registerdialog.cpp registerdialog.ui
        global.h
        global.cpp
        singleton.h
        httpmgr.h httpmgr.cpp
        config.ini
        timerbtn.h timerbtn.cpp
        clickedlabel.h clickedlabel.cpp
        resetdialog.ui
        resetdialog.h resetdialog.cpp
        tcpmgr.h tcpmgr.cpp
        chatdialog.h chatdialog.cpp chatdialog.ui
        clickedbtn.h clickedbtn.cpp
        customizeedit.h customizeedit.cpp
        chatuserlist.h chatuserlist.cpp
        chatuseritem.h chatuseritem.cpp chatuseritem.ui
        listitembase.h listitembase.cpp
        loadingdialog.h loadingdialog.cpp loadingdialog.ui
        chatpage.h chatpage.cpp chatpage.ui
        messagerecord.h
        messagelistmodel.h messagelistmodel.cpp
        messagemodelstore.h messagemodelstore.cpp
        messageitemdelegate.h messageitemdelegate.cpp
        messagetextedit.h messagetextedit.cpp
        statewidget.h statewidget.cpp
        searchlist.h searchlist.cpp
        userdata.h userdata.cpp
        adduseritem.h adduseritem.cpp adduseritem.ui
        findsuccessdialog.h findsuccessdialog.cpp findsuccessdialog.ui
        applyfrienddialog.h applyfrienddialog.cpp applyfrienddialog.ui
        clickedoncelabel.h clickedoncelabel.cpp

        usermgr.h usermgr.cpp
        contactuserlist.h contactuserlist.cpp
        contactuseritem.h contactuseritem.cpp contactuseritem.ui
        grouptipitem.h grouptipitem.cpp grouptipitem.ui

        applyfriendpage.h applyfriendpage.cpp applyfriendpage.ui
        applyfrienditem.h applyfrienditem.cpp applyfrienditem.ui
        applyfriendlist.h applyfriendlist.cpp
        findfaildialog.h findfaildialog.cpp findfaildialog.ui
        authfrienddialog.h authfrienddialog.cpp authfrienddialog.ui
        chatdetaillist.h chatdetaillist.cpp
        friendinfopage.h friendinfopage.cpp friendinfopage.ui
        userinfopage.h userinfopage.cpp userinfopage.ui
        editavatardialog.h editavatardialog.cpp editavatardialog.ui
        logmgr.h logmgr.cpp



    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET chat APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(chat SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(chat
            ${PROJECT_SOURCES}
        )
    endif()
endif()

set(CONFIG_FILE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/config.ini")
set(STATIC_FOLDER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/static")

add_custom_command(
    TARGET chat POST_BUILD
    # 使用 CMake 内置的跨平台拷贝命令，自动处理斜杠问题
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CONFIG_FILE_SRC}"
        "$<TARGET_FILE_DIR:chat>/config.ini"

    COMMAND ${CMAKE_COMMAND} -E copy_directory
           "${STATIC_FOLDER_SRC}"
           "$<TARGET_FILE_DIR:chat>/static"
    COMMENT "Copying config.ini to output directory..."
)

target_include_directories(chat PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(chat PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

target_link_libraries(chat PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
    spdlog::spdlog
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.chat)
endif()
set_target_properties(chat PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS chat
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(chat)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(message_model_tests
        tests/messagelistmodel_test.cpp
        messagerecord.h
        messagelistmodel.h messagelistmodel.cpp
        messagemodelstore.h messagemodelstore.cpp
        messageitemdelegate.h messageitemdelegate.cpp
    )
    target_include_directories(message_model_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(message_model_tests PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
    )
    add_test(NAME message_model_tests COMMAND message_model_tests)
    set_tests_properties(message_model_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=minimal"
    )
endif()
