foreach(module core order book engine events)
    file(GLOB_RECURSE sources
        "${SOURCE_ROOT}/include/meridian/${module}/*.hpp"
        "${SOURCE_ROOT}/src/${module}/*.cpp"
        "${SOURCE_ROOT}/src/${module}/*.hpp")
    foreach(source IN LISTS sources)
        file(STRINGS "${source}" includes REGEX "#[ \t]*include[ \t]*[<\"]")
        foreach(line IN LISTS includes)
            if(line MATCHES "[<\"][^>\"]*(marketdata|net)/")
                message(FATAL_ERROR "Forbidden core dependency in ${source}: ${line}")
            endif()
        endforeach()
    endforeach()
endforeach()
