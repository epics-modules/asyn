#!/bin/bash
set -e

if [ "$LIBCOM_ONLY" = "YES" ]
then
  cat << EOF >> ./configure/CONFIG_SITE.local
EPICS_LIBCOM_ONLY = YES
EOF
fi
