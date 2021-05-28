#!/bin/sh
#
# Check if we have any TAB or trailing SPACE in our code base
#

TAB=$(printf '\t')

fileswscheck=$(git ls-files '*.[ch]' '*.cpp' '*.hpp' '*.sh' '*.py' '*.iocsh')
if test -n "$fileswscheck"; then
  cmd=$(printf "%s %s" 'egrep -n "$TAB| \$"' "$fileswscheck")
  #echo cmd=$cmd
  eval $cmd
  if test $? -eq 0; then
    #TABS found
    exit 1
  fi
fi
exit 0
