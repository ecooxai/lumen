lumen_read_process_stats() {
    lumen_process_dir=$1
    if ! { IFS= read -r lumen_process_stat < "$lumen_process_dir/stat"; } 2>/dev/null ||
            [ -z "$lumen_process_stat" ]; then
        printf '%s\n' 'Cannot read complete process stat' >&2
        return 1
    fi
    lumen_sample=$lumen_process_stat
    lumen_thread_count=0
    for lumen_task_dir in "$lumen_process_dir"/task/[0-9]*; do
        [ -d "$lumen_task_dir" ] || continue
        if { IFS= read -r lumen_task_stat < "$lumen_task_dir/stat"; } 2>/dev/null; then
            if [ -z "$lumen_task_stat" ]; then
                printf '%s\n' 'Empty task stat' >&2
                return 1
            fi
            lumen_sample="$lumen_sample
$lumen_task_stat"
            lumen_thread_count=$((lumen_thread_count + 1))
        elif [ -d "$lumen_task_dir" ]; then
            printf '%s\n' 'Cannot read complete live task stat' >&2
            return 1
        fi
    done
    if [ "$lumen_thread_count" -eq 0 ]; then
        printf '%s\n' 'Missing thread telemetry' >&2
        return 1
    fi
    if ! { IFS= read -r lumen_process_stat < "$lumen_process_dir/stat"; } 2>/dev/null ||
            [ -z "$lumen_process_stat" ]; then
        printf '%s\n' 'Process disappeared during collection' >&2
        return 1
    fi
    printf '%s\n' "$lumen_sample"
}
