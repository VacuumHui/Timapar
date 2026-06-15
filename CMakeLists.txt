cmake_minimum_required(VERSION 3.14)
project(ParticleSimulation)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Автоматическое скачивание GLFW из GitHub во время сборки
include(FetchContent)
FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.3.8
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# Создание исполняемого файла
add_executable(simulation main.cpp)

# Подключение библиотек
target_link_libraries(simulation glfw)
if(WIN32)
    target_link_libraries(simulation opengl32)
    # Настройка скрытия консольного окна в Windows (запуск как чистого оконного приложения)
    set_target_properties(simulation PROPERTIES LINK_FLAGS "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
endif()
