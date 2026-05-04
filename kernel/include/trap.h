struct pt_regs {
    // General Register (Offset: 0 ~ 30)
    unsigned long ra;     // 8 * 0
    unsigned long sp;     // 8 * 1 User SP
    unsigned long gp;     // 8 * 2
    unsigned long tp;     // 8 * 3
    unsigned long t0;     // 8 * 4
    unsigned long t1;     // 8 * 5
    unsigned long t2;     // 8 * 6
    unsigned long s0;     // 8 * 7
    unsigned long s1;     // 8 * 8
    unsigned long a0;     // 8 * 9
    unsigned long a1;     // 8 * 10
    unsigned long a2;     // 8 * 11
    unsigned long a3;     // 8 * 12
    unsigned long a4;     // 8 * 13
    unsigned long a5;     // 8 * 14
    unsigned long a6;     // 8 * 15
    unsigned long a7;     // 8 * 16
    unsigned long s2;     // 8 * 17
    unsigned long s3;     // 8 * 18
    unsigned long s4;     // 8 * 19
    unsigned long s5;     // 8 * 20
    unsigned long s6;     // 8 * 21
    unsigned long s7;     // 8 * 22
    unsigned long s8;     // 8 * 23
    unsigned long s9;     // 8 * 24
    unsigned long s10;    // 8 * 25
    unsigned long s11;    // 8 * 26
    unsigned long t3;     // 8 * 27
    unsigned long t4;     // 8 * 28
    unsigned long t5;     // 8 * 29
    unsigned long t6;     // 8 * 30

    // CSR (Offset: 31 ~ 34)
    unsigned long sepc;   // 8 * 31
    unsigned long sstatus;// 8 * 32
    unsigned long scause; // 8 * 33
    unsigned long stval;  // 8 * 34
};

void do_trap(struct pt_regs* regs);
