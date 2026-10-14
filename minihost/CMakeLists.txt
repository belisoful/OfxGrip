add_library(ofxgrip_minihost STATIC
  MiniHost.cpp
  PropertySetImpl.cpp
  ParamImpl.cpp
  BundleLoader.cpp)
add_library(OfxGrip::minihost ALIAS ofxgrip_minihost)
target_include_directories(ofxgrip_minihost PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(ofxgrip_minihost PUBLIC OfxGrip::core OfxGrip::openfx_headers ${CMAKE_DL_LIBS})

add_executable(ofxgrip-minihost main.cpp)
target_link_libraries(ofxgrip-minihost PRIVATE OfxGrip::minihost)
