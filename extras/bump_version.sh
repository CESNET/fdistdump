#!/usr/bin/env bash

# Copyright 2018 CESNET
#
# This file is part of Fdistdump.
#
# Fdistdump is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Fdistdump is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Fdistdump.  If not, see <http://www.gnu.org/licenses/>.

set -eu

################################################################################
die() {
    echo Error: "$@" >&2
    exit 1
}
warn() {
    echo Warning: "$@" >&2
}
usage() {
    echo "Usage: $0 major|minor|patch"
}

################################################################################
main() {
    if [[ $# -ne 1 ]]; then
        usage
        return 1
    fi

    # check the if CWD is project root
    local -r GIT_ROOT="$(git rev-parse --show-toplevel)"
    if [[ $GIT_ROOT != "$PWD" ]]; then
        die "run this script in the project root ($GIT_ROOT)"
    fi

    # check if the current branch is develop
    local -r GIT_BRANCH="$(git branch | grep '^*' | cut -d ' ' -f 2)"
    if [[ $GIT_BRANCH != develop ]]; then
        die "run this script only on the develop branch"
    fi

    # find the most recent tag that is reachable from HEAD of the current branch
    local -r LATEST_TAG="$(git describe --abbrev=0)"
    [[ $LATEST_TAG =~ ^v[[:digit:]]+\.[[:digit:]]+\.[[:digit:]]+$ ]] \
        || die "invalid tag produced by git describe: \'$LATEST_TAG\'"
    # strip the tag's initial letter v and parse the string into an array of its
    # components
    local -r LATEST_VERSION_STR=${LATEST_TAG#v}
    IFS="." read -ra LATEST_VERSION_ARR <<<"$LATEST_VERSION_STR"
    [[ ${#LATEST_VERSION_ARR[@]} -ne 3 ]] \
        && die "invalid count of version string components"

    # increment specified component, preserve higher level components, clear
    # lower level components
    case "$1" in
    major)
        NEW_VERSION_ARR[0]=$((LATEST_VERSION_ARR[0]+1))
        NEW_VERSION_ARR[1]=0
        NEW_VERSION_ARR[2]=0
        ;;
    minor)
        NEW_VERSION_ARR[0]=${LATEST_VERSION_ARR[0]}
        NEW_VERSION_ARR[1]=$((LATEST_VERSION_ARR[1]+1))
        NEW_VERSION_ARR[2]=0
        ;;
    patch)
        NEW_VERSION_ARR[0]=${LATEST_VERSION_ARR[0]}
        NEW_VERSION_ARR[1]=${LATEST_VERSION_ARR[1]}
        NEW_VERSION_ARR[2]=$((LATEST_VERSION_ARR[2]+1))
        ;;
    *)
        echo "Usage: $0 major|minor|patch"
        exit 1
        ;;
    esac

    local -r NEW_VERSION_STR="${NEW_VERSION_ARR[0]}.${NEW_VERSION_ARR[1]}.${NEW_VERSION_ARR[2]}"
    echo lastest version string: "$LATEST_VERSION_STR"
    echo new version string:     "$NEW_VERSION_STR"

    # root CMakeLists.txt #############################################################
    local -r FILE_PATH="CMakeLists.txt"
    if ! grep -q "VERSION\s\+$LATEST_VERSION_STR" "$FILE_PATH"; then
        die "latest version string not found in $FILE_PATH"
    fi
    sed -i "/VERSION\s\+$LATEST_VERSION_STR/ \
        s/${LATEST_VERSION_STR}/${NEW_VERSION_STR}/" "$FILE_PATH"
    if grep "VERSION\s\+$LATEST_VERSION_STR" "$FILE_PATH"; then
        warn "latest version string found in $FILE_PATH after substitution"
    fi

    ############################################################################
    echo
    echo To finalize the process, inspect changes made by thit script and run \
         the following commands:
    echo git add configure.ac doc/man/fdistdump.1 fdistdump.spec
    echo git commit -m "\"VERSION BUMP: $1 ($LATEST_VERSION_STR -> $NEW_VERSION_STR)\""
    echo git tag -a "v$NEW_VERSION_STR"
    echo git checkout master
    echo git merge develop
    echo git push --all   \# to push all branches
    echo git push --tags  \# to push all tags
}

################################################################################
main "$@"
