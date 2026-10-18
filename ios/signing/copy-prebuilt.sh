#!/bin/sh
set -eu
: "${LUMEN_PREBUILT_APP:?Set LUMEN_PREBUILT_APP to the existing device app}"
: "${TARGET_BUILD_DIR:?}"
: "${UNLOCALIZED_RESOURCES_FOLDER_PATH:?}"
for name in Lumen icudtl.dat cacert.pem AppIcon60x60@3x.png 'AppIcon83.5x83.5@2x~ipad.png' 'AppIcon76x76@2x~ipad.png' AppIcon60x60@2x.png; do
    test -f "$LUMEN_PREBUILT_APP/$name"
    /bin/cp "$LUMEN_PREBUILT_APP/$name" "$TARGET_BUILD_DIR/$UNLOCALIZED_RESOURCES_FOLDER_PATH/$name"
done
/bin/chmod 755 "$TARGET_BUILD_DIR/$EXECUTABLE_PATH"
