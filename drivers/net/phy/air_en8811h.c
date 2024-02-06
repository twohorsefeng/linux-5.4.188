// SPDX-License-Identifier: GPL-2.0+

/* FILE NAME:  air_en8811h.c
 * PURPOSE:
 *      EN8811H phy driver for Linux
 * NOTES:
 *
 */

/* INCLUDE FILE DECLARATIONS
 */
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/phy.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <linux/uaccess.h>
#include <linux/version.h>

#include "air_en8811h_fw.h"

#define EN8811H_MDIO_PHY_ID         0x0f
#define EN8811H_PBUS_PHY_ID         0x17

#define EN8811H_PHY_ID1             0x03a2
#define EN8811H_PHY_ID2             0xa411
#define EN8811H_PHY_ID              ((EN8811H_PHY_ID1 << 16) | EN8811H_PHY_ID2)
#define EN8811H_SPEED_2500          0x03
#define EN8811H_PHY_READY           0x05
#define MAX_RETRY                   5

#define EN8811H_TX_POLARITY_REVERSE 0x0
#define EN8811H_TX_POLARITY_NORMAL  0x1

#define EN8811H_RX_POLARITY_REVERSE (0x1 << 1)
#define EN8811H_RX_POLARITY_NORMAL  (0x0 << 1)

/* CL45 MDIO control */
#define MII_MMD_ACC_CTL_REG         0x0d
#define MII_MMD_ADDR_DATA_REG       0x0e
#define MMD_OP_MODE_DATA            BIT(14)

MODULE_DESCRIPTION("Airoha EN8811H PHY drivers");
MODULE_AUTHOR("Airoha");
MODULE_LICENSE("GPL");

/************************************************************************
*                  F U N C T I O N S
************************************************************************/
/* Airoha MII read function */
static unsigned int ecnt_mii_cl22_read(struct mii_bus *ebus, unsigned int phy_addr,unsigned int phy_register,unsigned int *read_data)
{
    *read_data = mdiobus_read(ebus, phy_addr, phy_register);
    return 0;
}

/* Airoha MII write function */
static unsigned int ecnt_mii_cl22_write(struct mii_bus *ebus, unsigned int phy_addr, unsigned int phy_register,unsigned int write_data)
{
    mdiobus_write(ebus, phy_addr, phy_register, write_data);
    return 0;
}

static unsigned int mdiobus_write45(struct mii_bus *ebus, unsigned int port, unsigned int devad, unsigned int reg, unsigned int write_data)
{
    mdiobus_write(ebus, port, MII_MMD_ACC_CTL_REG, devad);
    mdiobus_write(ebus, port, MII_MMD_ADDR_DATA_REG, reg);
    mdiobus_write(ebus, port, MII_MMD_ACC_CTL_REG, MMD_OP_MODE_DATA | devad);
    mdiobus_write(ebus, port, MII_MMD_ADDR_DATA_REG, write_data);
    return 0;
}
/* Use default PBUS_PHY_ID */
/* EN8811H PBUS write function */
void EN8811H_PbusRegWr(struct phy_device *phydev, unsigned long pbus_address, unsigned long pbus_data)
{
    struct mii_bus *mbus;
    #if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0))
    mbus = phydev->bus;
    #else
    mbus = phydev->mdio.bus;
    #endif

    ecnt_mii_cl22_write(mbus, EN8811H_PBUS_PHY_ID, 0x1F, (unsigned int)(pbus_address >> 6));
    ecnt_mii_cl22_write(mbus, EN8811H_PBUS_PHY_ID, (unsigned int)((pbus_address >> 2) & 0xf), (unsigned int)(pbus_data & 0xFFFF));
    ecnt_mii_cl22_write(mbus, EN8811H_PBUS_PHY_ID, 0x10, (unsigned int)(pbus_data >> 16));
    return;
}

/* EN8811H BUCK write function */
void EN8811H_BuckPbusRegWr(struct phy_device *phydev, unsigned long pbus_address, unsigned int pbus_data)
{
    phy_write(phydev, 0x1F, (unsigned int)4);        /* page 4 */
    phy_write(phydev, 0x10, (unsigned int)0);
    phy_write(phydev, 0x11, (unsigned int)((pbus_address >> 16) & 0xffff));
    phy_write(phydev, 0x12, (unsigned int)(pbus_address & 0xffff));
    phy_write(phydev, 0x13, (unsigned int)((pbus_data >> 16) & 0xffff));
    phy_write(phydev, 0x14, (unsigned int)(pbus_data & 0xffff));
}

/* EN8811H BUCK read function */
unsigned int EN8811H_BuckPbusRegRd(struct phy_device *phydev, unsigned long pbus_address)
{
    unsigned int pbus_data = 0;
    unsigned int pbus_data_low, pbus_data_high;

    phy_write(phydev, 0x1F, (unsigned int)4);        /* page 4 */
    phy_write(phydev, 0x10, (unsigned int)0);
    phy_write(phydev, 0x15, (unsigned int)((pbus_address >> 16) & 0xffff));
    phy_write(phydev, 0x16, (unsigned int)(pbus_address & 0xffff));

    pbus_data_high = phy_read(phydev, 0x17);
    pbus_data_low = phy_read(phydev, 0x18);
    pbus_data = (pbus_data_high << 16) + pbus_data_low;
    return pbus_data;
}

static void MDIOWriteBuf(struct phy_device *phydev, unsigned long address, unsigned long array_size, const unsigned char *buffer)
{
    unsigned int write_data, offset ;

    phy_write(phydev, 0x1F, (unsigned int)4);            /* page 4 */
    phy_write(phydev, 0x10, (unsigned int)0x8000);        /* address increment*/
    phy_write(phydev, 0x11, (unsigned int)((address >> 16) & 0xffff));
    phy_write(phydev, 0x12, (unsigned int)(address & 0xffff));

    for (offset = 0; offset < array_size; offset += 4)
    {
        write_data = (buffer[offset + 3] << 8) | buffer[offset + 2];
        phy_write(phydev, 0x13, write_data);
        write_data = (buffer[offset + 1] << 8) | buffer[offset];
        phy_write(phydev, 0x14, write_data);
    }
    phy_write(phydev, 0x1F, (unsigned int)0);
}

static int EN8811H_get_features(struct phy_device *phydev)
{
    int ret;

    EN8811H_PbusRegWr(phydev, 0xcf928 , 0x0);
    ret = genphy_read_abilities(phydev);
    if (ret)
    {
        return ret;
    }

    /* EN8811H supports 100M/1G/2.5G speed. */
    linkmode_clear_bit(ETHTOOL_LINK_MODE_10baseT_Half_BIT,
               phydev->supported);
    linkmode_clear_bit(ETHTOOL_LINK_MODE_10baseT_Full_BIT,
               phydev->supported);
    linkmode_set_bit(ETHTOOL_LINK_MODE_100baseT_Half_BIT,
               phydev->supported);
    linkmode_set_bit(ETHTOOL_LINK_MODE_100baseT_Full_BIT,
               phydev->supported);
    linkmode_set_bit(ETHTOOL_LINK_MODE_1000baseT_Full_BIT,
               phydev->supported);
    linkmode_set_bit(ETHTOOL_LINK_MODE_2500baseX_Full_BIT,
               phydev->supported);
    return 0;
}

int EN8811H_phy_probe(struct phy_device *phydev)
{
    u32 reg_value, pid1 = 0, pid2 = 0;
    u32 retry;
    struct mii_bus *mbus;

    #if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0))
    mbus = phydev->bus;
    #else
    mbus = phydev->mdio.bus;
    #endif

    EN8811H_PbusRegWr(phydev, 0xcf928 , 0x0);
    retry = MAX_RETRY;
    do
    {
        ecnt_mii_cl22_read(mbus, EN8811H_MDIO_PHY_ID, MII_PHYSID1 , &pid1);
        ecnt_mii_cl22_read(mbus, EN8811H_MDIO_PHY_ID, MII_PHYSID2 , &pid2);
        printk("[Airoha] %s(): PHY-%d = %x - %x\n", __func__, EN8811H_MDIO_PHY_ID, pid1, pid2);
        if ((EN8811H_PHY_ID1 == pid1) && (EN8811H_PHY_ID2 == pid2))
        {
            break;
        }
        retry--;
    }while (retry);
    if (0 == retry)
    {
        printk( "[Airoha] EN8811H dose not exist !\n");
        return 0;
    }

    EN8811H_BuckPbusRegWr(phydev, 0x0f0018, 0x0);
    /* Download DM */
    MDIOWriteBuf(phydev, 0x00000000, EthMD32_dm_size, EthMD32_dm);
    /* Download PM */
    MDIOWriteBuf(phydev, 0x00100000, EthMD32_pm_size, EthMD32_pm);
    EN8811H_BuckPbusRegWr(phydev, 0x0f0018, 0x01);

    retry = MAX_RETRY;
    do
    {
        mdelay(100);
        reg_value = EN8811H_BuckPbusRegRd(phydev, 0x3b30);
        if (EN8811H_PHY_READY == reg_value)
        {
            break;
        }
        retry--;
    }while (retry);
    if (0 == retry)
    {
        printk( "[Airoha] EN8811H initialize fail !\n");
        return 0;
    }
    /* LED setup */
    reg_value = EN8811H_BuckPbusRegRd(phydev, 0xcf8b8) | 0x30;
    EN8811H_BuckPbusRegWr(phydev, 0xcf8b8, reg_value);
    mdiobus_write45(mbus, EN8811H_MDIO_PHY_ID, 0x1F, 0x24, 0xc007);
    mdiobus_write45(mbus, EN8811H_MDIO_PHY_ID, 0x1F, 0x25, 0x003f);
    mdiobus_write45(mbus, EN8811H_MDIO_PHY_ID, 0x1F, 0x26, 0xc080);
    mdiobus_write45(mbus, EN8811H_MDIO_PHY_ID, 0x1F, 0x27, 0x00c0);
	/* Serdes polarity */
	reg_value = EN8811H_BuckPbusRegRd(phydev, 0xca0f8);
    reg_value = (reg_value & 0xfffffffc) | EN8811H_TX_POLARITY_NORMAL | EN8811H_RX_POLARITY_NORMAL;
    EN8811H_BuckPbusRegWr(phydev, 0xca0f8, reg_value);

    printk( "[Airoha] EN8811H initialize OK ! (1.0.3)\n");
    return 0;
}

static struct phy_driver en8811h_driver[] = {
{
    .phy_id         = EN8811H_PHY_ID,
    .name           = "Airoha EN8811H",
    .phy_id_mask    = 0x0ffffff0,
    .probe          = EN8811H_phy_probe,
    .get_features   = EN8811H_get_features,
} };

module_phy_driver(en8811h_driver);

static struct mdio_device_id __maybe_unused en8811h_tbl[] = {
    { EN8811H_PHY_ID, 0x0ffffff0 },
    { }
};

MODULE_DEVICE_TABLE(mdio, en8811h_tbl);
