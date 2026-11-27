cmake_minimum_required(VERSION 3.16)
project(bxi_oldtest LANGUAGES C)

set(BXI_PCI_DRV_DIR "" CACHE PATH "Optional path to an external BXI PCI driver project")
if(BXI_PCI_DRV_DIR)
    set(BXI_PCI_LIB_DIR "${BXI_PCI_DRV_DIR}/lib")
else()
    set(BXI_PCI_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib")
endif()
if(NOT EXISTS "${BXI_PCI_LIB_DIR}/bxi_pci_drv.h" OR
   NOT EXISTS "${BXI_PCI_LIB_DIR}/libbxi_pci_drv.a")
    message(FATAL_ERROR "Missing BXI PCI driver header or library in ${BXI_PCI_LIB_DIR}")
endif()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_executable(bxi_oldtest motor_aging.c terminal.c)
set_target_properties(bxi_oldtest PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_compile_definitions(bxi_oldtest PRIVATE BXI_OLDTEST_CONFIG_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(bxi_oldtest PRIVATE -Wall -Wextra -Wpedantic)
target_include_directories(bxi_oldtest PRIVATE "${BXI_PCI_LIB_DIR}")
target_link_libraries(bxi_oldtest PRIVATE "${BXI_PCI_LIB_DIR}/libbxi_pci_drv.a" Threads::Threads)
