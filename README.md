# linux_kernel_pr4100_vbox
VirtualBox ready kernel for WD My Cloud PR4100 firmware
This kernel is used for running WD My Cloud PR4100 firmware inside a (Virtualbox) virtual machine.

Compile:

Setup toolchains:>https://github.com/fxsheep/x86_64-intel-linux-gnu

>cd linux-4.1.13

>./xbuild.sh build

...Then replace uImage (in partition /dev/sda2 inside vbox) with ./arch/x86/boot/bzImage 
