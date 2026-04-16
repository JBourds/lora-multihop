cmake_minimum_required(VERSION 3.20)
project(loramesh_nexus CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# FreeRTOS kernel via FetchContent (POSIX port)
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    freertos_kernel
    GIT_REPOSITORY https://github.com/FreeRTOS/FreeRTOS-Kernel.git
    GIT_TAG        V11.1.0
)

# Tell the FreeRTOS CMake build where our FreeRTOSConfig.h lives
set(FREERTOS_CONFIG_FILE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include CACHE STRING "" FORCE)
set(FREERTOS_PORT "GCC_POSIX" CACHE STRING "" FORCE)
set(FREERTOS_HEAP "3" CACHE STRING "" FORCE)
FetchContent_MakeAvailable(freertos_kernel)

# ---------------------------------------------------------------------------
# LoRaMesher sources (used as a library, not as an ESP-IDF component)
# ---------------------------------------------------------------------------
set(LORAMESH_ROOT "./LoRaMesher-nexus")
set(LORAMESH_SRCS
    ${LORAMESH_ROOT}/src/LoraMesher.cpp
    ${LORAMESH_ROOT}/src/services/PacketService.cpp
    ${LORAMESH_ROOT}/src/services/PacketQueueService.cpp
    ${LORAMESH_ROOT}/src/services/RoutingTableService.cpp
    ${LORAMESH_ROOT}/src/services/RoleService.cpp
    ${LORAMESH_ROOT}/src/services/SimulatorService.cpp
    ${LORAMESH_ROOT}/src/services/WifiService.cpp
    ${LORAMESH_ROOT}/src/services/PacketFactory.cpp
)

# ---------------------------------------------------------------------------
# RadioLib stub -- we only need enough headers to satisfy LoRaMesher's
# #include <RadioLib.h>.  The actual radio is replaced by LM_NexusModule.
# ---------------------------------------------------------------------------
set(RADIOLIB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/radiolib_stub")

# ---------------------------------------------------------------------------
# Our nexus-specific sources
# ---------------------------------------------------------------------------
set(NEXUS_SRCS
    src/main.cpp
    src/LM_NexusModule.cpp
    src/esp32_shims.cpp
)

add_executable(main ${NEXUS_SRCS} ${LORAMESH_SRCS})

target_include_directories(main PRIVATE
    include
    ${LORAMESH_ROOT}/src
    ${RADIOLIB_ROOT}
    ${FREERTOS_CONFIG_FILE_DIRECTORY}
)

target_compile_definitions(main PRIVATE
    NEXUS_SIM
    LM_NEXUS_MODULE
    # Suppress Arduino/ESP-IDF paths in BuildOptions.h
    # We define ARDUINO so it takes the simpler #ifdef path, then override
    # the Arduino-specific types with our shims.
    ARDUINO
)

target_link_libraries(main PRIVATE
    freertos_kernel
    pthread
)
