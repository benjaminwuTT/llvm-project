# RUN: llvm-mc -triple=riscv32 -mattr=+xtttensixwh -show-encoding %s \
# RUN:   | FileCheck -check-prefixes=CHECK-ASM,CHECK-ASM-AND-OBJ %s
# RUN: llvm-mc -filetype=obj -triple=riscv32 -mattr=+xtttensixwh %s \
# RUN:   | llvm-objdump -d --mattr=+xtttensixwh --no-print-imm-hex - \
# RUN:   | FileCheck -check-prefixes=CHECK-OBJ,CHECK-ASM-AND-OBJ %s

# Tenstorrent Wormhole Tensix instructions.

# CHECK-ASM-AND-OBJ: sfploadi L2, 16256, 2
# CHECK-ASM: encoding: [0x01,0xfe,0x88,0xc4]
sfploadi L2,0x3f80,2

# CHECK-ASM-AND-OBJ: sfpmad L4, L1, L2, L3, 0
# CHECK-ASM: encoding: [0x02,0x8d,0x04,0x10]
sfpmad L4,L1,L2,L3,0

# CHECK-ASM-AND-OBJ: sfpadd L4, L2, L3, 0
# CHECK-ASM: encoding: [0x02,0x8d,0x28,0x14]
sfpadd L4,L2,L3,0

# CHECK-ASM-AND-OBJ: sfpmul L4, L2, L3, 0
# CHECK-ASM: encoding: [0x02,0xe5,0x08,0x18]
sfpmul L4,L2,L3,0

# CHECK-ASM-AND-OBJ: sfpiadd L0, L1, 5, 5
# CHECK-ASM: encoding: [0x15,0x44,0x01,0xe4]
sfpiadd L0,L1,5,5

# CHECK-ASM-AND-OBJ: sfpiadd L0, L1, -3, 5
# CHECK-ASM: encoding: [0x15,0x44,0xff,0xe7]
sfpiadd L0,L1,-3,5

# CHECK-ASM-AND-OBJ: sfpsetcc L1, 0, 2
# CHECK-ASM: encoding: [0x09,0x04,0x00,0xec]
sfpsetcc L1,0,2

# CHECK-ASM-AND-OBJ: sfpencc 3, 10
# CHECK-ASM: encoding: [0x2a,0xc0,0x00,0x28]
sfpencc 3,10

# CHECK-ASM-AND-OBJ: sfppushc 0
# CHECK-ASM: encoding: [0x02,0x00,0x00,0x1c]
sfppushc 0

# CHECK-ASM-AND-OBJ: sfppopc 0
# CHECK-ASM: encoding: [0x02,0x00,0x00,0x20]
sfppopc 0

# CHECK-ASM-AND-OBJ: sfpcompc
# CHECK-ASM: encoding: [0x02,0x00,0x00,0x2c]
sfpcompc

# CHECK-ASM-AND-OBJ: sfpmov L1, L2, 2
# CHECK-ASM: encoding: [0x49,0x08,0x00,0xf0]
sfpmov L1,L2,2

# CHECK-ASM-AND-OBJ: sfpnot L1, L2
# CHECK-ASM: encoding: [0x42,0x08,0x00,0x00]
sfpnot L1,L2

# CHECK-ASM-AND-OBJ: sfpexexp L1, L2, 2
# CHECK-ASM: encoding: [0x49,0x08,0x00,0xdc]
sfpexexp L1,L2,2

# CHECK-ASM-AND-OBJ: sfpsetexp L1, L2, 5, 1
# CHECK-ASM: encoding: [0x46,0x48,0x01,0x08]
sfpsetexp L1,L2,5,1

# CHECK-ASM-AND-OBJ: sfpdivp2 L1, L2, 3, 1
# CHECK-ASM: encoding: [0x45,0xc8,0x00,0xd8]
sfpdivp2 L1,L2,3,1

# CHECK-ASM-AND-OBJ: sfplut L3, 4
# CHECK-ASM: encoding: [0x01,0x00,0xd0,0xcc]
sfplut L3,4

# CHECK-ASM-AND-OBJ: sfplutfp32 L3, 4
# CHECK-ASM: encoding: [0xd2,0x00,0x00,0x54]
sfplutfp32 L3,4

# CHECK-ASM-AND-OBJ: sfpswap L1, L2, 1
# CHECK-ASM: encoding: [0x46,0x08,0x00,0x48]
sfpswap L1,L2,1

# CHECK-ASM-AND-OBJ: sfptransp
# CHECK-ASM: encoding: [0x02,0x00,0x00,0x30]
sfptransp

# CHECK-ASM-AND-OBJ: sfpshft2 L1, L2, 0, 3
# CHECK-ASM: encoding: [0x4e,0x08,0x00,0x50]
sfpshft2 L1,L2,0,3

# CHECK-ASM-AND-OBJ: sfpshft L1, L2, 3, 1
# CHECK-ASM: encoding: [0x45,0xc8,0x00,0xe8]
sfpshft L1,L2,3,1

# CHECK-ASM-AND-OBJ: sfpconfig 15, 4660, 15
# CHECK-ASM: encoding: [0xfe,0xd3,0x48,0x44]
sfpconfig 15,4660,15

# CHECK-ASM-AND-OBJ: sfpnop
# CHECK-ASM: encoding: [0x02,0x00,0x00,0x3c]
sfpnop

# CHECK-ASM-AND-OBJ: sfpmuli L1, 16256, 0
# CHECK-ASM: encoding: [0x41,0x00,0xfe,0xd0]
sfpmuli L1,0x3f80,0

# CHECK-ASM-AND-OBJ: ttincrwc 0, 2, 4, 0
# CHECK-ASM: encoding: [0x00,0x40,0x02,0xe0]
ttincrwc 0,2,4,0

# CHECK-ASM-AND-OBJ: ttnop
# CHECK-ASM: encoding: [0x00,0x00,0x00,0x08]
ttnop

# CHECK-ASM-AND-OBJ: ttreplay 0, 8, 0, 1
# CHECK-ASM: encoding: [0x04,0x02,0x00,0x10]
ttreplay 0,8,0,1

# CHECK-ASM-AND-OBJ: ttreplay 5, 4, 1, 1
# CHECK-ASM: encoding: [0x0c,0x01,0x05,0x10]
ttreplay 5,4,1,1

# CHECK-ASM-AND-OBJ: sfploadi L0, 65535, 4
# CHECK-ASM: encoding: [0xfd,0xff,0x13,0xc4]
sfploadi L0,65535,4

# CHECK-ASM: .ttinsn 33554432
# CHECK-ASM: encoding: [0x00,0x00,0x00,0x08]
# CHECK-OBJ: ttnop
.ttinsn 0x02000000
