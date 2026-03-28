#!/usr/bin/awk -f

BEGIN {
    colors["RED"] = "\033[31m"
    colors["YELLOW"] = "\033[33m"
    colors["GREEN"] = "\033[32m"
    RESET = "\033[0m"
}

/ACTIVE/ {
    line = $0
    for (color in colors) {
        gsub(color, colors[color] "&" RESET, line)
    }
    print line
    next
} 
1
