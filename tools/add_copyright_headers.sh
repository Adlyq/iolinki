#!/bin/bash
# Script to add copyright headers to all source files
# Copyright (C) 2026 Andrii Shylenko

C_HEADER='/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

'

# Function to add C/H header
add_c_header() {
    local file="$1"
    # Check if file already has copyright
    if ! grep -q "Copyright (C) 2026 Andrii Shylenko" "$file"; then
        echo "Adding header to $file"
        echo "$C_HEADER$(cat "$file")" > "$file"
    fi
}

# Process C and H files
find src include tests examples -type f \( -name "*.c" -o -name "*.h" \) | while read -r file; do
    add_c_header "$file"
done

echo "Copyright headers added successfully!"
