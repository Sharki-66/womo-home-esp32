set(BSEC_LIB ${CMAKE_CURRENT_SOURCE_DIR}/lib/libalgobsec.a)

idf_component_register(INCLUDE_DIRS "include")

target_link_libraries(${COMPONENT_LIB} INTERFACE ${BSEC_LIB})
