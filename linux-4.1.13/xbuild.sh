#!/bin/sh

export KBUILD_BUILD_USER=kman
export KBUILD_BUILD_HOST=kmachine
export KCFLAGS="-DALPHA_CUSTOMIZE"
export KERNELDIR=`pwd`

# ITR-99975, for Intel platforms, leave BASEVERSION empty.
export BASEVERSION=
export BUILDNO=git$(git rev-parse --verify --short HEAD)

IMAGE_NAME=bzImage

build()
{
	if [ -z "$ARCH" ] ; then
	  echo "do \"export ARCH=x86_64\" first."
	  exit 1
	fi

	rm -f .version

	make ${IMAGE_NAME} -j || exit 1
	make modules -j || exit 1
	make all -C ../netatop-0.6 || exit 1
  ./mkimage -A x86 -O linux -T kernel -C gzip -n kernel -d ./arch/${ARCH}/boot/bzImage uImage || exit 1
}

install()
{
	cp -vf arch/${ARCH}/boot/${IMAGE_NAME} \
		${ROOTDIR}/merge/${PROJECT_NAME}/${IMAGE_NAME}

	cp -vf uImage ${ROOTDIR}/merge/${PROJECT_NAME}/ 

	[ -d "${ROOT_FS}/driver" ] || mkdir "${ROOT_FS}/driver"
	# for iSCSI Target
	cp -avf \
		./drivers/target/iscsi/iscsi_target_mod.ko \
		./drivers/target/target_core_mod.ko \
		./drivers/target/target_core_file.ko \
		./drivers/target/target_core_iblock.ko \
		${ROOT_FS}/driver/

	# for Virtual Volume
	cp -avf \
		drivers/scsi/scsi_transport_iscsi.ko \
		drivers/scsi/iscsi_tcp.ko \
		drivers/scsi/libiscsi_tcp.ko \
		drivers/scsi/libiscsi.ko \
		${ROOT_FS}/driver/

	# Bonding, We have 2 ethernets!
	cp -avf \
		drivers/net/bonding/bonding.ko \
		${ROOT_FS}/driver/

	# Tunnels
	cp -avf \
		net/ipv4/tunnel4.ko \
		net/ipv6/ipv6.ko \
		net/ipv6/sit.ko \
		net/ipv6/xfrm6_mode_beet.ko \
		net/ipv6/xfrm6_mode_transport.ko \
		net/ipv6/xfrm6_mode_tunnel.ko \
		net/ipv4/ipip.ko \
		net/ipv6/tunnel6.ko \
		net/ipv6/ip6_tunnel.ko \
		drivers/net/tun.ko \
		${ROOT_FS}/driver/
	
	# for VPN - PPTP
	mkdir -p ${MODULE_DIR}/apkg/addons/${PROJECT_NAME}/VPN/lib/modules
	cp -avf drivers/net/ppp/bsd_comp.ko                             ${MODULE_DIR}/apkg/addons/${PROJECT_NAME}/VPN/lib/modules
	cp -avf drivers/net/ppp/ppp_async.ko                    ${MODULE_DIR}/apkg/addons/${PROJECT_NAME}/VPN/lib/modules
	cp -avf drivers/net/ppp/ppp_deflate.ko                  ${MODULE_DIR}/apkg/addons/${PROJECT_NAME}/VPN/lib/modules
	cp -avf drivers/net/ppp/ppp_generic.ko                  ${MODULE_DIR}/apkg/addons/${PROJECT_NAME}/VPN/lib/modules
	cp -avf drivers/net/ppp/ppp_mppe.ko                             ${MODULE_DIR}/apkg/addons/${PROJECT_NAME}/VPN/lib/modules
	cp -avf drivers/net/ppp/ppp_synctty.ko                  ${MODULE_DIR}/apkg/addons/${PROJECT_NAME}/VPN/lib/modules
	cp -avf drivers/net/ppp/pppoe.ko                                ${MODULE_DIR}/apkg/addons/${PROJECT_NAME}/VPN/lib/modules
	cp -avf drivers/net/ppp/pppox.ko                                ${MODULE_DIR}/apkg/addons/${PROJECT_NAME}/VPN/lib/modules
	cp -avf drivers/net/slip/slhc.ko                        ${MODULE_DIR}/apkg/addons/${PROJECT_NAME}/VPN/lib/modules
	cp -avf lib/crc-ccitt.ko				${MODULE_DIR}/apkg/addons/${PROJECT_NAME}/VPN/lib/modules
	
	# Docker dependencies
	cp -avf \
		net/netfilter/nf_conntrack.ko \
		net/netfilter/nf_nat.ko \
		net/netfilter/x_tables.ko \
		net/netfilter/xt_addrtype.ko \
		net/netfilter/xt_conntrack.ko \
		net/netfilter/xt_nat.ko \
		net/netfilter/xt_tcpudp.ko \
		net/ipv4/ip_tunnel.ko \
		net/ipv4/netfilter/ip_tables.ko \
		net/ipv4/netfilter/nf_nat_ipv4.ko \
		net/ipv4/netfilter/nf_defrag_ipv4.ko \
		net/ipv4/netfilter/nf_conntrack_ipv4.ko \
		net/ipv4/netfilter/iptable_nat.ko \
		net/ipv4/netfilter/ipt_MASQUERADE.ko \
		net/ipv4/netfilter/nf_nat_masquerade_ipv4.ko \
		net/ipv4/netfilter/iptable_filter.ko \
		net/llc/llc.ko \
		net/802/stp.ko \
		net/bridge/bridge.ko \
		net/bridge/br_netfilter.ko \
		${ROOT_FS}/driver/

	# netatop
	cp -avf ../netatop-0.6/module/netatop.ko ${ROOT_FS}/driver/

	# btrfs support
	cp -avf fs/btrfs/btrfs.ko ${ROOT_FS}/driver/
}

clean()
{
	make clean
}

if [ "$1" = "build" ]; then
	build
elif [ "$1" = "install" ]; then
	install
elif [ "$1" = "clean" ]; then
	clean
else
	echo "Usage : $0 build or $0 install or $0 clean"
fi
