#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include <velib/utils/ve_item_utils.h>
#include <velib/vecan/products.h>
#include <velib/utils/ve_logger.h>

#include "ble-dbus.h"
#include "safiery.h"
#include "task.h"

#define HW_ID_PRO			3
#define HW_ID_PRO_200			4
#define HW_ID_PRO_H2O			5
#define HW_ID_PRO_PLUS_BLE		8
#define HW_ID_PRO_PLUS_CELL		9
#define HW_ID_TOPDOWN_BLE		10
#define HW_ID_TOPDOWN_CELL		11
#define HW_ID_UNIVERSAL			12

#define FLUID_TYPE_FRESH_WATER		1
#define FLUID_TYPE_WASTE_WATER		2
#define FLUID_TYPE_LIVE_WELL		3
#define FLUID_TYPE_OIL				4
#define FLUID_TYPE_BLACK_WATER		5
#define FLUID_TYPE_GASOLINE			6
#define FLUID_TYPE_DIESEL			7
#define FLUID_TYPE_LPG				8
#define FLUID_TYPE_LNG				9
#define FLUID_TYPE_HYDRAULIC_OIL	10
#define FLUID_TYPE_RAW_WATER		11

struct safiery_model {
	uint32_t	hwid;
	const char	*type;
	const float	*coefs;
	uint32_t	flags;
};

#define SAFIERY_FLAG_BUTANE	(1 << 0)
#define SAFIERY_FLAG_TOPDOWN	(1 << 1)

static const struct safiery_model *safiery_get_model(uint32_t hwid);

static struct VeSettingProperties capacity_props = {
	.type			= VE_FLOAT,
	.def.value.Float	= 0.2,
	.min.value.Float	= 0,
	.max.value.Float	= 1000,
};

static struct VeSettingProperties fluid_type_props = {
	.type			= VE_SN32,
	.def.value.SN32		= 0,
	.min.value.SN32		= 0,
	.max.value.SN32		= INT32_MAX - 3,
};

static struct VeSettingProperties empty_props = {
	.type			= VE_FLOAT,
	.def.value.Float	= 0,
	.min.value.Float	= 0,
	.max.value.Float	= 500,
};

static struct VeSettingProperties full_props = {
	.type			= VE_FLOAT,
	.def.value.Float	= 20,
	.min.value.Float	= 0,
	.max.value.Float	= 500,
};

static const struct dev_setting safiery_settings[] = {
	{
		.name	= "Capacity",
		.props	= &capacity_props,
	},
	{
		.name	= "FluidType",
		.props	= &fluid_type_props,
	},
};

static const struct dev_setting safiery_bottomup_settings[] = {
	{
		.name	= "RawValueEmpty",
		.props	= &empty_props,
	},
	{
		.name	= "RawValueFull",
		.props	= &full_props,
	},
};

static const struct dev_setting safiery_topdown_settings[] = {
	{
		.name	= "RawValueEmpty",
		.props	= &full_props,
	},
	{
		.name	= "RawValueFull",
		.props	= &empty_props,
	},
};

static struct VeSettingProperties butane_props = {
	.type			= VE_SN32,
	.def.value.SN32		= 0,
	.min.value.SN32		= 0,
	.max.value.SN32		= 100,
};

static const struct dev_setting safiery_lpg_settings[] = {
	{
		.name	= "ButaneRatio",
		.props	= &butane_props,
	},
};

static int safiery_init(struct VeItem *root, void *data)
{
	int hwid = (int)data;
	const struct safiery_model *model;
	VeVariant v;

	model = safiery_get_model(hwid);
	if (!model)
		return -1;

	ble_dbus_set_str(root, "RawUnit", "cm");
	ble_dbus_set_item(root, "Remaining",
			  veVariantInvalidType(&v, VE_FLOAT), &veUnitm3);
	ble_dbus_set_item(root, "Level",
			  veVariantInvalidType(&v, VE_FLOAT), &veUnitNone);

	if (model->flags & SAFIERY_FLAG_BUTANE) {
		ble_dbus_add_settings(root, safiery_lpg_settings,
				      array_size(safiery_lpg_settings));
	}

	if (model->flags & SAFIERY_FLAG_TOPDOWN)
		ble_dbus_add_settings(root, safiery_topdown_settings,
				      array_size(safiery_topdown_settings));
	else
		ble_dbus_add_settings(root, safiery_bottomup_settings,
				      array_size(safiery_bottomup_settings));

	return 0;
}

static const struct dev_info safiery_sensor = {
	.product_id	= VE_PROD_ID_TANK_SENSOR,
	.dev_instance	= 20,
	.dev_prefix	= "safiery_",
	.role		= "tank",
	.num_settings	= array_size(safiery_settings),
	.settings	= safiery_settings,
	.init		= safiery_init,
};

static const float safiery_coefs_h2o[] = {
	0.600592, 0.003124, -0.00001368,
};

static const float safiery_coefs_lpg[] = {
	0.573045, -0.002822, -0.00000535,
};

static const float safiery_coefs_gasoline[] = {
	0.7373417462, -0.001978229885, 0.00000202162,
};

static const float safiery_coefs_air[] = {
	0.153096, 0.000327, -0.000000294,
};

static const float safiery_coefs_butane[] = {
	0.03615, 0.000815,
};

static const struct safiery_model safiery_models[] = {
	{
		/* Pro Check LPG bottom-up */
		.hwid	= HW_ID_PRO,
		.type	= "LPG",
		.coefs	= safiery_coefs_lpg,
		.flags	= SAFIERY_FLAG_BUTANE,
	},
	{
		/* Pro Check H2O, bottom-up */
		.hwid	= HW_ID_PRO_H2O,
		.type	= "H20",
		.coefs	= safiery_coefs_h2o,
	},
	{
		/* Pro-200, top-down */
		.hwid	= HW_ID_PRO_200,
		.type	= "Pro200",
		.coefs	= safiery_coefs_air,
		.flags	= SAFIERY_FLAG_TOPDOWN,
	},
	{
		/* PRO+ bottom-up, boosted BLE */
		.hwid	= HW_ID_PRO_PLUS_BLE,
		.type	= "PPB",
		.flags	= SAFIERY_FLAG_BUTANE,
	},
	{
		/* PRO+ bottom-up, Bluetooth + cellular */
		.hwid	= HW_ID_PRO_PLUS_CELL,
		.type	= "PPC",
		.flags	= SAFIERY_FLAG_BUTANE,
	},
	{
		/* TD-40 or TD-200, top-down, boosted BLE */
		.hwid	= HW_ID_TOPDOWN_BLE,
		.type	= "TDB",
		.coefs	= safiery_coefs_air,
		.flags	= SAFIERY_FLAG_TOPDOWN,
	},
	{
		/* TD-40 or TD-200, top-down, Bluetooth + cellular */
		.hwid	= HW_ID_TOPDOWN_CELL,
		.type	= "TDC",
		.coefs	= safiery_coefs_air,
		.flags	= SAFIERY_FLAG_TOPDOWN,
	},
	{
		/* Pro Check Universal, bottom-up */
		.hwid	= HW_ID_UNIVERSAL,
		.type	= "Univ",
		.flags	= SAFIERY_FLAG_BUTANE,
	},
};

static const struct safiery_model *safiery_get_model(uint32_t hwid)
{
	int i;

	for (i = 0; i < array_size(safiery_models); i++)
		if (safiery_models[i].hwid == hwid)
			return &safiery_models[i];

	return NULL;
}

static float safiery_scale_butane(struct VeItem *root, int temp)
{
	float r = veItemValueInt(root, "ButaneRatio") / 100.0;

	return safiery_coefs_butane[0] * r + safiery_coefs_butane[1] * r * temp;
}

static int safiery_xlate_level(struct VeItem *root, VeVariant *val, uint64_t rv)
{
	const struct safiery_model *model;
	const float *coefs;
	float scale = 0;
	float level;
	int hwid;
	int temp;
	int tank_level_ext;

	hwid = veItemValueInt(root, "HardwareID");
	temp = veItemValueInt(root, "Temperature");
	temp += 40;

	/*
	  Check for presence of extension bit on certain hardware/firmware.
	  It will always be 0 on old firmware/hardware where raw value
	  saturates at 16383.  When extension bit is set, the raw_value
	  resolution changes to 4us with 16384 us offet.  Thus old sensors
	  and firmware still have 0 to 16383 us range with 1us, and new
	  versions add the range 16384 us to 81916 us with 4 us
	  resolution.
	*/
	tank_level_ext = veItemValueInt(root, "TankLevelExtension");
	if (tank_level_ext)
		rv = 16384 + 4 * rv;

	model = safiery_get_model(hwid);
	if (!model)
		return -1;

	coefs = model->coefs;

	if (!coefs) {
		int fluid_type = veItemValueInt(root, "FluidType");

		switch (fluid_type) {
		case FLUID_TYPE_FRESH_WATER:
		case FLUID_TYPE_WASTE_WATER:
		case FLUID_TYPE_LIVE_WELL:
		case FLUID_TYPE_BLACK_WATER:
		case FLUID_TYPE_RAW_WATER:
			coefs = safiery_coefs_h2o;
			break;
		case FLUID_TYPE_LPG:
			coefs = safiery_coefs_lpg;
			break;
		case FLUID_TYPE_GASOLINE:
		case FLUID_TYPE_DIESEL:
			coefs = safiery_coefs_gasoline;
			break;
		default:
			return -1;
		}
	}

	if (coefs == safiery_coefs_lpg)
		scale = safiery_scale_butane(root, temp);

	scale += coefs[0] + coefs[1] * temp + coefs[2] * temp * temp;
	level = rv * scale;
	veVariantFloat(val, level / 10);

	return 0;
}

static const struct reg_info safiery_adv[] = {
	{
		.type	= VE_UN8,
		.offset	= 0,
		.mask	= 0x7f,
		.name	= "HardwareID",
		.format	= &veUnitNone,
	},
	{
		.type	= VE_UN8,
		.offset	= 0,
		.shift	= 7,
		.mask	= 1,
		.name	= "TankLevelExtension",
		.format	= &veUnitNone,
	},
	{
		.type	= VE_UN8,
		.offset	= 1,
		.mask	= 0x7f,
		.scale	= 32,
		.name	= "BatteryVoltage",
		.format = &veUnitVolt2Dec,
	},
	{
		.type	= VE_UN8,
		.offset	= 2,
		.mask	= 0x7f,
		.scale	= 1,
		.bias	= -40,
		.name	= "Temperature",
		.format	= &veUnitCelsius1Dec,
	},
	{
		.type	= VE_UN8,
		.offset	= 2,
		.shift	= 7,
		.mask	= 1,
		.name	= "SyncButton",
		.format = &veUnitNone,
	},
	{
		.type	= VE_UN16,
		.offset	= 3,
		.mask	= 0x3fff,
		.xlate	= safiery_xlate_level,
		.name	= "RawValue",
		.format	= &veUnitcm,
	},
	{
		.type	= VE_SN8,
		.offset	= 8,
		.scale	= 1024,
		.name	= "AccelX",
		.format	= &veUnitG2Dec,
	},
	{
		.type	= VE_SN8,
		.offset	= 9,
		.scale	= 1024,
		.name	= "AccelY",
		.format	= &veUnitG2Dec,
	},
	{
		.type	= VE_SN8,
		.offset	= 10,
		.shift	= 1024,
		.name	= "AccelZ",
		.format	= &veUnitG2Dec,
	},
};

static void safiery_update_level(struct VeItem *root)
{
	const struct safiery_model *model;
	struct VeItem *item;
	int hwid;
	float capacity;
	int height;
	int empty;
	int full;
	float level;
	float remain;
	VeVariant v;

	hwid = veItemValueInt(root, "HardwareID");
	model = safiery_get_model(hwid);
	if (!model)
		return;

	item = veItemByUid(root, "Capacity");
	if (!item)
		return;

	veItemLocalValue(item, &v);
	veVariantToFloat(&v);
	capacity = v.value.Float;

	height = veItemValueInt(root, "RawValue");
	empty = veItemValueInt(root, "RawValueEmpty");
	full = veItemValueInt(root, "RawValueFull");

	if (model->flags & SAFIERY_FLAG_TOPDOWN) {
		if (empty <= full)
			goto out_inval;
	} else {
		if (empty >= full)
			goto out_inval;
	}

	level = (float)(height - empty) / (full - empty);

	if (level < 0)
		level = 0;
	if (level > 1)
		level = 1;

	remain = level * capacity;

	ble_dbus_set_int(root, "Level", lrintf(100 * level));

	item = veItemByUid(root, "Remaining");
	veItemOwnerSet(item, veVariantFloat(&v, remain));

	return;

out_inval:
	veItemInvalidate(veItemByUid(root, "Level"));
	veItemInvalidate(veItemByUid(root, "Remaining"));
	ble_dbus_set_int(root, "Status", 4);
}

int safiery_handle_mfg(const bdaddr_t *addr, const uint8_t *buf, int len)
{
	struct VeItem *root;
	const uint8_t *uid = buf + 5;
	const struct safiery_model *model;
	char name[24];
	char dev[16];
	int hwid;

	if (len != 10)
		return -1;

	if (uid[0] != addr->b[2] ||
	    uid[1] != addr->b[1] ||
	    uid[2] != addr->b[0])
		return -1;

	hwid = buf[0];

	model = safiery_get_model(hwid);
	if (!model)
		return -1;

	snprintf(dev, sizeof(dev), "%02x%02x%02x%02x%02x%02x",
		 addr->b[5], addr->b[4], addr->b[3],
		 addr->b[2], addr->b[1], addr->b[0]);

	root = ble_dbus_create(dev, &safiery_sensor, (void *)hwid);
	if (!root)
		return -1;

	snprintf(name, sizeof(name), "Safiery %s %02X:%02X:%02X",
		 model->type, uid[0], uid[1], uid[2]);
	ble_dbus_set_name(root, name);

	if (!ble_dbus_is_enabled(root))
		return 0;

	ble_dbus_set_regs(root, safiery_adv, array_size(safiery_adv), buf, len);

	safiery_update_level(root);
	ble_dbus_update(root);

	return 0;
}

