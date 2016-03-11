# Project Properties
####################
CMAKE_MINIMUM_REQUIRED (VERSION 2.6.2)
PROJECT(flow_button_gateway)
# This will be extended to take the version from SCM
# Version config can be simplified from 3.0+
SET(flow_button_gateway_VERSION_MAJOR 0)
SET(flow_button_gateway_VERSION_MINOR 3)
SET(flow_button_gateway_VERSION_PATCH 0)
SET(flow_button_gateway_VERSION
    ${flow_button_gateway_VERSION_MAJOR}.${flow_button_gateway_VERSION_MINOR}.${flow_button_gateway_VERSION_PATCH})

# Options
###################
SET(CMAKE_VERBOSE_MAKEFILE 1)
SET(CMAKE_BUILD_TYPE DEBUG) # Options MINSIZEREL, RELEASE, DEBUG
SET(DOCS_INTERNAL 1 CACHE BOOL "enable internal docs generation")

# Paths
########
ADD_SUBDIRECTORY(src)
