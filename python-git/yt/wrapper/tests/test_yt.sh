#!/bin/bash -eux

set +u
if [ -z "$1" ]; then
    echo "Tests sandbox path should be specified" && exit 1
fi
set -u

SANDBOX_DIR=$1
PIDS=""
YT="$PYTHON_BINARY ./yt"

add_pid_to_kill() {
    PIDS="$PIDS $1"
}

set_up() {
    $YT create map_node //home/wrapper_test --ignore-existing
}

tear_down() {
    $YT remove //home/wrapper_test --force --recursive

    for pid in $PIDS; do
        if ps ax | awk '{print $1}' | grep $pid; then
            set +e
            kill $pid
            set -e
        fi
    done

    rm -f $SANDBOX_DIR/script.sh
}

die() {
    tear_down
    echo "$@" && exit 1
}

check() {
    local first="$(echo -e "$1")"
    local second="$(echo -e "$2")"
    [ "${first}" = "${second}" ] || die "Test fail $1 does not equal $2"
}

check_failed() {
    set +e
    eval $1
    if [ "$?" = "0" ]; then
        die "Command \"$@\" should fail"
    fi
    set -e
}

run_test() {
    set_up
    eval $1
    tear_down
}

# Directory creation, list, get and set commands
test_cypress_commands()
{
    fix_yson_repr() {
        # COMPAT: yson representation slightly differ in prestable/0.17.3 and master.
        local yson_str="$1"
        echo $(python2 -c "import sys; sys.stdout.write('$yson_str'.replace(';}', '}').replace(';>', '>'))")
    }

    check "" "$($YT list //home/wrapper_test)"
    check "" "$($YT find //home/wrapper_test --name "xxx")"

    $YT set //home/wrapper_test/folder "{}"
    check "" "$($YT list //home/wrapper_test/folder)"
    check "folder" "$($YT list //home/wrapper_test)"
    check "folder" "$($YT list //home/wrapper_test --read-from cache)"
    check "{\"folder\"={}}" "$(fix_yson_repr $($YT get //home/wrapper_test --format "<format=text>yson"))"
    check "" "$($YT find //home/wrapper_test --name "xxx")"
    check "//home/wrapper_test/folder" "$($YT find //home/wrapper_test --name "folder")"
    check "//home/wrapper_test/folder" "$($YT find //home/wrapper_test --name "folder" --read-from cache)"
    check "//home/wrapper_test/folder" "$(YT_PREFIX="//home/" $YT find wrapper_test --name "folder")"

    $YT set //home/wrapper_test/folder/@attr '<a=b>c'
    check  '<"a"="b">"c"' "$(fix_yson_repr $($YT get //home/wrapper_test/folder/@attr --format '<format=text>yson'))"

    $YT set //home/wrapper_test/folder/@attr '{"attr": 10}' --format json
    check '{"attr":10}' $($YT get //home/wrapper_test/folder/@attr --format json)

    $YT create file //home/wrapper_test/file_with_attrs --attributes "{testattr=1;other=2}" --ignore-existing
    check "//home/wrapper_test/file_with_attrs" "$($YT find //home/wrapper_test --attribute-filter "testattr=1")"
    check "" "$($YT find //home/wrapper_test --attribute-filter "attr=1")"

}

test_list_long_format()
{
    $YT list -l "//home"

    # list with symlinks
    $YT create table "//home/wrapper_test/folder_with_symlinks/test_table" --recursive
    $YT link "//home/wrapper_test/folder_with_symlinks/test_table" "//home/wrapper_test/folder_with_symlinks/valid_link"
    $YT create table "//home/wrapper_test/table_to_delete"
    $YT link "//home/wrapper_test/table_to_delete" "//home/wrapper_test/folder_with_symlinks/invalid_link"
    $YT remove "//home/wrapper_test/table_to_delete"
    $YT list -l "//home/wrapper_test/folder_with_symlinks"
}

test_concatenate()
{
    echo "Hello" | $YT write-file //home/wrapper_test/file_a
    echo "World" | $YT write-file //home/wrapper_test/file_b

    $YT concatenate --src //home/wrapper_test/file_a --src //home/wrapper_test/file_b --dst //home/wrapper_test/output_file

    check "$(echo -e "Hello\nWorld")" "$($YT read-file //home/wrapper_test/output_file)"
}

# read and write table
test_table_commands()
{
    $YT create table //home/wrapper_test/test_table
    check "" "$($YT read //home/wrapper_test/test_table --format dsv)"

    echo -e "value=y\nvalue=x\n" | $YT write //home/wrapper_test/test_table --format dsv
    check "$(echo -e "value=y\nvalue=x\n")" "$($YT read //home/wrapper_test/test_table --format dsv)"
}

# download and upload file, use it in map operation
test_file_commands()
{
    echo "grep x" >$SANDBOX_DIR/script
    chmod +x $SANDBOX_DIR/script

    cat $SANDBOX_DIR/script | $YT upload //home/wrapper_test/script --executable

    check "grep x" "$($YT download //home/wrapper_test/script)"

    echo -e "value=y\nvalue=x\n" | $YT write //home/wrapper_test/input_table --format dsv

    $YT map "./script" --src //home/wrapper_test/input_table --dst //home/wrapper_test/output_table \
        --file //home/wrapper_test/script --format dsv
    check "value=x\n" "$($YT read //home/wrapper_test/output_table --format dsv)"

    $YT map "./script" --src //home/wrapper_test/input_table --dst //home/wrapper_test/output_table \
        --local-file $SANDBOX_DIR/script --format dsv
    check "value=x\n" "$($YT read //home/wrapper_test/output_table --format dsv)"

    rm -f $SANDBOX_DIR/script
}

test_copy_move_link()
{
    $YT create table //home/wrapper_test/table
    check "table" "$($YT list //home/wrapper_test)"

    $YT copy //home/wrapper_test/table //home/wrapper_test/other_table
    check $'other_table\ntable' "$($YT list //home/wrapper_test | sort)"

    $YT remove //home/wrapper_test/table
    check "other_table" "$($YT list //home/wrapper_test)"

    $YT move //home/wrapper_test/other_table //home/wrapper_test/table
    check "table" "$($YT list //home/wrapper_test)"

    $YT link //home/wrapper_test/table //home/wrapper_test/other_table
    check $'other_table\ntable' "$($YT list //home/wrapper_test | sort)"

    $YT remove //home/wrapper_test/table
    check_failed "$YT read //home/wrapper_test/other_table --format dsv"
}

test_merge_erase()
{
    for i in {1..3}; do
        echo -e "value=${i}" | $YT write "//home/wrapper_test/table${i}" --format dsv
    done
    $YT merge --src //home/wrapper_test/table{1..3} --dst "//home/wrapper_test/merge"
    check "3" "$($YT get //home/wrapper_test/merge/@row_count)"

    $YT erase '//home/wrapper_test/merge[#1:#2]'
    check "2" "$($YT get //home/wrapper_test/merge/@row_count)"

    $YT merge --src "//home/wrapper_test/merge" --src "//home/wrapper_test/merge" --dst "//home/wrapper_test/merge"
    check "4" "$($YT get //home/wrapper_test/merge/@row_count)"
}

test_map_reduce()
{
    export YT_TABULAR_DATA_FORMAT="dsv"
    echo -e "value=1\nvalue=2" | $YT write //home/wrapper_test/input_table
    check "2" "$($YT get //home/wrapper_test/input_table/@row_count)"

    $YT map-reduce --mapper cat --reducer "grep 2" --src //home/wrapper_test/input_table --dst //home/wrapper_test/input_table --reduce-by value
    check "1" "$($YT get //home/wrapper_test/input_table/@row_count)"
    unset YT_TABULAR_DATA_FORMAT
}

test_users()
{
    fix_yson_repr() {
        # COMPAT: yson representation slightly differ in prestable/0.17.3 and master.
        local yson_str="$1"
        echo $(python2 -c "import sys; sys.stdout.write('$yson_str'.replace(';]', ']'))")
    }

    $YT create user --attribute '{name=test_user}'
    $YT create group --attribute '{name=test_group}'

    check "[]" "$($YT get //sys/groups/test_group/@members --format '<format=text>yson')"

    $YT add-member test_user test_group
    check  '["test_user"]' "$(fix_yson_repr $($YT get //sys/groups/test_group/@members --format '<format=text>yson'))"

    $YT set "//home/wrapper_test/@acl/end" "{action=allow;subjects=[test_group];permissions=[write]}"
    $YT check-permission test_user write "//home/wrapper_test" | grep allow

    $YT remove-member test_user test_group
    check "[]" "$($YT get //sys/groups/test_group/@members --format '<format=text>yson')"

    $YT remove //sys/users/test_user
}

#TODO(ignat): move this test to python
test_concurrent_upload_in_operation()
{
    echo "cat" > $SANDBOX_DIR/script.sh
    chmod +x $SANDBOX_DIR/script.sh

    echo "x=y" | $YT write //home/wrapper_test/table --format dsv

    $YT map "cat" --src "//home/wrapper_test/table" --dst "//home/wrapper_test/out1" --format dsv --local-file $SANDBOX_DIR/script.sh &
    add_pid_to_kill "$!"
    $YT map "cat" --src "//home/wrapper_test/table" --dst "//home/wrapper_test/out2" --format dsv --local-file $SANDBOX_DIR/script.sh &
    add_pid_to_kill "$!"

    ok=0
    for i in {1..10}; do
        check=1
        for out_index in {1..2}; do
            if [ $($YT exists "//home/wrapper_test/out${out_index}") = "false" ]; then
                check=0
                break
            fi
            content=$($YT read "//home/wrapper_test/out${out_index}" --format dsv)
            if [ "$content" != "x=y" ]; then
                check=0
                break
            fi
        done

        if [ "$check" = "0" ]; then
            sleep 2
        else
            ok=1
            break
        fi
    done
    check "$ok" "1"
}

test_sorted_by()
{
    echo "x=y" | $YT write "<sorted-by=[x]>//home/wrapper_test/table" --format dsv
    echo "x=z" | $YT write "<sorted_by=[x]>//home/wrapper_test/table" --format dsv
    check "$($YT get //home/wrapper_test/table/@sorted)" "$TRUE"
}

test_transactions()
{
    local tx=$($YT start-tx)
    $YT abort-tx "$tx"

    tx=$($YT start-tx)
    $YT commit-tx "$tx"
}

test_hybrid_arguments()
{
    $YT create table //home/wrapper_test/hybrid_test

    $YT copy //home/wrapper_test/hybrid_test --destination-path //home/wrapper_test/hybrid_copy
    check "$($YT exists --path //home/wrapper_test/hybrid_copy)" "true"

    $YT copy --destination-path //home/wrapper_test/hybrid_copy2 --source-path //home/wrapper_test/hybrid_copy
    check "$($YT exists --path //home/wrapper_test/hybrid_copy2)" "true"

    $YT move //home/wrapper_test/hybrid_test --destination-path //home/wrapper_test/hybrid_moved
    check "$($YT exists //home/wrapper_test/hybrid_moved)" "true"

    $YT move --destination-path //home/wrapper_test/hybrid_test --source-path //home/wrapper_test/hybrid_moved
    check "$($YT exists //home/wrapper_test/hybrid_test)" "true"

    $YT link --link-path //home/wrapper_test/hybrid_link --target-path //home/wrapper_test/hybrid_test

    $YT remove --path //home/wrapper_test/hybrid_test
    check_failed "$YT read //home/wrapper_test/hybrid_link --format dsv"

    $YT create map_node //home/wrapper_test/test_dir
    $YT create --type map_node --path //home/wrapper_test/test_dir2
    check "$($YT list --path //home/wrapper_test/test_dir)" ""
    check "$($YT find --path //home/wrapper_test/test_dir --type file)" ""

    echo -ne "a\tb\n" | $YT write --table //home/wrapper_test/yamr_table --format "yamr"
    check "$($YT read --table //home/wrapper_test/yamr_table --format yamr)" "a\tb\n"

    echo -ne "abcdef" | $YT write-file --destination //home/wrapper_test/test_file
    check "$($YT read-file --path //home/wrapper_test/test_file)" "abcdef"

    TX=$($YT start-tx --timeout 10000)
    $YT lock --path //home/wrapper_test/test_file --tx $TX
    check_failed "$YT remove --path //home/wrapper_test/test_file"
    $YT ping-tx --transaction $TX
    $YT abort-tx --transaction $TX

    $YT check-permission root write //home/wrapper_test
    $YT check-permission --user root --permission write --path //home/wrapper_test

    $YT set --path //home/wrapper_test/value --value "def"
    check "$($YT get --path //home/wrapper_test/value)" "\"def\""

    $YT set //home/wrapper_test/value "abc"
    check "$($YT get --path //home/wrapper_test/value)" "\"abc\""

    echo -ne "with_pipe" | $YT set //home/wrapper_test/value
    check "$($YT get --path //home/wrapper_test/value)" "\"with_pipe\""
}

test_async_operations() {
    export YT_TABULAR_DATA_FORMAT="dsv"
    echo -e "x=1\n" | $YT write //home/wrapper_test/input_table
    map_op=$($YT map "tr 1 2" --src //home/wrapper_test/input_table --dst //home/wrapper_test/map_output --async)

    sort_op=$($YT sort --src //home/wrapper_test/input_table --dst //home/wrapper_test/sort_output --sort-by "x" --async)
    $YT track-op $sort_op

    reduce_op=$($YT reduce "cat" \
                --src //home/wrapper_test/sort_output \
                --dst //home/wrapper_test/reduce_output \
                --sort-by "x" \
                --reduce-by "x" \
                --async)
    $YT track-op $reduce_op

    op=$($YT map-reduce \
         --mapper "cat" \
         --reducer "cat" \
         --src //home/wrapper_test/sort_output \
         --dst //home/wrapper_test/map_reduce_output \
         --reduce-by "x" \
         --async)
    $YT track-op $op

    $YT track-op $map_op
    check "x=2\n" "$($YT read //home/wrapper_test/map_output)"

    unset YT_TABULAR_DATA_FORMAT
}

test_json_structured_format() {
    export YT_STRUCTURED_DATA_FORMAT="json"

    $YT set //home/wrapper_test/folder "{}"
    check "$(echo -e "{\n    \"folder\": {\n\n    }\n}\n\n")" "$($YT get //home/wrapper_test)"

    $YT set //home/wrapper_test/folder/@attr '{"test": "value"}'
    check  "$(echo -e "{\n    \"test\": \"value\"\n}\n\n")" "$($YT get //home/wrapper_test/folder/@attr)"

    unset YT_STRUCTURED_FORMAT
}

test_transform()
{
    export YT_TABULAR_DATA_FORMAT="dsv"
    echo -e "k=v\n" | $YT write //home/wrapper_test/table_to_transform
    $YT transform //home/wrapper_test/table_to_transform

    $YT transform //home/wrapper_test/table_to_transform --compression-codec zlib_6
    check '"zlib_6"' "$($YT get //home/wrapper_test/table_to_transform/@compression_codec)"

    $YT transform //home/wrapper_test/table_to_transform --compression-codec zlib_6 --check-codecs

    $YT transform //home/wrapper_test/table_to_transform //home/wrapper_test/other_table --compression-codec zlib_6
    check '"zlib_6"' "$($YT get //home/wrapper_test/other_table/@compression_codec)"

    $YT transform //home/wrapper_test/table_to_transform //home/wrapper_test/other_table --optimize-for scan
    check '"scan"' "$($YT get //home/wrapper_test/other_table/@optimize_for)"

    unset YT_TABULAR_DATA_FORMAT
}

tear_down
run_test test_cypress_commands
run_test test_list_long_format
run_test test_concatenate
run_test test_table_commands
run_test test_file_commands
run_test test_copy_move_link
run_test test_merge_erase
run_test test_map_reduce
run_test test_users
run_test test_concurrent_upload_in_operation
run_test test_sorted_by
run_test test_transactions
run_test test_hybrid_arguments
run_test test_async_operations
run_test test_json_structured_format
run_test test_transform
