# Add executable targets
########################
ADD_EXECUTABLE(flow_button_gateway_appd flow_button_gateway.c flow_interface.c)

# Add library targets
#####################
FIND_LIBRARY(LIB_FLOWCORE libflowcore.so PATHS ${STAGING_DIR}/usr/lib)
FIND_LIBRARY(LIB_FLOWMESSAGING libflowmessaging.so PATHS ${STAGING_DIR}/usr/lib)
FIND_LIBRARY(LIB_AWA libawa.so PATHS ${STAGING_DIR}/usr/lib)
FIND_LIBRARY(LIB_CONFIG libconfig.so PATHS ${STAGING_DIR}/usr/lib)
TARGET_LINK_LIBRARIES(flow_button_gateway_appd ${LIB_AWA} ${LIB_FLOWCORE} ${LIB_FLOWMESSAGING} ${LIB_CONFIG})

# Add install targets
######################
INSTALL(TARGETS flow_button_gateway_appd RUNTIME DESTINATION bin)

