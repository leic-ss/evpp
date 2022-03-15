include_directories(BEFORE ${PROJECT_SOURCE_DIR})

file(GLOB rpc_SRCS *.cc)

add_library(rpc_lib STATIC ${rpc_SRCS})
set_target_properties(rpc_lib PROPERTIES COMPILE_FLAGS "-fPIC")

add_library(RPC_OBJ OBJECT ${rpc_SRCS})
