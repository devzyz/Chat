cmake_minimum_required(VERSION 3.21)

project(chat VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network)

if(WIN32)
    get_target_property(QT_QMAKE_EXECUTABLE Qt${QT_VERSION_MAJOR}::qmake IMPORTED_LOCATION)
    if(NOT QT_QMAKE_EXECUTABLE)
        message(FATAL_ERROR "Unable to resolve qmake for the selected Qt kit")
    endif()
    execute_process(
        COMMAND "${QT_QMAKE_EXECUTABLE}" -query QT_INSTALL_PLUGINS
        OUTPUT_VARIABLE QT_INSTALL_PLUGINS
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE QT_PLUGIN_QUERY_RESULT
    )
    if(NOT QT_PLUGIN_QUERY_RESULT EQUAL 0 OR NOT EXISTS "${QT_INSTALL_PLUGINS}/platforms/qminimal.dll")
        message(FATAL_ERROR "The selected Qt kit does not provide the qminimal platform plugin")
    endif()
    file(TO_CMAKE_PATH "${QT_INSTALL_PLUGINS}/platforms" QT_TEST_PLATFORM_PLUGIN_PATH)
endif()

set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "Build spdlog as a static library" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "Disable spdlog examples" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "Disable spdlog tests" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "Disable spdlog benchmarks" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "Disable spdlog install target" FORCE)
set(SPDLOG_FMT_EXTERNAL OFF CACHE BOOL "Use spdlog bundled fmt" FORCE)
add_subdirectory(packages/spdlog EXCLUDE_FROM_ALL)

add_library(chat_network_core STATIC
    tcpframedecoder.h tcpframedecoder.cpp
)
target_include_directories(chat_network_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(chat_network_core PUBLIC Qt${QT_VERSION_MAJOR}::Core)

add_library(chat_auth_flow STATIC
    authflowcoordinator.h authflowcoordinator.cpp
)
target_include_directories(chat_auth_flow PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(chat_auth_flow PUBLIC Qt${QT_VERSION_MAJOR}::Widgets)

add_library(chat_message_model STATIC
    messagerecord.h
    messagelistmodel.h messagelistmodel.cpp
    messagemodelstore.h messagemodelstore.cpp
    messageitemdelegate.h messageitemdelegate.cpp
)
target_include_directories(chat_message_model PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(chat_message_model PUBLIC Qt${QT_VERSION_MAJOR}::Widgets)

add_library(chat_session_core STATIC
    clientsession.h clientsession.cpp
    global.h global.cpp
    logmgr.h logmgr.cpp
    messagerecord.h
    tcpframedecoder.h
    tcpmgr.h tcpmgr.cpp
    userdata.h userdata.cpp
    usermgr.h usermgr.cpp
)
target_include_directories(chat_session_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(chat_session_core PUBLIC
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
    spdlog::spdlog
    chat_network_core
)

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
        singleton.h
        httpmgr.h httpmgr.cpp
        config.ini
        timerbtn.h timerbtn.cpp
        clickedlabel.h clickedlabel.cpp
        resetdialog.ui
        resetdialog.h resetdialog.cpp
        tcpmgr.h
        chatdialog.h chatdialog.cpp chatdialog.ui
        clickedbtn.h clickedbtn.cpp
        customizeedit.h customizeedit.cpp
        chatuserlist.h chatuserlist.cpp
        chatuseritem.h chatuseritem.cpp chatuseritem.ui
        listitembase.h listitembase.cpp
        loadingdialog.h loadingdialog.cpp loadingdialog.ui
        chatpage.h chatpage.cpp chatpage.ui
        messagetextedit.h messagetextedit.cpp
        statewidget.h statewidget.cpp
        searchlist.h searchlist.cpp
        userdata.h
        adduseritem.h adduseritem.cpp adduseritem.ui
        findsuccessdialog.h findsuccessdialog.cpp findsuccessdialog.ui
        applyfrienddialog.h applyfrienddialog.cpp applyfrienddialog.ui
        clickedoncelabel.h clickedoncelabel.cpp

        usermgr.h
        clientsession.h
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
        logmgr.h



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
    chat_network_core
    chat_message_model
    chat_session_core
    chat_auth_flow
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
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)

    add_executable(message_model_tests
        tests/message-model/message_model_tests.cpp
    )
    target_link_libraries(message_model_tests PRIVATE
        chat_message_model
        Qt${QT_VERSION_MAJOR}::Test
    )
    set(MESSAGE_MODEL_TEST_ENVIRONMENT "QT_QPA_PLATFORM=minimal")
    if(WIN32)
        list(APPEND MESSAGE_MODEL_TEST_ENVIRONMENT
            "QT_QPA_PLATFORM_PLUGIN_PATH=${QT_TEST_PLATFORM_PLUGIN_PATH}")
    endif()

    add_executable(auth_flow_tests
        tests/auth-flow/auth_flow_tests.cpp
    )
    target_link_libraries(auth_flow_tests PRIVATE
        chat_auth_flow
        Qt${QT_VERSION_MAJOR}::Test
    )
    add_test(
        NAME auth_flow.register_network_error
        COMMAND auth_flow_tests registerNetworkErrorIsReportedOnlyOnce)
    set_tests_properties(auth_flow.register_network_error PROPERTIES
        ENVIRONMENT "${MESSAGE_MODEL_TEST_ENVIRONMENT}"
        LABELS "unit"
        TIMEOUT 10
    )
    add_test(
        NAME auth_flow.reset_network_error
        COMMAND auth_flow_tests resetNetworkErrorIsReportedOnlyOnce)
    set_tests_properties(auth_flow.reset_network_error PROPERTIES
        ENVIRONMENT "${MESSAGE_MODEL_TEST_ENVIRONMENT}"
        LABELS "unit"
        TIMEOUT 10
    )
    add_test(
        NAME auth_flow.login_network_error
        COMMAND auth_flow_tests loginNetworkErrorIsReportedOnlyOnce)
    set_tests_properties(auth_flow.login_network_error PROPERTIES
        ENVIRONMENT "${MESSAGE_MODEL_TEST_ENVIRONMENT}"
        LABELS "unit"
        TIMEOUT 10
    )
    add_test(
        NAME auth_flow.unknown_outcome
        COMMAND auth_flow_tests unknownModuleOrRequestDoesNotChangeTheActiveFlow)
    set_tests_properties(auth_flow.unknown_outcome PROPERTIES
        ENVIRONMENT "${MESSAGE_MODEL_TEST_ENVIRONMENT}"
        LABELS "unit"
        TIMEOUT 10
    )
    add_test(
        NAME auth_flow.malformed_json
        COMMAND auth_flow_tests malformedJsonProducesOneStableError)
    set_tests_properties(auth_flow.malformed_json PROPERTIES
        ENVIRONMENT "${MESSAGE_MODEL_TEST_ENVIRONMENT}"
        LABELS "unit"
        TIMEOUT 10
    )
    add_test(
        NAME auth_flow.business_error
        COMMAND auth_flow_tests businessErrorDoesNotConnectChat)
    set_tests_properties(auth_flow.business_error PROPERTIES
        ENVIRONMENT "${MESSAGE_MODEL_TEST_ENVIRONMENT}"
        LABELS "unit"
        TIMEOUT 10
    )
    add_test(
        NAME auth_flow.login_http_success
        COMMAND auth_flow_tests loginHttpSuccessConnectsOnlyOnce)
    set_tests_properties(auth_flow.login_http_success PROPERTIES
        ENVIRONMENT "${MESSAGE_MODEL_TEST_ENVIRONMENT}"
        LABELS "unit"
        TIMEOUT 10
    )
    add_test(
        NAME auth_flow.tcp_failure
        COMMAND auth_flow_tests tcpFailureDoesNotCreateChat)
    set_tests_properties(auth_flow.tcp_failure PROPERTIES
        ENVIRONMENT "${MESSAGE_MODEL_TEST_ENVIRONMENT}"
        LABELS "unit"
        TIMEOUT 10
    )
    add_test(
        NAME auth_flow.chat_login_failure
        COMMAND auth_flow_tests chatLoginFailureDoesNotShowChat)
    set_tests_properties(auth_flow.chat_login_failure PROPERTIES
        ENVIRONMENT "${MESSAGE_MODEL_TEST_ENVIRONMENT}"
        LABELS "unit"
        TIMEOUT 10
    )
    add_test(
        NAME auth_flow.chat_login_success
        COMMAND auth_flow_tests chatLoginSuccessShowsChatOnlyOnce)
    set_tests_properties(auth_flow.chat_login_success PROPERTIES
        ENVIRONMENT "${MESSAGE_MODEL_TEST_ENVIRONMENT}"
        LABELS "unit"
        TIMEOUT 10
    )
    add_test(
        NAME auth_flow.duplicate_and_late
        COMMAND auth_flow_tests duplicateAndLateOldFlowOutcomesAreIgnored)
    set_tests_properties(auth_flow.duplicate_and_late PROPERTIES
        ENVIRONMENT "${MESSAGE_MODEL_TEST_ENVIRONMENT}"
        LABELS "unit"
        TIMEOUT 10
    )

    add_executable(auth_flow_component_tests
        tests/auth-flow/auth_flow_component_tests.cpp
    )
    target_link_libraries(auth_flow_component_tests PRIVATE
        chat_auth_flow
        chat_session_core
        Qt${QT_VERSION_MAJOR}::Test
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Network
        spdlog::spdlog
    )
    add_test(
        NAME auth_flow.abnormal_disconnect_reset
        COMMAND auth_flow_component_tests abnormalDisconnectDelegatesToClientSessionReset)
    set_tests_properties(auth_flow.abnormal_disconnect_reset PROPERTIES
        ENVIRONMENT "${MESSAGE_MODEL_TEST_ENVIRONMENT}"
        LABELS "component"
        TIMEOUT 10
    )
    add_test(
        NAME message_model.append_ack_status_removal
        COMMAND message_model_tests appendAcknowledgeStatusAndRemovalKeepIndexesSynchronized)
    set_tests_properties(message_model.append_ack_status_removal PROPERTIES
        ENVIRONMENT "${MESSAGE_MODEL_TEST_ENVIRONMENT}" LABELS "unit" TIMEOUT 10)

    add_test(
        NAME message_model.unknown_ids
        COMMAND message_model_tests unknownStableIdsDoNotMutateTheModel)
    set_tests_properties(message_model.unknown_ids PROPERTIES
        ENVIRONMENT "${MESSAGE_MODEL_TEST_ENVIRONMENT}" LABELS "unit" TIMEOUT 10)

    add_test(
        NAME message_model.history_order
        COMMAND message_model_tests historyDeduplicatesAndRemainsChronological)
    set_tests_properties(message_model.history_order PROPERTIES
        ENVIRONMENT "${MESSAGE_MODEL_TEST_ENVIRONMENT}" LABELS "unit" TIMEOUT 10)

    add_test(
        NAME message_model.multiple_history_pages
        COMMAND message_model_tests multipleHistoryPagesRemainChronological)
    set_tests_properties(message_model.multiple_history_pages PROPERTIES
        ENVIRONMENT "${MESSAGE_MODEL_TEST_ENVIRONMENT}" LABELS "unit" TIMEOUT 10)

    add_test(
        NAME message_model.shifted_indexes
        COMMAND message_model_tests removalAndAcknowledgementRebuildShiftedIndexes)
    set_tests_properties(message_model.shifted_indexes PROPERTIES
        ENVIRONMENT "${MESSAGE_MODEL_TEST_ENVIRONMENT}" LABELS "unit" TIMEOUT 10)

    add_test(
        NAME message_model.text_and_chat_identity
        COMMAND message_model_tests textAndChatIdentityRoundTripWithoutNormalization)
    set_tests_properties(message_model.text_and_chat_identity PROPERTIES
        ENVIRONMENT "${MESSAGE_MODEL_TEST_ENVIRONMENT}" LABELS "unit" TIMEOUT 10)

    add_test(
        NAME message_model.store_pagination_state
        COMMAND message_model_tests storeRetainsOneModelAndPaginationStatePerChat)
    set_tests_properties(message_model.store_pagination_state PROPERTIES
        ENVIRONMENT "${MESSAGE_MODEL_TEST_ENVIRONMENT}" LABELS "component" TIMEOUT 10)

    add_test(
        NAME message_model.delegate_reflow
        COMMAND message_model_tests delegateReflowsLongTextForANarrowViewport)
    set_tests_properties(message_model.delegate_reflow PROPERTIES
        ENVIRONMENT "${MESSAGE_MODEL_TEST_ENVIRONMENT}" LABELS "component" TIMEOUT 10)

    add_executable(network_state_tests
        tests/network-state/tcp_frame_decoder_tests.cpp
    )
    target_link_libraries(network_state_tests PRIVATE
        chat_network_core
    )
    add_test(NAME network_state_tests COMMAND network_state_tests)
    set_tests_properties(network_state_tests PROPERTIES
        LABELS "unit"
        TIMEOUT 10
    )

    add_executable(session_reset_tests
        tests/session-reset/session_reset_tests.cpp
    )
    target_link_libraries(session_reset_tests PRIVATE
        chat_session_core
        Qt${QT_VERSION_MAJOR}::Test
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Network
        spdlog::spdlog
    )
    add_test(
        NAME session_reset.account_state
        COMMAND session_reset_tests accountStateDoesNotCrossLoginSessions)
    set_tests_properties(session_reset.account_state PROPERTIES
        ENVIRONMENT "${MESSAGE_MODEL_TEST_ENVIRONMENT}"
        LABELS "component"
        TIMEOUT 10
    )
    add_test(
        NAME session_reset.owned_ui_and_idempotence
        COMMAND session_reset_tests resetDestroysOwnedSessionUiOnceAndPreservesReason)
    set_tests_properties(session_reset.owned_ui_and_idempotence PROPERTIES
        ENVIRONMENT "${MESSAGE_MODEL_TEST_ENVIRONMENT}"
        LABELS "component"
        TIMEOUT 10
    )
    add_test(
        NAME session_reset.pending_batch
        COMMAND session_reset_tests resetDropsPendingTextBatchBeforeAnOldFailureArrives)
    set_tests_properties(session_reset.pending_batch PROPERTIES
        ENVIRONMENT "${MESSAGE_MODEL_TEST_ENVIRONMENT}"
        LABELS "component"
        TIMEOUT 10
    )

    add_executable(http_transport_tests
        tests/http-transport/http_transport_tests.cpp
    )
    target_link_libraries(http_transport_tests PRIVATE
        chat_gate_http_transport
        Qt${QT_VERSION_MAJOR}::Test
        Qt${QT_VERSION_MAJOR}::Network
    )
    foreach(HTTP_TRANSPORT_CASE IN ITEMS
        successPreservesRequestAndFlowIdentity
        refusedConnectionHasOneBoundedOutcome
        finiteDeadlineAbortsAnUnresponsivePeer
        malformedJsonIsRejectedWithoutLeakingItsBody
        maximumResponseIsAccepted
        oneByteOverMaximumIsRejected
        peerClosingMidResponseHasOneNetworkOutcome
        explicitCancelHasExactlyOneTerminalOutcome
        lateReplyFromAnOldFlowCannotCompleteTheNewFlow
        deletingTransportReleasesReplyAndLoopbackSocket
    )
        add_test(
            NAME http_transport.${HTTP_TRANSPORT_CASE}
            COMMAND http_transport_tests ${HTTP_TRANSPORT_CASE})
        set_tests_properties(http_transport.${HTTP_TRANSPORT_CASE} PROPERTIES
            ENVIRONMENT "${MESSAGE_MODEL_TEST_ENVIRONMENT}"
            LABELS "integration"
            TIMEOUT 10
        )
    endforeach()
endif()
