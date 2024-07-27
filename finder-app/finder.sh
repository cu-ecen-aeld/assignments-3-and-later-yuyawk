#!/bin/bash
#
# Searches for a string within the files inside a directory.
#
# Args:
#   $1: Path to a directory on the filesystem.
#   $2: Text string which will be searched within these files.

set -euo pipefail

main() {
  if [ "$#" -ne 2 ]; then
    echo "ERROR: Incorrect number of arguments" 1>&2
    exit 1
  fi

  local filesdir
  local searchstr
  filesdir="${1}"
  searchstr="${2}"

  if [ ! -d "${filesdir}" ]; then
    echo "ERROR: ${filesdir} is not a directory" 1>&2
    exit 1
  fi

  local output_x
  local output_y
  output_x="$(find "${filesdir}" -type f | wc -l)"
  output_y="$(find "${filesdir}" -type f | xargs grep -F "${searchstr}" | wc -l)"

  echo "The number of files are ${output_x} and the number of matching lines are ${output_y}"
}

main "$@"
