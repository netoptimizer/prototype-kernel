#!/bin/bash
# Wrapper script for: "BPF selftests test runner - test_progs"
#
# Author: Jesper Dangaard Brouer <brouer@redhat.com>
# License: GPL
#
# Demo on advanced use of kernel's BPF selftests test runner: 'test_progs'
# (kernel/tools/testing/selftests/bpf/test_progs).
#
# - For invoking and grouping the individual tests in 'test_progs'
#
# Problem statement:
# ------------------
# The BPF selftest 'test_progs' contains many tests, that cover all the
# different areas of the kernel where BPF is used.  The CI system sees this
# as one test, which is impractical for identifying what team/engineer is
# responsible for debugging the problem.
#
# Simple solution:
# ----------------
# Create a for-loop that invoke each top-level test avail in test_progs.
# Then each test FAIL/PASS result in the CI system have a separate bullet.
# (For Red Hat use-case in Beaker https://beaker-project.org/)
#
# Next problem:
# -------------
# Next problem is that number of top-level tests are close to 100 tests
# (as-of this writing 97 tests) and increasing.  This leads to CI/beaker
# interface having many tests to scroll over.
#
# Advance solution:
# -----------------
# The test_progs program support selecting several tests and also excluding
# tests via options.  This allow us to group top-level tests together.
# There by reducing tests that are seen in the CI/beaker interface.
#

TESTPROG=test_progs

if [[ ! -x $TESTPROG ]]; then
    echo "The binary $TESTPROG must be in current directory (together with BPF object files)"
    exit 2
fi

if [ "$EUID" -ne 0 ]; then
    # Can be run as normal user, will just use "sudo"
    export su=sudo
fi

OUTDIR=results_bpf_selftests
if [[ ! -d $OUTDIR ]]; then
    mkdir $OUTDIR
fi

# Define test groups as bash array (pattern that test_progs can use)
declare -a group
#
# group=(xdp cgroup)
group[0]=xdp
group[1]=cgroup
group[2]=fexit,fentry
group[3]=sockmap
group[5]=flow_dissector
group[6]=core_
group[7]=sockopt
group[8]=global_data

# Bash arrays hints
#echo "All elements in array:" ${group[@]}
#echo "Elements in array" ${#group[@]}
#elems=${#group[@]}

#VERBOSE=yes
function info() {
    if [[ -n "$VERBOSE" ]]; then
	echo "# $@"
    fi
}

function check_result()
{
    local test_name=$1
    local test_result=$2

    if [ "$result" -eq 0 ]; then
	echo "[PASS] $test_name"
    else
	echo "[FAIL] $test_name"
    fi

    return $test_result;
}

function run_bpf_test_progs_test()
{
    local pattern=$1
    local exclude=""

    if [[ -n "$2" ]]; then
	exclude="--name-blacklist=$2"
    fi

    # Replace all commas in pattern for filename and better testname
    local name=${pattern//,/__}

    OUTFILE=${OUTDIR}/test_progs__${name}.output

    # Run test name (this can be a pattern that match more tests)
    $su ./${TESTPROG} -t $pattern $exclude 2>&1  > $OUTFILE
    result=$?

    check_result $name $result
}


info "Run all tests in group array"
for pattern in "${group[@]}"; do
    #./${TESTPROG} --list -t $pattern
    run_bpf_test_progs_test $pattern
done

function comma_seperated_string()
{
    local seperator=""
    local s=""
    for pattern in "$@"; do
	s=${s}${seperator}${pattern}
	seperator=","
    done
    echo $s
}

exclude_str=$(comma_seperated_string ${group[@]})
if [[ -n $exclude_str ]]; then
    exclude="--name-blacklist=$exclude_str"
fi

# Get list of programs that have NOT been run as part of group selection
testnames=$(./${TESTPROG} --list $exclude)
result=$?
if [ "$result" -ne 0 ]; then
    echo "WARN: Your version of $TESTPROG doesn't support --list feature "
    #
    # Grab all tests and use blacklisting to exclude
    testnames=$(basename -s .c prog_tests/*.c)
    use_old_approach=yes
fi

info "Run the remaining tests individually"
for testname in $testnames; do
    info "Start TEST: $testname"
    if [[ -z "$use_old_approach" ]]; then
	run_bpf_test_progs_test $testname
    else
	# Old approach of excluding test patterns
	# doesn't work well, as there will be a number empty tests
	run_bpf_test_progs_test $testname $exclude_str
    fi
done

#set -xv
#./${TESTPROG} --count -t $exclude_str
#./${TESTPROG} --count -b $exclude_str
#./${TESTPROG} --list -b $exclude
