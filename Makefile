# env configs
BDIR 	:= /tmp/sss-build
GEM5DIR := ./build
M5DIR	:= ${HOME}/m5
LOG_DIR := ./logs

export M5_PATH=${M5DIR}

# ISA configs
ISA 	:= X86

ifeq (${ISA},X86)
KERNEL	:= x86_64-vmlinux-4.9.92
DISK	:= ${M5DIR}/disks/x86root.img
else
KERNEL	:= aarch64-vmlinux-4.9.92
DISK	:= ${M5DIR}/disks/linaro-aarch64-linux.img
endif


# hardware configs
CPU	:= AtomicSimpleCPU
CORES	:= 4
CLK	:= 2GHz 
CACHE	:= --caches --l2cache  
MEM	:= DDR4_2400_8x8
MEM_GB	:= 4
DUAL	:=

# debug configs
DPRINT_FLAGS	:= M5Print
DEBUG_FLAGS	:= --debug-flag=${DPRINT_FLAGS} --debug-file=debug.txt

LOG_FILE	:= ${LOG_DIR}/out-$(shell date +%F-%H%M%S).log


# gem5 configs
VARIANT 	:= opt
GEM5_CFG	:= ./configs/example/fs.py
SSS_CFG		:= ./src/dev/storage/simplessd/config/sample.cfg

HW_FLAGS	:= --num-cpu=${CORES} --cpu-clock=${CLK} ${CACHE} --cpu-type=${CPU} --mem-size=${MEM_GB}GB --mem-type=${MEM}
SYS_FLAGS	:= --kernel=${KERNEL} --disk-image=${DISK} ${DUAL} ${HW_FLAGS}
SIMPLESSD_FLAGS	:= --ssd-interface=nvme --ssd-config=${SSS_CFG}

GEM5_EXEC	:= ${GEM5_TARGET} ${DEBUG_FLAGS} ${GEM5_CFG} ${SYS_FLAGS} ${SIMPLESSD_FLAGS} 

#### config done ####

GEM5_TARGET	= ${GEM5DIR}/${ISA}/gem5.${VARIANT}

build: setup
	scons ${GEM5_TARGET} -j 8 --ignore-style

run: setup
	echo "M5_PATH at $$M5_PATH"
	${GEM5_EXEC} | tee ${LOG_FILE}

m5term:
	${MAKE} -C util/term
	./util/term/m5term localhost 3456

gdb:
	gdb -q --args ${GEM5_EXEC}

setup:
	mkdir -p "${BDIR}"
	mkdir -p "${LOG_DIR}"
	ln -nsrf "${BDIR}" build
	ln -nsrf cp2m5 "${M5DIR}"
