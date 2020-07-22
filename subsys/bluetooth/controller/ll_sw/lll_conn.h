/*
 * Copyright (c) 2018-2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#if defined(CONFIG_BT_CTLR_CONN_META)
#include "lll_conn_meta.h"
#endif /* CONFIG_BT_CTLR_CONN_META */

#define LLL_CONN_RSSI_SAMPLE_COUNT 10
#define LLL_CONN_RSSI_THRESHOLD    4

#define LLL_CONN_MIC_NONE 0
#define LLL_CONN_MIC_PASS 1
#define LLL_CONN_MIC_FAIL 2

struct lll_tx {
	uint16_t handle;
	void *node;
};

struct node_tx {
	union {
		void        *next;
		memq_link_t *link;
	};

	uint8_t pdu[];
};

struct lll_conn {
	struct lll_hdr hdr;

	uint8_t access_addr[4];
	uint8_t crc_init[3];

	uint16_t handle;
	uint16_t interval;
	uint16_t latency;

	uint16_t latency_prepare;
	uint16_t latency_event;
	uint16_t event_counter;

	uint8_t data_chan_map[5];
	uint8_t data_chan_count:6;
	uint8_t data_chan_sel:1;
	uint8_t role:1;

	union {
		struct {
			uint8_t data_chan_hop;
			uint8_t data_chan_use;
		};

		uint16_t data_chan_id;
	};

#if defined(CONFIG_BT_PERIPHERAL)
	struct {
		uint8_t  latency_enabled:1;
		uint32_t window_widening_periodic_us;
		uint32_t window_widening_max_us;
		uint32_t window_widening_prepare_us;
		uint32_t window_widening_event_us;
		uint32_t window_size_prepare_us;
		uint32_t window_size_event_us;
	} slave;
#endif /* CONFIG_BT_PERIPHERAL */

#if defined(CONFIG_BT_CTLR_DATA_LENGTH)
	uint16_t max_tx_octets;
	uint16_t max_rx_octets;

#if defined(CONFIG_BT_CTLR_PHY)
	uint16_t max_tx_time;
	uint16_t max_rx_time;
#endif /* CONFIG_BT_CTLR_PHY */
#endif /* CONFIG_BT_CTLR_DATA_LENGTH */

#if defined(CONFIG_BT_CTLR_PHY)
	uint8_t phy_tx:3;
	uint8_t phy_flags:1;
	uint8_t phy_tx_time:3;
	uint8_t phy_rx:3;
#endif /* CONFIG_BT_CTLR_PHY */

	MEMQ_DECLARE(tx);
	memq_link_t link_tx;
	memq_link_t *link_tx_free;
	uint8_t  packet_tx_head_len;
	uint8_t  packet_tx_head_offset;

	uint8_t sn:1;
	uint8_t nesn:1;
	uint8_t empty:1;

#if defined(CONFIG_BT_CTLR_LE_ENC)
	uint8_t enc_rx:1;
	uint8_t enc_tx:1;

	struct ccm ccm_rx;
	struct ccm ccm_tx;
#endif /* CONFIG_BT_CTLR_LE_ENC */

#if defined(CONFIG_BT_CTLR_CONN_RSSI)
	uint8_t  rssi_latest;
#if defined(CONFIG_BT_CTLR_CONN_RSSI_EVENT)
	uint8_t  rssi_reported;
	uint8_t  rssi_sample_count;
#endif /* CONFIG_BT_CTLR_CONN_RSSI_EVENT */
#endif /* CONFIG_BT_CTLR_CONN_RSSI */

#if defined(CONFIG_BT_CTLR_CONN_META)
	struct lll_conn_meta conn_meta;
#endif /* CONFIG_BT_CTLR_CONN_META */

#if defined(CONFIG_BT_CTLR_TX_PWR_DYNAMIC_CONTROL)
	int8_t tx_pwr_lvl;
#endif
};

int lll_conn_init(void);
int lll_conn_reset(void);

uint8_t lll_conn_sca_local_get(void);
uint32_t lll_conn_ppm_local_get(void);
uint32_t lll_conn_ppm_get(uint8_t sca);
void lll_conn_prepare_reset(void);
void lll_conn_abort_cb(struct lll_prepare_param *prepare_param, void *param);
void lll_conn_isr_rx(void *param);
void lll_conn_isr_tx(void *param);
void lll_conn_rx_pkt_set(struct lll_conn *lll);
void lll_conn_tx_pkt_set(struct lll_conn *lll, struct pdu_data *pdu_data_tx);
void lll_conn_pdu_tx_prep(struct lll_conn *lll, struct pdu_data **pdu_data_tx);
void lll_conn_flush(uint16_t handle, struct lll_conn *lll);

extern void ull_conn_lll_ack_enqueue(uint16_t handle, struct node_tx *tx);
extern uint16_t ull_conn_lll_max_tx_octets_get(struct lll_conn *lll);
