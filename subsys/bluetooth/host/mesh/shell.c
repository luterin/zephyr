/** @file
 *  @brief Bluetooth Mesh shell
 *
 */

/*
 * Copyright (c) 2017 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <zephyr.h>
#include <shell/shell.h>
#include <misc/printk.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/mesh.h>

static u16_t local = BT_MESH_ADDR_UNASSIGNED;
static u16_t dst = BT_MESH_ADDR_UNASSIGNED;
static u16_t net_idx;

static struct bt_mesh_cfg cfg_srv = {
	.relay = BT_MESH_RELAY_DISABLED,
	.beacon = BT_MESH_BEACON_DISABLED,
#if defined(CONFIG_BT_MESH_FRIEND)
	.frnd = BT_MESH_FRIEND_DISABLED,
#else
	.frnd = BT_MESH_FRIEND_NOT_SUPPORTED,
#endif
#if defined(CONFIG_BT_MESH_GATT_PROXY)
	.gatt_proxy = BT_MESH_GATT_PROXY_DISABLED,
#else
	.gatt_proxy = BT_MESH_GATT_PROXY_NOT_SUPPORTED,
#endif

	.default_ttl = 7,

	/* 3 transmissions with 20ms interval */
	.net_transmit = BT_MESH_TRANSMIT(2, 20),
	.relay_retransmit = BT_MESH_TRANSMIT(2, 20),
};

static struct bt_mesh_health health_srv = {
};

static struct bt_mesh_cfg_cli cfg_cli = {
};

static const u8_t dev_uuid[16] = { 0xdd, 0xdd };

static struct bt_mesh_model root_models[] = {
	BT_MESH_MODEL_CFG_SRV(&cfg_srv),
	BT_MESH_MODEL_CFG_CLI(&cfg_cli),
	BT_MESH_MODEL_HEALTH_SRV(&health_srv),
};

static struct bt_mesh_elem elements[] = {
	BT_MESH_ELEM(0, root_models, BT_MESH_MODEL_NONE),
};

static const struct bt_mesh_comp comp = {
	.cid = 0xffff,
	.elem = elements,
	.elem_count = ARRAY_SIZE(elements),
};

static void prov_complete(u16_t addr)
{
	printk("Local node provisioned, primary address 0x%04x\n", addr);
	local = addr;
	dst = addr;
}

static int output_number(bt_mesh_output_action action, uint32_t number)
{
	printk("OOB Number: %u\n", number);
	return 0;
}

static int output_string(const char *str)
{
	printk("OOB String: %s\n", str);
	return 0;
}

static bt_mesh_input_action input_act;
static u8_t input_size;

static int cmd_input_num(int argc, char *argv[])
{
	int err;

	if (argc < 2) {
		return -EINVAL;
	}

	if (input_act != BT_MESH_ENTER_NUMBER) {
		printk("A number hasn't been requested!\n");
		return 0;
	}

	if (strlen(argv[1]) < input_size) {
		printk("Too short input (%u digits required)\n",
		       input_size);
		return 0;
	}

	err = bt_mesh_input_number(strtoul(argv[1], NULL, 10));
	if (err) {
		printk("Numeric input failed (err %d)\n", err);
		return 0;
	}

	input_act = BT_MESH_NO_INPUT;
	return 0;
}

static int cmd_input_str(int argc, char *argv[])
{
	int err;

	if (argc < 2) {
		return -EINVAL;
	}

	if (input_act != BT_MESH_ENTER_STRING) {
		printk("A string hasn't been requested!\n");
		return 0;
	}

	if (strlen(argv[1]) < input_size) {
		printk("Too short input (%u characters required)\n",
		       input_size);
		return 0;
	}

	err = bt_mesh_input_string(argv[1]);
	if (err) {
		printk("String input failed (err %d)\n", err);
		return 0;
	}

	input_act = BT_MESH_NO_INPUT;
	return 0;
}

static int input(bt_mesh_input_action act, u8_t size)
{
	switch (act) {
	case BT_MESH_ENTER_NUMBER:
		printk("Enter a number (max %u digits) with: input-num <num>\n",
		       size);
		break;
	case BT_MESH_ENTER_STRING:
		printk("Enter a string (max %u chars) with: input-str <str>\n",
		       size);
		break;
	default:
		printk("Unknown input action %u (size %u) requested!\n",
		       act, size);
		return -EINVAL;
	}

	input_act = act;
	input_size = size;
	return 0;
}

static void link_open(void)
{
	printk("Provisioning link opened\n");
}

static void link_close(void)
{
	printk("Provisioning link closed\n");
}

static const u8_t static_val[] = {
	0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef
};

static const struct bt_mesh_prov prov = {
	.uuid = dev_uuid,
	.link_open = link_open,
	.link_close = link_close,
	.complete = prov_complete,
	.static_val = static_val,
	.static_val_len = sizeof(static_val),
	.output_size = 6,
	.output_actions = (BT_MESH_DISPLAY_NUMBER | BT_MESH_DISPLAY_STRING),
	.output_number = output_number,
	.output_string = output_string,
	.input_size = 6,
	.input_actions = (BT_MESH_ENTER_NUMBER | BT_MESH_ENTER_STRING),
	.input = input,
};

static int cmd_init(int argc, char *argv[])
{
	int err;

	err = bt_mesh_init(&prov, &comp);
	if (err) {
		printk("Mesh initialization failed (err %d)\n", err);
	}

	return 0;
}

static int cmd_reset(int argc, char *argv[])
{
	bt_mesh_reset();
	printk("Local node reset complete\n");
	return 0;
}

static bool str2bool(const char *str)
{
	return (!strcmp(str, "on") || !strcmp(str, "enable"));
}

#if defined(CONFIG_BT_MESH_LOW_POWER)
static int cmd_lpn(int argc, char *argv[])
{
	static bool enabled;
	int err;

	if (argc < 2) {
		printk("%s\n", enabled ? "enabled" : "disabled");
		return 0;
	}

	if (str2bool(argv[1])) {
		if (enabled) {
			printk("LPN already enabled\n");
			return 0;
		}

		err = bt_mesh_lpn_set(true);
		if (err) {
			printk("Enabling LPN failed (err %d)\n", err);
		} else {
			enabled = true;
		}
	} else {
		if (!enabled) {
			printk("LPN already disabled\n");
			return 0;
		}

		err = bt_mesh_lpn_set(false);
		if (err) {
			printk("Enabling LPN failed (err %d)\n", err);
		} else {
			enabled = false;
		}
	}

	return 0;
}
#endif /* MESH_LOW_POWER */

#if defined(CONFIG_BT_MESH_GATT_PROXY)
static int cmd_ident(int argc, char *argv[])
{
	int err;

	err = bt_mesh_proxy_identity_enable();
	if (err) {
		printk("Failed advertise using Node Identity (err %d)\n", err);
	}

	return 0;
}
#endif /* MESH_GATT_PROXY */

static int cmd_get_comp(int argc, char *argv[])
{
	struct net_buf_simple *comp = NET_BUF_SIMPLE(32);
	u8_t status, page = 0x00;
	int err;

	if (argc > 1) {
		page = strtol(argv[1], NULL, 0);
	}

	net_buf_simple_init(comp, 0);
	err = bt_mesh_cfg_comp_data_get(net_idx, dst, page, &status, comp);
	if (err) {
		printk("Getting composition failed (err %d)\n", err);
		return 0;
	}

	if (status != 0x00) {
		printk("Got non-success status 0x%02x\n", status);
		return 0;
	}

	printk("Got Composition Data for 0x%04x:\n", dst);
	printk("\tCID      0x%04x\n", net_buf_simple_pull_le16(comp));
	printk("\tPID      0x%04x\n", net_buf_simple_pull_le16(comp));
	printk("\tVID      0x%04x\n", net_buf_simple_pull_le16(comp));
	printk("\tCRPL     0x%04x\n", net_buf_simple_pull_le16(comp));
	printk("\tFeatures 0x%04x\n", net_buf_simple_pull_le16(comp));

	while (comp->len > 4) {
		u8_t sig, vnd;
		u16_t loc;
		int i;

		loc = net_buf_simple_pull_le16(comp);
		sig = net_buf_simple_pull_u8(comp);
		vnd = net_buf_simple_pull_u8(comp);

		printk("\n\tElement @ 0x%04x:\n", loc);

		if (comp->len < ((sig * 2) + (vnd * 4))) {
			printk("\t\t...truncated data!\n");
			break;
		}

		if (sig) {
			printk("\t\tSIG Models:\n");
		} else {
			printk("\t\tNo SIG Models\n");
		}

		for (i = 0; i < sig; i++) {
			u16_t mod_id = net_buf_simple_pull_le16(comp);

			printk("\t\t\t0x%04x\n", mod_id);
		}

		if (vnd) {
			printk("\t\tVendor Models:\n");
		} else {
			printk("\t\tNo Vendor Models\n");
		}

		for (i = 0; i < vnd; i++) {
			u16_t cid = net_buf_simple_pull_le16(comp);
			u16_t mod_id = net_buf_simple_pull_le16(comp);

			printk("\t\t\tCompany 0x%04x: 0x%04x\n", cid, mod_id);
		}
	}

	return 0;
}

static int cmd_dst(int argc, char *argv[])
{
	if (argc < 2) {
		printk("Destination address: 0x%04x%s\n", dst,
		       dst == local ? " (local)" : "");
		return 0;
	}

	if (!strcmp(argv[1], "local")) {
		dst = local;
	} else {
		dst = strtoul(argv[1], NULL, 0);
	}

	printk("Destination address set to 0x%04x%s\n", dst,
	       dst == local ? " (local)" : "");
	return 0;
}

static int cmd_netidx(int argc, char *argv[])
{
	if (argc < 2) {
		printk("NetIdx: 0x%04x\n", net_idx);
		return 0;
	}

	net_idx = strtoul(argv[1], NULL, 0);
	printk("NetIdx set to 0x%04x\n", net_idx);
	return 0;
}

static int cmd_beacon(int argc, char *argv[])
{
	u8_t status;
	int err;

	if (argc < 2) {
		err = bt_mesh_cfg_beacon_get(net_idx, dst, &status);
	} else {
		u8_t val = str2bool(argv[1]);

		err = bt_mesh_cfg_beacon_set(net_idx, dst, val, &status);
	}

	if (err) {
		printk("Unable to send Beacon Get/Set message (err %d)\n", err);
		return 0;
	}

	printk("Beacon state is 0x%02x\n", status);

	return 0;
}

static int cmd_ttl(int argc, char *argv[])
{
	u8_t ttl;
	int err;

	if (argc < 2) {
		err = bt_mesh_cfg_ttl_get(net_idx, dst, &ttl);
	} else {
		u8_t val = strtoul(argv[1], NULL, 0);

		err = bt_mesh_cfg_ttl_set(net_idx, dst, val, &ttl);
	}

	if (err) {
		printk("Unable to send Default TTL Get/Set (err %d)\n", err);
		return 0;
	}

	printk("Default TTL is 0x%02x\n", ttl);

	return 0;
}

static int cmd_friend(int argc, char *argv[])
{
	u8_t frnd;
	int err;

	if (argc < 2) {
		err = bt_mesh_cfg_friend_get(net_idx, dst, &frnd);
	} else {
		u8_t val = strtoul(argv[1], NULL, 0);

		err = bt_mesh_cfg_friend_set(net_idx, dst, val, &frnd);
	}

	if (err) {
		printk("Unable to send Friend Get/Set (err %d)\n", err);
		return 0;
	}

	printk("Friend is set to 0x%02x\n", frnd);

	return 0;
}

static int cmd_gatt_proxy(int argc, char *argv[])
{
	u8_t proxy;
	int err;

	if (argc < 2) {
		err = bt_mesh_cfg_gatt_proxy_get(net_idx, dst, &proxy);
	} else {
		u8_t val = strtoul(argv[1], NULL, 0);

		err = bt_mesh_cfg_gatt_proxy_set(net_idx, dst, val, &proxy);
	}

	if (err) {
		printk("Unable to send GATT Proxy Get/Set (err %d)\n", err);
		return 0;
	}

	printk("GATT Proxy is set to 0x%02x\n", proxy);

	return 0;
}

static int cmd_relay(int argc, char *argv[])
{
	u8_t relay, transmit;
	int err;

	if (argc < 2) {
		err = bt_mesh_cfg_relay_get(net_idx, dst, &relay, &transmit);
	} else {
		u8_t val = strtoul(argv[1], NULL, 0);
		u8_t count, interval, new_transmit;

		if (val) {
			if (argc > 2) {
				count = strtoul(argv[2], NULL, 0);
			} else {
				count = 2;
			}

			if (argc > 3) {
				interval = strtoul(argv[3], NULL, 0);
			} else {
				interval = 20;
			}

			new_transmit = BT_MESH_TRANSMIT(count, interval);
		} else {
			new_transmit = 0;
		}

		err = bt_mesh_cfg_relay_set(net_idx, dst, val, new_transmit,
					    &relay, &transmit);
	}

	if (err) {
		printk("Unable to send Relay Get/Set (err %d)\n", err);
		return 0;
	}

	printk("Relay is 0x%02x, Transmit 0x%02x (count %u interval %ums)\n",
	       relay, transmit, BT_MESH_TRANSMIT_COUNT(transmit),
	       BT_MESH_TRANSMIT_INT(transmit));

	return 0;
}

static int cmd_app_key_add(int argc, char *argv[])
{
	u8_t val[16] = { [0 ... 15] = 0xcc, };
	u16_t key_net_idx, key_app_idx;
	u8_t status;
	int err;

	if (argc < 3) {
		return -EINVAL;
	}

	key_net_idx = strtoul(argv[1], NULL, 0);
	key_app_idx = strtoul(argv[2], NULL, 0);

	/* TODO: decode key value that's given in hex */

	err = bt_mesh_cfg_app_key_add(net_idx, dst, key_net_idx, key_app_idx,
				      val, &status);
	if (err) {
		printk("Unable to send App Key Add (err %d)\n", err);
		return 0;
	}

	if (status) {
		printk("AppKeyAdd failed with status 0x%02x\n", status);
	} else {
		printk("AppKey added, NetKeyIndex 0x%04x AppKeyIndex 0x%04x\n",
		       key_net_idx, key_app_idx);
	}

	return 0;
}

static int cmd_mod_app_bind(int argc, char *argv[])
{
	u16_t elem_addr, mod_app_idx, mod_id, cid;
	u8_t status;
	int err;

	if (argc < 4) {
		return -EINVAL;
	}

	elem_addr = strtoul(argv[1], NULL, 0);
	mod_app_idx = strtoul(argv[2], NULL, 0);
	mod_id = strtoul(argv[3], NULL, 0);

	if (argc > 4) {
		cid = strtoul(argv[3], NULL, 0);
		err = bt_mesh_cfg_mod_app_bind_vnd(net_idx, dst, elem_addr,
						   mod_app_idx, mod_id, cid,
						   &status);
	} else {
		err = bt_mesh_cfg_mod_app_bind(net_idx, dst, elem_addr,
					       mod_app_idx, mod_id, &status);
	}

	if (err) {
		printk("Unable to send Model App Bind (err %d)\n", err);
		return 0;
	}

	if (status) {
		printk("Model App Bind failed with status 0x%02x\n", status);
	} else {
		printk("AppKey successfully bound\n");
	}

	return 0;
}

static int cmd_mod_sub_add(int argc, char *argv[])
{
	u16_t elem_addr, sub_addr, mod_id, cid;
	u8_t status;
	int err;

	if (argc < 4) {
		return -EINVAL;
	}

	elem_addr = strtoul(argv[1], NULL, 0);
	sub_addr = strtoul(argv[2], NULL, 0);
	mod_id = strtoul(argv[3], NULL, 0);

	if (argc > 4) {
		cid = strtoul(argv[3], NULL, 0);
		err = bt_mesh_cfg_mod_sub_add_vnd(net_idx, dst, elem_addr,
						  sub_addr, mod_id, cid,
						  &status);
	} else {
		err = bt_mesh_cfg_mod_sub_add(net_idx, dst, elem_addr,
					      sub_addr, mod_id, &status);
	}

	if (err) {
		printk("Unable to send Model Subscription Add (err %d)\n", err);
		return 0;
	}

	if (status) {
		printk("Model Subscription Add failed with status 0x%02x\n",
		       status);
	} else {
		printk("Model subscription was successful\n");
	}

	return 0;
}

static int cmd_hb_sub_set(int argc, char *argv[])
{
	u8_t status, period;
	u16_t sub_src, sub_dst;
	int err;

	if (argc < 4) {
		return -EINVAL;
	}

	sub_src = strtoul(argv[1], NULL, 0);
	sub_dst = strtoul(argv[2], NULL, 0);
	period = strtoul(argv[3], NULL, 0);

	err = bt_mesh_cfg_hb_sub_set(net_idx, dst, sub_src, sub_dst, period,
				     &status);
	if (err) {
		printk("Heartbeat Subscription Set failed (err %d)\n", err);
		return 0;
	}

	if (status) {
		printk("Heartbeat Subscription Set failed (status 0x%02x)\n",
		       status);
	} else {
		printk("Heartbeat subscription successfully set\n");
	}

	return 0;
}

static const struct shell_cmd mesh_commands[] = {
	{ "init", cmd_init, NULL },
	{ "reset", cmd_reset, NULL },
	{ "input-num", cmd_input_num, "<number>" },
	{ "input-str", cmd_input_str, "<string>" },
#if defined(CONFIG_BT_MESH_LOW_POWER)
	{ "lpn", cmd_lpn, "<value: off, on>" },
#endif
#if defined(CONFIG_BT_MESH_GATT_PROXY)
	{ "ident", cmd_ident, NULL },
#endif
	{ "dst", cmd_dst, "[destination address]" },
	{ "netidx", cmd_netidx, "[NetIdx]" },
	{ "get-comp", cmd_get_comp, "[page]" },
	{ "beacon", cmd_beacon, "[val: off, on]" },
	{ "ttl", cmd_ttl, "[ttl: 0x00, 0x02-0x7f]" },
	{ "friend", cmd_friend, "[val: off, on]" },
	{ "gatt-proxy", cmd_gatt_proxy, "[val: off, on]" },
	{ "relay", cmd_relay, "[val: off, on] [count: 0-7] [interval: 0-32]" },
	{ "app-key-add", cmd_app_key_add, "<NetKeyIndex> <AppKeyIndex> <val>" },
	{ "mod-app-bind", cmd_mod_app_bind,
		"<addr> <AppIndex> <Model ID> [Company ID]" },
	{ "mod-sub-add", cmd_mod_sub_add,
		"<elem addr> <sub addr> <Model ID> [Company ID]" },
	{ "hb-sub-set", cmd_hb_sub_set, "<src> <dst> <period>" },
	{ NULL, NULL, NULL}
};

SHELL_REGISTER("mesh", mesh_commands);
