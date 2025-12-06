#!/bin/bash

# Script to run clang-tidy or clang-format on Embedded C code
# Usage: ./run-checks.sh [tidy|format]

if [ $# -eq 0 ]; then
  echo "Usage: $0 [tidy|format]"
  echo "  tidy   - Run clang-tidy (analyzes code, gives warnings)"
  echo "  format - Run clang-format (formats code in-place)"
  exit 1
fi

MODE=$1

# Adjust this to your repo root if needed
cd /hil-rig-mcu-firmware || exit 1

echo "Collecting source files in src/..."

# Only pick .c, .cpp and .h files inside src/ and its subdirectories
FILES=$(find src -type f \( -name "*.c" -o -name "*.cpp" -o -name "*.h" \))

if [ -z "$FILES" ]; then
  echo "No C source/header files found in src/"
  exit 0
fi

case "$MODE" in

  tidy)
    echo "Running clang-tidy on:"
    for file in $FILES; do
      echo "  $file"
      clang-tidy -p build "$file"
    done
    ;;

  format)
    echo "Running clang-format on:"
    for file in $FILES; do
      echo "  $file"
      clang-format -i "$file"
    done
    ;;

  *)
    echo "Error: Unknown mode '$MODE'"
    echo "Usage: $0 [tidy|format]"
    exit 1
    ;;

esac

echo "Done!"


## Run with commands:
# chmod +x run_checks.sh
# ./run_checks.sh tidy    # for clang-tidy
# ./run_checks.sh format  # for clang-format