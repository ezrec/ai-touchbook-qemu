/*
 * TI OMAP on-chip I2C controller.  Only "new I2C" mode supported.
 *
 * Copyright (C) 2007 Andrzej Zaborowski  <balrog@zabor.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include "hw.h"
#include "i2c.h"
#include "omap.h"

struct omap_i2c_s {
    qemu_irq irq;
    qemu_irq drq[2];
    i2c_slave slave[4];
    i2c_bus *bus;

    uint8_t revision;
    uint8_t mask;
    uint16_t stat;
    uint16_t we;
    uint16_t dma;
    uint16_t count;
    int count_cur;
    uint16_t sysc;
    uint16_t control;
    uint16_t own_addr[4];
    uint16_t slave_addr;
    uint8_t sblock;
    uint8_t divider;
    uint16_t times[2];
    uint16_t test;
    int rxlen;
    int txlen;
    int fifosize;
    uint8_t fifo[64];
};

#define OMAP2_INTR_REV	0x34
#define OMAP2_GC_REV	0x34
#define OMAP3_INTR_REV  0x3c

//#define I2C_DEBUG
#ifdef I2C_DEBUG
#define TRACE(fmt, ...) fprintf(stderr, "%s " fmt "\n", __FUNCTION__, ##__VA_ARGS__)
#else
#define TRACE(...)
#endif

static void omap_i2c_interrupts_update(struct omap_i2c_s *s)
{
    TRACE("IRQ=%04x,RDRQ=%d,XDRQ=%d", 
          s->stat & s->mask,
          ((s->dma >> 15 ) & 1) & ((s->stat >> 3) & 1),
          ((s->dma >> 7 ) & 1 )& ((s->stat >> 4 ) & 1));
    qemu_set_irq(s->irq, s->stat & s->mask);
    if ((s->dma >> 15) & 1)                          /* RDMA_EN */
        qemu_set_irq(s->drq[0], (s->stat >> 3) & 1); /* RRDY */
    if ((s->dma >> 7) & 1)                           /* XDMA_EN */
        qemu_set_irq(s->drq[1], (s->stat >> 4) & 1); /* XRDY */
}

/* These are only stubs now.  */
static void omap_i2c_event(i2c_slave *i2c, enum i2c_event event)
{
    fprintf(stderr, "%s: I^2C slave mode not supported\n", __FUNCTION__);
    
    /* code below is broken, i2c_slave CANNOT be cast to omap_i2c_s! */
     
    //struct omap_i2c_s *s = (struct omap_i2c_s *) i2c;
    // 
    //if ((~s->control >> 15) & 1)				/* I2C_EN */
    //    return;
    //
    //switch (event) {
    //    case I2C_START_SEND:
    //    case I2C_START_RECV:
    //        s->stat |= 1 << 9;					/* AAS */
    //        break;
    //    case I2C_FINISH:
    //        s->stat |= 1 << 2;					/* ARDY */
    //        break;
    //    case I2C_NACK:
    //        s->stat |= 1 << 1;					/* NACK */
    //        break;
    //    default:
    //        break;
    //}
    //
    //omap_i2c_interrupts_update(s);
}

static int omap_i2c_rx(i2c_slave *i2c)
{
    fprintf(stderr, "%s: I^2C slave mode not supported\n", __FUNCTION__);
    return 0;
    
    /* code below is broken, i2c_slave CANNOT be cast to omap_i2c_s! */
    
    //struct omap_i2c_s *s = (struct omap_i2c_s *) i2c;
    //uint8_t ret = 0;
    //
    //if ((~s->control >> 15) & 1)				/* I2C_EN */
    //    return -1;
    //
    //if (s->rxlen < s->txlen)
    //    ret = s->fifo[s->rxlen++];
    //else
    //    s->stat |= 1 << 10;					/* XUDF */
    //s->stat |= 1 << 4;						/* XRDY */
    //
    //omap_i2c_interrupts_update(s);
    //return ret;
}

static int omap_i2c_tx(i2c_slave *i2c, uint8_t data)
{
    fprintf(stderr, "%s: I^2C slave mode not supported\n", __FUNCTION__);
    return 1;
    
    /* code below is broken, i2c_slave CANNOT be cast to omap_i2c_s! */
    
    //struct omap_i2c_s *s = (struct omap_i2c_s *) i2c;
    //
    //if ((~s->control >> 15) & 1)				/* I2C_EN */
    //    return 1;
    //
    //if (s->txlen < s->fifosize)
    //    s->fifo[s->txlen++] = data;
    //else
    //    s->stat |= 1 << 11;					/* ROVR */
    //s->stat |= 1 << 3;						/* RRDY */
    //
    //omap_i2c_interrupts_update(s);
    //return 1;
}

static void omap_i2c_fifo_run(struct omap_i2c_s *s)
{
    int ack = 1, i;

    if (!i2c_bus_busy(s->bus))
        return;

    if ((s->control >> 2) & 1) {				/* RM */
        if ((s->control >> 1) & 1) {				/* STP */
            i2c_end_transfer(s->bus);
            s->control &= ~(1 << 1);				/* STP */
            s->count_cur = s->count;
            s->txlen = 0;
        } else if ((s->control >> 9) & 1) {			/* TRX */
            for (i = 0; ack && i < s->txlen; i++)
                ack = (i2c_send(s->bus, s->fifo[i]) >= 0);
            s->txlen = 0;
            s->stat |= 1 << 4;					/* XRDY */
        } else {
            for (i = 0; i < 4; i++)
                s->fifo[i] = i2c_recv(s->bus);
            s->rxlen = 4;
            s->stat |= 1 << 3;					/* RRDY */
        }
    } else {
        if ((s->control >> 9) & 1) {                /* TRX */
            TRACE("master transmit");
            /* this implementation actually always transfers everything
               in the FIFO if acknowledged by the slave; thus a situation
               where we would set XDR status never really happens */
            for (i = 0; ack && s->count_cur && i < s->txlen; i++, s->count_cur--)
                ack = (i2c_send(s->bus, s->fifo[i]) >= 0);
            s->txlen -= i;
            if (s->txlen)
                memmove(s->fifo, s->fifo + i, s->txlen);
            s->stat &= ~0x4410;                     /* XDR | XUDF | XRDY */
            if (ack && s->count_cur) {              /* send more? */
                if (!s->txlen)
                    s->stat |= 1 << 10;             /* XUDF */
                if (s->revision >= OMAP3_INTR_REV
                    && (s->mask & (1 << 14))
                    && s->txlen >= s->fifosize - (s->dma & 0x3f)) /* XTRSH */
                    s->stat |= 1 << 14;             /* XDR */
                else
                    s->stat |= 1 << 4;              /* XRDY */
            }
            if (!s->count_cur)                      /* everything sent? */
                s->stat |= 1 << 2;                  /* ARDY */
        } else {                                           /* !TRX */
            TRACE("master receive");
            for (; s->count_cur && s->rxlen < s->fifosize; s->count_cur--) {
                i = i2c_recv(s->bus);
                if (i < 0) break; /* stop receiving if nothing to receive */
                s->fifo[s->rxlen++] = (uint8_t)(i & 0xff);
                TRACE("received fifo[%02x] = %02x", s->rxlen - 1, s->fifo[s->rxlen - 1]);
            }
            s->stat &= ~((1 << 3) | (1 << 13));            /* RRDY | RDR */
            if (s->rxlen) {
                if (s->revision < OMAP3_INTR_REV
                    || !(s->mask & (1 << 13)))
                    s->stat |= 1 << 3;                     /* RRDY */
                else {
                    if (s->rxlen > ((s->dma >> 8) & 0x3f)) /* RTRSH */
                        s->stat |= 1 << 3;                 /* RRDY */
                    else
                        s->stat |= 1 << 13;                /* RDR */
                }
            } else if (!s->count_cur && (s->control & 2))  /* STP */
                s->stat |= 1 << 2;                         /* ARDY */
        }
        if (!s->count_cur) {
            TRACE("no more data to transmit/receive");
            if ((s->control >> 1) & 1) {   /* STP */
                i2c_end_transfer(s->bus);
                s->control &= ~0x0602;     /* MST | TRX | STP */
                s->count_cur = s->count;
                s->txlen = 0;
            } 
        }
    }

    s->stat |= (!ack) << 1;	              /* NACK */
    if (!ack)
        s->control &= ~(1 << 1);          /* STP */
    TRACE("finished, STAT = %04x, CNT = %d", s->stat, s->count_cur);
}

void omap_i2c_reset(struct omap_i2c_s *s)
{
    s->mask = 0;
    s->stat = 0;
    s->dma = 0;
    s->count = 0;
    s->count_cur = 0;
    s->we = 0;
    s->sysc = 0;
    s->rxlen = 0;
    s->txlen = 0;
    s->control = 0;
    s->own_addr[0] = 0;
    s->own_addr[1] = 0;
    s->own_addr[2] = 0;
    s->own_addr[3] = 0;
    s->slave_addr = 0;
    s->sblock = 0;
    s->divider = 0;
    s->times[0] = 0;
    s->times[1] = 0;
    s->test = 0;
}

static uint32_t omap_i2c_read(void *opaque, target_phys_addr_t addr)
{
    struct omap_i2c_s *s = (struct omap_i2c_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;
    uint16_t ret;

    switch (offset) {
        case 0x00: /* I2C_REV */
            TRACE("REV returns %04x", s->revision);
            return s->revision;
        case 0x04: /* I2C_IE */
            TRACE("IE returns %04x", s->mask);
            return s->mask;
        case 0x08: /* I2C_STAT */
            TRACE("STAT returns %04x", s->stat | (i2c_bus_busy(s->bus) << 12));
            return s->stat | (i2c_bus_busy(s->bus) << 12);
        case 0x0c: /* I2C_IV / I2C_WE */
            if (s->revision >= OMAP3_INTR_REV)
                return s->we;
            if (s->revision >= OMAP2_INTR_REV)
                break;
            ret = ffs(s->stat & s->mask);
            if (ret)
                s->stat ^= 1 << (ret - 1);
            omap_i2c_interrupts_update(s);
            return ret;
        case 0x10: /* I2C_SYSS */
            return (s->control >> 15) & 1; /* reset completed == I2C_EN */
        case 0x14: /* I2C_BUF */
            TRACE("BUF returns %04x", s->dma);
            return s->dma;
        case 0x18: /* I2C_CNT */
            TRACE("CNT returns %04x", s->count_cur);
            return s->count_cur; /* DCOUNT */
        case 0x1c: /* I2C_DATA */
            ret = 0;
            if (s->rxlen) {
                if (s->revision < OMAP3_INTR_REV) {
                    if (s->control & (1 << 14)) /* BE */
                        ret = (((uint16_t)s->fifo[0]) << 8) | s->fifo[1];
                    else
                        ret = (((uint16_t)s->fifo[1]) << 8) | s->fifo[0];
                    if (s->rxlen == 1) {
                        s->stat |= 1 << 15; /* SBD */
                        s->rxlen = 0;
                    } else {
                        s->rxlen -= 2;
                        if (s->rxlen)
                            memmove(s->fifo, s->fifo + 2, s->rxlen);
                    }
                    if (!s->rxlen) {
                        s->stat &= ~(1 << 3); /* RRDY */
                        s->stat |= 1 << 2;    /* ARDY */
                    }
                } else {
                    s->stat &= ~(1 << 7); /* AERR */
                    ret = s->fifo[0];
                    if (--s->rxlen) {
                        memmove(s->fifo, s->fifo + 1, s->rxlen);
                        if (s->rxlen < ((s->dma & 0x3f) >> 8)) {
                            s->stat &= ~(1 << 3); /* RRDY */
                            s->stat |= 1 << 13;   /* RDR */
                        }
                    } else {
                        s->stat &= ~((1 << 3) | (1 << 13)); /* RRDY | RDR */
                        s->stat |= 1 << 2;                  /* ARDY */
                    }
                }
                s->stat &= ~(1 << 11); /* ROVR */
            } else if (s->revision >= OMAP3_INTR_REV)
                s->stat |= (1 << 7); /* AERR */
            TRACE("DATA returns %04x", ret);
            omap_i2c_fifo_run(s);
            omap_i2c_interrupts_update(s);
            return ret;
        case 0x20: /* I2C_SYSC */
            TRACE("SYSC returns %04x", s->sysc);
            return s->sysc;
        case 0x24: /* I2C_CON */
            TRACE("CON returns %04x", s->control);
            return s->control;
        case 0x28: /* I2C_OA / I2C_OA0 */
            return s->own_addr[0];
        case 0x2c: /* I2C_SA */
            return s->slave_addr;
        case 0x30: /* I2C_PSC */
            return s->divider;
        case 0x34: /* I2C_SCLL */
            return s->times[0];
        case 0x38: /* I2C_SCLH */
            return s->times[1];
        case 0x3c: /* I2C_SYSTEST */
            if (s->test & (1 << 15)) { /* ST_EN */
                s->test ^= 0xa;
                return s->test;
            }
            return s->test & ~0x300f;
        case 0x40: /* I2C_BUFSTAT */
            if (s->revision >= OMAP3_INTR_REV) {
                switch (s->fifosize) {
                    case 8:  ret = 0x0000; break;
                    case 16: ret = 0x4000; break;
                    case 32: ret = 0x8000; break;
                    case 64: ret = 0xc000; break;
                    default: ret = 0x0000; break;
                }
                ret |= ((s->rxlen) & 0x3f) << 8; /* RXSTAT */
                ret |= s->txlen & 0x3f;          /* TXSTAT */
                TRACE("BUFSTAT returns %04x", ret);
                return ret;
            }
            break;
        case 0x44: /* I2C_OA1 */
        case 0x48: /* I2C_OA2 */
        case 0x4c: /* I2C_OA3 */
            if (s->revision >= OMAP3_INTR_REV)
                return s->own_addr[(addr >> 2) & 3];
            break;
        case 0x50: /* I2C_ACTOA */
            if (s->revision >= OMAP3_INTR_REV)
                return 0; /* TODO: determine accessed slave own address */
            break;
        case 0x54: /* I2C_SBLOCK */
            if (s->revision >= OMAP3_INTR_REV)
                return s->sblock;
            break;
        default:
            break;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_i2c_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap_i2c_s *s = (struct omap_i2c_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;
    int nack;

    switch (offset) {
        case 0x00: /* I2C_REV */
        case 0x10: /* I2C_SYSS */
        case 0x40: /* I2C_BUFSTAT */
        case 0x50: /* I2C_ACTOA */
            OMAP_RO_REG(addr);
            break;
        case 0x04: /* I2C_IE */
            TRACE("IE = %04x", value);
            if (s->revision >= OMAP3_INTR_REV)
                s->mask = value & 0x63ff;
            else
                s->mask = value & (s->revision < OMAP2_GC_REV ? 0x1f : 0x3f);
            break;
        case 0x08: /* I2C_STAT */
            if (s->revision < OMAP2_INTR_REV)
                OMAP_RO_REG(addr);
            else {
                TRACE("STAT = %04x", value);
                /* RRDY and XRDY are reset by hardware. (in all versions???) */
                s->stat &= ~(value & (s->revision < OMAP3_INTR_REV ? 0x27 : 0x63e7));
                omap_i2c_interrupts_update(s);
            }
            break;
        case 0x0c: /* I2C_IV / I2C_WE */
            if (s->revision < OMAP3_INTR_REV)
                OMAP_RO_REG(addr);
            else
                s->we = value & 0x636f;
            break;
        case 0x14: /* I2C_BUF */
            TRACE("BUF = %04x", value);
            if (s->revision < OMAP3_INTR_REV)
                s->dma = value & 0x8080;
            else {
                s->dma = value & 0xbfbf;
                if (value & (1 << 14)) /* RXFIFO_CLR */
                    s->rxlen = 0;
                if (value & (1 << 6))  /* TXFIFO_CLR */
                    s->txlen = 0;
            }
            if (value & (1 << 15))     /* RDMA_EN */
                s->mask &= ~(1 << 3);  /* RRDY_IE */
            if (value & (1 << 7))      /* XDMA_EN */
                s->mask &= ~(1 << 4);  /* XRDY_IE */
            break;
        case 0x18: /* I2C_CNT */
            TRACE("CNT = %04x", value);
            s->count = value; /* DCOUNT */
            break;
        case 0x1c: /* I2C_DATA */
            TRACE("DATA = %04x", value);
            if (s->revision < OMAP3_INTR_REV) {
                if (s->txlen > 2) {
                    /* XXX: remote access (qualifier) error - what's that? */
                    break;
                }
                if (s->control & (1 << 14)) { /* BE */
                    s->fifo[s->txlen++] = (uint8_t)((value >> 8) & 0xff);
                    s->fifo[s->txlen++] = (uint8_t)(value & 0xff);
                } else {
                    s->fifo[s->txlen++] = (uint8_t)(value & 0xff);
                    s->fifo[s->txlen++] = (uint8_t)((value >> 8) & 0xff);
                }
            } else {
                if (s->txlen < s->fifosize) {
                    s->stat &= ~(1 << 7); /* AERR */
                    s->fifo[s->txlen++] = (uint8_t)(value & 0xff);
                } else
                    s->stat |= (1 << 7); /* AERR */
            }
            s->stat &= ~(1 << 10); /* XUDF */
            omap_i2c_fifo_run(s);
            omap_i2c_interrupts_update(s);
            break;
        case 0x20: /* I2C_SYSC */
            if (s->revision < OMAP2_INTR_REV) {
                OMAP_BAD_REG(addr);
                break;
            }
            TRACE("SYSC = %04x", value);
            if (value & 2)
                omap_i2c_reset(s);
            else if (s->revision >= OMAP3_INTR_REV)
                s->sysc = value & 0x031d;
            break;
        case 0x24: /* I2C_CON */
            TRACE("CON = %04x", value);
            s->control = value & (s->revision < OMAP3_INTR_REV ? 0xcf87 : 0xbff3);
            if (~value & (1 << 15)) { /* I2C_EN */
                if (s->revision < OMAP2_INTR_REV)
                    omap_i2c_reset(s);
                break;
            }
            if (s->revision >= OMAP3_INTR_REV && ((value >> 12) & 3) > 1) { /* OPMODE */
                fprintf(stderr,
                        "%s: only FS and HS modes are supported\n",
                        __FUNCTION__);
                break;
            }
            if ((value & (1 << 10))) { /* MST */
                if (value & 1) { /* STT */
                    nack = !!i2c_start_transfer(s->bus, s->slave_addr, /*SA*/
                                                (~value >> 9) & 1);    /*TRX*/
                    s->stat |= nack << 1;        /* NACK */
                    s->control &= ~(1 << 0);     /* STT */
                    s->txlen = 0;
                    s->rxlen = 0;
                    if (nack)
                        s->control &= ~(1 << 1); /* STP */
                    else {
                        s->count_cur = s->count;
                        omap_i2c_fifo_run(s);
                    }
                    omap_i2c_interrupts_update(s);
                } else if (value & 2) { /* STP, but not STT */
                    i2c_end_transfer(s->bus);
                    s->control &= ~0x0602;     /* MST | TRX | STP */
                    s->count_cur = s->count;
                    s->txlen = 0;
                }
            }
            break;
        case 0x28: /* I2C_OA / I2C_OA0 */
            TRACE("OA0 = %04x", value);
            s->own_addr[0] = value & (s->revision < OMAP3_INTR_REV 
                                      ? 0x3ff : 0xe3ff);
            i2c_set_slave_address(&s->slave[0], 
                                  value & (s->revision >= OMAP3_INTR_REV 
                                           && (s->control & 0x80) 
                                           ? 0x3ff: 0x7f));
            break;
        case 0x2c: /* I2C_SA */
            TRACE("SA = %04x", value);
            s->slave_addr = value & 0x3ff;
            break;
        case 0x30: /* I2C_PSC */
            s->divider = value;
            break;
        case 0x34: /* I2C_SCLL */
            s->times[0] = value & (s->revision < OMAP3_INTR_REV 
                                   ? 0xff : 0xffff);
            break;
        case 0x38: /* I2C_SCLH */
            s->times[1] = value & (s->revision < OMAP3_INTR_REV
                                   ? 0xff : 0xffff);
            break;
        case 0x3c: /* I2C_SYSTEST */
            value &= s->revision < OMAP3_INTR_REV ? 0xf805 : 0xf815;
            if ((value & (1 << 15))) { /* ST_EN */
                fprintf(stderr, "%s: System Test not supported\n",
                        __FUNCTION__);
                s->test = (s->test & 0x0a) | value;
            } else
                s->test = (s->test & 0x1f) | (value & 0xf800);
            if (value & (1 << 11)) /* SBB */
                if (s->revision >= OMAP2_INTR_REV) {
                    s->stat |= 0x3f;
                    if (s->revision >= OMAP3_INTR_REV)
                        s->stat |= 0x600;
                    omap_i2c_interrupts_update(s);
                }
            break;
        case 0x44: /* I2C_OA1 */
        case 0x48: /* I2C_OA2 */
        case 0x4c: /* I2C_OA3 */
            if (s->revision < OMAP3_INTR_REV)
                OMAP_BAD_REG(addr);
            else {
                addr = (addr >> 2) & 3;
                TRACE("OA%d = %04x", addr, value);
                s->own_addr[addr] = value & 0x3ff;
                i2c_set_slave_address(&s->slave[addr], 
                                      value & ((s->control & (0x80 >> addr)) 
                                               ? 0x3ff: 0x7f));
            }
            break;
        case 0x54: /* I2C_SBLOCK */
            if (s->revision < OMAP3_INTR_REV)
                OMAP_BAD_REG(addr);
            else {
                s->sblock = value & 0x0f;
            }
            break;
        default:
            OMAP_BAD_REG(addr);
            break;
    }
}

static void omap_i2c_writeb(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap_i2c_s *s = (struct omap_i2c_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;

    switch (offset) {
        case 0x1c: /* I2C_DATA */
            TRACE("DATA = %02x", value);
            if (s->revision < OMAP3_INTR_REV && s->txlen > 2) {
                /* XXX: remote access (qualifier) error - what's that? */
                break;
            }
            if (s->txlen < s->fifosize) {
                s->fifo[s->txlen++] = (uint8_t)(value & 0xff);
                if (s->revision >= OMAP3_INTR_REV)
                    s->stat &= ~(1 << 7); /* AERR */
                s->stat &= ~(1 << 10);    /* XUDF */
                omap_i2c_fifo_run(s);
            } else if (s->revision >= OMAP3_INTR_REV)
                s->stat |= (1 << 7);      /* AERR */
            omap_i2c_interrupts_update(s);
            break;
        default:
            OMAP_BAD_REG(addr);
            break;
    }
}

static CPUReadMemoryFunc *omap_i2c_readfn[] = {
    omap_badwidth_read16,
    omap_i2c_read,
    omap_badwidth_read16,
};

static CPUWriteMemoryFunc *omap_i2c_writefn[] = {
    omap_i2c_writeb,	/* Only the last fifo write can be 8 bit.  */
    omap_i2c_write,
    omap_badwidth_write16,
};

struct omap_i2c_s *omap_i2c_init(target_phys_addr_t base,
                qemu_irq irq, qemu_irq *dma, omap_clk clk)
{
    int iomemtype;
    struct omap_i2c_s *s = (struct omap_i2c_s *)
            qemu_mallocz(sizeof(struct omap_i2c_s));

    /* TODO: set a value greater or equal to real hardware */
    s->revision = 0x11;
    s->irq = irq;
    s->drq[0] = dma[0];
    s->drq[1] = dma[1];
    s->slave[0].event = omap_i2c_event;
    s->slave[0].recv = omap_i2c_rx;
    s->slave[0].send = omap_i2c_tx;
    s->bus = i2c_init_bus();
    s->fifosize = 4;
    omap_i2c_reset(s);

    iomemtype = cpu_register_io_memory(0, omap_i2c_readfn,
                    omap_i2c_writefn, s);
    cpu_register_physical_memory(base, 0x800, iomemtype);

    return s;
}

struct omap_i2c_s *omap2_i2c_init(struct omap_target_agent_s *ta,
                qemu_irq irq, qemu_irq *dma, omap_clk fclk, omap_clk iclk)
{
    int iomemtype;
    struct omap_i2c_s *s = (struct omap_i2c_s *)
            qemu_mallocz(sizeof(struct omap_i2c_s));

    s->revision = 0x34;
    s->irq = irq;
    s->drq[0] = dma[0];
    s->drq[1] = dma[1];
    s->slave[0].event = omap_i2c_event;
    s->slave[0].recv = omap_i2c_rx;
    s->slave[0].send = omap_i2c_tx;
    s->bus = i2c_init_bus();
    s->fifosize = 4;
    omap_i2c_reset(s);

    iomemtype = l4_register_io_memory(0, omap_i2c_readfn,
                    omap_i2c_writefn, s);
    omap_l4_attach(ta, 0, iomemtype);

    return s;
}

struct omap_i2c_s *omap3_i2c_init(struct omap_target_agent_s *ta,
                                  qemu_irq irq, qemu_irq *dma,
                                  omap_clk fclk, omap_clk iclk,
                                  int fifosize)
{
    int iomemtype;
    struct omap_i2c_s *s = (struct omap_i2c_s *)qemu_mallocz(sizeof(struct omap_i2c_s));
    
    if (fifosize != 8 && fifosize != 16 && fifosize != 32 && fifosize != 64)
        fprintf(stderr, "%s: unsupported FIFO depth specified (%d)\n",
                __FUNCTION__, fifosize);
    s->revision = OMAP3_INTR_REV;
    s->irq = irq;
    s->drq[0] = dma[0];
    s->drq[1] = dma[1];
    s->slave[0].event = s->slave[1].event = s->slave[2].event =
        s->slave[3].event = omap_i2c_event;
    s->slave[0].recv = s->slave[1].recv = s->slave[2].recv =
        s->slave[3].recv = omap_i2c_rx;
    s->slave[0].send = s->slave[1].send = s->slave[2].send =
        s->slave[3].send = omap_i2c_tx;
    s->bus = i2c_init_bus();
    s->fifosize = fifosize;
    omap_i2c_reset(s);
    
    iomemtype = l4_register_io_memory(0, omap_i2c_readfn,
                                      omap_i2c_writefn, s);
    omap_l4_attach(ta, 0, iomemtype);
    
    return s;
}

i2c_bus *omap_i2c_bus(struct omap_i2c_s *s)
{
    return s->bus;
}
