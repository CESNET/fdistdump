#!/usr/bin/env bash

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
    #local -r LATEST_TAG="$(git describe --abbrev=0)"
    local -r LATEST_TAG="v0.3.0"
    [[ $LATEST_TAG =~ ^v[[:digit:]]+\.[[:digit:]]+\.[[:digit:]]+$ ]] \
        || die "invalid tag produced by git describe: \'$LATEST_TAG\'"
    # strip the tag's initial letter v and parse the string into an array of its
    # components
    local -r LATEST_VERSION_STR=${LATEST_TAG#v}
    IFS="." read -ra LATEST_VERSION_ARR <<<"$LATEST_VERSION_STR"
    [[ ${#LATEST_VERSION_ARR[@]} -ne 3 ]] \
        && die "invalid number of version string components"

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

    local FILE_PATH
    # configure.ac #############################################################
    FILE_PATH="configure.ac"
    if ! grep "AC_INIT" "$FILE_PATH" | grep -q "$LATEST_VERSION_STR"; then
        die "latest version string not found in $FILE_PATH"
    fi
    sed -i "/AC_INIT/ s/${LATEST_VERSION_STR}/${NEW_VERSION_STR}/" "$FILE_PATH"
    if grep "AC_INIT" "$FILE_PATH" | grep "$LATEST_VERSION_STR"; then
        warn "latest version string found in $FILE_PATH after substitution"
    fi

    # man page fdistdump.1 #####################################################
    FILE_PATH="doc/man/fdistdump.1"
    if ! grep "\.TH FDISTDUMP 1" "$FILE_PATH" \
        | grep -q "$LATEST_VERSION_STR"; then
        die "latest version string not found in $FILE_PATH"
    fi
    sed -i "/\.TH FDISTDUMP 1/ \
            { \
              s/[[:digit:]]\{4\}-[[:digit:]]\{1,2\}-[[:digit:]]\{1,2\}/$(date -I)/ ;
              s/${LATEST_VERSION_STR}/${NEW_VERSION_STR}/ \
            }" "$FILE_PATH"

    # fdistdump.spec ###########################################################
    FILE_PATH="fdistdump.spec"
    if ! grep -q "Version:[[:space:]]*$LATEST_VERSION_STR" "$FILE_PATH"; then
        die "latest version string not found in $FILE_PATH"
    fi
    SPEC_DATE="$(LC_ALL=c date "+%a %b %d %Y")"
    GIT_NAME="$(git config --get user.name)"
    GIT_EMAIL="$(git config --get user.email)"
    HEADER="* $SPEC_DATE $GIT_NAME <$GIT_EMAIL> - $NEW_VERSION_STR-1"
    BODY="- $1 version bump ($LATEST_VERSION_STR -> $NEW_VERSION_STR)"
    sed -i -e "/Version:/ s/${LATEST_VERSION_STR}/${NEW_VERSION_STR}/" \
           -e "/Release:/ s/\(Release:[[:space:]]\+\)[[:digit:]]\+/\11/" \
           -e "/%changelog/a $HEADER\n$BODY\n" \
        "$FILE_PATH"

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
