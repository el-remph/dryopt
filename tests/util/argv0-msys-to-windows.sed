#!/bin/sed -f
# Convert MSYS /c/Users/Foo into Windows C:\Users\Foo

# Make this a no-op when it doesn't apply (incomprehensible even for sed)
/^\/[[:alpha:]]\//!b

# Remember path
h

# Isolate volume from path (with some pointless code golf without -E)
s|^/.|&\n|
s|^/||
s/\n.*//

# Upcase
y/qwertyuiopasdfghjklzxcvbnm/QWERTYUIOPASDFGHJKLZXCVBNM/

# Get path back
G

# Lose vestigial lowercase volume
s|\n/.|:|

# Add .exe if not already present
/\.exe$/!s/$/.exe/

# Slashes to backslashes
y|/|\\|
