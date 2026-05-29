cmake_minimum_required(VERSION 3.20)
project(DfApp VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

# ── SdrSdk / SdrTaskApi ───────────────────────────────────────────────────────
get_filename_component(PARENT "${CMAKE_SOURCE_DIR}" DIRECTORY)
if(EXISTS "${PARENT}/SdrSdk/CMakeLists.txt")
    message(STATUS "[DfApp] using sibling SdrSdk at ${PARENT}/SdrSdk")
    set(SDRSDK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDRSDK_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    add_subdirectory("${PARENT}/SdrSdk" "${CMAKE_BINARY_DIR}/SdrSdk")
elseif(EXISTS "${PARENT}/SdrTaskApi/CMakeLists.txt")
    message(STATUS "[DfApp] using sibling SdrTaskApi at ${PARENT}/SdrTaskApi")
    add_subdirectory("${PARENT}/SdrTaskApi" "${CMAKE_BINARY_DIR}/SdrTaskApi")
else()
    find_package(sdr_task_api QUIET)
    if(NOT sdr_task_api_FOUND)
        message(FATAL_ERROR "[DfApp] SdrSdk not found.\n"
            "  Clone as sibling: git clone https://github.com/BMichaud7/SdrSdk.git ../SdrSdk")
    endif()
endif()

# ── Eigen3 (header-only, for MUSIC covariance / eigendecomposition) ───────────
find_package(Eigen3 3.4 QUIET NO_MODULE)
if(NOT Eigen3_FOUND)
    message(STATUS "[DfApp] Eigen3 not found — fetching from GitLab")
    include(FetchContent)
    FetchContent_Declare(Eigen3
        GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
        GIT_TAG        3.4.0
        GIT_SHALLOW    TRUE)
    set(EIGEN_BUILD_DOC     OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING       OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(Eigen3)
endif()

# ── qpid-proton C++ ───────────────────────────────────────────────────────────
find_package(ProtonCpp QUIET)
if(NOT ProtonCpp_FOUND)
    pkg_check_modules(PROTON REQUIRED IMPORTED_TARGET libqpid-proton-cpp)
endif()

# ── nlohmann/json ─────────────────────────────────────────────────────────────
find_package(nlohmann_json 3.11 QUIET)
if(NOT nlohmann_json_FOUND)
    include(FetchContent)
    FetchContent_Declare(nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.11.3)
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# ── spdlog ────────────────────────────────────────────────────────────────────
find_package(spdlog QUIET)
if(NOT spdlog_FOUND)
    pkg_check_modules(SPDLOG QUIET IMPORTED_TARGET spdlog)
    if(SPDLOG_FOUND)
        add_library(spdlog::spdlog INTERFACE IMPORTED)
        target_include_directories(spdlog::spdlog INTERFACE ${SPDLOG_INCLUDE_DIRS})
        target_link_libraries(spdlog::spdlog INTERFACE ${SPDLOG_LIBRARIES})
        target_compile_definitions(spdlog::spdlog INTERFACE SPDLOG_FMT_EXTERNAL)
    else()
        include(FetchContent)
        FetchContent_Declare(spdlog
            GIT_REPOSITORY https://github.com/gabime/spdlog.git
            GIT_TAG        v1.13.0)
        FetchContent_MakeAvailable(spdlog)
    endif()
endif()

# ── tinyxml2 ──────────────────────────────────────────────────────────────────
pkg_check_modules(TINYXML2 REQUIRED IMPORTED_TARGET tinyxml2)

# ── libpqxx (optional) ────────────────────────────────────────────────────────
option(WITH_DB "Enable PostgreSQL df_results persistence via libpqxx" OFF)
if(WITH_DB)
    pkg_check_modules(PQXX QUIET IMPORTED_TARGET libpqxx)
    if(NOT PQXX_FOUND)
        find_library(LIBPQ_LIB pq)
        if(NOT LIBPQ_LIB)
            message(FATAL_ERROR "[DfApp] libpq not found.\n"
                "  Ubuntu: apt-get install libpq-dev\n"
                "  CentOS: dnf install libpq-devel")
        endif()
        message(STATUS "[DfApp] libpqxx not found — fetching 7.9.2 from GitHub")
        include(FetchContent)
        FetchContent_Declare(pqxx
            GIT_REPOSITORY https://github.com/jtv/libpqxx.git
            GIT_TAG        7.9.2
            GIT_SHALLOW    TRUE)
        set(PQXX_LIBRARIES_ONLY ON  CACHE BOOL "" FORCE)
        set(BUILD_DOC          OFF CACHE BOOL "" FORCE)
        set(BUILD_TESTING      OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(pqxx)
        add_library(PkgConfig::PQXX ALIAS pqxx)
        set(PQXX_FOUND TRUE)
    endif()
endif()

# ── Binary ────────────────────────────────────────────────────────────────────
add_executable(sdr_df
    src/Config.cpp
    src/DfEngine.cpp
    src/DfService.cpp
    src/main.cpp
)

target_include_directories(sdr_df PRIVATE include)

target_link_libraries(sdr_df PRIVATE
    sdr_task_api
    Eigen3::Eigen
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    PkgConfig::TINYXML2
    Threads::Threads
)

if(ProtonCpp_FOUND)
    target_link_libraries(sdr_df PRIVATE Proton::cpp)
else()
    target_link_libraries(sdr_df PRIVATE PkgConfig::PROTON)
endif()

if(WITH_DB)
    target_link_libraries(sdr_df PRIVATE PkgConfig::PQXX)
    target_compile_definitions(sdr_df PRIVATE DF_WITH_DB=1)
endif()

target_compile_options(sdr_df PRIVATE -O3 -march=native)

# ── Install ───────────────────────────────────────────────────────────────────
include(GNUInstallDirs)
install(TARGETS sdr_df RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES config/df.xml
        DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/sdr-df)

# ── Tests ─────────────────────────────────────────────────────────────────────
include(CTest)
if(BUILD_TESTING)
    include(FetchContent)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    enable_testing()

    add_executable(df_tests tests/unit/test_df_engine.cpp src/DfEngine.cpp src/Config.cpp)
    target_include_directories(df_tests PRIVATE include)
    target_link_libraries(df_tests PRIVATE
        sdr_task_api
        Eigen3::Eigen
        spdlog::spdlog
        PkgConfig::TINYXML2
        GTest::gtest_main
    )
    target_compile_options(df_tests PRIVATE -O2)
    include(GoogleTest)
    gtest_discover_tests(df_tests)
endif()

message(STATUS "┌─ DfApp ${PROJECT_VERSION} ──────────────────────────────────┐")
message(STATUS "│  C++ standard  : C++20                                       │")
message(STATUS "│  Eigen3        : MUSIC array processing                      │")
message(STATUS "│  PostgreSQL DB : ${WITH_DB}                                  │")
message(STATUS "└──────────────────────────────────────────────────────────────┘")
