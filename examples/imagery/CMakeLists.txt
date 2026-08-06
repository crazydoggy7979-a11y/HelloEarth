add_executable(
    SingleLocalTIF
    SingleLocalTIF.cpp
)

target_compile_features(
    SingleLocalTIF
    PRIVATE
    cxx_std_17
)

if(MSVC)
    target_compile_options(
        SingleLocalTIF
        PRIVATE
        /utf-8
    )
endif()

target_link_libraries(
    SingleLocalTIF
    PRIVATE
    osgEarth::osgEarth
    GDAL::GDAL
)

install(
    TARGETS SingleLocalTIF
    RUNTIME DESTINATION bin
)