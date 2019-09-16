project(coding_tests)

set(
  SRC
  base64_test.cpp
  bit_streams_test.cpp
  compressed_bit_vector_test.cpp
  csv_reader_test.cpp
  dd_vector_test.cpp
  elias_coder_test.cpp
  endianness_test.cpp
  file_container_test.cpp
  file_data_test.cpp
  file_sort_test.cpp
  geometry_coding_test.cpp
  hex_test.cpp
  huffman_test.cpp
  map_uint32_to_val_tests.cpp
  mem_file_reader_test.cpp
  mem_file_writer_test.cpp
  png_decoder_test.cpp
  point_coding_tests.cpp
  reader_cache_test.cpp
  reader_test.cpp
  reader_test.hpp
  reader_writer_ops_test.cpp
  simple_dense_coding_test.cpp
  string_utf8_multilang_tests.cpp
  succinct_mapper_test.cpp
  test_polylines.cpp
  test_polylines.hpp
  traffic_test.cpp
  url_encode_test.cpp
  value_opt_string_test.cpp
  var_record_reader_test.cpp
  var_serial_vector_test.cpp
  varint_test.cpp
  writer_test.cpp
)

geocore_add_test(${PROJECT_NAME} ${SRC})

geocore_link_libraries(
  ${PROJECT_NAME}
  coding
  platform_tests_support
  platform
)
