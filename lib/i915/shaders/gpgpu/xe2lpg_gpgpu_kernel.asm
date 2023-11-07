L0:
         mov (4|M0)               r1.0<1>:ub    r1.0<0;1,0>:ub                        // Load r1.0-3 with color byte
         shl (1|M0)               r2.0<1>:ud    r0.1<0;1,0>:ud    0x4:ud              // Load r2.0-3 with tg id X << 4
         mov (1|M0)               r2.1<1>:ud    r0.6<0;1,0>:ud                        // Load r2.4-7 with tg id Y

         // payload setup
         mov (16|M0)              r4.0<1>:ud    0x0:ud                                // Zero out register R4
         mov (2|M0)               r4.5<1>:ud    r2.0<2;2,1>:ud                        // Store X and Y block start (160:191 and 192:223)
         mov (1|M0)               r4.14<1>:w    0xF:w                                 // Store X and Y block size (224:231 and 232:239)
         mov (16|M0)              r5.0<1>:ud    r1.0<0;1,0>:ud                        // Load r5-r6 with color byte

         send.tgm (16|M0)         null     r4    null:0    0x0    0x64000007          // Send TypedStore2DBlock to tgm port
         send.gtwy (8|M0)         null    r80    null:0    0x0    0x02000000 {EOT}
