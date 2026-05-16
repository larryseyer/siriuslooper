#!/bin/bash

if [ -z "$1" ]; then
    echo "Usage: bu.sh \"commit message\""
    exit 1
fi

git add .
git commit -m "$1"

BRANCH=$(git rev-parse --abbrev-ref HEAD)
git push origin "$BRANCH"

# Backup
SOURCE="/Users/larryseyer/Sirius Looper"
DEST_DIR="/Users/larryseyer/Dropbox/Automagic Art/Source Backup/Sirius Looper Backup"
TIMESTAMP=$(date +"%Y_%m_%d")
MESSAGE=$(echo "$1" | sed 's/ /_/g')
ZIP_FILE="$DEST_DIR/Sirius_${TIMESTAMP}_${MESSAGE}.zip"
cd "$SOURCE" || exit 1
zip -r "$ZIP_FILE" . \
    -x "build/*" -x "build/" \
    -x "build-ios/*" -x "build-ios/" \
    -x "build-ios-sim/*" -x "build-ios-sim/" \
    -x "build-ios-device/*" -x "build-ios-device/" \
    -x ".git/*" -x ".git/" \
    -x "*.zip"
