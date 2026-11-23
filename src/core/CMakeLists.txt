add_library(gmax_core STATIC
  version.cpp
  atpg/fault_file.cpp
  atpg/fault_list.cpp
  db/crc32.cpp
  db/db.cpp
  db/dfdb.cpp
  db/string_table.cpp
  frontend/decompose.cpp
  frontend/diagnostics.cpp
  frontend/expr.cpp
  frontend/json.cpp
  frontend/liberty.cpp
  frontend/yosys.cpp
  frontend/yosys_recipe.cpp
  sim/fault.cpp
  sim/logic.cpp)

target_include_directories(gmax_core PUBLIC "${CMAKE_SOURCE_DIR}/src")

gmax_set_warnings(gmax_core)
gmax_set_sanitizers(gmax_core)
