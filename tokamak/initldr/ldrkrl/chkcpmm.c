/**********************************************************
        系统全局内存检查文件chkcpmm.c
**********************************************************/
#include "cmctl.h"

unsigned int acpi_get_bios_ebda()
{
    unsigned int address = *(unsigned short *)0x40E;
    address <<= 4;
    return address;
}

int acpi_checksum(unsigned char *ap, s32_t len)
{
    int sum = 0;
    while (len--)
    {
        sum += *ap++;
    }
    return sum & 0xFF;
}

mrsdp_t *acpi_rsdp_isok(mrsdp_t *rdp)
{
    if (rdp->rp_len == 0 || rdp->rp_revn == 0)
    {
        return NULL;
    }

    if (0 == acpi_checksum((unsigned char *)rdp, (s32_t)rdp->rp_len))
    {
        return rdp;
    }

    return NULL;
}

mrsdp_t *findacpi_rsdp_core(void *findstart, u32_t findlen)
{
    if (NULL == findstart || 1024 > findlen) // findlen 必須要 >= 1024
    {
        return NULL;
    }

    u8_t *tmpdp = (u8_t *)findstart;

    mrsdp_t *retdrp = NULL;
    for (u64_t i = 0; i <= findlen; i++)
    {
        //判斷前8個字節是否是:RSD PTR 
        if (('R' == tmpdp[i]) && ('S' == tmpdp[i + 1]) && ('D' == tmpdp[i + 2]) && (' ' == tmpdp[i + 3]) &&
            ('P' == tmpdp[i + 4]) && ('T' == tmpdp[i + 5]) && ('R' == tmpdp[i + 6]) && (' ' == tmpdp[i + 7]))
        {
            retdrp = acpi_rsdp_isok((mrsdp_t *)(&tmpdp[i]));
            if (NULL != retdrp)
            {
                return retdrp;
            }
        }
    }
    return NULL;
}

PUBLIC mrsdp_t *find_acpi_rsdp()
{
    // 設置 fndp 指向 0x40e0
    void *fndp = (void *)acpi_get_bios_ebda();
    mrsdp_t *rdp = findacpi_rsdp_core(fndp, 1024);
    if (NULL != rdp)
    {
        return rdp;
    }

    // 0E0000h和0FFFFFH
    fndp = (void *)(0xe0000);
    rdp = findacpi_rsdp_core(fndp, (0xfffff - 0xe0000));
    if (NULL != rdp)
    {
        return rdp;
    }

    return NULL;
}

PUBLIC void init_acpi(machbstart_t *mbsp)
{
    mrsdp_t *rdp = NULL;
    rdp = find_acpi_rsdp();
    if (NULL == rdp)
    {
        kerror("Your computer is not support ACPI!!");
    }

    // 將電源管理信息複製到 mbsp 中;
    m2mcopy(rdp, &mbsp->mb_mrsdp, (sint_t)((sizeof(mrsdp_t))));
    if (acpi_rsdp_isok(&mbsp->mb_mrsdp) == NULL)
    {
        kerror("Your computer is not support ACPI!!");
    }

    return;
}

void init_mem(machbstart_t *mbsp)
{
    // 定义在 initldr/ldrkrl/ldrtype.h
    e820map_t *retemp;
    u32_t retemnr = 0;
    mbsp->mb_ebdaphyadr = acpi_get_bios_ebda();

    // 获取内存的结构
    mmap(&retemp, &retemnr);
    if (retemnr == 0)
    {
        kerror("no e820map\n");
    }

    // 0x8000000 = 128M
    // 根据e820map_t结构数据检查内存大小
    if (chk_memsize(retemp, retemnr, 0x100000, 0x8000000) == NULL)
    {
        kerror("Your computer is low on memory, the memory cannot be less than 128MB!");
    }

    mbsp->mb_e820padr = (u64_t)((u32_t)(retemp));    // 把e820map_t结构数组的首地址传给mbsp->mb_e820padr
    mbsp->mb_e820nr = (u64_t)retemnr;                // 把e820map_t结构数组元素个数传给mbsp->mb_e820nr
    mbsp->mb_e820sz = retemnr * (sizeof(e820map_t)); // 把e820map_t结构数组大小传给mbsp->mb_e820sz

    // 将BIOS获取的所有的段的大小累加起来
    mbsp->mb_memsz = get_memsize(retemp, retemnr);   // 根据e820map_t结构数据计算内存大小

#ifdef ACPI_CHECK
    init_acpi(mbsp); // qemu 不支持电源管理,因此就注释掉
#endif

    return;
}

// 检测CPU是否支持长模式
void init_chkcpu(machbstart_t *mbsp)
{
    if (!chk_cpuid())
    {
        kerror("Your CPU is not support CPUID sys is die!");
        CLI_HALT(); // chkcpmm.h 中定义的宏;
    }

    if (!chk_cpu_longmode())
    {
        kerror("Your CPU is not support 64bits mode sys is die!");
        CLI_HALT();
    }
    mbsp->mb_cpumode = 0x40; // 如果成功则设置机器信息结构的cpu模式为64位
    return;
}

// 初始化内核栈
// #define IKSTACK_PHYADR (0x90000-0x10)
// #define IKSTACK_SIZE 0x1000
void init_krlinitstack(machbstart_t *mbsp)
{
    // fs.c 中, move_krlimg() 用于判断给定的内存区域中是否
    // 与 mbsp 中其他部分占用的内存重叠
    if (1 > move_krlimg(mbsp, (u64_t)(0x8f000), 0x1001))
    {
        kerror("iks_moveimg err");
    }
    // 内核栈空间是: 0x8f000 ~ 0x8f000 + 0x1001
    mbsp->mb_krlinitstack = IKSTACK_PHYADR; // 0x90000 - 0x10 是栈底
    mbsp->mb_krlitstacksz = IKSTACK_SIZE;   // 4k大小
    return;
}

// 建立 MMU 页表: TODO
void init_bstartpages(machbstart_t *mbsp)
{
    // KINITPAGE_PHYADR = 0x1000000 16M 地址处
    u64_t *p = (u64_t *)(KINITPAGE_PHYADR); // 顶级页目录

    // 页目录指针
    u64_t *pdpte = (u64_t *)(KINITPAGE_PHYADR + 0x1000);

    // 页目录
    u64_t *pde = (u64_t *)(KINITPAGE_PHYADR + 0x2000);

    // 物理地址从0开始
    u64_t adr = 0;

    // 共有16个页目录, 页目录指针占一页, 顶级页目录占一页
    if (1 > move_krlimg(mbsp, (u64_t)(KINITPAGE_PHYADR),
                        (0x1000 * 16 + 0x2000)))
    {
        kerror("move_krlimg err");
    }

    // PGENTY_SIZE = 512
    // 将顶级页目录、页目录指针的空间清0
    for (uint_t mi = 0; mi < PGENTY_SIZE; mi++)
    {
        p[mi] = 0;
        pdpte[mi] = 0;
    }

    // 映射
    for (uint_t pdei = 0; pdei < 16; pdei++)
    {
        // 大页 KPDE_PS 2MB, 可读写 KPDE_RW, 存在 KPDE_P
        pdpte[pdei] = (u64_t)((u32_t)pde | KPDPTE_RW | KPDPTE_P);
        for (uint_t pdeii = 0; pdeii < PGENTY_SIZE; pdeii++)
        {
            pde[pdeii] = 0 | adr | KPDE_PS | KPDE_RW | KPDE_P;
            adr += 0x200000; // 2M 大小
        }
        pde = (u64_t *)((u32_t)pde + 0x1000);
    }

    // 让顶级页目录中第0项和第((KRNL_VIRTUAL_ADDRESS_START) >> KPML4_SHIFT) & 0x1ff项,指向同一个页目录指针页
    p[((KRNL_VIRTUAL_ADDRESS_START) >> KPML4_SHIFT) & 0x1ff] = (u64_t)((u32_t)pdpte | KPML4_RW | KPML4_P);
    p[0] = (u64_t)((u32_t)pdpte | KPML4_RW | KPML4_P);

    // 把页表首地址保存在机器信息结构中
    mbsp->mb_pml4padr = (u64_t)(KINITPAGE_PHYADR);
    mbsp->mb_subpageslen = (u64_t)(0x1000 * 16 + 0x2000);
    mbsp->mb_kpmapphymemsz = (u64_t)(0x400000000);
    return;
}

// 移动 e820 结构数据信息
void init_meme820(machbstart_t *mbsp)
{
    e820map_t *semp = (e820map_t *)((u32_t)(mbsp->mb_e820padr));
    u64_t senr = mbsp->mb_e820nr;

    e820map_t *demp = (e820map_t *)((u32_t)(mbsp->mb_nextwtpadr));
    if (1 > move_krlimg(mbsp, (u64_t)((u32_t)demp),
                        (senr * (sizeof(e820map_t)))))
    {
        kerror("move_krlimg err");
    }

    m2mcopy(semp, demp, (sint_t)(senr * (sizeof(e820map_t))));
    mbsp->mb_e820padr = (u64_t)((u32_t)(demp));
    mbsp->mb_e820sz = senr * (sizeof(e820map_t));
    mbsp->mb_nextwtpadr = P4K_ALIGN((u32_t)(demp) + (u32_t)(senr * (sizeof(e820map_t))));
    mbsp->mb_kalldendpadr = mbsp->mb_e820padr + mbsp->mb_e820sz;
    return;
}

// 调用BIOS中断, 获取内存结构信息
void mmap(e820map_t **retemp, u32_t *retemnr)
{
    realadr_call_entry(RLINTNR(0), 0, 0);
    *retemnr = *((u32_t *)(E80MAP_NR));
    *retemp = (e820map_t *)(*((u32_t *)(E80MAP_ADRADR)));
    return;
}

// 检查是存在 [sadr, sadr + size] 的连续内存区域
e820map_t *chk_memsize(e820map_t *e8p, u32_t enr, u64_t sadr, u64_t size)
{
    u64_t len = sadr + size;
    if (enr == 0 || e8p == NULL)
    {
        return NULL;
    }

    for (u32_t i = 0; i < enr; i++)
    {
        if (e8p[i].type == RAM_USABLE)
        {
            if ((sadr >= e8p[i].saddr) && (len < (e8p[i].saddr + e8p[i].lsize)))
            {
                return &e8p[i];
            }
        }
    }
    return NULL;
}

// 将所有的内存区域块的大小累加
u64_t get_memsize(e820map_t *e8p, u32_t enr)
{
    u64_t len = 0;
    if (enr == 0 || e8p == NULL)
    {
        return 0;
    }

    for (u32_t i = 0; i < enr; i++)
    {
        if (e8p[i].type == RAM_USABLE)
        {
            len += e8p[i].lsize;
        }
    }
    return len;
}

// 通过改写 eflags 寄存器的第 21 位, 观察其位的变化判断是否支持 CPUID
// 如果支持 cpuid 指令, 就返回1; 不支持返回 0
int chk_cpuid()
{
    int rets = 0;
    __asm__ __volatile__(
        "pushfl \n\t"
        "popl %%eax \n\t"
        "movl %%eax, %%ebx \n\t"
        "xorl $0x0200000, %%eax \n\t"
        "pushl %%eax \n\t"
        "popfl \n\t"
        "pushfl \n\t"
        "popl %%eax \n\t"
        "xorl %%ebx, %%eax \n\t"
        "jz 1f \n\t"
        "movl $1, %0 \n\t"
        "jmp 2f \n\t"
        "1: movl $0, %0 \n\t"
        "2: \n\t"
        : "=c"(rets)
        :
        :);
    return rets;
}

// 检查 CPU 是否支持长模式
int chk_cpu_longmode()
{
    int rets = 0;
    __asm__ __volatile__(
        "movl $0x80000000, %%eax \n\t"
        "cpuid \n\t"                   // 把eax中放入0x80000000调用CPUID指令
        "cmpl $0x80000001, %%eax \n\t" // 看eax中返回结果
        "setnb %%al \n\t"              // 不为0x80000001, 则不支持0x80000001号功
        "jb 1f \n\t"
        "movl $0x80000001, %%eax \n\t"
        "cpuid \n\t"          // 把eax中放入0x800000001调用CPUID指令, 检查edx中的返回数据
        "bt $29, %%edx  \n\t" // 将 edx 的第29位复制到CF标志中, long mode  support 位
        //"setcb %%al \n\t"
        "setc %%al \n\t" // TODO: setcb 与 setc 的区别
        "1: \n\t"
        "movzx %%al, %%eax \n\t" // 进行0扩展
        : "=a"(rets)
        :
        :);
    return rets;
}

void init_chkmm()
{
    e820map_t *map = (e820map_t *)EMAP_PTR;
    u16_t *map_nr = (u16_t *)EMAP_NR_PTR;
    u64_t mmsz = 0;

    for (int j = 0; j < (*map_nr); j++)
    {
        if (map->type == RAM_USABLE)
        {
            mmsz += map->lsize;
        }
        map++;
    }

    if (mmsz < BASE_MEM_SZ)
    {
        kprint("Your computer is low on memory, the memory cannot be less than 64MB!");
        CLI_HALT();
    }

    if (!chk_cpuid())
    {
        kprint("Your CPU is not support CPUID sys is die!");
        CLI_HALT();
    }

    if (!chk_cpu_longmode())
    {
        kprint("Your CPU is not support 64bits mode sys is die!");
        CLI_HALT();
    }
    ldr_createpage_and_open();

    return;
}

void out_char(char *c)
{
    char *str = c, *p = (char *)0xb8000;

    while (*str)
    {
        *p = *str;
        p += 2;
        str++;
    }

    return;
}

void init_bstartpagesold(machbstart_t *mbsp)
{
    if (1 > move_krlimg(mbsp, (u64_t)(PML4T_BADR), 0x3000))
    {
        kerror("ip_moveimg err");
    }

    pt64_t *pml4p = (pt64_t *)PML4T_BADR, *pdptp = (pt64_t *)PDPTE_BADR, *pdep = (pt64_t *)PDE_BADR; //*ptep=(pt64_t*)PTE_BADR;
    for (int pi = 0; pi < PG_SIZE; pi++)
    {
        pml4p[pi] = 0;
        pdptp[pi] = 0;

        pdep[pi] = 0;
    }

    pml4p[0] = 0 | PDPTE_BADR | PDT_S_RW | PDT_S_PNT;
    pdptp[0] = 0 | PDE_BADR | PDT_S_RW | PDT_S_PNT;
    pml4p[256] = 0 | PDPTE_BADR | PDT_S_RW | PDT_S_PNT;

    pt64_t tmpba = 0, tmpbd = 0 | PDT_S_SIZE | PDT_S_RW | PDT_S_PNT;

    for (int di = 0; di < PG_SIZE; di++)
    {
        pdep[di] = tmpbd;
        tmpba += 0x200000;
        tmpbd = tmpba | PDT_S_SIZE | PDT_S_RW | PDT_S_PNT;
    }
    mbsp->mb_pml4padr = (u64_t)((u32_t)pml4p);
    mbsp->mb_subpageslen = 0x3000;
    mbsp->mb_kpmapphymemsz = (0x200000 * 512);
    return;
}

void ldr_createpage_and_open()
{
    pt64_t *pml4p = (pt64_t *)PML4T_BADR, *pdptp = (pt64_t *)PDPTE_BADR, *pdep = (pt64_t *)PDE_BADR;
    for (int pi = 0; pi < PG_SIZE; pi++)
    {
        pml4p[pi] = 0;
        pdptp[pi] = 0;
        pdep[pi] = 0;
    }

    pml4p[0] = 0 | PDPTE_BADR | PDT_S_RW | PDT_S_PNT;
    pdptp[0] = 0 | PDE_BADR | PDT_S_RW | PDT_S_PNT;

    pml4p[256] = 0 | PDPTE_BADR | PDT_S_RW | PDT_S_PNT;

    pt64_t tmpba = 0, tmpbd = 0 | PDT_S_SIZE | PDT_S_RW | PDT_S_PNT;

    for (int di = 0; di < PG_SIZE; di++)
    {
        pdep[di] = tmpbd;
        tmpba += 0x200000;
        tmpbd = tmpba | PDT_S_SIZE | PDT_S_RW | PDT_S_PNT;
    }
    return;
}
