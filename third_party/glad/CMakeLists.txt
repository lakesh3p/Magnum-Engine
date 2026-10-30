# glad2 loader for OpenGL 4.5 core, generated once with:
#   python -m glad --api gl:core=4.5 --out-path third_party/glad c
add_library(glad STATIC src/gl.c)
target_include_directories(glad PUBLIC include)
