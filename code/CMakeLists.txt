add_subdirectory(buffer)
add_subdirectory(http)
add_subdirectory(log)
add_subdirectory(pool)
add_subdirectory(server)
add_subdirectory(timer)

add_executable(server main.cpp ${code_buffer} ${code_http} ${code_log} ${code_pool} ${code_server} ${code_timer})
target_link_libraries(server pthread mysqlclient)