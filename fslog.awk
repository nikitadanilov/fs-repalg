# -*- awk -*-
#
# collect stats from fslog output file
#
# Sample line
#
#0000e926 0207ba9e 02f4          khelper 00000001 00000017 00000000             nash 00000000 R .+udra..
#
#

BEGIN {
        #
        # Declare global variables
        #

        lastno = "";
        missed = 0;
        nr     = 0;
        puniq  = 0;
        funiq  = 0;

        printf "" > "fslog.err"
        printf "" > "fslog.stat"
        printf "" > "fslog.dev"
        printf "" > "fslog.file"

        printf "" > "fslog.trace"
}

{
        no   = $1;
        time = $2;
        pid  = $3;
        comm = $4;
        dev  = $5;
        ino  = $6;
        gen  = $7;
        name = substr($0, 68, 16);
        ind  = substr($0, 85, 8);
        type = substr($0, 94, 1);

        fid  = dev " " ino " " gen " " name;
        page_id = fid " " ind;

        nr++;
        if (lastno != "") {
                if (lastno + 1 != no) {
                        printf "record %s missed\n", lastno + 1 >> "fslog.err"
                        missed++;
                }
                lastno = no;
        }
        device_access[dev]++;
        file_access[fid]++;
        if (file_map[fid] == "")
                file_map[fid] = funiq++;
        if (page_map[page_id] == "")
                page_map[page_id] = puniq++;
        printf "%s %8.8x %8.8x\n", $0, file_map[fid], page_map[page_id];
        printf "%8.8x %8.8x %8.8s %c\n",
               page_map[page_id], file_map[fid], ind, type >> "fslog.trace"
}

END {
        printf "total:  %i\n", nr          >> "fslog.stat"
        printf "missed: %i\n", missed      >> "fslog.stat"
        printf "unique pages: %i\n", puniq >> "fslog.stat"
        printf "unique files: %i\n", funiq >> "fslog.stat"

        for (dev in device_access) {
                printf "%8.8x %s\n", device_access[dev], dev >> "fslog.dev"
        }

        for (fid in file_access) {
                printf "%8.8x %8.8x %s\n",
                       file_access[fid], file_map[fid], fid >> "fslog.file"
        }
}
