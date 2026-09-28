# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# src/core/SourceCommit.cmake (the commit About -> Licenses names) in a
# made-up git repository: a clean checkout names HEAD, a new commit renames
# it on the next run, local changes name none, a given id wins, a given
# non-hex id fails, and an untracked file changes nothing.
#
#   cmake -DGIT_EXECUTABLE=<git> -DSCRIPT=<SourceCommit.cmake> -DWORK=<dir> -P test_source_commit.cmake

set(repo "${WORK}/repo")
set(out "${WORK}/SourceCommit.h")
set(nothing "")
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${repo}")

function(check what)
    if(${ARGN})
        message(STATUS "ok: ${what}")
    else()
        message(SEND_ERROR "FAILED: ${what}")
    endif()
endfunction()

function(git)
    execute_process(COMMAND "${GIT_EXECUTABLE}" -c user.name=t -c user.email=t@example.invalid
                            -c commit.gpgsign=false ${ARGN}
        WORKING_DIRECTORY "${repo}" RESULT_VARIABLE r OUTPUT_QUIET ERROR_VARIABLE e)
    if(NOT r EQUAL 0)
        message(FATAL_ERROR "git ${ARGN}: ${e}")
    endif()
endfunction()

function(head out_var)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD WORKING_DIRECTORY "${repo}"
        OUTPUT_VARIABLE h OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(${out_var} "${h}" PARENT_SCOPE)
endfunction()

# Runs the script; sets named (the id in the header, or "") and result.
function(run)
    execute_process(COMMAND "${CMAKE_COMMAND}" -DOUTPUT=${out} -DSOURCE_DIR=${repo}
                            -DGIT_EXECUTABLE=${GIT_EXECUTABLE} ${ARGN} -P "${SCRIPT}"
        RESULT_VARIABLE r OUTPUT_QUIET ERROR_QUIET)
    set(result "${r}" PARENT_SCOPE)
    set(named "<no file>")
    if(EXISTS "${out}")
        file(READ "${out}" text)
        if(text MATCHES "kSourceCommit = \"([0-9a-f]*)\"")
            set(named "${CMAKE_MATCH_1}")
        endif()
    endif()
    set(named "${named}" PARENT_SCOPE)
endfunction()

git(init -q)
file(WRITE "${repo}/a.txt" "one\n")
git(add a.txt)
git(commit -q -m one)
head(first)

run()
check("a clean checkout names HEAD (${named})" result EQUAL 0 AND named STREQUAL first)
file(READ "${out}" before)

file(WRITE "${repo}/untracked.txt" "not in git\n")
run()
file(READ "${out}" after)
check("an untracked file changes nothing (${named})" named STREQUAL first AND before STREQUAL after)

file(WRITE "${repo}/a.txt" "two\n")
git(commit -q -am two)
head(second)
run()
check("a new commit is named on the next build (${named})" named STREQUAL second AND NOT second STREQUAL first)

file(WRITE "${repo}/a.txt" "three, not committed\n")
run()
check("local changes name no commit (${named})" result EQUAL 0 AND named STREQUAL nothing)

run(-DCOMMIT=0123456789abcdef0123456789abcdef01234567)
check("a given id wins (${named})" named STREQUAL "0123456789abcdef0123456789abcdef01234567")

run(-DCOMMIT=not-a-commit)
check("a given id that is not hex fails (${result})" NOT result EQUAL 0)

file(REMOVE_RECURSE "${WORK}")
