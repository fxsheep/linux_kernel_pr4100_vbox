all:
		./mkversion
		cd module; make
#		cd daemon; make

install:	module/netatop.ko daemon/netatopd
		install -d /lib/modules/`uname -r`/extra
		install -m 0644 module/netatop.ko -t /lib/modules/`uname -r`/extra
		depmod
		install daemon/netatopd -t /usr/sbin
		install -T -m 0755 netatop.init /etc/init.d/netatop
		install man/netatop.4 -t /usr/share/man/man4
		install man/netatopd.8 -t /usr/share/man/man8

clean:
		cd module; make clean
#		cd daemon; make clean
