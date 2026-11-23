add_library(gmax_core STATIC
  version.cpp
  db/crc32.cpp
  db/db.cpp
  db/dfdb.cpp
  db/string_table.cpp
  frontend/decompose.cpp
  frontend/expr.cpp
  frontend/liberty.cpp
  sim/logic.cpp)

target_include_directories(gmax_core PUBLIC "${CMAKE_SOURCE_DIR}/src")

gmax_set_warnings(gmax_core)
gmax_set_sanitizers(gmax_core)
