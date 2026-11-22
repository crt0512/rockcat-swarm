# shared bits for the capture scripts, gets sourced and does nothing by itself

CLI="${ROCKCAT_CLI:-rockcat-cli}"
command -v "$CLI" >/dev/null || { echo "cant find rockcat-cli, build it and put it in PATH or set ROCKCAT_CLI=/path/to/rockcat-cli"; exit 1; }

# prints the hidraw node of the first device with one of the given pids that declares report 06
# a device shows up as several nodes and only one of them has the reports we want
find_node() {
    "$CLI" raw list | awk -v pids="$1" '
        BEGIN { n = split(pids, want, " ") }
        /^\/dev\// { node = $1; split($2, id, ":"); pid = id[2] }
        /reports:/ && / 06( |$)/ { for (i = 1; i <= n; i++) if (pid == want[i]) { print node; exit } }'
}

# select, wait for the status, read. this is the only kind of write these scripts do and it changes nothing on the device
# usage: fetch SELECT_A SELECT_B REPORT LENGTH
fetch() {
    "$CLI" capture "$OUT" --device "$NODE" raw set 04 "$1" "$2" || { echo "select $1 $2 was refused, stopping"; exit 1; }
    "$CLI" capture "$OUT" --device "$NODE" raw get "$3" "$4" >/dev/null || { echo "report $3 didnt come, stopping"; exit 1; }
}

plain() {
    "$CLI" capture "$OUT" --device "$NODE" raw get "$1" "$2" >/dev/null || { echo "report $1 didnt come, stopping"; exit 1; }
}
