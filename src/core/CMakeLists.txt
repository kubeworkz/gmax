add_library(gmax_core STATIC
  version.cpp
  atpg/fault_file.cpp
  atpg/fault_list.cpp
  atpg/implication.cpp
  atpg/logic5.cpp
  atpg/podem.cpp
  atpg/sat.cpp
  db/constraints.cpp
  db/crc32.cpp
  db/db.cpp
  db/dfdb.cpp
  db/string_table.cpp
  dft/capture.cpp
  frontend/decompose.cpp
  frontend/diagnostics.cpp
  frontend/expr.cpp
  frontend/json.cpp
  frontend/liberty.cpp
  frontend/sdc.cpp
  frontend/sdc_constraints.cpp
  frontend/yosys.cpp
  frontend/yosys_recipe.cpp
  report/report.cpp
  sim/fault.cpp
  sim/logic.cpp)

target_include_directories(gmax_core PUBLIC "${CMAKE_SOURCE_DIR}/src")

gmax_set_warnings(gmax_core)
gmax_set_sanitizers(gmax_core)
