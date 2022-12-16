project(test_uring LANGUAGES CXX)

#find_package(LIBURING REQUIRED)

add_executable(${PROJECT_NAME})

set(TARGET_SOURCES
    main.cpp
)

target_sources(${PROJECT_NAME} PRIVATE ${TARGET_SOURCES})

set(TARGET_LIBRARIES
    uring
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE ${TARGET_LIBRARIES}
)
