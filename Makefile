# env configs
BDIR 	:= /tmp/sss-build
GEM5DIR := ./build
M5DIR	:= ${HOME}/m5
LOG_DIR := ./logs

TIME	:= $(shell date +%F-%H%M%S)

export M5_PATH=${M5DIR}

# ISA configs
ISA 	:= X86

DISK_EX	:= ${M5DIR}/disks/slets.img

ifeq (${ISA},X86)
KERNEL	:= x86_64-vmlinux-4.9.92
DISK	:= ${M5DIR}/disks/x86nvme.img ${DISK_EX}
else
KERNEL	:= aarch64-vmlinux-4.9.92
DISK	:= ${M5DIR}/disks/linaro-aarch64-linux.img
endif


# hardware configs
CPU	:= AtomicSimpleCPU
CORES	:= 4
CLK	:= 3GHz
CACHE	:= --caches --l2cache
MEM	:= DDR4_2400_8x8
MEM_GB	:= 4
DUAL	:=

# debug configs
DPRINT_FLAGS	:= M5Print
DEBUG_FLAGS	:= --debug-flag=${DPRINT_FLAGS} --debug-file=debug.txt

M5_LOG_FILE	:= ${LOG_DIR}/m5-${TIME}.log
M5_STAT_FILE	:= ${LOG_DIR}/m5-${TIME}.stat.txt
HOST_LOG_FILE	:= ${LOG_DIR}/m5-${TIME}.host.log

GDB_BIN		:= gdb
GDB_LOGGING	:= on
_GDB_LOGGING	= "set logging ${GDB_LOGGING}" #"set trace-commands ${GDB_LOGGING}"
GDB_LOG_FILE	:= ${LOG_DIR}/gdb-${TIME}.log
_GDB_LOG_FILE	= "set logging file ${GDB_LOG_FILE}"

GDB_PAGINATION	:= off
_GDB_PAGINATION	= "set pagination ${GDB_PAGINATION}"

GDB_STOP_SIG	:= SIGUSR1
_GDB_STOP_SIG	= "handle ${GDB_STOP_SIG} nopass stop"

GDB_EX_OPTIONS	= -ex ${_GDB_LOG_FILE} -ex ${_GDB_LOGGING} -ex ${_GDB_PAGINATION} -ex ${_GDB_STOP_SIG} -ex "set print object on"
_GDB_EX_OPTIONS = ${GDB_EX_OPTIONS}

# gem5 configs
VARIANT 	:= opt
GEM5_SCRIPT	:=
GEM5_CFG	:= ./configs/example/fs.py $(addprefix --script ,${GEM5_SCRIPT})
SSS_CFG		:= ./src/dev/storage/simplessd/config/sample.cfg

PORT		:= 3456

HW_FLAGS	= --num-cpu=${CORES} --cpu-clock=${CLK} ${CACHE} --cpu-type=${CPU} --mem-size=${MEM_GB}GB --mem-type=${MEM}
SYS_FLAGS	= --kernel=${KERNEL} $(addprefix --disk-image=,${DISK}) ${DUAL} ${HW_FLAGS}
SIMPLESSD_FLAGS	:= --ssd-interface=nvme --ssd-config=${SSS_CFG}


#### config done ####

GEM5_TARGET	= ${GEM5DIR}/${ISA}/gem5.${VARIANT}
GEM5_EXEC_CMD	= ${GEM5_TARGET} ${DEBUG_FLAGS} ${GEM5_CFG} ${SYS_FLAGS} ${SIMPLESSD_FLAGS}

build: setup
	scons ${GEM5_TARGET} -j 8 --ignore-style

run-timing: CPU = TimingSimpleCPU
run-timing: CORES = 1
run-timing: run

run: setup
	echo "M5_PATH at $$M5_PATH"
	touch ${M5_STAT_FILE}
	ln -srf ${M5_STAT_FILE} m5out/stats.txt
	${GEM5_EXEC_CMD} | tee ${M5_LOG_FILE}

m5term:
	${MAKE} -C util/term
	./util/term/m5term localhost ${PORT}

.PHONY: socat
socat:
	./socat -R ${HOST_LOG_FILE} -,raw,echo=0 tcp:localhost:${PORT}

gdb:
	${GDB_BIN} -q ${_GDB_EX_OPTIONS} --args ${GEM5_EXEC_CMD}

gdb-stop:
	pkill -${GDB_STOP_SIG} -o $(shell basename ${GEM5_TARGET})

setup:
	mkdir -p "${BDIR}"
	mkdir -p "${LOG_DIR}"
	ln -nsrf "${BDIR}" build
	ln -nsrf cp2m5 "${M5DIR}"

clean:
	# clear all intermediate files
	rm parsetab.py **/*.pyc
