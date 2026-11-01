function(kot_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE kot_core Qt6::Test kot_warnings)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

kot_add_test(tst_rng)
kot_add_test(tst_strongid)
