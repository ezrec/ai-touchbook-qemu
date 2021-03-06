/*
 * QEMU USB EHCI Emulation
 * 
 * Copyright(c) 2008  Emutex Ltd. (mark.burkley@emutex.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or(at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * TODO:
 *  o Downstream port handoff
 */

#include "hw.h"
#include "qemu-timer.h"
#include "usb.h"
#include "pci.h"
#include "omap.h"

// #define DEBUG(x) timed_printf x
// #define DEBUG(x) printf x
#define DEBUG(x)
// #define TDEBUG
// #define DEBUG_PACKET

#define ASSERT(x) { if (!(x)) { printf("Assertion failed in usb-echi.c line %d\n", __LINE__); exit(1); } }

#define MMIO_SIZE        0x1000

#define CAPREGBASE         0x0000        // Capability Registers Base Address

#define CAPLENGTH        CAPREGBASE + 0x0000
#define HCIVERSION        CAPREGBASE + 0x0002
#define HCSPARAMS        CAPREGBASE + 0x0004
#define HCCPARAMS        CAPREGBASE + 0x0008
#define EECP                HCCPARAMS + 1
#define HCSPPORTROUTE1        CAPREGBASE + 0x000c
#define HCSPPORTROUTE2        CAPREGBASE + 0x0010

#define OPREGBASE         0x0020        // Operational Registers Base Address

#define USBCMD                 OPREGBASE + 0x0000
#define USBCMD_RUNSTOP         (1 << 0)        // run / Stop
#define USBCMD_HCRESET         (1 << 1)        // HC Reset
#define USBCMD_FLS         (3 << 2)        // Frame List Size
#define USBCMD_FLS_SH        2                // Frame List Size Shift
#define USBCMD_PSE         (1 << 4)        // Periodic Schedule Enable
#define USBCMD_ASE         (1 << 5)        // Asynch Schedule Enable
#define USBCMD_IAAD         (1 << 6)        // Int Asynch Advance Doorbell
#define USBCMD_LHCR         (1 << 7)        // Light Host Controller Reset
#define USBCMD_ASPMC         (3 << 8)        // Async Sched Park Mode Count
#define USBCMD_ASPME         (1 << 11)        // Async Sched Park Mode Enable
#define USBCMD_ITC         (0x7f << 16)        // Int Threshold Control
#define USBCMD_ITC_SH         16                // Int Threshold Control Shift

#define USBSTS                 OPREGBASE + 0x0004
#define USBSTS_RO_MASK        0x0000003f
#define USBSTS_INT         (1 << 0)        // USB Interrupt
#define USBSTS_ERRINT         (1 << 1)        // Error Interrupt
#define USBSTS_PCD         (1 << 2)        // Port Change Detect
#define USBSTS_FLR         (1 << 3)        // Frame List Rollover
#define USBSTS_HSE         (1 << 4)        // Host System Error
#define USBSTS_IAA         (1 << 5)        // Interrupt Async Advance
#define USBSTS_HALT         (1 << 12)        // HC Halted
#define USBSTS_REC         (1 << 13)        // Reclamation
#define USBSTS_PSS         (1 << 14)        // Periodic Schedule Status
#define USBSTS_ASS         (1 << 15)        // Asynchronous Schedule Status

/*
 *  Interrupt enable bits correspond to the interrupt active bits in USBSTS
 *  so no need to redefine here.
 */
#define USBINTR                 OPREGBASE + 0x0008
#define USBINTR_MASK                0x0000003f

#define FRINDEX                 OPREGBASE + 0x000c
#define CTRLDSSEGMENT                 OPREGBASE + 0x0010
#define PERIODICLISTBASE        OPREGBASE + 0x0014
#define ASYNCLISTADDR                OPREGBASE + 0x0018
#define ASYNCLISTADDR_MASK        0xffffffe0

#define CONFIGFLAG                OPREGBASE + 0x0040

#define PORTSC                         (OPREGBASE + 0x0044)
#define PORTSC_BEGIN                PORTSC
#define PORTSC_END                (PORTSC + 4 * NB_PORTS)
/*
 * Bits that are reserverd or are read-only are masked out of values
 * written to us by software 
 */
#define PORTSC_RO_MASK        0x007021c5
#define PORTSC_RWC_MASK        0x0000002a
#define PORTSC_WKOC_E        (1 << 22)        // Wake on Over Current Enable
#define PORTSC_WKDS_E        (1 << 21)        // Wake on Disconnect Enable
#define PORTSC_WKCN_E        (1 << 20)        // Wake on Connect Enable
#define PORTSC_PTC        (15 << 16)        // Port Test Control
#define PORTSC_PTC_SH   16                // Port Test Control shift
#define PORTSC_PIC        (3 << 14)        // Port Indicator Control
#define PORTSC_PIC_SH   14                // Port Indicator Control Shift
#define PORTSC_POWNER        (1 << 13)        // Port Owner
#define PORTSC_PPOWER         (1 << 12)        // Port Power
#define PORTSC_LINESTAT        (3 << 10)        // Port Line Status
#define PORTSC_LINESTAT_SH   10                // Port Line Status Shift
#define PORTSC_PRESET        (1 << 8)        // Port Reset
#define PORTSC_SUSPEND        (1 << 7)        // Port Suspend
#define PORTSC_FPRES        (1 << 6)        // Force Port Resume
#define PORTSC_OCC        (1 << 5)        // Over Current Change
#define PORTSC_OCA        (1 << 4)        // Over Current Active
#define PORTSC_PEDC        (1 << 3)        // Port Enable/Disable Change
#define PORTSC_PED        (1 << 2)        // Port Enable/Disable
#define PORTSC_CSC        (1 << 1)        // Connect Status Change
#define PORTSC_CONNECT        (1 << 0)        // Current Connect Status

#define EHCI_NOMICROFRAMES

#ifdef EHCI_NOMICROFRAMES
#define FRAME_TIMER_FREQ 2000
#else
#define FRAME_TIMER_FREQ 8000
#endif

#define NB_MAXINTRATE        8        // Max rate at which controller issues ints
#define NB_PORTS         4        // Number of downstream ports
#define BUFF_SIZE        20480        // Max bytes to transfer per transaction
#define MAX_ITERATIONS        1000        // Max number of states before we abort
#define MAX_QH                1000        // Max allowable queue heads in a chain

/*  Internal periodic / asynchronous schedule state machine states
 */
typedef enum {
    EST_INACTIVE = 1000,
    EST_ACTIVE,
    EST_EXECUTING,
    EST_SLEEPING,
    /*  The following states are internal to the state machine function
    */
    EST_WAITLISTHEAD,
    EST_DORELOAD,
    EST_WAITSTARTEVENT,
    EST_FETCHENTRY,
    EST_FETCHQH,
    EST_FETCHITD,
    EST_ADVANCEQUEUE,
    EST_FETCHQTD,
    EST_EXECUTE,
    EST_WRITEBACK,
    EST_HORIZONTALQH
} EHCI_STATES;

/*  EHCI spec version 1.0 Section 3.3
 */
typedef struct EHCIitd {
    uint32_t next;
#define NLPTR_GET(x)                ((x) & 0xffffffe0)
#define NLPTR_TYPE_GET(x)        (((x) >> 1) & 3)
#define NLPTR_TYPE_ITD                0
#define NLPTR_TYPE_QH                1
#define NLPTR_TYPE_STITD        2
#define NLPTR_TYPE_FSTN                3
#define NLPTR_TBIT(x)                ((x) & 1)

    uint32_t transact[8];
#define ITD_XACT_ACTIVE                        (1 << 31)
#define ITD_XACT_DBERROR                (1 << 30)
#define ITD_XACT_BABBLE                        (1 << 29)
#define ITD_XACT_XACTERR                (1 << 28)
#define ITD_XACT_LENGTH_MASK                0x0fff0000
#define ITD_XACT_LENGTH_SH                16
#define ITD_XACT_IOC                        (1 << 15)
#define ITD_XACT_PGSEL_MASK                0x00007000
#define ITD_XACT_PGSEL_SH                12
#define ITD_XACT_OFFSET_MASK                0x00000fff

    uint32_t bufptr[7];
#define ITD_BUFPTR_MASK                        0xfffff000
#define ITD_BUFPTR_SH                        12
#define ITD_BUFPTR_EP_MASK                0x00000f00
#define ITD_BUFPTR_EP_SH                8
#define ITD_BUFPTR_DEVADDR_MASK                0x0000007f
#define ITD_BUFPTR_DIRECTION                (1 << 11)
#define ITD_BUFPTR_MAXPKT_MASK                0x000007ff
#define ITD_BUFPTR_MULT_MASK                0x00000003
} EHCIitd;

/*  EHCI spec version 1.0 Section 3.4
 */
typedef struct EHCIsitd {
    uint32_t next;                        // Standard next link pointer
    uint32_t epchar;
#define SITD_EPCHAR_IO                        (1 << 31)
#define SITD_EPCHAR_PORTNUM_MASK        0x7f000000
#define SITD_EPCHAR_PORTNUM_SH                24
#define SITD_EPCHAR_HUBADD_MASK                0x007f0000
#define SITD_EPCHAR_HUBADDR_SH                16
#define SITD_EPCHAR_EPNUM_MASK                0x00000f00
#define SITD_EPCHAR_EPNUM_SH                8
#define SITD_EPCHAR_DEVADDR_MASK        0x0000007f
    
    uint32_t uframe;
#define SITD_UFRAME_CMASK_MASK                0x0000ff00
#define SITD_UFRAME_CMASK_SH                8
#define SITD_UFRAME_SMASK_MASK                0x000000ff

    uint32_t results;
#define SITD_RESULTS_IOC                (1 << 31)
#define SITD_RESULTS_PGSEL                (1 << 30)
#define SITD_RESULTS_TBYTES_MASK        0x03ff0000
#define SITD_RESULTS_TYBYTES_SH                16
#define SITD_RESULTS_CPROGMASK_MASK        0x0000ff00
#define SITD_RESULTS_CPROGMASK_SH        8
#define SITD_RESULTS_ACTIVE                (1 << 7)
#define SITD_RESULTS_ERR                (1 << 6)
#define SITD_RESULTS_DBERR                (1 << 5)
#define SITD_RESULTS_BABBLE                (1 << 4)
#define SITD_RESULTS_XACTERR                (1 << 3)
#define SITD_RESULTS_MISSEDUF                (1 << 2)
#define SITD_RESULTS_SPLITXSTATE        (1 << 1)

    uint32_t bufptr[2];
#define BUFPTR_MASK                        0xfffff000
#define BUFPTR_CURROFF_MASK                0x00000fff
#define BUFPTR_TPOS_MASK                0x00000018
#define BUFPTR_TPOS_SH                        3
#define BUFPTR_TCNT_MASK                0x00000007

    uint32_t backptr;                        // Standard next link pointer
} EHCIsitd;

/*  EHCI spec version 1.0 Section 3.5
 */
typedef struct EHCIqtd {
    uint32_t next;                        // Standard next link pointer
    uint32_t altnext;                        // Standard next link pointer
    uint32_t token;
#define QTD_TOKEN_DTOGGLE                (1 << 31)
#define QTD_TOKEN_TBYTES_MASK                0x7fff0000
#define QTD_TOKEN_TBYTES_SH                16
#define QTD_TOKEN_IOC                        (1 << 15)
#define QTD_TOKEN_CPAGE_MASK                0x00007000
#define QTD_TOKEN_CPAGE_SH                12
#define QTD_TOKEN_CERR_MASK                0x00000c00
#define QTD_TOKEN_CERR_SH                10
#define QTD_TOKEN_PID_MASK                0x00000300
#define QTD_TOKEN_PID_SH                8
#define QTD_TOKEN_ACTIVE                (1 << 7)
#define QTD_TOKEN_HALT                        (1 << 6)
#define QTD_TOKEN_DBERR                        (1 << 5)
#define QTD_TOKEN_BABBLE                (1 << 4)
#define QTD_TOKEN_XACTERR                (1 << 3)
#define QTD_TOKEN_MISSEDUF                (1 << 2)
#define QTD_TOKEN_SPLITXSTATE                (1 << 1)
#define QTD_TOKEN_PING                        (1 << 0)

    uint32_t bufptr[5];                        // Standard buffer pointer
} EHCIqtd;

/*  EHCI spec version 1.0 Section 3.6
 */
typedef struct EHCIqh {
    uint32_t next;                        // Standard next link pointer

    uint32_t epchar;
#define QH_EPCHAR_RL_MASK                0xf0000000
#define QH_EPCHAR_RL_SH                        28
#define QH_EPCHAR_C                        (1 << 27)
#define QH_EPCHAR_MPLEN_MASK                0x07FF0000
#define QH_EPCHAR_MPLEN_SH                16
#define QH_EPCHAR_H                        (1 << 15)
#define QH_EPCHAR_DTC                        (1 << 14)
#define QH_EPCHAR_EPS_MASK                0x00003000
#define QH_EPCHAR_EPS_SH                12
#define QH_EPCHAR_EP_MASK                0x00000f00
#define QH_EPCHAR_EP_SH                        8
#define QH_EPCHAR_I                        (1 << 7)
#define QH_EPCHAR_DEVADDR_MASK                0x0000007f

    uint32_t epcap;
#define QH_EPCAP_MULT_MASK                0xc0000000
#define QH_EPCAP_MULT_SH                30
#define QH_EPCAP_PORTNUM_MASK                0x3f800000
#define QH_EPCAP_PORTNUM_SH                23
#define QH_EPCAP_HUBADDR_MASK                0x007f0000
#define QH_EPCAP_HUBADDR_SH                16
#define QH_EPCAP_CMASK_MASK                0x0000ff00
#define QH_EPCAP_CMASK_SH                8
#define QH_EPCAP_SMASK_MASK                0x000000ff

    uint32_t current;                        // Standard next link pointer
    uint32_t qtdnext;                        // Standard next link pointer
    uint32_t altnext;
#define QH_ALTNEXT_NAKCNT_MASK                0x0000001e
#define QH_ALTNEXT_NAKCNT_SH                1

    uint32_t token;                        // Same as QTD token
    uint32_t bufptr[5];                        // Standard buffer pointer
#define BUFPTR_CPROGMASK_MASK                0x000000ff
#define BUFPTR_FRAMETAG_MASK                0x0000001f
#define BUFPTR_SBYTES_MASK                0x00000fe0
#define BUFPTR_SBYTES_SH                5
} EHCIqh;

/*  EHCI spec version 1.0 Section 3.7
 */
typedef struct EHCIfstn {
    uint32_t next;                        // Standard next link pointer
    uint32_t backptr;                        // Standard next link pointer
} EHCIfstn;

typedef struct {
    PCIDevice dev;
    qemu_irq irq;
    int mem;
    int num_ports;
    /*  
     *  EHCI spec version 1.0 Section 2.3
     *  Host Controller Operational Registers
     */
    union {
        uint8_t mmio[MMIO_SIZE];
        struct {
            uint8_t cap[OPREGBASE];
            uint32_t usbcmd;
            uint32_t usbsts;
            uint32_t usbintr;
            uint32_t frindex;
            uint32_t ctrldssegment;
            uint32_t periodiclistbase;
            uint32_t asynclistaddr;
            uint32_t notused[9];
            uint32_t configflag;
            uint32_t portsc[NB_PORTS];
        };
    };
    /*
     *  Internal states, shadow registers, etc
     */
    uint32_t sofv;
    QEMUTimer *frame_timer;
    int attach_poll_counter;
    int astate;                        // Current state in asynchronous schedule
    int pstate;                        // Current state in periodic schedule
    USBPort ports[NB_PORTS];
    unsigned char buffer[BUFF_SIZE];
    EHCIqh qh;
    EHCIqtd qtd;
    USBPacket usb_packet;
    int async_port_in_progress;
    int async_complete;
    uint32_t qhaddr;
    uint32_t itdaddr;
    uint32_t qtdaddr;
    uint32_t tbytes;
    int pid;
    int exec_status;
    int isoch_pause;
    uint32_t last_run_usec;
    uint32_t frame_end_usec;
} EHCIState;

static inline uint32_t get_field(uint32_t data, uint32_t mask, int shift)
{
    return((data & mask) >> shift);
}

static inline void set_field(uint32_t *data, uint32_t val, 
                              uint32_t mask, int shift)
{
    *data &= ~mask;
    *data |=(val << shift);
}

static void dump_ptr(const char *s, off_t ptr, int has_type)
{
    int t = NLPTR_TYPE_GET(ptr);

    printf("%s%08llX", s, (unsigned long long)NLPTR_GET(ptr));
    
    if (has_type) {
        printf("(PTR type is %s)", 
            t == NLPTR_TYPE_ITD ? "ITD" :
           (t == NLPTR_TYPE_QH ? "QH" :
           (t == NLPTR_TYPE_STITD ? "STITD" :
           (t == NLPTR_TYPE_FSTN ? "FSTN" : "****"))));
    }
        
    printf("%s\n", NLPTR_TBIT(ptr) ? " TBIT set" : "");
}

static void dump_qtd(EHCIqtd *qtd, uint32_t qtdaddr)
{
    int pid;

    pid =(qtd->token & QTD_TOKEN_PID_MASK) >> QTD_TOKEN_PID_SH;
    
    printf("    QTD analysis      %08X\n"
            "    === ========      ========\n", qtdaddr);

    printf("    NakCnt:           %d\n", 
             (qtd->altnext & QH_ALTNEXT_NAKCNT_MASK) >> QH_ALTNEXT_NAKCNT_SH);
    dump_ptr("    Next:             ", qtd->next, 0);
    dump_ptr("    Alternate:        ", qtd->altnext, 0);
    printf("    Data Toggle:      %s        ", 
              qtd->token & QTD_TOKEN_DTOGGLE ? "Yes " : "No  ");
    printf("    Total Bytes:      %d\n", 
             (qtd->token & QTD_TOKEN_TBYTES_MASK) >> QTD_TOKEN_TBYTES_SH);
    printf("    IOC:              %s        ", 
              qtd->token & QTD_TOKEN_IOC ? "Yes " : "No  ");
    printf("    C_Page:           %d\n", 
             (qtd->token & QTD_TOKEN_CPAGE_MASK) >> QTD_TOKEN_CPAGE_SH);
    printf("    CErr:             %-4d        ", 
             (qtd->token & QTD_TOKEN_CERR_MASK) >> QTD_TOKEN_CERR_SH);
    printf("    PID code:         %s\n", 
              pid == 0 ? "OUT" :
             (pid == 1 ? "IN" :
             (pid == 2 ? "SETUP" : "****")));
    printf("    Flags:            %s%s%s%s%s%s%s%s\n", 
              qtd->token & QTD_TOKEN_ACTIVE ? "ACTIVE " : "",
              qtd->token & QTD_TOKEN_HALT ? "HALT " : "",
              qtd->token & QTD_TOKEN_DBERR ? "DBERR " : "",
              qtd->token & QTD_TOKEN_BABBLE ? "BABBLE " : "",
              qtd->token & QTD_TOKEN_XACTERR ? "XACTERR " : "",
              qtd->token & QTD_TOKEN_MISSEDUF ? "MISSEDUF " : "",
              qtd->token & QTD_TOKEN_SPLITXSTATE ? "SPLITXSTATE " : "",
              qtd->token & QTD_TOKEN_PING ? "PING " : "");
    printf("    Current Offset    %d\n", 
              qtd->bufptr[0] & BUFPTR_CURROFF_MASK);
    printf("    === ========      ========\n");
}

static void dump_qh(EHCIqh *qh, uint32_t qhaddr)
{
    int speed =(qh->epchar & QH_EPCHAR_EPS_MASK) >> QH_EPCHAR_EPS_SH;

    printf("QH analysis       %08X\n"
            "== ========       ========\n", qhaddr);

    dump_ptr("Horizontal:       ", qh->next, 1);
    printf("Nak Count Reload: %d\n", 
           (qh->epchar & QH_EPCHAR_RL_MASK) >> QH_EPCHAR_RL_SH);
    printf("Max Pkt Len:      %d\n", 
           (qh->epchar & QH_EPCHAR_MPLEN_MASK) >> QH_EPCHAR_MPLEN_SH);
    printf("Control Endpoint: %s        ", 
           (qh->epchar & QH_EPCHAR_C) ? "Yes " : "No  ");
    printf("H-bit:            %s\n", 
           (qh->epchar & QH_EPCHAR_H) ? "Yes " : "No  ");
    printf("DTC:              %s        ", 
           (qh->epchar & QH_EPCHAR_DTC) ? "Yes " : "No  ");
    printf("EndPoint Speed:   %s\n", 
            speed == 0 ? "Full" :
           (speed == 1 ? "Low " :
           (speed == 2 ? "High" : "****")));
    printf("EndPoint:         %-4d        ", 
           (qh->epchar & QH_EPCHAR_EP_MASK) >> QH_EPCHAR_EP_SH);
    printf("Inactive on next: %s\n", 
           (qh->epchar & QH_EPCHAR_I) ? "Yes" : "No");
    printf("DevAddr:          %-4d        ", 
             qh->epchar & QH_EPCHAR_DEVADDR_MASK);
    printf("Mult:             %-4d\n", 
           (qh->epcap & QH_EPCAP_MULT_MASK) >> QH_EPCAP_MULT_SH);
    printf("PortNum:          %-4d        ", 
           (qh->epcap & QH_EPCAP_PORTNUM_MASK) >> QH_EPCAP_PORTNUM_SH);
    printf("HubAddr:          %d\n", 
           (qh->epcap & QH_EPCAP_HUBADDR_MASK) >> QH_EPCAP_HUBADDR_SH);
    printf("C-mask:           %-4d        ", 
           (qh->epcap & QH_EPCAP_CMASK_MASK) >> QH_EPCAP_CMASK_SH);
    printf("S-mask:           %d\n", 
              qh->epcap & QH_EPCAP_SMASK_MASK);
    dump_ptr("Current:          ", qh->current, 0);

    dump_qtd((EHCIqtd *)&qh->qtdnext, qhaddr + 16);

    printf("C-prog mask:      %d\n", 
              qh->bufptr[1] & BUFPTR_CPROGMASK_MASK);
    printf("S-bytes:          %d\n", 
              qh->bufptr[2] & BUFPTR_FRAMETAG_MASK);
    printf("FrameTag:         %d\n", 
             (qh->bufptr[2] & BUFPTR_SBYTES_MASK) >> BUFPTR_SBYTES_SH);
    printf("== ========       ========\n");
}

#ifdef DEBUG_PACKET

static void dump_itd(EHCIitd *itd, uint32_t addr)
{
    int i;

    printf("ITD analysis       %08X\n"
            "=== ========       ========\n", addr);

    dump_ptr("Horizontal:       ", itd->next, 1);

    for(i = 0; i < 8; i++) {
        printf("Trans Desc %d, len %5d, off %03X, page sel %d, ioc:%s ",
                  i,
                  get_field(itd->transact[i], ITD_XACT_LENGTH_MASK,
                             ITD_XACT_LENGTH_SH),
                  get_field(itd->transact[i], ITD_XACT_OFFSET_MASK, 0),
                  get_field(itd->transact[i], ITD_XACT_PGSEL_MASK,
                             ITD_XACT_PGSEL_SH),
                  itd->transact[i] & ITD_XACT_IOC ? "Yes" : "No ");

        if (itd->transact[i] & ITD_XACT_ACTIVE)
            printf("ACTIVE ");

        if (itd->transact[i] & ITD_XACT_DBERROR)
            printf("DATAERR ");

        if (itd->transact[i] & ITD_XACT_BABBLE)
            printf("BABBLE ");

        if (itd->transact[i] & ITD_XACT_XACTERR)
            printf("XACTERR ");

        printf("\n");
    }

    printf("Device:     %d\n",
            get_field(itd->bufptr[0], ITD_BUFPTR_DEVADDR_MASK, 0));

    printf("Endpoint:   %d\n",
            get_field(itd->bufptr[0], ITD_BUFPTR_EP_MASK, ITD_BUFPTR_EP_SH));

    printf("Direction:  %s\n",
            itd->bufptr[1] & ITD_BUFPTR_DIRECTION ? "IN" : "OUT");

    printf("Max Packet: %d\n",
            get_field(itd->bufptr[1], ITD_BUFPTR_MAXPKT_MASK, 0));

    printf("Mult:       %d\n",
            get_field(itd->bufptr[2], ITD_BUFPTR_MULT_MASK, 0));

    for(i = 0; i < 7; i++)
        printf("Buf Ptr %d: %05X\n", i, itd->bufptr[i] >> 12);
}

#endif

static inline int timed_printf(char *fmt, ...)
{
    int msec, usec;
    static int usec_last;
    va_list ap;

    usec = qemu_get_clock(vm_clock) / 1000;

    msec = usec - usec_last;
    usec_last = usec;
    usec = msec;

    msec /= 1000;
    msec %= 1000;

    usec %= 1000;

    va_start(ap, fmt);
    printf("%03d.%03d ", msec, usec);
    vprintf(fmt, ap);
    va_end(ap);

    return 0;
}

static void inline ehci_set_interrupt(EHCIState *ehci, int intr)
{
    int level = 0;

    // TODO honour interrupt threshold requests

    ehci->usbsts |= intr;

    if ((ehci->usbsts & USBINTR_MASK) & ehci->usbintr)
        level = 1;

#ifdef TDEBUG
    DEBUG(("ehci: setting interrupt level %d(st/en)=(%02X/%02X)\n", level,
            ehci->usbsts & USBINTR_MASK, ehci->usbintr));
#endif
    qemu_set_irq(ehci->irq, level);
}

/* Attach or detach a device on root hub */

static void ehci_attach(USBPort *port, USBDevice *dev)
{
    EHCIState *s = port->opaque;
    uint32_t *portsc = &s->portsc[port->index];

    if (dev) {
        if (port->dev) {
            usb_attach(port, NULL);
        }

        *portsc |= PORTSC_CONNECT;

        usb_send_msg(dev, USB_MSG_ATTACH);
        port->dev = dev;
    } else {
        *portsc &= ~PORTSC_CONNECT;

        if (port->dev) {
            dev = port->dev;
            usb_send_msg(dev, USB_MSG_DETACH);
        }

        port->dev = NULL;
    }

    *portsc |= PORTSC_CSC;
    
    /* 
     *  If a high speed device is attached then we own this port(indicated
     *  by zero in the PORTSC_POWNER bit field) so set the status bit
     *  and set an interrupt if enabled.
     */
    if ( !(*portsc & PORTSC_POWNER)) {
        ehci_set_interrupt(s, USBSTS_PCD);
    }
}

static void ehci_reset(EHCIState *s)
{
    uint8_t *pci_conf;
    int i;

    pci_conf = s->dev.config;

    memset(&s->mmio[OPREGBASE], 0x00, MMIO_SIZE - OPREGBASE);

    s->usbcmd = NB_MAXINTRATE << USBCMD_ITC_SH;
    s->usbsts = USBSTS_HALT;

    s->astate = EST_INACTIVE;
    s->pstate = EST_INACTIVE;
    s->async_port_in_progress = -1;
    s->async_complete = 0;
    s->isoch_pause = -1;
    s->attach_poll_counter = 0;

    for(i = 0; i < NB_PORTS; i++) {
        s->portsc[i] = PORTSC_POWNER | PORTSC_PPOWER;

        if (s->ports[i].dev)
            ehci_attach(&s->ports[i], s->ports[i].dev);
    }
}

static uint32_t ehci_mem_readb(void *ptr, target_phys_addr_t addr)
{
    EHCIState *s = ptr;
    uint32_t val;

    val = s->mmio[addr];

    DEBUG(("mem_readb : addr=0x%08X, val=%02X\n",(int)addr,(int)val));

    return val;
}

static uint32_t ehci_mem_readw(void *ptr, target_phys_addr_t addr)
{
    EHCIState *s = ptr;
    uint32_t val;

    val = s->mmio[addr] |(s->mmio[addr+1] << 8);

    DEBUG(("mem_readw : addr=0x%08X, val=0x%04X\n",(int)addr,(int)val));

    return val;
}

static uint32_t ehci_mem_readl(void *ptr, target_phys_addr_t addr)
{
    EHCIState *s = ptr;
    uint32_t val;


    val = s->mmio[addr] |(s->mmio[addr+1] << 8) |
        (s->mmio[addr+2] << 16) |(s->mmio[addr+3] << 24);

    DEBUG(("mem_readl : addr=0x%08X, val=0x%08X\n",(unsigned) addr, val));

    return val;
}

static void ehci_mem_writeb(void *ptr, target_phys_addr_t addr, uint32_t val)
{
    printf("EHCI doesn't handle byte writes to MMIO\n");
    exit(1);
}

static void ehci_mem_writew(void *ptr, target_phys_addr_t addr, uint32_t val)
{
    printf("EHCI doesn't handle 16-bit writes to MMIO\n");
    exit(1);
}

static void handle_port_status_write(EHCIState *s, int port, uint32_t val)
{
    uint32_t *portsc = &s->portsc[port];
    int rwc;
    USBDevice *dev = s->ports[port].dev;

    DEBUG(("PORTSC  %08X->%08X  rwc=%08X  rw=%08X\n", *portsc, val,
          (val & PORTSC_RWC_MASK),
           val & PORTSC_RO_MASK));

    rwc = val & PORTSC_RWC_MASK;
    val &= PORTSC_RO_MASK;

    // handle_read_write_clear(&val, portsc, PORTSC_PEDC | PORTSC_CSC);

    *portsc &= ~rwc;

    DEBUG(("PORTSC  ->  %08X\n", *portsc));

    if ((val & PORTSC_PRESET) && !(*portsc & PORTSC_PRESET)) {
        DEBUG(("USBTRAN Port %d reset begin\n", port));
    }

    if (!(val & PORTSC_PRESET) &&(*portsc & PORTSC_PRESET)) {
        DEBUG(("USBTRAN Port %d reset done\n", port));
        ehci_attach(&s->ports[port], dev);

        // TODO how to handle reset of ports with no device
        if (dev)
            usb_send_msg(dev, USB_MSG_RESET);

        if (s->ports[port].dev) {
            DEBUG(("Device was connected before reset, clearing CSC bit\n"));
            *portsc &= ~PORTSC_CSC;
        }

        /*  Table 2.16 Set the enable bit(and enable bit change) to indicate
         *  to SW that this port has a high speed device attached
         * 
         *  TODO - when to disable?
         */
        val |= PORTSC_PED;
        val |= PORTSC_PEDC;
    }

    *portsc &= ~PORTSC_RO_MASK;
    *portsc |= val;
    DEBUG(("Port status set to 0x%08x\n", *portsc));
}

static void ehci_mem_writel(void *ptr, target_phys_addr_t addr, uint32_t val)
{
    EHCIState *s = ptr;
    int i;

    /* Only aligned reads are allowed on OHCI */
    if (addr & 3) {
        printf("usb-ehci: Mis-aligned write\n");
        return;
    }

    if (addr >= PORTSC && addr < PORTSC + 4 * NB_PORTS) {
        DEBUG(("mem_writel : PORTSC%d->0x%08X \n",(int)(addr-PORTSC)/4, val));
        handle_port_status_write(s,(addr-PORTSC)/4, val);
        return;
    }

    /* Do any register specific pre-write processing here.  */

    switch(addr) 
    {
    case USBCMD:
        if ((val & USBCMD_RUNSTOP) && !(s->usbcmd & USBCMD_RUNSTOP)) {
            DEBUG(("mem_writel : USBCMD run, clear halt\n"));
            // printf("Call mod_timer for %p\n", s->frame_timer);
            qemu_mod_timer(s->frame_timer, qemu_get_clock(vm_clock));
            s->last_run_usec = qemu_get_clock(vm_clock) / 1000;
            s->usbsts &= ~USBSTS_HALT;
        }

        if (!(val & USBCMD_RUNSTOP) &&(s->usbcmd & USBCMD_RUNSTOP)) {
            DEBUG(("mem_writel : USBCMD  ** STOP **\n"));
            // printf("Call del_timer for %p\n", s->frame_timer);
#if 0
            qemu_del_timer(s->frame_timer);
#endif
            // TODO - should finish out some stuff before setting halt
            s->usbsts |= USBSTS_HALT;
        }

        if (val & USBCMD_HCRESET) {
            DEBUG(("mem_writel : USBCMD resetting ...\n"));
            ehci_reset(s);
            DEBUG(("mem_writel : USBCMD reset done, clear reset request bit\n"));
            val &= ~USBCMD_HCRESET;
        }

            break;

    case USBSTS:
        val &= USBSTS_RO_MASK;              // bits 6 thru 31 are RO
        DEBUG(("mem_writel : USBSTS RWC set to 0x%08X\n", val));

        val =(s->usbsts &= ~val);         // bits 0 thru 5 are R/WC
        DEBUG(("mem_writel : USBSTS updating interrupt condition\n"));
        ehci_set_interrupt(s, 0);

        break;

    case USBINTR:
        val &= USBINTR_MASK;
        // DEBUG(("mem_writel : USBINTR set to 0x%08X\n", val));
        break;
    
    case FRINDEX:
        s->sofv = val >> 3;
        break;
        
    case CONFIGFLAG:
        val &= 0x1;

        if (val) {
            for(i = 0; i < NB_PORTS; i++)
                s->portsc[i] &= ~PORTSC_POWNER;
        }

        break;
    }

    if (addr != 0x28) {
        DEBUG(("mem_writel : addr=0x%08X, val=0x%08X\n",(unsigned) addr, val));
    }

    *(uint32_t *)(&s->mmio[addr]) = val;
}


// TODO : Put in common header file, duplication from usb-ohci.c

/* Get an array of dwords from main memory */
static inline int get_dwords(uint32_t addr, uint32_t *buf, int num)
{
    int i;

    for(i = 0; i < num; i++, buf++, addr += sizeof(*buf)) {
        cpu_physical_memory_rw(addr,(uint8_t *)buf, sizeof(*buf), 0);
        *buf = le32_to_cpu(*buf);
    }

    return 1;
}

/* Put an array of dwords in to main memory */
static inline int put_dwords(uint32_t addr, uint32_t *buf, int num)
{
    int i;

    for(i = 0; i < num; i++, buf++, addr += sizeof(*buf)) {
        uint32_t tmp = cpu_to_le32(*buf);
        cpu_physical_memory_rw(addr,(uint8_t *)&tmp, sizeof(tmp), 1);
    }

    return 1;
}
    
// 4.10.2

static void ehci_qh_do_overlay(EHCIState *ehci, EHCIqh *qh, EHCIqtd *qtd)
{
    int i;
    int dtoggle;
    int ping;

    // remember values in fields to preserve in qh after overlay

    dtoggle = qh->token & QTD_TOKEN_DTOGGLE;
    ping = qh->token & QTD_TOKEN_PING;

    DEBUG(("setting qh.current from %08X to 0x%08X\n", qh->current,
            ehci->qtdaddr));
    qh->current  = ehci->qtdaddr;
    qh->qtdnext  = qtd->next;
    qh->altnext  = qtd->altnext;
    qh->token    = qtd->token;

    if (qh->current < 0x1000) {
#ifdef DEBUG_PACKET
        dump_qh(qh, qh->current);
#endif
        ASSERT(1==2);
    }
    
    if (((qh->epchar & QH_EPCHAR_EPS_MASK) >> QH_EPCHAR_EPS_SH) == 2) {
        qh->token &= ~QTD_TOKEN_PING;
        qh->token |= ping;
    }

    for(i = 0; i < 5; i++)
        qh->bufptr[i] = qtd->bufptr[i];

    if (!(qh->epchar & QH_EPCHAR_DTC)) {
        // preserve QH DT bit
        qh->token &= ~QTD_TOKEN_DTOGGLE;
        qh->token |= dtoggle;
    }

    qh->bufptr[1] &= ~BUFPTR_CPROGMASK_MASK;
    qh->bufptr[2] &= ~BUFPTR_FRAMETAG_MASK;

    // TODO NakCnt
}

static void ehci_buffer_rw(EHCIState *ehci, EHCIqh *qh, int bytes, int rw)
{
    int bufpos = 0;
    int cpage;
    uint32_t head;
    uint32_t tail;

    cpage = get_field(qh->token, QTD_TOKEN_CPAGE_MASK, QTD_TOKEN_CPAGE_SH);
    ASSERT(cpage == 0);

    DEBUG(("exec: %sing %d bytes to/from %08x\n", 
           rw ? "writ" : "read", bytes, qh->bufptr[0]));

    if (!bytes)
        return;

    do {
        head = qh->bufptr[cpage];
        tail =(qh->bufptr[cpage] & 0xfffff000) + 0x1000;

        if (bytes <=(tail - head))
            tail = head + bytes;
            
        DEBUG(("DATA %s cpage:%d head:%08X tail:%08X target:%08X\n",
                rw ? "WRITE" : "READ ", cpage, head, tail, bufpos));

        ASSERT(bufpos + tail - head <= BUFF_SIZE);
        ASSERT(tail - head > 0);
        
        cpu_physical_memory_rw(qh->bufptr[cpage], &ehci->buffer[bufpos], 
                                tail - head, rw);

        bufpos +=(tail - head);
        bytes -=(tail - head);

        qh->bufptr[cpage] +=(tail - head);

        if (bytes > 0)
            cpage++;
    } while (bytes > 0);

    set_field(&qh->token, cpage, QTD_TOKEN_CPAGE_MASK, QTD_TOKEN_CPAGE_SH);
}        

#ifdef TDEBUG
static int transactid;
#endif

static void ehci_async_complete_packet(USBPacket *packet, void *opaque)
{
    EHCIState *ehci = opaque;
#ifdef DEBUG_PACKET
    printf("Async packet complete\n");
#endif
    ehci->async_complete = 1;
    ehci->exec_status = packet->len;
}

static int ehci_execute_complete(EHCIState *ehci, 
                                  EHCIqh *qh,
                                  int ret)
{
    if (ret == USB_RET_ASYNC && !ehci->async_complete) {
        DEBUG(("not done yet\n"));
        return ret;
    }

    ehci->async_complete = 0;
    ehci->async_port_in_progress = -1;

    if (ret < 0) {
        switch(ret) {
        case USB_RET_NODEV:
            printf("USB no device\n");
            break;
        case USB_RET_STALL:
            printf("USB stall\n");
            qh->token |= QTD_TOKEN_HALT;
            break;
        case USB_RET_NAK:
            DEBUG(("USBTRAN RSP NAK, returning without clear active\n"));
            return USB_RET_NAK;
            break;
        case USB_RET_BABBLE:
            printf("USB babble TODO\n");
            ASSERT(ret >= 0);
            break;
        default:
            printf("USB invalid response %d to handle\n",
                    ret);
            ASSERT(ret >= 0);
            break;
        }
    } else {
        // if (ret < maxpkt)
        // {
        //     DEBUG(("Short packet condition\n"));
        //     // TODO check 4.12 for splits
         // }
    
        if (ehci->tbytes && ehci->pid == USB_TOKEN_IN) {
            ASSERT(ret > 0);
            ehci_buffer_rw(ehci, qh, ret, 1);
#ifdef TDEBUG
            printf("Data after execution:\n");
            // dump_data(ehci->buffer, ehci->tbytes < 64 ? ehci->tbytes : 64);
            // decode_data(ehci->pid, ehci->buffer, ret);
#endif
            ehci->tbytes -= ret;
        } else
            ehci->tbytes = 0;

        ASSERT(ehci->tbytes >= 0);

        set_field(&qh->token, ehci->tbytes, 
                   QTD_TOKEN_TBYTES_MASK, QTD_TOKEN_TBYTES_SH);
    }

    qh->token ^= QTD_TOKEN_DTOGGLE;
    qh->token &= ~QTD_TOKEN_ACTIVE;

    if (qh->token & QTD_TOKEN_IOC) {
        // TODO should do this after writeback to memory
        ehci_set_interrupt(ehci, USBSTS_INT);
    }
#ifdef DEBUG_PACKET
    DEBUG(("QH after execute:-\n"));
    dump_qh(qh, NLPTR_GET(ehci->qhaddr));
#endif

#ifdef TDEBUG
    printf("USBTRAN RSP %3d                          return:(%5d) ",
            transactid,
            ret);

    if (ehci->pid == USB_TOKEN_IN) {
        printf("[%02X %02X %02X %02X ...]\n",
            *ehci->buffer, *(ehci->buffer+1), 
            *(ehci->buffer+2), *(ehci->buffer+3));
    }
    else
        printf("\n");
#endif
    return ret;
}

// 4.10.3
    
static int ehci_execute(EHCIState *ehci, 
                          uint32_t qhaddr,
                          EHCIqh *qh)
{
    USBPort *port;
    USBDevice *dev;
    int smask;
    int maxpkt;
    int ret;
    int i;
    int endp;
    int devadr;
    
    smask = QH_EPCAP_SMASK_MASK & qh->epcap;
    ehci->tbytes =(qh->token & QTD_TOKEN_TBYTES_MASK) >> QTD_TOKEN_TBYTES_SH;
    ehci->pid =(qh->token & QTD_TOKEN_PID_MASK) >> QTD_TOKEN_PID_SH;
    maxpkt = get_field(qh->epchar, QH_EPCHAR_MPLEN_MASK, QH_EPCHAR_MPLEN_SH);
    endp = get_field(qh->epchar, QH_EPCHAR_EP_MASK, QH_EPCHAR_EP_SH);
    devadr = get_field(qh->epchar, QH_EPCHAR_DEVADDR_MASK, 0);

    if ( !(qh->token & QTD_TOKEN_ACTIVE)) {
        printf("Attempting to execute inactive QH\n");
        exit(1);;
    }

    if (smask) {
        DEBUG(("active interrupt transfer frindex %d for dev %d EP %d\n", 
                ehci->frindex, devadr, endp));
        // TODO are interrupt always IN ?
        ehci->pid = USB_TOKEN_IN;
    } else {
        DEBUG(("Active non-interrupt QH, executing\n"));

        DEBUG(("pid is %2X\n", ehci->pid));

        switch(ehci->pid) {
        case 0: ehci->pid = USB_TOKEN_OUT; break;
        case 1: ehci->pid = USB_TOKEN_IN; break;
        case 2: ehci->pid = USB_TOKEN_SETUP; break;
        default: printf("bad token\n"); break;
        }
    }

    // TODO set reclam

#ifdef DEBUG_PACKET
    DEBUG(("QH before execute:-\n"));
    dump_qh(qh, NLPTR_GET(qhaddr));
#endif

    if (ehci->tbytes && ehci->pid != USB_TOKEN_IN) {
        ehci_buffer_rw(ehci, qh, ehci->tbytes, 0);
#ifdef TDEBUG
        printf("Data before execution:\n");
        // dump_data(ehci->buffer, ehci->tbytes < 64 ? ehci->tbytes : 64);
        // decode_data(ehci->pid, ehci->buffer, ehci->tbytes);
#endif
    }

#ifdef TDEBUG
    printf("\nUSBTRAN REQ %3d dev:%d ep:%d pid:%02X %s bytes:(%5d) ",
            ++transactid,
            devadr,
            endp,
            ehci->pid,
           (ehci->pid == USB_TOKEN_SETUP ? "(SETUP)" :
           (ehci->pid == USB_TOKEN_IN ? "(IN)   " :
           (ehci->pid == USB_TOKEN_OUT ? "(OUT)  " : "(*****)"))),
            ehci->tbytes);

    if (ehci->pid != USB_TOKEN_IN) {
        printf("[%02X %02X %02X %02X ...]\n",
            *ehci->buffer, *(ehci->buffer+1), 
            *(ehci->buffer+2), *(ehci->buffer+3));
    }
    else
        printf("\n");
#endif

    ret = USB_RET_NODEV;

    for(i = 0; i < NB_PORTS; i++) {
        port = &ehci->ports[i];
        dev = port->dev;

        // TODO sometime we will also need to check if we are the port owner

        if (!(ehci->portsc[i] &(PORTSC_CONNECT))) {
            DEBUG(("Port %d, no exec, not connected(%08X)\n",
                    i, ehci->portsc[i]));
            continue;
        }

        ehci->usb_packet.pid = ehci->pid;
        ehci->usb_packet.devaddr = devadr;
        ehci->usb_packet.devep = endp;
        ehci->usb_packet.data = ehci->buffer;
        ehci->usb_packet.len = ehci->tbytes;
        ehci->usb_packet.complete_cb = ehci_async_complete_packet;
        ehci->usb_packet.complete_opaque = ehci;

        DEBUG(("calling dev->handle_packet\n"));
        ret = dev->handle_packet(dev, &ehci->usb_packet);

        if (ret != USB_RET_NODEV)
            break;
    }

    DEBUG(("exit loop dev->handle_packet\n"));

    if (ret > BUFF_SIZE || ehci->tbytes > BUFF_SIZE) {
        printf("bogus QH byte count\n");
        dump_qh(qh, NLPTR_GET(qhaddr));
        ASSERT(ret <= BUFF_SIZE && ehci->tbytes <= BUFF_SIZE);
    }
        
    if (ret == USB_RET_ASYNC) {
        ehci->async_port_in_progress = i;
        ehci->async_complete = 0;
    }

    return ret;
}

/*  4.7.2
 */

static void ehci_process_itd(EHCIState *ehci, 
                             EHCIitd *itd)
{
    USBPort *port;
    USBDevice *dev;
    int ret;
    int i, j;
    int ptr;
    int pid;
    int pg;
    int len;
    int dir;
    int devadr;
    int endp;
    int maxpkt;

    dir =(itd->bufptr[1] & ITD_BUFPTR_DIRECTION);
    devadr = get_field(itd->bufptr[0], 
                        ITD_BUFPTR_DEVADDR_MASK, 0);
    endp = get_field(itd->bufptr[0], 
                      ITD_BUFPTR_EP_MASK, ITD_BUFPTR_EP_SH);
    maxpkt = get_field(itd->bufptr[1], ITD_BUFPTR_MAXPKT_MASK, 0);

#ifdef EHCI_NOMICROFRAMES
    for(i = 0; i < 8; i++) {
#else
    i =(ehci->frindex & 7);
#endif

    if (itd->transact[i] & ITD_XACT_ACTIVE) {
        DEBUG(("ISOCHRONOUS active for frame %d, interval %d\n", 
                ehci->frindex >> 3, i));

        pg = get_field(itd->transact[i], ITD_XACT_PGSEL_MASK,
                        ITD_XACT_PGSEL_SH);
        ptr =(itd->bufptr[pg] & ITD_BUFPTR_MASK) | 
             (itd->transact[i] & ITD_XACT_OFFSET_MASK);
        len = get_field(itd->transact[i], ITD_XACT_LENGTH_MASK,
                         ITD_XACT_LENGTH_SH);

        ASSERT(len <= BUFF_SIZE);

        DEBUG(("ISOCH: buffer %08X len %d\n", ptr, len));

        if (!dir) {
            cpu_physical_memory_rw(ptr, &ehci->buffer[0], len, 0);
            pid = USB_TOKEN_OUT;
        } else
            pid = USB_TOKEN_IN;

        ret = USB_RET_NODEV;

        for(j = 0; j < NB_PORTS; j++) {
            port = &ehci->ports[j];
            dev = port->dev;

            // TODO sometime we will also need to check if we are the port owner

            if (!(ehci->portsc[j] &(PORTSC_CONNECT))) {
                DEBUG(("Port %d, no exec, not connected(%08X)\n",
                        j, ehci->portsc[j]));
                continue;
            }

            ehci->usb_packet.pid = ehci->pid;
            ehci->usb_packet.devaddr = devadr;
            ehci->usb_packet.devep = endp;
            ehci->usb_packet.data = ehci->buffer;
            ehci->usb_packet.len = len;
            ehci->usb_packet.complete_cb = ehci_async_complete_packet;
            ehci->usb_packet.complete_opaque = ehci;

            DEBUG(("calling dev->handle_packet\n"));
            ret = dev->handle_packet(dev, &ehci->usb_packet);

            if (ret != USB_RET_NODEV)
                break;
        }

        /*  In isoch, there is no facility to indicate a NAK so let's
         *  instead just complete a zero-byte transaction.  Setting
         *  DBERR seems too draconian.
         */

        if (ret == USB_RET_NAK) {
            if (ehci->isoch_pause > 0) {
                DEBUG(("ISOCH: received a NAK but paused so returning\n"));
                ehci->isoch_pause--;
                return;
            } else if (ehci->isoch_pause == -1) {
                DEBUG(("ISOCH: recv NAK & isoch pause inactive, setting\n"));
                // Pause frindex for up to 50 msec waiting for data from
                // remote
                ehci->isoch_pause = 50;
                return;
            } else {
                DEBUG(("ISOCH: isoch pause timeout! return 0\n"));
                ret = 0;
            }
        } else {
            DEBUG(("ISOCH: received ACK, clearing pause\n"));
            ehci->isoch_pause = -1;
        }
            
        if (ret >= 0) {
            itd->transact[i] &= ~ITD_XACT_ACTIVE;

            if (itd->transact[i] & ITD_XACT_IOC) {
                // TODO should do this after writeback to memory
                ehci_set_interrupt(ehci, USBSTS_INT);
            }
        }

        if (ret >= 0 && dir) {
            cpu_physical_memory_rw(ptr, &ehci->buffer[0], len, 1);

            if (ret != len) {
                DEBUG(("ISOCH IN expected %d, got %d\n",
                        len, ret));
                set_field(&itd->transact[i], 
                           ret,
                           ITD_XACT_LENGTH_MASK,
                           ITD_XACT_LENGTH_SH);
            }
        }
    }

#ifdef EHCI_NOMICROFRAMES
    }
#endif
}

/* This is the state machine that is common to both async and periodic */

static int ehci_advance_state(EHCIState *ehci,
                                int async,
                                int state,
                                uint32_t entry)
{
    EHCIqh *qh = &ehci->qh;
    EHCIqtd *qtd = &ehci->qtd;
    EHCIitd itd;
    int again = 0;
    int loopcount = 0;
    int transactCtr;
    int smask;
    int reload;
    int nakcnt;

    do {
        DEBUG(("advance_state: again=%d\n", again));
        again = 0;
        // ASSERT(loopcount++ < MAX_ITERATIONS);

        switch(state) {
        /*  This state is the entry point for asynchronous schedule
         *  processing.  Entry here consitutes a EHCI start event state(4.8.5)
         */
        case EST_WAITLISTHEAD:
            DEBUG(("WAITLISTHEAD\n"));

            if (async)
                ehci->usbsts |= USBSTS_REC;

            /*  Find the head of the list 
             */

            for(loopcount = 0; loopcount < MAX_QH; loopcount++) {
                get_dwords(NLPTR_GET(entry),(uint32_t *) qh, 
                            sizeof(EHCIqh) >> 2);

                if (qh->epchar & QH_EPCHAR_H) {
                    DEBUG(("QH %08X is the HEAD of the list\n", entry));
                    break;
                }

                DEBUG(("QH %08X is NOT the HEAD of the list\n", entry));
                entry = qh->next;
            }

            entry |=(NLPTR_TYPE_QH << 1);
            ASSERT(loopcount < MAX_QH);
            loopcount = 0;

            state = EST_FETCHENTRY;
            again = 1;
            break;

        /*  This state is the entry point for periodic schedule
         *  processing as well as being a continuation state for async
         *  processing.
         */
        case EST_FETCHENTRY:
            DEBUG(("FETCHENTRY\n"));

            if (qemu_get_clock(vm_clock) / 1000 > ehci->frame_end_usec) {
                if (async) {
                    DEBUG(("FRAME timer elapsed, exit state machine\n"));
                    state = EST_ACTIVE;
                    break;
                } else
                    DEBUG(("WARNING - frame timer elapsed during periodic\n"));
            }

            if (NLPTR_TBIT(entry)) {
                state = EST_ACTIVE;
                break;
            }

            if (NLPTR_TYPE_GET(entry) == NLPTR_TYPE_QH) {
                state = EST_FETCHQH;
                ehci->qhaddr = entry;
                again = 1;
                break;
            }

            if (NLPTR_TYPE_GET(entry) == NLPTR_TYPE_ITD) {
                state = EST_FETCHITD;
                ehci->itdaddr = entry;
                again = 1;
                break;
            }

            // TODO handle types other than QH
            ASSERT(NLPTR_TYPE_GET(entry) == NLPTR_TYPE_QH);
            break;

        case EST_FETCHQH:
            get_dwords(NLPTR_GET(ehci->qhaddr),(uint32_t *) qh, sizeof(EHCIqh) >> 2);
            DEBUG(("FETCHQH: Fetched QH at address %08X "
                    "(next is %08X, h-bit is %d)\n", 
                    ehci->qhaddr, qh->next, qh->epchar & QH_EPCHAR_H));

#ifdef DEBUG_PACKET
            dump_qh(qh, NLPTR_GET(ehci->qhaddr));
#endif

            if (async) {
                /*  EHCI spec version 1.0 Section 4.8.3
                 */
                if (qh->epchar & QH_EPCHAR_H) {
                    DEBUG(("h-bit set\n"));

                    if (!(ehci->usbsts & USBSTS_REC)) {
                        DEBUG(("h-bit and !reclam, done\n"));
                        state = EST_ACTIVE;
                        break;
                    }
                }
                /*  EHCI spec version 1.0 Section 4.10.1
                 */
                if ( !(qh->epcap & QH_EPCAP_SMASK_MASK)) {
                    DEBUG(("not intr, clear reclam\n"));
                    ehci->usbsts &= ~USBSTS_REC;
                }
            } else {
                DEBUG(("exec: qh check, frindex is %d,%d\n", 
                         (ehci->frindex >> 3),(ehci->frindex & 7)));
            }
            
            reload = get_field(qh->epchar, QH_EPCHAR_RL_MASK, QH_EPCHAR_RL_SH);

            if (reload) {
                DEBUG(("reloading nakcnt to %d\n",
                        reload));
                set_field(&qh->altnext, reload, QH_ALTNEXT_NAKCNT_MASK, 
                           QH_ALTNEXT_NAKCNT_SH);
            }

            if (qh->token & QTD_TOKEN_ACTIVE) { 
                if ((qh->token & QTD_TOKEN_HALT)) {
                    printf("Active, Halt, ** ILLEGAL **\n");
                    state = EST_ACTIVE;
                } else {
                    DEBUG(("Active, !Halt, execute - fetchqtd\n"));
                    ehci->qtdaddr = qh->current;
                    state = EST_FETCHQTD;
                    again = 1;
                }
            } else {
                if (qh->token & QTD_TOKEN_HALT) {
                    DEBUG(("!Active, Halt, go horiz\n"));
                    state = EST_HORIZONTALQH;
                    again = 1;
                } else {
                    /*  EHCI spec version 1.0 Section 4.10.2
                     */
                    DEBUG(("!Active, !Halt, adv q\n"));
                    state = EST_ADVANCEQUEUE;
                    again = 1;
                }
            }

            break;

        case EST_FETCHITD:
            get_dwords(NLPTR_GET(ehci->itdaddr),(uint32_t *) &itd, 
                        sizeof(EHCIitd) >> 2);
            DEBUG(("FETCHITD: Fetched ITD at address %08X "
                    "(next is %08X)\n", 
                    ehci->itdaddr, itd.next));

#ifdef DEBUG_PACKET
            dump_itd(&itd, NLPTR_GET(ehci->itdaddr));
#endif

            ehci_process_itd(ehci, &itd);
#ifdef DEBUG_PACKET
            dump_itd(&itd, NLPTR_GET(ehci->itdaddr));
#endif
            put_dwords(NLPTR_GET(ehci->itdaddr),(uint32_t *) &itd, 
                        sizeof(EHCIitd) >> 2);
            entry = itd.next;
            state = EST_FETCHENTRY;
            again = 1;
            break;

        case EST_ADVANCEQUEUE:
            DEBUG(("ADVANCEQUEUE\n"));
            if ((qh->token & QTD_TOKEN_TBYTES_MASK) != 0 &&
                NLPTR_TBIT(qh->altnext) == 0) {
                ehci->qtdaddr = qh->altnext;
                DEBUG(("tbytes!=0 and tbit = 0, go with altnext\n"));
            } else {
                if (NLPTR_TBIT(qh->qtdnext)) {
                    state = EST_HORIZONTALQH;
                    again = 1;
                    break;
                }

                ehci->qtdaddr = qh->qtdnext;
            }
            state = EST_FETCHQTD;
            again = 1;
            break;

        case EST_FETCHQTD:
            DEBUG(("FETCHQTD: Fetching QTD at address %08X\n", ehci->qtdaddr));
            get_dwords(NLPTR_GET(ehci->qtdaddr),(uint32_t *) qtd, 
                        sizeof(EHCIqtd) >> 2);

            if (qtd->token & QTD_TOKEN_ACTIVE) {
                state = EST_EXECUTE;
                again = 1;
                break;
            } else {
                DEBUG(("abort advance, not active\n"));
                state = EST_HORIZONTALQH;
                again = 1;
                break;
            }

            break;

        case EST_HORIZONTALQH:
            entry = qh->next;
            state = EST_FETCHENTRY;
            again = 1;
            break;

        case EST_EXECUTE:
            if (async) {
                DEBUG(("\n\n>>>>> ASYNC STATE MACHINE execute\n"));
            } else {
                DEBUG(("\n\n>>>>> PERIODIC STATE MACHINE execute\n"));
            }

#ifdef DEBUG_PACKET
            dump_qh(qh, NLPTR_GET(ehci->qhaddr));
            dump_qtd(qtd, NLPTR_GET(ehci->qtdaddr));
#endif

            smask = QH_EPCAP_SMASK_MASK & qh->epcap;

#ifndef EHCI_NOMICROFRAMES
            if (smask &&(smask &(1 <<(ehci->frindex & 7))) == 0) {
                DEBUG(("PERIODIC active not interval: "
                        "mask is %d, " 
                        "frindex is %d,%d\n", 
                        smask,
                        (ehci->frindex >> 3),(ehci->frindex & 7)));

                state = EST_HORIZONTALQH;
                again = 1;
                break;
            }
#endif

            if (smask) {
                DEBUG(("PERIODIC active !!! "
                        "mask is %d, " 
                        "frindex is %d,%d\n", 
                        smask,
                        (ehci->frindex >> 3),(ehci->frindex & 7)));
            }
    
            reload = get_field(qh->epchar, QH_EPCHAR_RL_MASK, QH_EPCHAR_RL_SH);
            nakcnt = get_field(qh->altnext, QH_ALTNEXT_NAKCNT_MASK, QH_ALTNEXT_NAKCNT_SH);
            if (reload && !nakcnt) {
                DEBUG(("RL != 0 but NakCnt == 0, no execute\n"));
                state = EST_HORIZONTALQH;
                again = 1;
                break;
            }

            transactCtr = get_field(qh->epcap, 
                                     QH_EPCAP_MULT_MASK, QH_EPCAP_MULT_SH);

            // TODO verify enough time remains in the uframe as in 4.4.1.1

            // TODO write back ptr to async list when done or out of time

            // TODO Windows does not seem to ever set the MULT field

#if 0
            if (!transactCtr &&(qh->epcap & QH_EPCAP_SMASK_MASK) > 0) 
            {
                DEBUG(("ZERO transactctr for int qh, go HORIZ\n"));
                *state = EST_HORIZONTALQH;
                again = 1;
                break;
            }
#endif

            if (!transactCtr) {
                transactCtr = 1; // TODO - check at what level do we repeat
            
                if (qh->epcap & QH_EPCAP_SMASK_MASK) 
                    DEBUG(("WARN - ZERO transactctr force to 1 for intr\n"));
            }
            
            DEBUG(("exec: ctr is %d\n", transactCtr));
            DEBUG(("exec: frindex is %d,%d\n", 
                   (ehci->frindex >> 3),(ehci->frindex & 7)));
    
            ehci_qh_do_overlay(ehci, qh, qtd);
            ehci->exec_status = ehci_execute(ehci, ehci->qhaddr, qh);
            state = EST_EXECUTING;

            if (ehci->exec_status != USB_RET_ASYNC)
                again = 1;

            break;
            
        case EST_EXECUTING:
            DEBUG(("Enter EXECUTING\n"));
            ehci->exec_status = ehci_execute_complete(ehci, qh, 
                                                       ehci->exec_status);

            if (ehci->exec_status == USB_RET_ASYNC)
                break;

            DEBUG(("finishing exec\n"));
            transactCtr = get_field(qh->epcap, 
                                     QH_EPCAP_MULT_MASK, QH_EPCAP_MULT_SH);

            if (transactCtr)
                transactCtr--;

            set_field(&qh->epcap, transactCtr, 
                       QH_EPCAP_MULT_MASK, QH_EPCAP_MULT_SH);

            reload = get_field(qh->epchar, QH_EPCHAR_RL_MASK, QH_EPCHAR_RL_SH);
            nakcnt = get_field(qh->altnext, QH_ALTNEXT_NAKCNT_MASK, QH_ALTNEXT_NAKCNT_SH);

            if (reload != 0) {
                if (ehci->exec_status == USB_RET_NAK) {
                    nakcnt--;

                    DEBUG(("Nak occured and RL != 0, dec NakCnt to %d\n",
                            nakcnt));
                } else {
                    nakcnt = reload;

                    DEBUG(("Nak didn't occur, reloading to %d\n",
                            nakcnt));
                }

                set_field(&qh->altnext, nakcnt, QH_ALTNEXT_NAKCNT_MASK,
                           QH_ALTNEXT_NAKCNT_SH);
            }

            /* 
             *  Write the qh back to guest physical memory.  This step isn't
             *  in the EHCI spec but we need to do it since we don't share
             *  physical memory with our guest VM.
             */
            
            DEBUG(("write QH to VM memory\n"));
#ifdef DEBUG_PACKET
            dump_qh(qh, NLPTR_GET(ehci->qhaddr));
#endif
            put_dwords(NLPTR_GET(ehci->qhaddr),(uint32_t *) qh, 
                        sizeof(EHCIqh) >> 2);

            // 4.10.5

            if (qh->token & QTD_TOKEN_ACTIVE)
                state = EST_HORIZONTALQH;
            else
                state = EST_WRITEBACK;

            again = 1;
            break;

        case EST_WRITEBACK:
            /*  Write back the QTD from the QH area */
            DEBUG(("write QTD to VM memory\n"));
            put_dwords(NLPTR_GET(ehci->qtdaddr),(uint32_t *) &qh->qtdnext, 
                        sizeof(EHCIqtd) >> 2);
            /* TODO confirm next state.  For now, keep going if async
             * but stop after one qtd if periodic
             */
            // if (async)
            // {
                state = EST_FETCHQH;
                again = 1;
            // }
            // else
           //          state = EST_ACTIVE;
            break;

        default:
            printf("Bad state!\n");
            break;
            }
    }
    while (again);

    return state;
}

static void ehci_advance_async_state(EHCIState *ehci)
{
    switch(ehci->astate) {
    case EST_INACTIVE:
        if (ehci->usbcmd & USBCMD_ASE) {
            DEBUG(("ASYNC going active\n"));
            ehci->usbsts |= USBSTS_ASS;
            ehci->astate = EST_ACTIVE;
            // No break, fall through to ACTIVE
        } else
            break;

    case EST_ACTIVE:
        if ( !(ehci->usbcmd & USBCMD_ASE)) {
            DEBUG(("ASYNC going inactive\n"));
            ehci->usbsts &= ~USBSTS_ASS;
            ehci->astate = EST_INACTIVE;
            break;
        }

        DEBUG(("\n\n\n\n    ===   ===   ===   ===   ===   ===\n\n\n\n"));
        if (ehci->usbcmd & USBCMD_IAAD) {
            /*  Async advance doorbell interrupted requested
             */
            ehci->usbcmd &= ~USBCMD_IAAD;
            ehci_set_interrupt(ehci, USBSTS_IAA);
        }
        
        ehci->astate = ehci_advance_state(ehci, 1, 
                                           EST_WAITLISTHEAD, 
                                           ehci->asynclistaddr);
        break;

    case EST_EXECUTING:
        DEBUG(("async state adv for executing\n"));
        ehci->astate = ehci_advance_state(ehci, 1, 
                                           EST_EXECUTING, ehci->qhaddr);
        break;

    default:
        printf("Bad asynchronous state %d\n",
                ehci->astate);
        ASSERT(1==2);
    }
}

static void ehci_advance_periodic_state(EHCIState *ehci)
{
    uint32_t entry;
    uint32_t list;

    // 4.6

    switch(ehci->pstate) {
    case EST_INACTIVE:
        if ( !(ehci->frindex & 7) &&(ehci->usbcmd & USBCMD_PSE)) {
            DEBUG(("PERIODIC going active\n"));
            ehci->usbsts |= USBSTS_PSS;
            ehci->pstate = EST_ACTIVE;
            // No break, fall through to ACTIVE
        } else
            break;

    case EST_ACTIVE:
        if ( !(ehci->frindex & 7) && !(ehci->usbcmd & USBCMD_PSE)) {
            DEBUG(("PERIODIC going inactive\n"));
            ehci->usbsts &= ~USBSTS_PSS;
            ehci->pstate = EST_INACTIVE;
            break;
        }

        list = ehci->periodiclistbase & 0xfffff000;
        list |=((ehci->frindex & 0x1ff8) >> 1);

        cpu_physical_memory_rw(list,(uint8_t *) &entry, sizeof entry, 0);
        entry = le32_to_cpu(entry);

        DEBUG(("periodic state adv fr=%d.  [%08X] -> %08X\n", 
                ehci->frindex / 8, list, entry));
        ehci->pstate = ehci_advance_state(ehci, 0, 
                                           EST_FETCHENTRY, entry);
        break;

    case EST_EXECUTING:
        DEBUG(("periodic state adv for executing\n"));
        ehci->pstate = ehci_advance_state(ehci, 0, 
                                           EST_EXECUTING, ehci->qhaddr);
        break;

    default:
        printf("Bad periodic state %d\n",
                ehci->pstate);
        ASSERT(1==2);
    }
}

static void ehci_frame_timer(void *opaque)
{
    EHCIState *ehci = opaque;
    int64_t expire_time;
    int usec_elapsed;
    int frames;
    int usec_now;
    int i;
    int skipped_frames = 0;

    usec_now = qemu_get_clock(vm_clock) / 1000;
    usec_elapsed = usec_now - ehci->last_run_usec;
    ehci->frame_end_usec = usec_now + 1000;

#ifdef EHCI_NOMICROFRAMES
    frames = usec_elapsed / 1000;
    ehci->frame_end_usec = usec_now + 1000;
#else
    frames = usec_elapsed / 125;
    ehci->frame_end_usec = usec_now + 125;
#endif

#ifdef TDEBUG
    DEBUG(("Frame timer, usec elapsed since last %d, frames %d\n",
            usec_elapsed, frames));
#endif

    for(i = 0; i < frames; i++) {
        if ( !(ehci->usbsts & USBSTS_HALT)) {
            if (ehci->isoch_pause <= 0) {
#ifdef EHCI_NOMICROFRAMES
                ehci->frindex += 8;
#else
                ehci->frindex++;
#endif
            }

            if (ehci->frindex > 0x00001fff) {
                ehci->frindex = 0;
#ifdef TDEBUG
                DEBUG(("PERIODIC frindex rollover\n"));
#endif
                ehci_set_interrupt(ehci, USBSTS_FLR);
            }

            ehci->sofv =(ehci->frindex - 1) >> 3;
            ehci->sofv &= 0x000003ff;
        }

        if (frames - i > 10)
            skipped_frames++;
        else {
            // TODO could this cause periodic frames to get skipped if async
            // active?
            if (ehci->astate != EST_EXECUTING)
                ehci_advance_periodic_state(ehci);
        }

#ifdef EHCI_NOMICROFRAMES
        ehci->last_run_usec += 1000;
#else
        ehci->last_run_usec += 125;
#endif
    }
    
    if (skipped_frames)
        DEBUG(("WARNING - EHCI skipped %d frames\n", skipped_frames));

    /*  Async is not inside loop since it executes everything it can once
     *  called
     */
    if (ehci->pstate != EST_EXECUTING)
        ehci_advance_async_state(ehci);

    expire_time = qemu_get_clock(vm_clock) +
        (ticks_per_sec / FRAME_TIMER_FREQ);

    qemu_mod_timer(ehci->frame_timer, expire_time);
    
    usec_elapsed = qemu_get_clock(vm_clock) / 1000 - usec_now;

#ifdef TDEBUG
    DEBUG(("TIMING: frame_timer used %d usec\n", usec_elapsed));
#endif
}

static CPUReadMemoryFunc *ehci_readfn[3]={
    ehci_mem_readb,
    ehci_mem_readw,
    ehci_mem_readl
};

static CPUWriteMemoryFunc *ehci_writefn[3]={
    ehci_mem_writeb,
    ehci_mem_writew,
    ehci_mem_writel
};

static void ehci_map(PCIDevice *pci_dev, int region_num, 
                    uint32_t addr, uint32_t size, int type)
{
    EHCIState *s =(EHCIState *)pci_dev;

    DEBUG(("ehci_map: region %d, addr %08X, size %d, s->mem %08X\n",
            region_num, addr, size, s->mem));
    cpu_register_physical_memory(addr, size, s->mem);
}

static EHCIState *ehci_info;

void usb_ehci_info(void)
{
    int i;
    uint32_t val;
    static uint32_t last[0x64 + NB_PORTS * 4];

    for(i = 0; i <= 0x64 + NB_PORTS * 4; i+=4) {
        val = *(uint32_t *) &(ehci_info->mmio[i]);

        printf("MMIO %08X : %08X   [+/- %08X]\n", i, val, val ^ last[i/4]);
        last[i/4] = val;
    }
}

enum ehci_type {
	EHCI_TYPE_UNKNOWN = 0,
	EHCI_TYPE_PCI,
	EHCI_TYPE_OMAP
};

static void usb_ehci_init(EHCIState *ehci, int num_ports, int devfn,
                  qemu_irq irq, enum ehci_type type,
                  const char *name, uint32_t localmem_base)
{
    int i;

    ehci->mmio[0x00] = OPREGBASE;
    ehci->mmio[0x01] = 0x00;
    ehci->mmio[0x02] = 0x00;
    ehci->mmio[0x03] = 0x01;        // HC version
    ehci->mmio[0x04] = num_ports;   // Number of downstream ports
    ehci->mmio[0x05] = 0x00;        // No companion ports at present
    ehci->mmio[0x06] = 0x00;        
    ehci->mmio[0x07] = 0x00;
    ehci->mmio[0x08] = 0x80;        // We can cache whole frame, not 64-bit capable
    ehci->mmio[0x09] = 0x68;        // EECP
    ehci->mmio[0x0a] = 0x00;
    ehci->mmio[0x0b] = 0x00;

    ehci->irq = irq;

    // TODO - port registration is going to need an overhaul since ports
    // can be low, full or high speed and are not tied to UHCI or EHCI.
    // This works for now since we register last so are top of the free
    // list but really all ports need to be owned by EHCI and it should
    // hand off to companion controllers if device is full or low speed.

    DEBUG(("ehci init : registering USB ports with no device attached\n"));

    // TODO come up with a better port allocation scheme
    for(i = num_ports-1; i > 0; i--) {
        qemu_register_usb_port(&ehci->ports[i], ehci, i, ehci_attach);
        ehci->ports[i].dev = 0;
    }

    ehci->frame_timer = qemu_new_timer(vm_clock, ehci_frame_timer, ehci);
    DEBUG(("Alloc new timer for %p\n", ehci->frame_timer));

    ehci->mem = cpu_register_io_memory(ehci_readfn, ehci_writefn, ehci);

    ehci_reset(ehci);
    ehci_info = ehci;
}

void usb_ehci_init_pci(PCIBus *bus, int devfn)
{
    EHCIState *ehci;
    uint8_t *pci_conf;

    ehci = DO_UPCAST(EHCIState, dev,
                     pci_register_device(bus, "EHCI USB", sizeof(*ehci),
                                        devfn, NULL, NULL));
    if (ehci == NULL) {
        fprintf(stderr, "usb-ohci: Failed to register PCI device\n");
        return;
    }

    pci_conf = ehci->dev.config;
    pci_conf[0x00] = 0x86;
    pci_conf[0x01] = 0x80; // Intel
    pci_conf[0x02] = 0xCD;
    pci_conf[0x03] = 0x24; // ICH4 / 82801D
    pci_conf[0x08] = 0x10; // revision number
    pci_conf[0x09] = 0x20;
    pci_conf[0x0a] = 0x03;
    pci_conf[0x0b] = 0x0c;
    pci_conf[0x0e] = 0x00; // header_type 0
    pci_conf[0x34] = 0x00; // capabilities pointer

    // pci_conf[0x34] = 0x50; // capabilities pointer

    pci_conf[0x3d] = 4; // interrupt pin 3
    pci_conf[0x3e] = 0; // MaxLat
    pci_conf[0x3f] = 0; // MinGnt

    // pci_conf[0x50] = 0x01; // power management caps

    pci_conf[0x60] = 0x20;  // SBRN
    pci_conf[0x61] = 0x20;  // FLADJ
    pci_conf[0x62] = 0x7f;
    pci_conf[0x63] = 0x00;  // PORTWAKECAP
    pci_conf[0x64] = 0x00;
    pci_conf[0x65] = 0x00;
    pci_conf[0x66] = 0x00;
    pci_conf[0x67] = 0x00;
    pci_conf[0x68] = 0x01;
    pci_conf[0x69] = 0x00;
    pci_conf[0x6a] = 0x00;
    pci_conf[0x6b] = 0x00;  // USBLEGSUP
    pci_conf[0x6c] = 0x00;
    pci_conf[0x6d] = 0x00;
    pci_conf[0x6e] = 0x00;
    pci_conf[0x6f] = 0xc0;  // USBLEFCTLSTS
    
    usb_ehci_init(ehci, NB_PORTS, devfn, ehci->dev.irq[0],
    		    EHCI_TYPE_PCI, ehci->dev.name, 0);

    pci_register_bar(&ehci->dev, 0, MMIO_SIZE, 
                           PCI_ADDRESS_SPACE_MEM, ehci_map);
}

int usb_ehci_init_omap(target_phys_addr_t base, uint32_t region_size,
                       int num_ports, qemu_irq irq)
{
	EHCIState *ehci = (EHCIState *)qemu_mallocz(sizeof(EHCIState));

	usb_ehci_init(ehci, num_ports, -1, irq, EHCI_TYPE_OMAP, "EHCI USB", 0);
	return ehci->mem;
}
