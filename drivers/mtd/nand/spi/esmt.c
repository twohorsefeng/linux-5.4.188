// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016-2017 ESMT Technology, Inc.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mtd/spinand.h>

#define SPINAND_MFR_ESMT_C8	                0xC8
#define SPINAND_MFR_ESMT_2C		            0x2C

#define ESMT_C8_1_STATUS_ECC_MASK           (7 << 4)
#define ESMT_C8_1_STATUS_ECC_NO_BITFLIPS	(0 << 4)
#define ESMT_C8_1_STATUS_ECC_1_3_BITFLIPS	(1 << 4)
#define ESMT_C8_1_STATUS_ECC_UNCOR_ERROR	(2 << 4)
#define ESMT_C8_1_STATUS_ECC_4_6_BITFLIPS	(3 << 4)
#define ESMT_C8_1_STATUS_ECC_7_8_BITFLIPS	(5 << 4)

#define ESMT_2C_STATUS_ECC_MASK		   GENMASK(7, 4)
#define ESMT_2C_STATUS_ECC_NO_BITFLIPS	    (0 << 4)
#define ESMT_2C_STATUS_ECC_1TO3_BITFLIPS	(1 << 4)
#define ESMT_2C_STATUS_ECC_4TO6_BITFLIPS	(3 << 4)
#define ESMT_2C_STATUS_ECC_7TO8_BITFLIPS	(5 << 4)

#define ESMT_2C_CFG_CR			            BIT(0)

static SPINAND_OP_VARIANTS(read_cache_variants,
		SPINAND_PAGE_READ_FROM_CACHE_QUADIO_OP(0, 2, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_X4_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_DUALIO_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_X2_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_OP(true, 0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_OP(false, 0, 1, NULL, 0));

static SPINAND_OP_VARIANTS(dummy2_read_cache_variants,
	   SPINAND_PAGE_READ_FROM_CACHE_QUADIO_OP(0, 2, NULL, 0),
	   SPINAND_PAGE_READ_FROM_CACHE_X4_OP(0, 1, NULL, 0),
	   SPINAND_PAGE_READ_FROM_CACHE_DUALIO_OP(0, 1, NULL, 0),
	   SPINAND_PAGE_READ_FROM_CACHE_X2_OP(0, 1, NULL, 0),
	   SPINAND_PAGE_READ_FROM_CACHE_OP(true, 0, 1, NULL, 0),
	   SPINAND_PAGE_READ_FROM_CACHE_OP(false, 0, 1, NULL, 0));

static SPINAND_OP_VARIANTS(write_cache_variants,
	   SPINAND_PROG_LOAD_X4(true, 0, NULL, 0),
	   SPINAND_PROG_LOAD(true, 0, NULL, 0));

static SPINAND_OP_VARIANTS(update_cache_variants,
	   SPINAND_PROG_LOAD_X4(false, 0, NULL, 0),
	   SPINAND_PROG_LOAD(false, 0, NULL, 0));

static int esmt_c8_1_ooblayout_ecc(struct mtd_info *mtd, int section,
				                   struct mtd_oob_region *region)
{
	if (section > 3)
		return -ERANGE;

	region->offset = (16 * section) + 8;
	region->length = 8;

	return 0;
}

static int esmt_c8_1_ooblayout_free(struct mtd_info *mtd, int section,
				                    struct mtd_oob_region *region)
{
	if (section > 3)
		return -ERANGE;

	region->offset = (16 * section) + 2;
	region->length = 6;

	return 0;
}

static const struct mtd_ooblayout_ops esmt_c8_1_ooblayout = {
	.ecc = esmt_c8_1_ooblayout_ecc,
	.free = esmt_c8_1_ooblayout_free,
};

static int esmt_c8_2_ooblayout_ecc(struct mtd_info *mtd, int section,
				                   struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	region->offset = 64;
	region->length = 64;

	return 0;
}

static int esmt_c8_2_ooblayout_free(struct mtd_info *mtd, int section,
				                    struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	/* Reserve 1 bytes for the BBM. */
	region->offset = 1;
	region->length = 63;

	return 0;
}

static const struct mtd_ooblayout_ops esmt_c8_2_ooblayout = {
	.ecc = esmt_c8_2_ooblayout_ecc,
	.free = esmt_c8_2_ooblayout_free,
};

static int esmt_2c_8_ooblayout_ecc(struct mtd_info *mtd, int section,
				                   struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	region->offset = mtd->oobsize / 2;
	region->length = mtd->oobsize / 2;

	return 0;
}

static int esmt_2c_8_ooblayout_free(struct mtd_info *mtd, int section,
				                    struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	/* Reserve 2 bytes for the BBM. */
	region->offset = 2;
	region->length = (mtd->oobsize / 2) - 2;

	return 0;
}

static const struct mtd_ooblayout_ops esmt_2c_8_ooblayout = {
	.ecc = esmt_2c_8_ooblayout_ecc,
	.free = esmt_2c_8_ooblayout_free,
};

static int esmt_c8_1_ecc_get_status(struct spinand_device *spinand, u8 status)
{
	switch (status & ESMT_C8_1_STATUS_ECC_MASK) {
	case ESMT_C8_1_STATUS_ECC_NO_BITFLIPS:
		return 0;

	case ESMT_C8_1_STATUS_ECC_1_3_BITFLIPS:
		/* 1-3 bits are flipped. return the maximum. */
		return 3;

	case ESMT_C8_1_STATUS_ECC_UNCOR_ERROR:
		return -EBADMSG;

	case ESMT_C8_1_STATUS_ECC_4_6_BITFLIPS:
		/* 4-6 bits are flipped. return the maximum. */
		return 6;

	case ESMT_C8_1_STATUS_ECC_7_8_BITFLIPS:
		/* 7-8 bits are flipped. return the maximum. */
		return 8;

	default:
		break;
	}

	return -EINVAL;
}

static int esmt_2c_8_ecc_get_status(struct spinand_device *spinand, u8 status)
{
	switch (status & ESMT_2C_STATUS_ECC_MASK) {
	case STATUS_ECC_NO_BITFLIPS:
		return 0;

	case STATUS_ECC_UNCOR_ERROR:
		return -EBADMSG;

	case ESMT_2C_STATUS_ECC_1TO3_BITFLIPS:
		return 3;

	case ESMT_2C_STATUS_ECC_4TO6_BITFLIPS:
		return 6;

	case ESMT_2C_STATUS_ECC_7TO8_BITFLIPS:
		return 8;

	default:
		break;
	}

	return -EINVAL;
}

static const struct spinand_info esmt_c8_spinand_table[] = {
	// 2Gb 3.3V
	SPINAND_INFO("F50L2G41KA(2V)",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0x41),
		     NAND_MEMORG(1, 2048, 128, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&dummy2_read_cache_variants,
					                  &write_cache_variants,
					                  &update_cache_variants),
		     0,
		     SPINAND_ECCINFO(&esmt_c8_2_ooblayout, esmt_c8_1_ecc_get_status)),
	// 1Gb 3.3V
	SPINAND_INFO("F50L1G41LB(2M)",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0x01),
		     NAND_MEMORG(1, 2048, 64, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&dummy2_read_cache_variants,
					                  &write_cache_variants,
					                  &update_cache_variants),
		     0,
		     SPINAND_ECCINFO(&esmt_c8_1_ooblayout, NULL)),
};

static const struct spinand_info esmt_2c_spinand_table[] = {
	/* 2Gb 3.3V */
	SPINAND_INFO("F50L2G41XA(2B)",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x24),
		     NAND_MEMORG(1, 2048, 128, 64, 2048, 40, 2, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					                  &write_cache_variants,
					                  &update_cache_variants),
		     0,
		     SPINAND_ECCINFO(&esmt_2c_8_ooblayout, esmt_2c_8_ecc_get_status)),
	/* 4Gb 3.3V */
	SPINAND_INFO("F50L4G41XB(2X)",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x34),
		     NAND_MEMORG(1, 4096, 256, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					                  &write_cache_variants,
					                  &update_cache_variants),
		     SPINAND_HAS_CR_FEAT_BIT,
		     SPINAND_ECCINFO(&esmt_2c_8_ooblayout, esmt_2c_8_ecc_get_status)),
};

static const struct spinand_manufacturer_ops esmt_c8_spinand_manuf_ops = {
};

const struct spinand_manufacturer esmt_c8_spinand_manufacturer = {
	.id = SPINAND_MFR_ESMT_C8,
	.name = "ESMT_C8",
	.chips = esmt_c8_spinand_table,
	.nchips = ARRAY_SIZE(esmt_c8_spinand_table),
	.ops = &esmt_c8_spinand_manuf_ops,
};

static int esmt_2c_spinand_init(struct spinand_device *spinand)
{
	/*
	 * M70A device series enable Continuous Read feature at Power-up,
	 * which is not supported. Disable this bit to avoid any possible
	 * failure.
	 */
	if (spinand->flags & SPINAND_HAS_CR_FEAT_BIT)
		return spinand_upd_cfg(spinand, ESMT_2C_CFG_CR, 0);

	return 0;
}

static const struct spinand_manufacturer_ops esmt_2c_spinand_manuf_ops = {
	.init = esmt_2c_spinand_init,
};

const struct spinand_manufacturer esmt_2c_spinand_manufacturer = {
	.id = SPINAND_MFR_ESMT_2C,
	.name = "ESMT_2C",
	.chips = esmt_2c_spinand_table,
	.nchips = ARRAY_SIZE(esmt_2c_spinand_table),
	.ops = &esmt_2c_spinand_manuf_ops,
};