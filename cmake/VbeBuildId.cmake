function(vbe_resolve_build_id out_var repo_dir)
    set(_vbe_id "unknown")

    if(DEFINED ENV{GITHUB_SHA} AND NOT "$ENV{GITHUB_SHA}" STREQUAL "")
        string(SUBSTRING "$ENV{GITHUB_SHA}" 0 8 _vbe_id)
    else()
        find_package(Git QUIET)
        if(GIT_FOUND)
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" rev-parse --short=8 HEAD
                WORKING_DIRECTORY "${repo_dir}"
                RESULT_VARIABLE _vbe_git_result
                OUTPUT_VARIABLE _vbe_git_output
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            if(_vbe_git_result EQUAL 0 AND NOT "${_vbe_git_output}" STREQUAL "")
                set(_vbe_id "${_vbe_git_output}")
            endif()
        endif()
    endif()

    set(${out_var} "${_vbe_id}" PARENT_SCOPE)
endfunction()
