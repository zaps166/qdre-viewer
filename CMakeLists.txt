cmake_minimum_required(VERSION 3.20)

project(qdre-viewer
    VERSION 2.0
    LANGUAGES CXX
)

find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE PROJECT_GIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()
if(PROJECT_GIT_HASH)
    string(APPEND CMAKE_PROJECT_VERSION "-${PROJECT_GIT_HASH}")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

set(CMAKE_AUTOMOC_COMPILER_PREDEFINES OFF)
set(QT_NO_PRIVATE_MODULE_WARNING ON)
set(QT_QML_GENERATE_AOTSTATS OFF)

option(USE_WIDGETS "Compile with QtWidgets (for native file dialogs and KDE style" ON)
option(USE_COLORD "Compile with colord support for X11 color management" ON)
option(DISCARD_QML_CONTENTS "Don't store QML source code" OFF)
option(USE_ASAN "Compile with address sanitizer" OFF)

find_package(Qt6 REQUIRED COMPONENTS
    GuiPrivate
    QuickControls2
    LinguistTools
)
if(USE_WIDGETS)
    find_package(Qt6 REQUIRED COMPONENTS
        Widgets
    )
endif()
if(USE_COLORD)
    find_package(Qt6 REQUIRED COMPONENTS
        DBus
    )
endif()

find_package(PkgConfig REQUIRED)

pkg_check_modules(EXIV2 REQUIRED IMPORTED_TARGET exiv2)

qt_standard_project_setup(REQUIRES 6.11
    I18N_TRANSLATED_LANGUAGES pl
)

set(SOURCES
    Headers.hpp
    Application.hpp Application.cpp
    ImageProvider.hpp ImageProvider.cpp
    DirectoryNavigation.hpp DirectoryNavigation.cpp
)
if(USE_COLORD)
    list(APPEND SOURCES
        Colord.hpp Colord.cpp
    )
endif()
qt_add_executable(${PROJECT_NAME}
    ${SOURCES}
)

set(QML_MODULE_ARGS
    URI "QDREViewer"
    QML_FILES Main.qml View.qml
    QML_FILES Defines.js
)
if(DISCARD_QML_CONTENTS)
    list(APPEND QML_MODULE_ARGS DISCARD_QML_CONTENTS)
else()
    list(APPEND QML_MODULE_ARGS NO_CACHEGEN)
endif()

qt_add_qml_module(${PROJECT_NAME}
    ${QML_MODULE_ARGS}
)

qt_add_translations(${PROJECT_NAME}
    RESOURCE_PREFIX "/qt/qml/QDREViewer/i18n"
    TS_FILE_BASE qml
    TS_OUTPUT_DIRECTORY translations
    NO_GENERATE_PLURALS_TS_FILE
    LUPDATE_OPTIONS -no-obsolete -locations none
)

target_compile_definitions(${PROJECT_NAME}
    PRIVATE
    QT_NO_CAST_FROM_ASCII
    QT_USE_QSTRINGBUILDER
    QT_NO_CONTEXTLESS_CONNECT
    QT_NO_SIGNALS_SLOTS_KEYWORDS
    PROJECT_VERSION="${CMAKE_PROJECT_VERSION}"
)
if(USE_WIDGETS)
    target_compile_definitions(${PROJECT_NAME}
        PRIVATE
        USE_WIDGETS
        APPLICATION_BASE_CLASS=QApplication
    )
else()
    target_compile_definitions(${PROJECT_NAME}
        PRIVATE
        APPLICATION_BASE_CLASS=QGuiApplication
    )
endif()
if(USE_COLORD)
    target_compile_definitions(${PROJECT_NAME}
        PRIVATE
        USE_COLORD
    )
endif()

target_precompile_headers(${PROJECT_NAME}
    PRIVATE
    Headers.hpp
)

target_compile_options(${PROJECT_NAME}
    PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wold-style-cast
    -Wnon-virtual-dtor
    -Woverloaded-virtual
    -Wnull-dereference
    -Wdouble-promotion
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
    Qt6::GuiPrivate
    Qt6::QuickControls2
    PkgConfig::EXIV2
)
if(USE_WIDGETS)
    target_link_libraries(${PROJECT_NAME}
        PRIVATE
        Qt6::Widgets
    )
endif()
if(USE_COLORD)
    target_link_libraries(${PROJECT_NAME}
        PRIVATE
        Qt6::DBus
    )
endif()

if(USE_ASAN)
    target_compile_options(${PROJECT_NAME}
        PRIVATE
        -fsanitize=address
        -fno-omit-frame-pointer
    )
    target_link_options(${PROJECT_NAME}
        PRIVATE
        -fsanitize=address
    )
endif()

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(FILES
    "data/${PROJECT_NAME}.desktop"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
)
install(FILES
    "data/${PROJECT_NAME}.svg"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
)
install(FILES
    "LICENSE"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/${PROJECT_NAME}"
)
