cmake_minimum_required(VERSION 3.16)

# Single source of truth for the firmware version (Semantic Versioning).
# Bump this for every feature change and tag the release to match, see AGENTS.md.
set(PROJECT_VER "0.4.0")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ESPAirspy)
