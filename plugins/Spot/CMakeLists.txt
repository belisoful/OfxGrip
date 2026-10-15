ofxgrip_add_plugin(NAME OfxGripSpot IDENTIFIER com.belisoful.ofxgrip.spot SOURCES SpotPlugin.cpp)
ofxgrip_add_kernels(TARGET ofxgrip_plugin_OfxGripSpot KERNELS Spot.slang ENTRY spotKernel)
