// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 MediaTek Inc. */

#include "mt7915.h"
#include "eeprom.h"
#include "mcu.h"

/** global debugfs **/

static int
mt7915_implicit_txbf_set(void *data, u64 val)
{
	struct mt7915_dev *dev = data;

	if (test_bit(MT76_STATE_RUNNING, &dev->mphy.state))
		return -EBUSY;

	dev->ibf = !!val;

	return mt7915_mcu_set_txbf(dev, MT_BF_TYPE_UPDATE);
}

static int
mt7915_implicit_txbf_get(void *data, u64 *val)
{
	struct mt7915_dev *dev = data;

	*val = dev->ibf;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_implicit_txbf, mt7915_implicit_txbf_get,
			 mt7915_implicit_txbf_set, "%lld\n");

/* test knob of system layer 1/2 error recovery */
static int mt7915_ser_trigger_set(void *data, u64 val)
{
	enum {
		SER_SET_RECOVER_L1 = 1,
		SER_SET_RECOVER_L2,
		SER_ENABLE = 2,
		SER_RECOVER
	};
	struct mt7915_dev *dev = data;
	int ret = 0;

	switch (val) {
	case SER_SET_RECOVER_L1:
	case SER_SET_RECOVER_L2:
		ret = mt7915_mcu_set_ser(dev, SER_ENABLE, BIT(val), 0);
		if (ret)
			return ret;

		return mt7915_mcu_set_ser(dev, SER_RECOVER, val, 0);
	default:
		break;
	}

	return ret;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_ser_trigger, NULL,
			 mt7915_ser_trigger_set, "%lld\n");

static int
mt7915_radar_trigger(void *data, u64 val)
{
	struct mt7915_dev *dev = data;

	return mt7915_mcu_rdd_cmd(dev, RDD_RADAR_EMULATE, 1, 0, 0);
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_radar_trigger, NULL,
			 mt7915_radar_trigger, "%lld\n");

static int
mt7915_fw_debug_set(void *data, u64 val)
{
	struct mt7915_dev *dev = data;
	enum {
		DEBUG_TXCMD = 62,
		DEBUG_CMD_RPT_TX,
		DEBUG_CMD_RPT_TRIG,
		DEBUG_SPL,
		DEBUG_RPT_RX,
	} debug;

	dev->fw_debug = !!val;

	mt7915_mcu_fw_log_2_host(dev, dev->fw_debug ? 2 : 0);

	for (debug = DEBUG_TXCMD; debug <= DEBUG_RPT_RX; debug++)
		mt7915_mcu_fw_dbg_ctrl(dev, debug, dev->fw_debug);

	return 0;
}

static int
mt7915_fw_debug_get(void *data, u64 *val)
{
	struct mt7915_dev *dev = data;

	*val = dev->fw_debug;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_fw_debug, mt7915_fw_debug_get,
			 mt7915_fw_debug_set, "%lld\n");

static void
mt7915_ampdu_stat_read_phy(struct mt7915_phy *phy,
			   struct seq_file *file)
{
	struct mt7915_dev *dev = file->private;
	bool ext_phy = phy != &dev->phy;
	int bound[15], range[4], i, n;

	if (!phy)
		return;

	/* Tx ampdu stat */
	for (i = 0; i < ARRAY_SIZE(range); i++)
		range[i] = mt76_rr(dev, MT_MIB_ARNG(ext_phy, i));

	for (i = 0; i < ARRAY_SIZE(bound); i++)
		bound[i] = MT_MIB_ARNCR_RANGE(range[i / 4], i % 4) + 1;

	seq_printf(file, "\nPhy %d\n", ext_phy);

	seq_printf(file, "Length: %8d | ", bound[0]);
	for (i = 0; i < ARRAY_SIZE(bound) - 1; i++)
		seq_printf(file, "%3d -%3d | ",
			   bound[i] + 1, bound[i + 1]);

	seq_puts(file, "\nCount:  ");
	n = ext_phy ? ARRAY_SIZE(dev->mt76.aggr_stats) / 2 : 0;
	for (i = 0; i < ARRAY_SIZE(bound); i++)
		seq_printf(file, "%8d | ", dev->mt76.aggr_stats[i + n]);
	seq_puts(file, "\n");

	seq_printf(file, "BA miss count: %d\n", phy->mib.ba_miss_cnt);
}

static void
mt7915_txbf_stat_read_phy(struct mt7915_phy *phy, struct seq_file *s)
{
	static const char * const bw[] = {
		"BW20", "BW40", "BW80", "BW160"
	};
	struct mib_stats *mib;

	if (!phy)
		return;

	mib = &phy->mib;

	/* Tx Beamformer monitor */
	seq_puts(s, "\nTx Beamformer applied PPDU counts: ");

	seq_printf(s, "iBF: %d, eBF: %d\n",
		   mib->tx_bf_ibf_ppdu_cnt,
		   mib->tx_bf_ebf_ppdu_cnt);

	/* Tx Beamformer Rx feedback monitor */
	seq_puts(s, "Tx Beamformer Rx feedback statistics: ");

	seq_printf(s, "All: %d, HE: %d, VHT: %d, HT: %d, ",
		   mib->tx_bf_rx_fb_all_cnt,
		   mib->tx_bf_rx_fb_he_cnt,
		   mib->tx_bf_rx_fb_vht_cnt,
		   mib->tx_bf_rx_fb_ht_cnt);

	seq_printf(s, "%s, NC: %d, NR: %d\n",
		   bw[mib->tx_bf_rx_fb_bw],
		   mib->tx_bf_rx_fb_nc_cnt,
		   mib->tx_bf_rx_fb_nr_cnt);

	/* Tx Beamformee Rx NDPA & Tx feedback report */
	seq_printf(s, "Tx Beamformee successful feedback frames: %d\n",
		   mib->tx_bf_fb_cpl_cnt);
	seq_printf(s, "Tx Beamformee feedback triggered counts: %d\n",
		   mib->tx_bf_fb_trig_cnt);

	/* Tx SU & MU counters */
	seq_printf(s, "Tx multi-user Beamforming counts: %d\n",
		   mib->tx_bf_cnt);
	seq_printf(s, "Tx multi-user MPDU counts: %d\n", mib->tx_mu_mpdu_cnt);
	seq_printf(s, "Tx multi-user successful MPDU counts: %d\n",
		   mib->tx_mu_acked_mpdu_cnt);
	seq_printf(s, "Tx single-user successful MPDU counts: %d\n",
		   mib->tx_su_acked_mpdu_cnt);

	seq_puts(s, "\n");
}

static int
mt7915_tx_stats_show(struct seq_file *file, void *data)
{
	struct mt7915_dev *dev = file->private;
	int stat[8], i, n;

	mt7915_ampdu_stat_read_phy(&dev->phy, file);
	mt7915_txbf_stat_read_phy(&dev->phy, file);

	mt7915_ampdu_stat_read_phy(mt7915_ext_phy(dev), file);
	mt7915_txbf_stat_read_phy(mt7915_ext_phy(dev), file);

	/* Tx amsdu info */
	seq_puts(file, "Tx MSDU statistics:\n");
	for (i = 0, n = 0; i < ARRAY_SIZE(stat); i++) {
		stat[i] = mt76_rr(dev,  MT_PLE_AMSDU_PACK_MSDU_CNT(i));
		n += stat[i];
	}

	for (i = 0; i < ARRAY_SIZE(stat); i++) {
		seq_printf(file, "AMSDU pack count of %d MSDU in TXD: 0x%x ",
			   i + 1, stat[i]);
		if (n != 0)
			seq_printf(file, "(%d%%)\n", stat[i] * 100 / n);
		else
			seq_puts(file, "\n");
	}

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(mt7915_tx_stats);

static int
mt7915_queues_acq(struct seq_file *s, void *data)
{
	struct mt7915_dev *dev = dev_get_drvdata(s->private);
	int i;

	for (i = 0; i < 16; i++) {
		int j, acs = i / 4, index = i % 4;
		u32 ctrl, val, qlen = 0;

		val = mt76_rr(dev, MT_PLE_AC_QEMPTY(acs, index));
		ctrl = BIT(31) | BIT(15) | (acs << 8);

		for (j = 0; j < 32; j++) {
			if (val & BIT(j))
				continue;

			mt76_wr(dev, MT_PLE_FL_Q0_CTRL,
				ctrl | (j + (index << 5)));
			qlen += mt76_get_field(dev, MT_PLE_FL_Q3_CTRL,
					       GENMASK(11, 0));
		}
		seq_printf(s, "AC%d%d: queued=%d\n", acs, index, qlen);
	}

	return 0;
}

static int
mt7915_queues_read(struct seq_file *s, void *data)
{
	struct mt7915_dev *dev = dev_get_drvdata(s->private);
	struct mt76_phy *mphy_ext = dev->mt76.phy2;
	struct mt76_queue *ext_q = mphy_ext ? mphy_ext->q_tx[MT_TXQ_BE] : NULL;
	struct {
		struct mt76_queue *q;
		char *queue;
	} queue_map[] = {
		{ dev->mphy.q_tx[MT_TXQ_BE],	 "WFDMA0" },
		{ ext_q,			 "WFDMA1" },
		{ dev->mphy.q_tx[MT_TXQ_BE],	 "WFDMA0" },
		{ dev->mt76.q_mcu[MT_MCUQ_WM],	 "MCUWM"  },
		{ dev->mt76.q_mcu[MT_MCUQ_WA],	 "MCUWA"  },
		{ dev->mt76.q_mcu[MT_MCUQ_FWDL], "MCUFWQ" },
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(queue_map); i++) {
		struct mt76_queue *q = queue_map[i].q;

		if (!q)
			continue;

		seq_printf(s,
			   "%s:	queued=%d head=%d tail=%d\n",
			   queue_map[i].queue, q->queued, q->head,
			   q->tail);
	}

	return 0;
}

static void
mt7915_puts_rate_txpower(struct seq_file *s, struct mt7915_phy *phy)
{
	static const char * const sku_group_name[] = {
		"CCK", "OFDM", "HT20", "HT40",
		"VHT20", "VHT40", "VHT80", "VHT160",
		"RU26", "RU52", "RU106", "RU242/SU20",
		"RU484/SU40", "RU996/SU80", "RU2x996/SU160"
	};
	s8 txpower[MT7915_SKU_RATE_NUM], *buf;
	int i;

	if (!phy)
		return;

	seq_printf(s, "\nBand %d\n", phy != &phy->dev->phy);

	mt7915_mcu_get_txpower_sku(phy, txpower, sizeof(txpower));
	for (i = 0, buf = txpower; i < ARRAY_SIZE(mt7915_sku_group_len); i++) {
		u8 mcs_num = mt7915_sku_group_len[i];

		if (i >= SKU_VHT_BW20 && i <= SKU_VHT_BW160)
			mcs_num = 10;

		mt76_seq_puts_array(s, sku_group_name[i], buf, mcs_num);
		buf += mt7915_sku_group_len[i];
	}
}

static int
mt7915_read_rate_txpower(struct seq_file *s, void *data)
{
	struct mt7915_dev *dev = dev_get_drvdata(s->private);

	mt7915_puts_rate_txpower(s, &dev->phy);
	mt7915_puts_rate_txpower(s, mt7915_ext_phy(dev));

	return 0;
}

int mt7915_init_debugfs(struct mt7915_dev *dev)
{
	struct dentry *dir;

	dir = mt76_register_debugfs(&dev->mt76);
	if (!dir)
		return -ENOMEM;

	debugfs_create_devm_seqfile(dev->mt76.dev, "queues", dir,
				    mt7915_queues_read);
	debugfs_create_devm_seqfile(dev->mt76.dev, "acq", dir,
				    mt7915_queues_acq);
	debugfs_create_file("tx_stats", 0400, dir, dev, &mt7915_tx_stats_fops);
	debugfs_create_file("fw_debug", 0600, dir, dev, &fops_fw_debug);
	debugfs_create_file("implicit_txbf", 0600, dir, dev,
			    &fops_implicit_txbf);
	debugfs_create_u32("dfs_hw_pattern", 0400, dir, &dev->hw_pattern);
	/* test knobs */
	debugfs_create_file("radar_trigger", 0200, dir, dev,
			    &fops_radar_trigger);
	debugfs_create_file("ser_trigger", 0200, dir, dev, &fops_ser_trigger);
	debugfs_create_devm_seqfile(dev->mt76.dev, "txpower_sku", dir,
				    mt7915_read_rate_txpower);

	return 0;
}

#ifdef CONFIG_MAC80211_DEBUGFS
/** per-station debugfs **/

static int mt7915_sta_fixed_rate_set(void *data, u64 rate)
{
	struct ieee80211_sta *sta = data;
	struct mt7915_sta *msta = (struct mt7915_sta *)sta->drv_priv;

	/* usage: <he ltf> <tx mode> <ldpc> <stbc> <bw> <gi> <nss> <mcs>
	 * <tx mode>: see enum mt76_phy_type
	 */
	return mt7915_mcu_set_fixed_rate(msta->vif->phy->dev, sta, rate);
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_fixed_rate, NULL,
			 mt7915_sta_fixed_rate_set, "%llx\n");

void mt7915_sta_add_debugfs(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta, struct dentry *dir)
{
	debugfs_create_file("fixed_rate", 0600, dir, sta, &fops_fixed_rate);
}
#endif
