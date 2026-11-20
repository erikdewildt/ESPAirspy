cmake_minimum_required(VERSION 3.16)

# Single source of truth for the firmware version (Semantic Versioning).
# Bump this for every feature change; CI publishes a matching release, see AGENTS.md.
set(PROJECT_VER "0.4.1")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ESPAirspy)
