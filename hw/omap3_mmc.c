/*
 * OMAP3 Multimedia Card/Secure Digital/Secure Digital I/O (MMC/SD/SDIO) Card Interface emulation
 *
 * Copyright (C) 2008 yajin  <yajin@vm-kernel.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

/*The MMCHS of OMAP3530/3430 is different from OMAP1 and OAMP2420.*/


#include "hw.h"
#include "omap.h"
#include "sd.h"


#define MMC_DEBUG_LEVEL 0

#if MMC_DEBUG_LEVEL>0
#define TRACE(fmt,...) fprintf(stderr, "%s: " fmt "\n", __FUNCTION__, ##__VA_ARGS__)
#if MMC_DEBUG_LEVEL>1
#define TRACE2(...) TRACE(__VA_ARGS__)
#else
#define TRACE2(...)
#endif
#else
#define TRACE(...)
#define TRACE2(...)
#endif

struct omap3_mmc_s
{
    qemu_irq irq;
    qemu_irq *dma;
    qemu_irq coverswitch;
    omap_clk clk;
    SDState *card;

    uint32_t sysconfig;         /*0x10 */
    uint32_t sysstatus;         /*0x14 */
    uint32_t csre;              /*0x24 */
    uint32_t systest;           /*0x28 */
    uint32_t con;               /*0x2c */
    uint32_t pwcnt;             /*0x30 */
    uint32_t blk;               /*0x104 */
    uint32_t arg;               /*0x108 */
    uint32_t cmd;               /*0x10c */
    uint32_t rsp10;             /*0x110 */
    uint32_t rsp32;             /*0x114 */
    uint32_t rsp54;             /*0x118 */
    uint32_t rsp76;             /*0x11c */
    uint32_t data;              /*0x120 */
    uint32_t pstate;            /*0x124 */
    uint32_t hctl;              /*0x128 */
    uint32_t sysctl;            /*0x12c */
    uint32_t stat;              /*0x130 */
    uint32_t ie;                /*0x134 */
    uint32_t ise;               /*0x138 */
    uint32_t ac12;              /*0x13c */
    uint32_t capa;              /*0x140 */
    uint32_t cur_capa;          /*0x148 */
    uint32_t rev;               /*0x1fc */

    /*for quick reference */
    uint16_t blen_counter;
    uint16_t nblk_counter;

    uint32_t fifo[256];
    int fifo_start;
    int fifo_len;

    int ddir;
    int transfer;
    int stop;
    
    uint32_t stat_pending;
};


typedef enum
{
    sd_nore = 0,     /* no response */
    sd_136_bits = 1, /* response length 136 bits */
    sd_48_bits = 2,  /* response length 48 bits */
    sd_48b_bits = 3, /* response length 48 bits with busy after response */
} omap3_sd_rsp_type_t;

static void omap3_mmc_command(struct omap3_mmc_s *host);

static void omap3_mmc_interrupts_update(struct omap3_mmc_s *s)
{
    qemu_set_irq(s->irq, !!((s->stat | s->stat_pending) & s->ie & s->ise));
}

static void omap3_mmc_fifolevel_update(struct omap3_mmc_s *host)
{
    enum { ongoing, ready, aborted } state = ongoing;
    
    if ((host->cmd & (1 << 21))) { /* DP */
        TRACE2("ddir=%d, dma=%d, fifo_len=%d bytes",
               host->ddir, host->cmd & 1, host->fifo_len * 4);

        if (host->ddir) {
            if (!host->fifo_len)
                state = host->stop ? aborted : ready;
            else {
                if (host->cmd & 1) { /* DE */
                    if (host->fifo_len * 4 == (host->blk & 0x7ff)) { /* BLEN */
                        if (host->stop)
                            state = aborted;
                        else
                            qemu_irq_raise(host->dma[1]);
                    } else
                        qemu_irq_lower(host->dma[1]);
                } else {
                    if (host->stop 
                        && host->fifo_len * 4 == (host->blk & 0x7ff))
                        state = aborted;
                    else {
                        host->pstate |= 0x0800;      /* BRE */
                        host->stat_pending |= 0x20;  /* BRR */
                    }
                }
            }
        } else {
            if (host->transfer && host->blen_counter) {
                if (host->cmd & 1) { /* DE */
                    if (host->blen_counter == (host->blk & 0x7ff)) { /* BLEN */
                        if (host->stop)
                            state = aborted;
                        else
                            qemu_irq_raise(host->dma[0]);
                    } else
                        qemu_irq_lower(host->dma[0]);
                } else {
                    if (host->stop
                        && host->blen_counter == (host->blk & 0x7ff))
                        state = aborted;
                    else {
                        host->pstate |= 0x0400;      /* BWE */
                        host->stat_pending |= 0x10;  /* BWR */
                    }
                }
            } else
                state = host->stop ? aborted : ready;
        }

        if ((host->cmd & 1) || state != ongoing) { /* DE */
            host->pstate &= ~0x0c00;               /* BRE | BWE */
            host->stat_pending &= ~0x30;           /* BRR | BWR */
            host->stat &= ~0x30;                   /* BRR | BWR */
            if (state != ongoing) {
                host->stat_pending |= 0x2;         /* TC */
                if (state == aborted) {
                    host->cmd = host->stop;
                    host->stop = 0;
                    omap3_mmc_command(host);
                }
            }
        }
    }
}

static void omap3_mmc_transfer(struct omap3_mmc_s *host)
{
    int i;

    /* if data transfer is inactive
       OR block count enabled with zero block count
       OR in receive mode and we have unread data in FIFO
       OR in transmit mode and we have no data in FIFO,
       don't do anything */
    if (!host->transfer
        || ((host->cmd & 2) && !host->nblk_counter)
        || (host->ddir && host->fifo_len)
        || (!host->ddir && !host->fifo_len))
        return;
    
    TRACE2("begin, %d blocks (%d bytes/block) left to %s, %d bytes in FIFO",
           (host->cmd & 2) ? host->nblk_counter : 1,
           host->blk & 0x7ff, 
           host->ddir ? "receive" : "send",
           host->fifo_len * 4);

    if (host->ddir) {
        while (host->blen_counter && host->fifo_len < 255) {
            for (i = 0; i < 32 && host->blen_counter; i += 8, host->blen_counter--)
                host->fifo[(host->fifo_start + host->fifo_len) & 0xff] |=
                    sd_read_data(host->card) << i;
            host->fifo_len++;
        }
        TRACE2("end, %d bytes in FIFO:", host->fifo_len * 4);
#if MMC_DEBUG_LEVEL>1
        for (i = 0; i < host->fifo_len; i += 4)
            TRACE2("[0x%03x] %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                   i * 4,
                   host->fifo[(host->fifo_start + i) & 0xff] & 0xff,
                   (host->fifo[(host->fifo_start + i) & 0xff] >> 8) & 0xff,
                   (host->fifo[(host->fifo_start + i) & 0xff] >> 16) & 0xff,
                   (host->fifo[(host->fifo_start + i) & 0xff] >> 24) & 0xff,
                   host->fifo[(host->fifo_start + i + 1) & 0xff] & 0xff,
                   (host->fifo[(host->fifo_start + i + 1) & 0xff] >> 8) & 0xff,
                   (host->fifo[(host->fifo_start + i + 1) & 0xff] >> 16) & 0xff,
                   (host->fifo[(host->fifo_start + i + 1) & 0xff] >> 24) & 0xff,
                   host->fifo[(host->fifo_start + i + 2) & 0xff] & 0xff,
                   (host->fifo[(host->fifo_start + i + 2) & 0xff] >> 8) & 0xff,
                   (host->fifo[(host->fifo_start + i + 2) & 0xff] >> 16) & 0xff,
                   (host->fifo[(host->fifo_start + i + 2) & 0xff] >> 24) & 0xff,
                   host->fifo[(host->fifo_start + i + 3) & 0xff] & 0xff,
                   (host->fifo[(host->fifo_start + i + 3) & 0xff] >> 8) & 0xff,
                   (host->fifo[(host->fifo_start + i + 3) & 0xff] >> 16) & 0xff,
                   (host->fifo[(host->fifo_start + i + 3) & 0xff] >> 24) & 0xff);
#endif
    } else {
        while (host->blen_counter && host->fifo_len) {
            for (i = 0; i < 32 && host->blen_counter; i += 8, host->blen_counter--)
                sd_write_data(host->card, (host->fifo[host->fifo_start] >> i) & 0xff);
            host->fifo_start++;
            host->fifo_len--;
            host->fifo_start &= 0xff;
        }
        TRACE2("end, %d bytes pending in current block", host->blen_counter);
    }

    if (!host->blen_counter) {
        if (host->cmd & 2) /* BCE */
            host->nblk_counter--;
        TRACE2("block done, %d blocks left",
               (host->cmd & (1 << 5)) ? host->nblk_counter : 0);
        host->blen_counter = host->blk & 0x7ff;
        if (!(host->cmd & (1 << 5)) /* MSBS */
            || !host->nblk_counter) {
            host->nblk_counter = (host->blk >> 16) & 0xffff;
            host->transfer = 0;
            host->pstate &= ~0x0306; /* RTA | WTA | DLA | DATI */
        }
    }
}

static void omap3_mmc_command(struct omap3_mmc_s *host)
{
    uint32_t rspstatus, mask;
    int rsplen, timeout;
    struct sd_request_s request;
    uint8_t response[16];
    int cmd = (host->cmd >> 24) & 0x3f; /* INDX */
    
    TRACE("%d type=%d arg=0x%08x", cmd, (host->cmd >> 22) & 3, host->arg);
    
    if ((host->con & 2) && !cmd) { /* INIT and CMD0 */
        host->stat_pending |= 0x1;
        host->pstate &= 0xfffffffe;
        return;
    }
    
    if (host->cmd & (1 << 21)) { /* DP */
        host->fifo_start = 0;
        host->fifo_len = 0;
        host->transfer = 1;
        host->ddir = (host->cmd >> 4) & 1;
        /* DLA | DATI | (RTA/WTA) */
        host->pstate |= 0x6 | (host->ddir ? 0x200 : 0x100);
    } else {
        host->transfer = 0;
        host->pstate &= ~0x306; /* RTA | WTA | DLA | DATI */
    }
    
    timeout = 0;
    mask = 0;
    rspstatus = 0;
    
    request.cmd = cmd;
    request.arg = host->arg;
    request.crc = 0; /* FIXME */
    
    rsplen = sd_do_command(host->card, &request, response);
    
    switch ((host->cmd >> 16) & 3) { /* RSP_TYPE */
        case sd_nore:
            rsplen = 0;
            break;
        case sd_136_bits:
            if (rsplen < 16) {
                timeout = 1;
                break;
            }
            rsplen = 16;
            host->rsp76 = (response[0] << 24) | (response[1] << 16) |
            (response[2] << 8) | (response[3] << 0);
            host->rsp54 = (response[4] << 24) | (response[5] << 16) |
            (response[6] << 8) | (response[7] << 0);
            host->rsp32 = (response[8] << 24) | (response[9] << 16) |
            (response[10] << 8) | (response[11] << 0);
            host->rsp10 = (response[12] << 24) | (response[13] << 16) |
            (response[14] << 8) | (response[15] << 0);
            break;
            case sd_48_bits:
            case sd_48b_bits:
            if (rsplen < 4) {
                timeout = 1;
                break;
            }
            rsplen = 4;
            host->rsp10 = (response[0] << 24) | (response[1] << 16) |
            (response[2] << 8) | (response[3] << 0);
            switch (cmd) {
                case 41: /* r3 */
                case 8:  /* r7 */
                    break;
                case 3:  /* r6 */
                    mask = 0xe00;
                    rspstatus = (response[2] << 8) | response[3];
                    break;
                default:
                    mask = OUT_OF_RANGE | ADDRESS_ERROR | BLOCK_LEN_ERROR |
                    ERASE_SEQ_ERROR | ERASE_PARAM | WP_VIOLATION |
                    LOCK_UNLOCK_FAILED | COM_CRC_ERROR | ILLEGAL_COMMAND |
                    CARD_ECC_FAILED | CC_ERROR | SD_ERROR |
                    CID_CSD_OVERWRITE | WP_ERASE_SKIP;
                    rspstatus = (response[0] << 24) | (response[1] << 16) |
                    (response[2] << 8) | (response[3] << 0);
                    break;
            }
            default:
            break;
    }
    
    if (cmd == 12 || cmd == 52) { /* stop transfer commands */
        //host->fifo_start = 0;
        //host->fifo_len = 0;
        host->transfer = 0;
        host->pstate &= ~0x0f06;     /* BRE | BWE | RTA | WTA | DLA | DATI */
        host->stat_pending &= ~0x30; /* BRR | BWR */
        host->stat &= ~0x30;         /* BRR | BWR */
        host->stat_pending |= 0x2;   /* TC */
        qemu_irq_lower(host->dma[0]);
        qemu_irq_lower(host->dma[1]);
    }
    
    if (rspstatus & mask & host->csre) {
        host->stat_pending |= 1 << 28;    /* CERR */
        host->pstate &= ~0x306;           /* RTA | WTA | DLA | DATI */
        host->transfer = 0;
    } else {
        host->stat &= ~(1 << 28);         /* CERR */
        host->stat_pending &= ~(1 << 28); /* CERR */
    }
    host->stat_pending |= timeout ? (1 << 16) : 0x1; /* CTO : CC */
    //    host->stat_pending &= host->ie; /* use only enabled signals */
}

static void omap3_mmc_reset(struct omap3_mmc_s *s)
{
    s->sysconfig = 0x00000015;
    s->con       = 0x00000500;
    s->pstate    = 0x00040000;
    s->capa      = 0x00e10080;
    s->rev       = 0x26000000;

    s->fifo_start = 0;
    s->fifo_len   = 0;
    s->stop       = 0;
}

static uint32_t omap3_mmc_read(void *opaque, target_phys_addr_t addr)
{
    struct omap3_mmc_s *s = (struct omap3_mmc_s *) opaque;
    uint32_t i ;

    switch (addr) {
        case 0x10:
            TRACE2("SYSCONFIG = %08x", s->sysconfig);
            return s->sysconfig;
        case 0x14:
            TRACE2("SYSSTATUS = %08x", s->sysstatus | 0x1);
            return s->sysstatus | 0x1; /*reset completed */
        case 0x24:
            TRACE2("CSRE = %08x", s->csre);
            return s->csre;
        case 0x28:
            TRACE2("SYSTEST = %08x", s->systest);
            return s->systest;
        case 0x2c: /* MMCHS_CON */
            TRACE2("CON = %08x", s->con);
            return s->con;
        case 0x30:
            TRACE2("PWCNT = %08x", s->pwcnt);
            return s->pwcnt;
        case 0x104: /* MMCHS_BLK */
            TRACE2("BLK = %08x", s->blk);
            return s->blk;
        case 0x108: /* MMCHS_ARG */
            TRACE2("ARG = %08x", s->arg);
            return s->arg;
        case 0x10c:
            TRACE2("CMD = %08x", s->cmd);
            return s->cmd;
        case 0x110:
            TRACE2("RSP10 = %08x", s->rsp10);
            return s->rsp10;
        case 0x114:
            TRACE2("RSP32 = %08x", s->rsp32);
            return s->rsp32;
        case 0x118:
            TRACE2("RSP54 = %08x", s->rsp54);
            return s->rsp54;
        case 0x11c:
            TRACE2("RSP76 = %08x", s->rsp76);
            return s->rsp76;
        case 0x120:
            /* in PIO mode, access allowed only when BRE is set */
            if (!(s->cmd & 1) && !(s->pstate & 0x0800)) {
                s->stat_pending |= 1 << 29; /* BADA */
                i = 0;
            } else {
                i = s->fifo[s->fifo_start];
                s->fifo[s->fifo_start] = 0;
                if (s->fifo_len == 0) {
                    fprintf(stderr, "MMC: FIFO underrun\n");
                    return i;
                }
                s->fifo_start++;
                s->fifo_len--;
                s->fifo_start &= 255;
                //if (s->ddir && !s->fifo_len)
                //    s->stat_pending |= 0x2; /* TC */
                //omap3_mmc_fifolevel_update(s);
                omap3_mmc_transfer(s);
                omap3_mmc_fifolevel_update(s);
            }
            omap3_mmc_interrupts_update(s);
            return i;
        case 0x124: /* MMCHS_PSTATE */
            TRACE2("PSTATE = %08x", s->pstate);
            return s->pstate;
        case 0x128:
            TRACE2("HCTL = %08x", s->hctl);
            return s->hctl;
        case 0x12c: /* MMCHS_SYSCTL */
            TRACE2("SYSCTL = %08x", s->sysctl);
            return s->sysctl;
        case 0x130: /* MMCHS_STAT */
            s->stat |= s->stat_pending;
            if (s->stat & 0xffff0000)
                   s->stat |= 1 << 15;    /* ERRI */
            else
                   s->stat &= ~(1 << 15); /* ERRI */
            s->stat_pending = 0;
            TRACE2("STAT = %08x", s->stat);
            return s->stat;
        case 0x134:
            TRACE2("IE = %08x", s->ie);
            return s->ie;
        case 0x138:
            TRACE2("ISE = %08x", s->ise);
            return s->ise;
        case 0x13c:
            TRACE2("AC12 = %08x", s->ac12);
            return s->ac12;
        case 0x140: /* MMCHS_CAPA */
            TRACE2("CAPA = %08x", s->capa);
            return s->capa;
        case 0x148:
            TRACE2("CUR_CAPA = %08x", s->cur_capa);
            return s->cur_capa;
        case 0x1fc:
            TRACE2("REV = %08x", s->rev);
            return s->rev;
        default:
            OMAP_BAD_REG(addr);
            exit(-1);
            return 0;
    }
}

static void omap3_mmc_write(void *opaque, target_phys_addr_t addr,
                            uint32_t value)
{
    struct omap3_mmc_s *s = (struct omap3_mmc_s *) opaque;
    
    switch (addr) {
        case 0x014:
        case 0x110:
        case 0x114:
        case 0x118:
        case 0x11c:
        case 0x124:
        case 0x13c:
        case 0x1fc:
            OMAP_RO_REG(addr);
            break;
        case 0x010:
            TRACE2("SYSCONFIG = %08x", value);
            if (value & 2)
                omap3_mmc_reset(s);
            s->sysconfig = value & 0x31d;
            break;
        case 0x024:
            TRACE2("CSRE = %08x", value);
            s->csre = value;
            break;
        case 0x028:
            TRACE2("SYSTEST = %08x", value);
            s->systest = value;
            break;
        case 0x02c: /* MMCHS_CON */
            TRACE2("CON = %08x", value);
            if (value & 0x10) {
                fprintf(stderr, "%s: SYSTEST mode is not supported\n", __FUNCTION__);
                exit(-1);
            }
            if (value & 0x20) {
                fprintf(stderr, "%s: 8-bit data width is not supported\n", __FUNCTION__);
                exit(-1);
            }
            s->con = value & 0x1ffff;
            break;
        case 0x030:
            TRACE2("PWCNT = %08x", value);
            s->pwcnt = value;
            break;
        case 0x104: /* MMCHS_BLK */
            TRACE2("BLK = %08x", value);
            s->blk = value & 0xffff07ff;
            s->blen_counter = value & 0x7ff;
            s->nblk_counter = (value >> 16) & 0xffff;
            break;
        case 0x108: /* MMCHS_ARG */
            TRACE2("ARG = %08x", value);
            s->arg = value;
            break;
        case 0x10c: /* MMCHS_CMD */
            TRACE2("CMD = %08x", value);
            if (!s->stop
                && (((value >> 24) & 0x3f) == 12 || ((value >> 24) & 0x3f) == 52)) {
                s->stop = value & 0x3ffb0037;
            } else {
                s->cmd = value & 0x3ffb0037;
                omap3_mmc_command(s);
            }
            omap3_mmc_transfer(s);
            omap3_mmc_fifolevel_update(s);
            omap3_mmc_interrupts_update(s);
            break;
        case 0x120:
            /* in PIO mode, access allowed only when BWE is set */
            if (!(s->cmd & 1) && !(s->pstate & 0x0400)) {
                s->stat_pending |= 1 << 29; /* BADA */
            } else {
                if (s->fifo_len == 256) {
                    fprintf(stderr, "%s: FIFO overrun\n", __FUNCTION__);
                    break;
                }
                s->fifo[(s->fifo_start + s->fifo_len) & 255] = value;
                s->fifo_len++;
                omap3_mmc_transfer(s);
                omap3_mmc_fifolevel_update(s);
            }
            omap3_mmc_interrupts_update(s);
            break;
        case 0x128: /* MMCHS_HCTL */
            TRACE2("HCTL = %08x", value);
            s->hctl = value & 0xf0f0f02;
            if (s->hctl & (1 << 16)) /* SBGR */
                fprintf(stderr, "%s: Stop at block gap feature not implemented!\n", __FUNCTION__);
            break;
        case 0x12c: /* MMCHS_SYSCTL */
            TRACE2("SYSCTL = %08x", value);
            if (value & 0x04000000) { /* SRD */
                s->data    = 0;
                s->pstate &= ~0x00000f06; /* BRE, BWE, RTA, WTA, DLA, DATI */
                s->hctl   &= ~0x00030000; /* SGBR, CR */
                s->stat   &= ~0x00000034; /* BRR, BWR, BGE */
                s->stat_pending &= ~0x00000034;
                s->fifo_start = 0;
                s->fifo_len = 0;
            }
            if (value & 0x02000000) { /* SRC */
                s->pstate &= ~0x00000001; /* CMDI */
            }
            if (value & 0x01000000) { /* SRA */
                uint32_t capa = s->capa;
                uint32_t cur_capa = s->cur_capa;
                omap3_mmc_reset(s);
                s->capa = capa;
                s->cur_capa = cur_capa;
            }
            value = (value & ~2) | ((value & 1) << 1); /* copy ICE directly to ICS */
            s->sysctl = value & 0x000fffc7;
            break;
        case 0x130:
            TRACE2("STAT = %08x", value);
            value = value & 0x317f0237;
            s->stat &= ~value;
            /* stat_pending is NOT cleared */
            omap3_mmc_interrupts_update(s);
            break;
        case 0x134: /* MMCHS_IE */
            TRACE2("IE = %08x", value);
            if (!(s->con & 0x4000)) /* if CON:OBIE is clear, ignore write to OBI_ENABLE */
                value = (value & ~0x200) | (s->ie & 0x200);
            s->ie = value & 0x317f0337;
            if (!(s->ie & 0x100)) {
                s->stat &= ~0x100;
                s->stat_pending &= ~0x100;
            }
            omap3_mmc_interrupts_update(s);
            break;
        case 0x138:
            TRACE2("ISE = %08x", value);
            s->ise = value & 0x317f0337;
            omap3_mmc_interrupts_update(s);
            break;
        case 0x140: /* MMCHS_CAPA */
            TRACE2("CAPA = %08x", value);
            s->capa &= ~0x07000000;
            s->capa |= value & 0x07000000;
            break;
        case 0x148:
            TRACE2("CUR_CAPA = %08x", value);
            s->cur_capa = value & 0xffffff;
            break;
        default:
            OMAP_BAD_REG(addr);
            exit(-1);
    }
}

static CPUReadMemoryFunc *omap3_mmc_readfn[] = {
    omap_badwidth_read32,
    omap_badwidth_read32,
    omap3_mmc_read,
};

static CPUWriteMemoryFunc *omap3_mmc_writefn[] = {
    omap_badwidth_write32,
    omap_badwidth_write32,
    omap3_mmc_write,
};

static void omap3_mmc_enable(struct omap3_mmc_s *s, int enable)
{
    sd_enable(s->card, enable);
}

struct omap3_mmc_s *omap3_mmc_init(struct omap_target_agent_s *ta,
                                   BlockDriverState * bd, qemu_irq irq,
                                   qemu_irq dma[], omap_clk fclk, omap_clk iclk)
{
    int iomemtype;
    struct omap3_mmc_s *s = (struct omap3_mmc_s *)
        qemu_mallocz(sizeof(struct omap3_mmc_s));

    s->irq = irq;
    s->dma = dma;
    s->clk = fclk;

    omap3_mmc_reset(s);

    iomemtype = l4_register_io_memory(0, omap3_mmc_readfn,
                                      omap3_mmc_writefn, s);
    omap_l4_attach(ta, 0, iomemtype);

    /* Instantiate the storage */
    if (bd!=NULL) {
    	s->card = sd_init(bd, 0);
	    omap3_mmc_enable(s,1);
    }

    return s;
}
