#!/bin/bash
#
# Copyright (C) 2014, 2015 Red Hat <contact@redhat.com>
# Copyright (C) 2015 FUJITSU LIMITED
#
# Author: Loic Dachary <loic@dachary.org>
# Authro: Miyamae, Takeshi <miyamae.takeshi@jp.fujitsu.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU Library Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Library Public License for more details.
#
: ${ACTION:=--check}
: ${STRIPE_WIDTHS:=4096 4651 8192 10000 65000 65536}
: ${VERBOSE:=} # VERBOSE=--debug-osd=20
: ${MYDIR:=--base $(dirname $0)}

TMP=$(mktemp -d)
trap "rm -fr $TMP" EXIT

function non_regression() {
    local action=$1
    shift

    if test $action != NOOP ; then
        ceph_erasure_code_non_regression $action "$@" || return 1
    fi
}

function verify_directories() {
    local base=$(dirname "$(head -1 $TMP/used)")
    ls "$base" | grep 'plugin=' | sort > $TMP/exist_sorted
    sed -e 's|.*/||' $TMP/used | sort > $TMP/used_sorted
    if ! cmp $TMP/used_sorted $TMP/exist_sorted ; then
        echo "The following directories contain a payload that should have been verified"
        echo "but they have not been. It probably means that a change in the script"
        echo "made it skip these directories. If the modification is intended, the directories"
        echo "should be removed."
        comm -13 $TMP/used_sorted $TMP/exist_sorted
        return 1
    fi
}

function shec_action() {
    local action=$1
    shift

    non_regression $action "$@" || return 1
    if test "$action" = --check ; then
        simd_variation_action $action "$@" || return 1
    fi
}

function test_shec() {
    while read k m c ; do
        for stripe_width in $STRIPE_WIDTHS ; do
            shec_action $ACTION --stripe-width $stripe_width --plugin shec --parameter technique=multiple --parameter k=$k --parameter m=$m --parameter c=$c $VERBOSE $MYDIR || return 1
        done
    done <<EOF
1 1 1
2 1 1
3 2 1
3 2 2
3 3 2
4 1 1
4 2 2
4 3 2
5 2 1
6 3 2
6 4 2
6 4 3
7 2 1
8 3 2
8 4 2
8 4 3
9 4 2
9 5 3
12 7 4
EOF
}

function test_lrc() {
    while read k m l ; do
        for stripe_width in $STRIPE_WIDTHS ; do
            non_regression $ACTION --stripe-width $stripe_width --plugin lrc --parameter k=$k --parameter m=$m --parameter l=$l $VERBOSE $MYDIR || return 1
        done
    done <<EOF
2 2 2
4 2 3
8 4 3
EOF
}

function test_isa() {
    local action=$ACTION

    if ! ceph_erasure_code --plugin_exists isa ; then
        action=NOOP
    fi

    while read k m ; do
        for technique in reed_sol_van cauchy ; do
            for stripe_width in $STRIPE_WIDTHS ; do
                non_regression $action --stripe-width $stripe_width --plugin isa --parameter technique=$technique --parameter k=$k --parameter m=$m $VERBOSE $MYDIR || return 1
            done
        done
    done <<EOF
2 1
2 2
3 1
3 2
4 2
4 3
7 3
7 4
8 3
8 4
9 3
9 4
10 4
EOF
}

#
# Verify all SIMD code paths of the jerasure plugin 
# encode/decode in the same.
#
function simd_variation_action() {

    if which arch > /dev/null 2>1 ; then
        arch=$(arch)
    else
        arch=$(uname -p)
    fi

    # WARNING: If you modify this function please manually test that gf-complete is
    # running the appropriate SIMD paths. One way to do that is to enable
    # DEBUG_CPU_DETECTION when building libec___.so.
    # See src/erasure-code/jerasure/CMakeLists.txt for how to do that.

    case $arch in
        aarch64*|arm*) 
            export GF_COMPLETE_DISABLE_NEON=1
            ceph_erasure_code_non_regression "$@" || return 1

            unset GF_COMPLETE_DISABLE_NEON
            ceph_erasure_code_non_regression "$@" || return 1
            ;;
        i[[3456]]86*|x86_64*|amd64*)
            export GF_COMPLETE_DISABLE_SSE2=1
            export GF_COMPLETE_DISABLE_SSE3=1
            export GF_COMPLETE_DISABLE_SSSE3=1
            export GF_COMPLETE_DISABLE_SSE4=1
            export GF_COMPLETE_DISABLE_SSE4_PCLMUL=1
            ceph_erasure_code_non_regression "$@" || return 1

            unset GF_COMPLETE_DISABLE_SSE2
            export GF_COMPLETE_DISABLE_SSE3=1
            export GF_COMPLETE_DISABLE_SSSE3=1
            export GF_COMPLETE_DISABLE_SSE4=1
            export GF_COMPLETE_DISABLE_SSE4_PCLMUL=1
            ceph_erasure_code_non_regression "$@" || return 1

            unset GF_COMPLETE_DISABLE_SSE2
            unset GF_COMPLETE_DISABLE_SSE3
            export GF_COMPLETE_DISABLE_SSSE3=1
            export GF_COMPLETE_DISABLE_SSE4=1
            export GF_COMPLETE_DISABLE_SSE4_PCLMUL=1
            ceph_erasure_code_non_regression "$@" || return 1

            unset GF_COMPLETE_DISABLE_SSE2
            unset GF_COMPLETE_DISABLE_SSE3
            unset GF_COMPLETE_DISABLE_SSSE3
            export GF_COMPLETE_DISABLE_SSE4=1
            export GF_COMPLETE_DISABLE_SSE4_PCLMUL=1
            ceph_erasure_code_non_regression "$@" || return 1

            unset GF_COMPLETE_DISABLE_SSE2
            unset GF_COMPLETE_DISABLE_SSE3
            unset GF_COMPLETE_DISABLE_SSSE3
            unset GF_COMPLETE_DISABLE_SSE4
            export GF_COMPLETE_DISABLE_SSE4_PCLMUL=1
            ceph_erasure_code_non_regression "$@" || return 1

            unset GF_COMPLETE_DISABLE_SSE2
            unset GF_COMPLETE_DISABLE_SSE3
            unset GF_COMPLETE_DISABLE_SSSE3
            unset GF_COMPLETE_DISABLE_SSE4
            unset GF_COMPLETE_DISABLE_SSE4_PCLMUL
            ceph_erasure_code_non_regression "$@" || return 1
            ;;
        *)
            echo unsupported arch $arch
            return 1
            ;;
    esac
} 

function jerasure_action() {
    local action=$1
    shift

    non_regression $action "$@" || return 1
    if test "$action" = --check ; then
        simd_variation_action $action "$@" || return 1
    fi
}

function test_jerasure() {
    while read k m ; do
        for stripe_width in $STRIPE_WIDTHS ; do
            for technique in cauchy_good cauchy_orig ; do
                for alignment in '' '--parameter jerasure-per-chunk-alignment=true' ; do
                    jerasure_action $ACTION --stripe-width $stripe_width --parameter packetsize=32 --plugin jerasure --parameter technique=$technique --parameter k=$k --parameter m=$m $alignment $VERBOSE $MYDIR || return 1
                done
            done
        done
    done <<EOF
2 1
2 2
3 1
3 2
4 2
4 3
7 3
7 4
7 5
8 3
8 4
9 3
9 4
9 5
9 6
EOF

    while read k m ; do
        for stripe_width in $STRIPE_WIDTHS ; do
            for alignment in '' '--parameter jerasure-per-chunk-alignment=true' ; do
                jerasure_action $ACTION --stripe-width $stripe_width --plugin jerasure --parameter technique=reed_sol_van --parameter k=$k --parameter m=$m $alignment $VERBOSE $MYDIR || return 1
            done
        done
    done <<EOF
2 1
2 2
3 1
3 2
4 2
4 3
7 3
7 4
7 5
8 3
8 4
9 3
9 4
9 5
9 6
EOF

    for k in $(seq 2 6) ; do
        for stripe_width in $STRIPE_WIDTHS ; do
            for technique in reed_sol_r6_op liberation blaum_roth liber8tion ; do
                for alignment in '' '--parameter jerasure-per-chunk-alignment=true' ; do
                    jerasure_action $ACTION --stripe-width $stripe_width --parameter packetsize=32 --plugin jerasure --parameter technique=$technique --parameter k=$k --parameter m=2 $alignment $VERBOSE $MYDIR || return 1
                done
            done
        done
    done
}

function run() {
    local all_funcs=$(set | sed -n -e 's/^\(test_[0-9a-z_]*\) .*/\1/p')
    local funcs=${@:-$all_funcs}
    PS4="$0":'$LINENO: ${FUNCNAME[0]} '
    set -x
    for func in $funcs ; do
        $func || return 1
    done
    if test "$all_funcs" = "$funcs" ; then
        verify_directories || return 1
    fi
}

run "$@" || exit 1
