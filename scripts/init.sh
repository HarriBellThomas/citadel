#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
PARENT="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && cd .. && pwd )"
KERNEL_ARCHIVE_URL="https://mirrors.edge.kernel.org/pub/linux/kernel/v5.x/linux-5.6.2.tar.xz"
KERNEL_ARCHIVE="${KERNEL_ARCHIVE_URL##*/}"
KERNEL_TAR_ARCHIVE=${KERNEL_ARCHIVE%".xz"}
KERNEL_VERSION=${KERNEL_ARCHIVE%".tar.xz"}
CITADEL_KERNEL_FOLDER="$KERNEL_VERSION.citadel"
UPDATE_MODE=0

# Header.
printf "\n//  Citadel Build Script  //\n\n"

# Get the desired install location.
[ -f "$DIR/.citadel_path" ] && old_dir=$(cat "$DIR/.citadel_path") 
old_dir=${old_dir:-"$PARENT/kernel"}

echo "Install Directory [$old_dir]"
read -e -p "-> " KERNEL_SOURCE_PATH
KERNEL_SOURCE_PATH=${KERNEL_SOURCE_PATH:-"$old_dir"}


if [ "$old_dir" = "$KERNEL_SOURCE_PATH" ]; then
    UPDATE_MODE=1
else
    echo "$KERNEL_SOURCE_PATH" > $DIR/.citadel_path
fi

printf "\nGenerating $KERNEL_SOURCE_PATH$CITADEL_KERNEL_FOLDER ($UPDATE_MODE)..."
[ ! -d "$KERNEL_SOURCE_PATH" ] && mkdir -p $KERNEL_SOURCE_PATH && UPDATE_MODE=0
cd $KERNEL_SOURCE_PATH

if [ $UPDATE_MODE -eq 0 ]; then

    # Download kernel source.
    [ -d "$CITADEL_KERNEL_FOLDER" ] && rm -rf $CITADEL_KERNEL_FOLDER

    if [ ! -f "$KERNEL_TAR_ARCHIVE" ]; then
        wget $KERNEL_ARCHIVE_URL
        xz -d -v $KERNEL_ARCHIVE
    fi

    tar xf ${KERNEL_ARCHIVE%".xz"}
    mv $KERNEL_VERSION $CITADEL_KERNEL_FOLDER
# else
    # echo "Skipping source retrieval"
fi

# Move Citadel source.
CITADEL_LSM_PATH="$KERNEL_SOURCE_PATH/$CITADEL_KERNEL_FOLDER/security/citadel"
mkdir -p $CITADEL_LSM_PATH
cp -r $PARENT/lsm/* $CITADEL_LSM_PATH

mv $CITADEL_LSM_PATH/kernel.config "$KERNEL_SOURCE_PATH/$CITADEL_KERNEL_FOLDER/.config"
mv $CITADEL_LSM_PATH/security.Kconfig "$KERNEL_SOURCE_PATH/$CITADEL_KERNEL_FOLDER/security/Kconfig"

printf "done.\n"


# Generate keys.
printf "Generating keys..."
$DIR/generate_keys.sh > /dev/null 2>&1
cp $DIR/keys/lsm_keys.h "$CITADEL_LSM_PATH/includes"
cp $DIR/keys/enclave_keys.h $PARENT/daemon/enclave_core
cp $DIR/keys/sealed_enclave_keys.h "$CITADEL_LSM_PATH/includes"
rm -rf $DIR/keys
printf "done.\n"



printf "\nBuilding libcitadel...\n"
cd $PARENT/libcitadel
make
cd $PARENT
printf "Done.\n"

printf "\nBuilding citadel.basic.signed.so...\n"
cd $PARENT/daemon
make
cd $PARENT
printf "Done.\n"


cd $DIR
