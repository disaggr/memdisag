Qemu setup

$ qemu-img create -f qcow2 im1.img 3G
$ qemu-system-x86_64 -enable-kvm -cdrom ../alpine-extended-3.13.5-x86_64.iso -boot menu=on -drive file=im1.img -m 768M
	# setup-alpine
$ qemu-system-x86_64 --enable-kvm -drive file=im1.img -m 768M -cpu host -object memory-backend-file,size=1M,share,mem-path=/dev/shm/ivshmem,id=hostmem -device ivshmem-plain,memdev=hostmem
$ qemu-system-x86_64 --enable-kvm -drive file=im1.img -m 768M -cpu host -object memory-backend-file,size=1M,share,mem-path=/dev/shm/ivshmem,id=hostmem -device ivshmem-plain,memdev=hostmem -net nic -net user,smb=/home/robin/Development/code,smbserver=10.0.2.4
	# mount -t cifs //10.0.2.4/qemu /mnt/qemu -o user=robin
	(Add to .profile file for execution on boot)

$ cmake /mnt/qemu/arrow/cpp -DARROW_PLASMA=ON -DARROW_OPTIONAL_INSTALL=ON
$ make plasma



/sys/devices/pci0000:00/0000:00:04.0/resource2
    int fd = open("/sys/devices/pci0000:00/0000:00:04.0/resource2", O_RDWR | O_SYNC);
    void* pointer = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    	(size = PlasmaAllocator::GetFootprintLimit() - 256 * sizeof(size_t))


https://github.com/apache/arrow/blob/master/docs/source/developers/cpp/building.rst
Best effort allocator
	ordered map with free mem regions (size, offset)
	malloc: look for smallest region fitting malloc size and mark region occupied
	free: mark region as free (check if adjacent regions free to merge)

release/plasma-store-server -m 10000 -s /tmp/plasma -v /home/robin/Development/code/build/shmem1 -w /home/robin/Development/code/build/shmem2

release/plasma-store-server -m 10000 -s /tmp/plasma2 -v /home/robin/Development/code/build/shmem2 -w /home/robin/Development/code/build/shmem1

target_link_libraries(plasma-store-server ${GFLAGS_LIBRARIES} -lprotobuf -lgrpc -lgrpc++ -L${DEP_DIR}/lib)
target_link_libraries(plasma-store-server protobuf gpr grpc++ grpc++_alts grpc++_error_details grpc++_reflection grpc++_unsecure grpc grpc_plugin_support grpc_unsecure grpcpp_channelz)
