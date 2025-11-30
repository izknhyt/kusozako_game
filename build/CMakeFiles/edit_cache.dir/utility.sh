set -e

cd /Users/izumimotohayato/development/kusozako/build
/opt/homebrew/bin/ccmake -S$(CMAKE_SOURCE_DIR) -B$(CMAKE_BINARY_DIR)
