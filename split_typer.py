#!/usr/bin/env python3
"""
Split typer.tks into smaller files based on logical function groups.
Each output file should be <= 300 code lines.
"""

import re
import os

filepath = '/home/user/teko-lang/.claude/worktrees/wf_368fdb92-bd4-4/src/checker/typer.tks'
outdir = '/home/user/teko-lang/.claude/worktrees/wf_368fdb92-bd4-4/src/checker'

with open(filepath, 'r') as f:
    content = f.read()
    lines = content.split('\n')

# Extract namespace and use statements
ns_line = lines[0]  # "// src/checker/typer.tks  (namespace 'teko::checker')"
use_lines = []
code_start = 0
for i, line in enumerate(lines):
    if line.startswith('use '):
        use_lines.append(line)
        code_start = i + 1
    elif line.strip() and not line.startswith('//') and not line.startswith('use '):
        break

use_block = '\n'.join(use_lines)

# Define function extraction regions (start_line, end_line, output_filename, description)
splits = [
    # Literal typing (type_number, type_strlit, etc.)
    (22, 57, 'literals', 'Literal typing: numbers, strings, chars, bools, null, variables'),
    # Lambda/closure handling
    (59, 274, 'lambda', 'Lambda/closure collection, parameter typing, and lambda type building'),
    # Tilde chain operations
    (276, 378, 'tilde', 'Tilde (~) operator chain flattening and string concatenation'),
    # Bignum operations
    (379, 527, 'bignum', 'Bignum type operations: adoption, function mapping, binop/compare'),
    # Arithmetic, bitwise, logical binops
    (529, 727, 'binop', 'Binary operations: shift, arithmetic, bitwise, logical, general type_binary'),
    # Unary operations
    (749, 786, 'unary', 'Unary operations: negate, not, bitwise NOT, deref, etc.'),
    # Compare operations
    (787, 838, 'compare', 'Comparison operations: equality, relational'),
    # Builtin calls: list, mem_free, region ops
    (839, 1003, 'builtins', 'Built-in function typing: list, mem_free, region_new, region_alloc, variadic'),
    # Default arguments
    (1004, 1101, 'defargs', 'Default argument resolution and variadic packing'),
    # Flags methods
    (1102, 1145, 'flags', 'Flags method typing'),
    # Interface dispatch and type params
    (1146, 1244, 'iface_tp', 'Interface dispatch resolution and type parameter handling'),
]

header = f"""// src/checker/{{}}.tks  (namespace 'teko::checker')

{use_block}
"""

print("Extracting function groups from typer.tks...")
for start, end, filename, desc in splits:
    # Extract lines (1-indexed in spec, 0-indexed in python)
    extracted = lines[start-1:end]

    # Count code lines (non-comment, non-empty)
    code_count = 0
    for line in extracted:
        stripped = line.strip()
        if stripped and not stripped.startswith('//'):
            code_count += 1

    # Write to file
    outfile = os.path.join(outdir, f'{filename}.tks')
    content_out = header.format(filename) + '\n' + '\n'.join(extracted)

    with open(outfile, 'w') as f:
        f.write(content_out)

    print(f"  {filename:15s}: {end-start+1:4d} total lines, ~{code_count:3d} code lines")

print("\nDone! Next steps:")
print("  1. Review generated files for correctness")
print("  2. Verify all exports are preserved")
print("  3. Delete or minimize typer.tks")
print("  4. Commit changes")
