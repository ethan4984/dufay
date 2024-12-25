build_servers:
	cd sched && make && cp sched ../../build/system-root/servers/sched
	cd fs/vfs && make && cp vfs ../../../build/system-root/servers/vfs
	cd io/nvme && make && cp nvme ../../../build/system-root/servers/nvme
	cd io/pci && make && cp pci ../../../build/system-root/servers/pci

clean:
	cd sched && make clean
	cd fs/vfs && make clean
	cd io/nvme && make clean
	cd io/pci && make clean
