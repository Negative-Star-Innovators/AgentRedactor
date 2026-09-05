# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Release")
  file(REMOVE_RECURSE
  "gui/CMakeFiles/agentredactor-gui_autogen.dir/AutogenUsed.txt"
  "gui/CMakeFiles/agentredactor-gui_autogen.dir/ParseCache.txt"
  "gui/agentredactor-gui_autogen"
  )
endif()
