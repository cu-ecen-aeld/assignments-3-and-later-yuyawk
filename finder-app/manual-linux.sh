#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.
#
# Args:
#   $1: The location on the filesystem where the output files should be placed.
#       Optional. Defaults to "/tmp/aeld".

set -euo pipefail

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.1.10
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR="$(realpath "$(dirname $0)")"
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-

if [ $# -lt 1 ]; then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR="$(realpath "$1")"
	echo "Using passed directory ${OUTDIR} for output"
fi

if ! mkdir -p "${OUTDIR}"; then
    echo "ERROR: Failed to create ${OUTDIR}" >&2
    exit 1
fi

cd "${OUTDIR}"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone "${KERNEL_REPO}" --depth 1 --single-branch --branch "${KERNEL_VERSION}"
    # yuyawk: Workaround for ODR violation
    sed -i -e 's/^YYLTYPE yylloc;$/extern YYLTYPE yylloc;/' linux-stable/scripts/dtc/dtc-lexer.l
fi
if [ ! -e "${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image" ]; then
    cd "${OUTDIR}/linux-stable"
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout "${KERNEL_VERSION}"

    # kernel build steps here
    make "ARCH=${ARCH}" "CROSS_COMPILE=${CROSS_COMPILE}" mrproper
    make "ARCH=${ARCH}" "CROSS_COMPILE=${CROSS_COMPILE}" defconfig
    make "-j$(nproc)" "ARCH=${ARCH}" "CROSS_COMPILE=${CROSS_COMPILE}" all
    make "ARCH=${ARCH}" "CROSS_COMPILE=${CROSS_COMPILE}" modules
    make "ARCH=${ARCH}" "CROSS_COMPILE=${CROSS_COMPILE}" dtbs
fi

echo "Adding the Image in outdir"
cp "${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image" "${OUTDIR}"

echo "Creating the staging directory for the root filesystem"
cd "${OUTDIR}"
if [ -d "${OUTDIR}/rootfs" ]; then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    rm -rf "${OUTDIR}/rootfs" || sudo rm -rf "${OUTDIR}/rootfs"
fi

# Create necessary base directories
mkdir -p "${OUTDIR}/rootfs"
cd "${OUTDIR}/rootfs"
mkdir -p bin dev etc home lib lib64 proc sbin sys tmp usr var
mkdir -p usr/bin usr/lib usr/sbin
mkdir -p var/log

cd "${OUTDIR}"
if [ ! -d "${OUTDIR}/busybox" ]; then
    git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # Configure busybox
    make distclean
    make defconfig
else
    cd busybox
fi

# Make and install busybox
make "ARCH=${ARCH}" "CROSS_COMPILE=${CROSS_COMPILE}"
make "CONFIG_PREFIX=${OUTDIR}/rootfs" "ARCH=${ARCH}" "CROSS_COMPILE=${CROSS_COMPILE}" install

echo "Library dependencies"
${CROSS_COMPILE}readelf -a "${OUTDIR}/rootfs/bin/busybox" | grep "program interpreter"
${CROSS_COMPILE}readelf -a "${OUTDIR}/rootfs/bin/busybox" | grep "Shared library"

# Add library dependencies to rootfs
CROSS_COMPILE_SYSROOT="$("${CROSS_COMPILE}gcc" -print-sysroot)"
cp "${CROSS_COMPILE_SYSROOT}/lib/ld-linux-aarch64.so.1" "${OUTDIR}/rootfs/lib"
cp "${CROSS_COMPILE_SYSROOT}/lib64/libm.so.6" "${OUTDIR}/rootfs/lib64"
cp "${CROSS_COMPILE_SYSROOT}/lib64/libresolv.so.2" "${OUTDIR}/rootfs/lib64"
cp "${CROSS_COMPILE_SYSROOT}/lib64/libc.so.6" "${OUTDIR}/rootfs/lib64"

# Make device nodes
sudo mknod -m 0666 "${OUTDIR}/rootfs/dev/null" c 1 3
sudo mknod -m 0666 "${OUTDIR}/rootfs/dev/console" c 5 1

# Clean and build the writer utility
pushd "${FINDER_APP_DIR}"
make clean
make "CROSS_COMPILE=${CROSS_COMPILE}"
popd

# Copy the finder related scripts and executables to the /home directory
# on the target rootfs
mkdir -p "${OUTDIR}/rootfs/home/finder-app"
cp "${FINDER_APP_DIR}/finder.sh" "${OUTDIR}/rootfs/home/finder-app"
cp "${FINDER_APP_DIR}/finder-test.sh" "${OUTDIR}/rootfs/home/finder-app"
cp "${FINDER_APP_DIR}/writer" "${OUTDIR}/rootfs/home/finder-app"
cp "${FINDER_APP_DIR}/autorun-qemu.sh" "${OUTDIR}/rootfs/home/finder-app"
cp -Lr "${FINDER_APP_DIR}/conf" "${OUTDIR}/rootfs/home"
pushd "${OUTDIR}/rootfs/home/finder-app"
ln -s ../conf conf
popd

# Chown the root directory
sudo chown -R root:root "${OUTDIR}/rootfs"

# Create initramfs.cpio.gz
cd "${OUTDIR}/rootfs"
find . | cpio -H newc -ov --owner root:root > "${OUTDIR}/initramfs.cpio"
cd "${OUTDIR}"
gzip -f initramfs.cpio
