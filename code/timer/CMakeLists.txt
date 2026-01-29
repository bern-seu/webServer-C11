get_filename_component(CODE_DIR "${CMAKE_CURRENT_SOURCE_DIR}" ABSOLUTE)
aux_source_directory(${CODE_DIR} CODE_LIST)
set(code_timer ${CODE_LIST} CACHE INTERNAL "Description of the variable")