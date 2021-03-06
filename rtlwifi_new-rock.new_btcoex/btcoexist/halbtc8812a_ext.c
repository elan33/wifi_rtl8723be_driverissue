

#include "halbt_precomp.h"
#include "halbtc8812a_ext.h"
#include <linux/kthread.h>





/*global for socket TRX, it is actually rtlpriv*/
struct rtl_priv *pbtcoexadapter = NULL;

void *safe_memcpy(void *dest, const void *src, u32 n , u32 max_len)
{
	if (n > max_len) {
		memcpy(dest, src, max_len);
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_WARNING,
			     "critical error in memcpy!\n");
	} else {
		/*ok case*/
		memcpy(dest, src, n);
	}
	return NULL;
}


void btinfo_evt_dump(struct btinfo_8761au *info)
{
	RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD, "cid:0x%02x, len:%u\n",
		     info->cid,
		     info->len);

	if (info->len > 2)
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
			     "byte2:%s%s%s%s%s%s%s%s\n"
			     , info->connection ? "connection " : ""
			     , info->scoe_sco ? "scoe_sco " : ""
			     , info->inq_page ? "inq_page " : ""
			     , info->acl_busy ? "acl_busy " : ""
			     , info->sco_busy ? "sco_busy " : ""
			     , info->hid ? "hid " : ""
			     , info->a2dp ? "a2dp " : ""
			     , info->ftp ? "ftp" : ""
			    );

	if (info->len > 3)
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD, "retry_cnt:%u\n",
			     info->retry_cnt);

	if (info->len > 4)
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD, "rssi:%u\n", info->rssi);

	if (info->len > 5)
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD, "byte5:%s%s\n"
			     , info->esco_sco ? "eSCO_SCO " : ""
			     , info->master_slave ? "Master_Slave " : "");

}


void rtl_btcoex_btinfo_cmd(struct rtl_priv *rtlpriv, u8 *buf, u16 buf_len)
{


	struct btinfo_8761au *info = (struct btinfo_8761au *)buf;
	u8 cmd_idx;
	u8 len;

	cmd_idx = info->cid;

	if (info->len > buf_len - 2) {
		WARN_ON(1);
		len = buf_len - 2;
	} else {
		len = info->len;
	}

	btinfo_evt_dump(info);

	/* transform BT-FW btinfo to WiFI-FW C2H format and notify */
	if (cmd_idx == BTINFO_WIFI_FETCH) {
		buf[1] = 0;
	} else if (cmd_idx == BTINFO_BT_AUTO_RPT) {
		buf[1] = 2;
	} else if (0x01 == cmd_idx || 0x02 == cmd_idx) {
		/* troy,it should run here */
		buf[1] = buf[0];
	}


	rtlpriv->btcoexist.btc_ops->btc_btinfo_notify(rtlpriv, &buf[1],
			len + 1);

}



u8 rtl_send_complete_event_to_BT(struct rtl_priv *rtlpriv,
				 enum RTW_HCI_EXT_CMD BT_RELATED_CMD,
				 enum RTL_HCI_STATUS status)
{
	struct rtl_hci_event *hci_event;
	u8 local_buf[6] = "";
	u8	len = 0, tx_event_length = 0;
	u8 *ret_par;
	u8 *event_data = NULL;

	RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
		     "#LEVEL_END,rtl_send_complete_event_to_BT\n");

	hci_event = (struct rtl_hci_event *)(&local_buf[0]);
	event_data = hci_event->data;
	hci_event->event_code = HCI_EVENT_COMMAND_COMPLETE;
	*event_data = 0x1;
	*(event_data + 1) = HCIOPCODELOW(BT_RELATED_CMD, OGF_EXTENSION);
	*(event_data + 2) = HCIOPCODEHIGHT(BT_RELATED_CMD, OGF_EXTENSION);

	len = len + 3;
	ret_par = &hci_event->data[len];
	ret_par[0] = status;
	len++;
	hci_event->length = len;
	/* total tx event length + event_code length + sizeof(length) */
	tx_event_length = hci_event->length + 2;
	rtl_btcoex_dump_tx_msg((u8 *)hci_event, tx_event_length,
			       "rtl_send_complete_event_to_BT");
	status = rtl_btcoex_sendmsgbysocket(rtlpriv, (u8 *)hci_event,
					    tx_event_length, false);
	return status;


}


u8 rtl_btcoex_parse_BT_info_notify_cmd(struct rtl_priv *rtlpriv, u8 *pcmd,
				       u16 cmdlen)
{

	u8 curPollEnable = pcmd[0];
	u8 curPollTime = pcmd[1];
	u8 btInfoReason = pcmd[2];
	u8 btInfoLen = pcmd[3];
	u8 btinfo[BT_INFO_LENGTH];


	enum RTL_HCI_STATUS status = HCI_STATUS_SUCCESS;


	RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD, "%s\n", __func__);
	RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
		     "current Poll Enable: %d, currrent Poll Time: %d\n",
		     curPollEnable, curPollTime);
	RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
		     "BT Info reason: %d, BT Info length: %d\n",
		     btInfoReason, btInfoLen);
	RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
		     "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
		     pcmd[4], pcmd[5], pcmd[6], pcmd[7],
		     pcmd[8], pcmd[9], pcmd[10], pcmd[11]);


	memset(btinfo, 0, BT_INFO_LENGTH);


	if (BT_INFO_LENGTH != btInfoLen) {
		status = HCI_STATUS_INVALID_HCI_CMD_PARA_VALUE;
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_WARNING,
			     "Error BT Info Length: %d\n",
			     btInfoLen);
	} else {
		if (0x1 == btInfoReason || 0x2 == btInfoReason) {
			safe_memcpy(btinfo, &pcmd[4], btInfoLen,
				    BT_INFO_LENGTH);
			btinfo[0] = btInfoReason;
			rtl_btcoex_btinfo_cmd(rtlpriv, btinfo, btInfoLen);
		} else {
			RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
				     "Other BT info reason\n");
		}
	}

	return rtl_send_complete_event_to_BT(rtlpriv, HCI_BT_INFO_NOTIFY,
					     status);

}



u8 rtl_btcoex_parse_BT_patch_ver_info_cmd(struct rtl_priv *rtlpriv, u8 *pcmd,
		u16 cmdlen)
{
	enum RTL_HCI_STATUS status = HCI_STATUS_SUCCESS;
	u16		btPatchVer = 0x0, btHciVer = 0x0;


	btHciVer = pcmd[0] | pcmd[1] << 8;
	btPatchVer = pcmd[2] | pcmd[3] << 8;


	RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD, "%s, cmd:%02x %02x %02x %02x\n",
		     __func__, pcmd[0] , pcmd[1] , pcmd[2] , pcmd[3]);
	RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD, "%s, HCI Ver:%d, Patch Ver:%d\n",
		     __func__, btHciVer, btPatchVer);

	rtlpriv->btcoexist.btc_ops->btc_set_bt_patch_version(btHciVer,
			btPatchVer);

	return rtl_send_complete_event_to_BT(rtlpriv,
					     HCI_BT_PATCH_VERSION_NOTIFY,
					     status);

}

u8 rtl_btcoex_parse_HCI_Ver_notify_cmd(struct rtl_priv *rtlpriv, u8 *pcmd,
				       u16 cmdlen)
{
	enum RTL_HCI_STATUS status = HCI_STATUS_SUCCESS;
	u16 hciver = pcmd[0] | pcmd[1] << 8;

	struct bt_coex_info *pcoex_info = &rtlpriv->coex_info;
	struct BT_MGNT *bt_mgnt = &pcoex_info->bt_mgnt;

	bt_mgnt->ext_config.hci_extension_ver = hciver;
	RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD, "%s, HCI Version: %d\n",
		     __func__,
		     bt_mgnt->ext_config.hci_extension_ver);
	if (bt_mgnt->ext_config.hci_extension_ver  < 4) {
		status = HCI_STATUS_INVALID_HCI_CMD_PARA_VALUE;
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
			     "%s, Version = %d, HCI Version < 4\n",
			     __func__, bt_mgnt->ext_config.hci_extension_ver);

	} else {
		rtlpriv->btcoexist.btc_ops->btc_set_hci_version(hciver);
	}

	return rtl_send_complete_event_to_BT(rtlpriv,
					     HCI_EXTENSION_VERSION_NOTIFY,
					     status);

}






u8 rtl_btcoex_parse_WIFI_scan_notify_cmd(struct rtl_priv *rtlpriv, u8 *pcmd,
		u16 cmdlen)
{
	enum RTL_HCI_STATUS status = HCI_STATUS_SUCCESS;

	struct bt_coex_info *pcoex_info = &rtlpriv->coex_info;
	struct BT_MGNT *bt_mgnt = &pcoex_info->bt_mgnt;

	bt_mgnt->ext_config.enable_wifi_scan_notify = pcmd[0];
	RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
		     "%s, enable_wifi_scan_notify: %d\n",
		     __func__, bt_mgnt->ext_config.enable_wifi_scan_notify);


	return rtl_send_complete_event_to_BT(rtlpriv,
					     HCI_ENABLE_WIFI_SCAN_NOTIFY,
					     status);

}


u8 rtl_btcoex_parse_HCI_link_status_notify_cmd(struct rtl_priv *rtlpriv,
		u8 *pcmd, u16 cmdlen)
{
	enum RTL_HCI_STATUS	status = HCI_STATUS_SUCCESS;
	struct bt_coex_info	*pcoex_info = &rtlpriv->coex_info;
	struct BT_MGNT *bt_mgnt = &pcoex_info->bt_mgnt;
	u8		i, num_of_handle = 0;
	u16		connect_handle;
	u8		bt_profile, bt_corespec, link_role;
	u8		*pTriple;
	struct HCI_LINK_INFO *acl_link;


	bt_mgnt->support_profile = false;

	bt_mgnt->ext_config.number_of_acl = 0;
	bt_mgnt->ext_config.number_of_sco = 0;

	num_of_handle = pcmd[0];

	RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD, "num_of_handle = 0x%x\n",
		     num_of_handle);
	RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD, "hci_extension_ver = %d\n",
		     bt_mgnt->ext_config.hci_extension_ver);


	pTriple = &pcmd[1];
	for (i = 0; i < num_of_handle; i++) {
		if (bt_mgnt->ext_config.hci_extension_ver < 1) {
			connect_handle = *((u8 *)&pTriple[0]);
			bt_profile = pTriple[2];
			bt_corespec = pTriple[3];
			if (BT_PROFILE_SCO == bt_profile) {
				bt_mgnt->ext_config.number_of_sco++;
			} else {
				bt_mgnt->ext_config.number_of_acl++;
				acl_link =  &bt_mgnt->ext_config.acl_link[i];
				acl_link->connect_handle = connect_handle;
				acl_link->bt_profile = bt_profile;
				acl_link->bt_corespec = bt_corespec;
			}
			RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
				"Connection_Handle=0x%x, bt_profile=%d, BTSpec=%d\n",
				     connect_handle,
				     bt_profile, bt_corespec);
			pTriple += 4;
		} else if (bt_mgnt->ext_config.hci_extension_ver >= 1) {
			connect_handle = *((u16 *)&pTriple[0]);
			bt_profile = pTriple[2];
			bt_corespec = pTriple[3];
			link_role = pTriple[4];
			if (BT_PROFILE_SCO == bt_profile) {
				bt_mgnt->ext_config.number_of_sco++;
			} else {
				bt_mgnt->ext_config.number_of_acl++;
				acl_link =  &bt_mgnt->ext_config.acl_link[i];
				acl_link->connect_handle = connect_handle;
				acl_link->bt_profile = bt_profile;
				acl_link->bt_corespec = bt_corespec;
				acl_link->link_role = link_role;
			}
			/* RT_DISP(FIOCTL, IOCTL_BT_HCICMD_EXT, */


			RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
				"Connection_Handle=0x%x, bt_profile=%d, BTSpec=%d, link_role=%d\n",
				     connect_handle, bt_profile, bt_corespec,
				     link_role);

			pTriple += 5;
		}
	}


	rtlpriv->btcoexist.btc_ops->btc_stack_update_profile_info();
	return rtl_send_complete_event_to_BT(rtlpriv, HCI_LINK_STATUS_NOTIFY,
					     status);


}



u8 rtl_btcoex_parse_HCI_BT_coex_notify_cmd(struct rtl_priv *rtlpriv, u8 *pcmd,
		u16 cmdlen)
{
	enum RTL_HCI_STATUS	status = HCI_STATUS_SUCCESS;

	return rtl_send_complete_event_to_BT(rtlpriv, HCI_BT_COEX_NOTIFY,
					     status);
}



u8 rtl_btcoex_parse_HCI_BT_operation_notify_cmd(struct rtl_priv *rtlpriv,
		u8 *pcmd, u16 cmdlen)
{
	enum RTL_HCI_STATUS	status = HCI_STATUS_SUCCESS;

	RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD, "%s, OP code: %d\n", __func__,
		     pcmd[0]);

	switch (pcmd[0]) {
	case HCI_BT_OP_NONE:
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
			     "[bt operation] : Operation None!!\n");
		break;
	case HCI_BT_OP_INQUIRY_START:
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
			     "[bt operation] : Inquiry start!!\n");
		break;
	case HCI_BT_OP_INQUIRY_FINISH:
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
			     "[bt operation] : Inquiry finished!!\n");
		break;
	case HCI_BT_OP_PAGING_START:
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
			     "[bt operation] : Paging is started!!\n");
		break;
	case HCI_BT_OP_PAGING_SUCCESS:
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
			"[bt operation] : Paging complete successfully!!\n");
		break;
	case HCI_BT_OP_PAGING_UNSUCCESS:
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
			"[bt operation] : Paging complete unsuccessfully!!\n");
		break;
	case HCI_BT_OP_PAIRING_START:
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
			     "[bt operation] : Pairing start!!\n");
		break;
	case HCI_BT_OP_PAIRING_FINISH:
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
			     "[bt operation] : Pairing finished!!\n");
		break;
	case HCI_BT_OP_BT_DEV_ENABLE:
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
			     "[bt operation] : BT Device is enabled!!\n");
		break;
	case HCI_BT_OP_BT_DEV_DISABLE:
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
			     "[bt operation] : BT Device is disabled!!\n");
		break;
	default:
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
			     "[bt operation] : Unknown, error!!\n");
		break;
	}


	return rtl_send_complete_event_to_BT(rtlpriv, HCI_BT_OPERATION_NOTIFY,
					     status);
}


u8 rtl_btcoex_parse_BT_AFH_MAP_notify_cmd(struct rtl_priv *rtlpriv, u8 *pcmd,
		u16 cmdlen)
{
	enum RTL_HCI_STATUS	status = HCI_STATUS_SUCCESS;

	return rtl_send_complete_event_to_BT(rtlpriv, HCI_BT_AFH_MAP_NOTIFY,
					     status);
}

u8 rtl_btcoex_parse_BT_register_val_notify_cmd(struct rtl_priv *rtlpriv,
		u8 *pcmd, u16 cmdlen)
{

	enum RTL_HCI_STATUS	status = HCI_STATUS_SUCCESS;

	return rtl_send_complete_event_to_BT(rtlpriv,
				     HCI_BT_REGISTER_VALUE_NOTIFY, status);
}

u8 rtl_btcoex_parse_HCI_BT_abnormal_notify_cmd(struct rtl_priv *rtlpriv,
		u8 *pcmd, u16 cmdlen)
{
	enum RTL_HCI_STATUS	status = HCI_STATUS_SUCCESS;

	return rtl_send_complete_event_to_BT(rtlpriv, HCI_BT_ABNORMAL_NOTIFY,
					     status);
}

u8 rtl_btcoex_parse_HCI_query_RF_status_cmd(struct rtl_priv *rtlpriv, u8 *pcmd,
		u16 cmdlen)
{
	enum RTL_HCI_STATUS	status = HCI_STATUS_SUCCESS;

	return rtl_send_complete_event_to_BT(rtlpriv, HCI_QUERY_RF_STATUS,
					     status);
}

/*****************************************
* HCI cmd format :
*| 15 - 0						|
*| OPcode (OCF|OGF<<10)		|
*| 15 - 8		|7 - 0			|
*|Cmd para	|Cmd para Length	|
*|Cmd para......				|
******************************************/

/* bit 0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
 *   |	OCF			             |	   OGF       | */
void rtl_btcoex_parse_hci_extend_cmd(struct rtl_priv *rtlpriv, u8 *pcmd,
				     u16 len, const u16 hci_OCF)
{

	RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD, "#LEVEL2,");
	switch (hci_OCF) {
	case HCI_EXTENSION_VERSION_NOTIFY:
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
			     "HCI_EXTENSION_VERSION_NOTIFY\n#LEVEL3,");
		rtl_btcoex_parse_HCI_Ver_notify_cmd(rtlpriv, pcmd, len);
		break;
	case HCI_LINK_STATUS_NOTIFY:
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
			     "HCI_LINK_STATUS_NOTIFY#LEVEL3\n");
		rtl_btcoex_parse_HCI_link_status_notify_cmd(rtlpriv, pcmd, len);
		break;
	case HCI_BT_OPERATION_NOTIFY:
		/* only for 8723a 2ant*/
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
			     "HCI_BT_OPERATION_NOTIFY\n#LEVEL3,");
		rtl_btcoex_parse_HCI_BT_operation_notify_cmd(rtlpriv, pcmd,
				len);
		break;
	case HCI_ENABLE_WIFI_SCAN_NOTIFY:
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
			     "HCI_ENABLE_WIFI_SCAN_NOTIFY\n#LEVEL3,");
		rtl_btcoex_parse_WIFI_scan_notify_cmd(rtlpriv, pcmd, len);
		break;
	case HCI_QUERY_RF_STATUS:
		/* only for 8723b 2ant */
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
			     "HCI_QUERY_RF_STATUS\n#LEVEL3,");
		rtl_btcoex_parse_HCI_query_RF_status_cmd(rtlpriv, pcmd, len);
		break;
	case HCI_BT_ABNORMAL_NOTIFY:
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
			     "HCI_BT_ABNORMAL_NOTIFY\n#LEVEL3,");
		rtl_btcoex_parse_HCI_BT_abnormal_notify_cmd(rtlpriv, pcmd, len);
		break;
	case HCI_BT_INFO_NOTIFY:
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
			     "HCI_BT_INFO_NOTIFY\n#LEVEL3,");
		rtl_btcoex_parse_BT_info_notify_cmd(rtlpriv, pcmd, len);
		break;
	case HCI_BT_COEX_NOTIFY:
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
			     "HCI_BT_COEX_NOTIFY\n#LEVEL3,");
		rtl_btcoex_parse_HCI_BT_coex_notify_cmd(rtlpriv, pcmd, len);
		break;
	case HCI_BT_PATCH_VERSION_NOTIFY:
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
			     "HCI_BT_PATCH_VERSION_NOTIFY\n#LEVEL3,");
		rtl_btcoex_parse_BT_patch_ver_info_cmd(rtlpriv, pcmd, len);
		break;
	case HCI_BT_AFH_MAP_NOTIFY:
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
			     "HCI_BT_AFH_MAP_NOTIFY\n#LEVEL3,");
		rtl_btcoex_parse_BT_AFH_MAP_notify_cmd(rtlpriv, pcmd, len);
		break;
	case HCI_BT_REGISTER_VALUE_NOTIFY:
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
			     "HCI_BT_REGISTER_VALUE_NOTIFY\n#LEVEL3,");
		rtl_btcoex_parse_BT_register_val_notify_cmd(rtlpriv, pcmd, len);
		break;
	default:
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_WARNING,
			     "ERROR!!! Unknown OCF: %x\n",
			     hci_OCF);
		break;

	}
}

void rtl_btcoex_parse_hci_cmd(struct rtl_priv *rtlpriv, u8 *pcmd, u16 len)
{
	u16 opcode = pcmd[0] | pcmd[1] << 8;
	u16 hci_OGF = HCI_OGF(opcode);
	u16 hci_OCF = HCI_OCF(opcode);
	u8 cmdlen = len - 3;


	RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD, "#LEVEL1,OGF: %x,OCF: %x\n",
		     hci_OGF,
		     hci_OCF);

	switch (hci_OGF) {
	case OGF_EXTENSION:
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
			     "#LEVEL1,HCI_EXTENSION_CMD_OGF\n");
		rtl_btcoex_parse_hci_extend_cmd(rtlpriv, &pcmd[3], cmdlen,
						hci_OCF);
		break;
	default:
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
			     "#LEVEL1,Other OGF: %x\n", hci_OGF);
		break;
	}
}



u16 rtl_btcoex_parse_recv_data(u8 *msg, u8 msg_size)
{
	u8 *cmp_msg1 = attend_ack;
	u8 *cmp_msg2 = leave_ack;
	u8 *cmp_msg3 = bt_leave;
	u8 *cmp_msg4 = invite_req;
	u8 *cmp_msg5 = attend_req;
	u8 *cmp_msg6 = invite_rsp;
	u8 res = OTHER;

	RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
		     "\n>>>>>>>>>>>>>>>>>>>>>>>BT_TO_WIFI");


	if (memcmp(cmp_msg1, msg, msg_size) == 0) {
		res = RX_ATTEND_ACK;
	} else if (memcmp(cmp_msg2, msg, msg_size) == 0) {
		res = RX_LEAVE_ACK;
	} else if (memcmp(cmp_msg3, msg, msg_size) == 0) {
		res = RX_BT_LEAVE;
	} else if (memcmp(cmp_msg4, msg, msg_size) == 0) {
		res = RX_INVITE_REQ;
	} else if (memcmp(cmp_msg5, msg, msg_size) == 0) {
		res = RX_ATTEND_REQ;
	} else if (memcmp(cmp_msg6, msg, msg_size) == 0) {
		res = RX_INVITE_RSP;
	} else {
		/*DBG_871X("%s, OGF|OCF:%02x%02x\n",__func__,msg[1],msg[0]);*/
		res = OTHER;
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD, ",other_cmd!\n");
	}


	if (OTHER != res)
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD, ",base_cmd:%s\n", msg);




	return res;
}


#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 15, 0))
void rtl_btcoex_recvmsg_int(struct sock *sk_in, s32 bytes)
{
#else
void rtl_btcoex_recvmsg_int(struct sock *sk_in)
{
#endif

	struct rtl_priv *rtlpriv = pbtcoexadapter;
	struct bt_coex_info *pcoex_info = &rtlpriv->coex_info;



	/* printk("wifi:call recv wq\n"); */
	pcoex_info->sk_store = sk_in;
	queue_delayed_work(pbtcoexadapter->works.rtl_wq,
			   &rtlpriv->works.socket_rcv_wq, 0);

}




void rtl_btcoex_recvmsgbysocket(void *data)
{

	u8 recv_data[RECV_DATA_MAX_LEN];
	u32 len = 0;
	u16 recv_length = 0;
	u16 parse_res = 0;

	struct rtl_priv *rtlpriv = pbtcoexadapter;
	struct bt_coex_info *pcoex_info = &rtlpriv->coex_info;
	struct sock *sk = pcoex_info->sk_store;
	struct sk_buff *skb = NULL;

	if (sk == NULL)
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_WARNING,
			     "critical error when receive socket data!\n");


	len = skb_queue_len(&sk->sk_receive_queue);
	while (len > 0) {
		skb = skb_dequeue(&sk->sk_receive_queue);

		/*important: cut the udp header from skb->data!
		  header length is 8 byte*/
		recv_length = skb->len - 8;
		memset(recv_data, 0, sizeof(recv_data));
		safe_memcpy(recv_data, skb->data + 8, recv_length,
			    RECV_DATA_MAX_LEN);

		parse_res = rtl_btcoex_parse_recv_data(recv_data, recv_length);
		if (RX_ATTEND_ACK == parse_res) { /* attend ack */
			pcoex_info->bt_attend = true;
			RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
				"RX_ATTEND_ACK!,sock_open:%d, BT_attend:%d\n",
				     pcoex_info->sock_open,
				     pcoex_info->bt_attend);
		}
		if (RX_ATTEND_REQ == parse_res) { /* attend req from BT */
			pcoex_info->bt_attend = true;
			RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
				"RX_BT_ATTEND_REQ!,sock_open:%d, BT_attend:%d\n",
				     pcoex_info->sock_open,
				     pcoex_info->bt_attend);
			rtl_btcoex_sendmsgbysocket(pbtcoexadapter, attend_ack,
						   sizeof(attend_ack),
						   false);
		}
		if (RX_INVITE_REQ == parse_res) { /* attend req from BT */
			pcoex_info->bt_attend = true;
			RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
				"RX_INVITE_REQ!,sock_open:%d, BT_attend:%d\n",
				     pcoex_info->sock_open,
				     pcoex_info->bt_attend);
			rtl_btcoex_sendmsgbysocket(pbtcoexadapter, invite_rsp,
						   sizeof(invite_rsp), false);
		}
		if (RX_INVITE_RSP == parse_res) { /* attend req from BT */
			pcoex_info->bt_attend = true;
			RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
				"RX_INVITE_RSP!,sock_open:%d, BT_attend:%d\n",
				     pcoex_info->sock_open,
				     pcoex_info->bt_attend);
		} else if (RX_LEAVE_ACK == parse_res) {
			/* mean BT know wifi
								 * will leave */
			pcoex_info->bt_attend = false;
			RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
				"RX_LEAVE_ACK!,sock_open:%d, BT_attend:%d\n",
				     pcoex_info->sock_open,
				     pcoex_info->bt_attend);
		} else if (RX_BT_LEAVE == parse_res) { /* BT leave */
			rtl_btcoex_sendmsgbysocket(pbtcoexadapter, leave_ack,
						   sizeof(leave_ack),
						   false); /* no ack */
			pcoex_info->bt_attend = false;
			RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
				     "RX_BT_LEAVE!sock_open:%d, BT_attend:%d\n",
				     pcoex_info->sock_open,
				     pcoex_info->bt_attend);
		} else { /*todo: check if recv data are really hci cmds*/
			if (true == pcoex_info->bt_attend)
				rtl_btcoex_parse_hci_cmd(pbtcoexadapter,
						 recv_data, recv_length);
		}
		len--;
		kfree_skb(skb);
		/*never do a sleep in this context!*/
	}


}




u8 rtl_btcoex_sendmsgbysocket(struct rtl_priv *rtlpriv, u8 *msg, u8 msg_size,
			      bool force)
{



	struct bt_coex_info *pcoex_info = &rtlpriv->coex_info;
	struct socket_msg_to_send *msg_to_send = NULL;


	msg_to_send = kzalloc(sizeof(struct socket_msg_to_send), GFP_ATOMIC);


	if (msg == NULL || msg_size == 0 || msg_to_send == NULL) {
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_WARNING, "msg error!\n");
		return -1;
	}

	if (false == force) {
		if (false == pcoex_info->bt_attend) {
			RT_TRACE_BTC(COMP_COEX_COMM, DBG_WARNING,
				     "TX Blocked: WiFi-BT disconnected\n");
			return _FAIL;
		}
	}

	RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
		     "<<<<<<<<<<<<<<<<<<<<<<<<WIFI_TO_BT,msg:%s\n", msg);


	if (msg_size > SOCK_MSG_SIZE) {
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_EMERG,
				     "msg_size is truncated to %d from %d\n",
				     SOCK_MSG_SIZE, msg_size);
		msg_size = SOCK_MSG_SIZE;
	}

	memcpy(msg_to_send->msg, msg, msg_size);
	msg_to_send->msg_size = msg_size;

	spin_lock_bh(&rtlpriv->locks.socket_lock);
	list_add_tail(&(msg_to_send->list), &(pcoex_info->sock_msg_send_list));
	spin_unlock_bh(&rtlpriv->locks.socket_lock);

	return _SUCCESS;


}


u8 rtl_btcoex_create_kernel_socket(struct rtl_priv *rtlpriv, u8 is_invite)
{

	s8 kernel_socket_err;
	struct bt_coex_info *pcoex_info = &rtlpriv->coex_info;
	u8 ret;

	RT_TRACE_BTC(COMP_COEX_COMM, DBG_WARNING, "%s CONNECT_PORT %d\n",
		     __func__,
		     CONNECT_PORT);


	if (NULL == pcoex_info) {
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_WARNING, "coex_info: NULL\n");
		return _FAIL;
	}

	kernel_socket_err = sock_create(PF_INET, SOCK_DGRAM, 0,
					&pcoex_info->udpsock);
	RT_TRACE_BTC(COMP_COEX_COMM, DBG_WARNING, "create socket,err= %d\n",
		     kernel_socket_err);

	if (kernel_socket_err < 0) {
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_WARNING,
			     "Error during creation of socket error:%d\n",
			     kernel_socket_err);
		return _FAIL;
	}

	memset(&(pcoex_info->sin), 0, sizeof(pcoex_info->sin));
	pcoex_info->sin.sin_family = AF_INET;
	pcoex_info->sin.sin_port = htons(CONNECT_PORT);
	pcoex_info->sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	memset(&(pcoex_info->bt_addr), 0, sizeof(pcoex_info->bt_addr));
	pcoex_info->bt_addr.sin_family = AF_INET;
	pcoex_info->bt_addr.sin_port = htons(CONNECT_PORT_BT);
	pcoex_info->bt_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	pcoex_info->sk_store = NULL;

	kernel_socket_err = pcoex_info->udpsock->ops->bind(
				    pcoex_info->udpsock,
				    (struct sockaddr *)&pcoex_info->sin,
				    sizeof(pcoex_info->sin));

	if (kernel_socket_err == 0) {
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_WARNING,
			     "binding socket success\n");
		pcoex_info->udpsock->sk->sk_data_ready = rtl_btcoex_recvmsg_int;
		pcoex_info->sock_open |=  KERNEL_SOCKET_OK;
		pcoex_info->bt_attend = false;
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_WARNING,
			     "WIFI sending attend_req\n");
		rtl_btcoex_sendmsgbysocket(rtlpriv, attend_req,
					   sizeof(attend_req), true);
		ret = _SUCCESS;
	} else {
		pcoex_info->bt_attend = false;
		sock_release(pcoex_info->udpsock);
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_WARNING,
			     "Error binding socket: %d\n", kernel_socket_err);
		ret = _FAIL;
	}

	return ret;
}


void rtl_btcoex_close_kernel_socket(struct rtl_priv *rtlpriv)
{

	struct bt_coex_info *pcoex_info = &rtlpriv->coex_info;

	if (pcoex_info->sock_open & KERNEL_SOCKET_OK) {
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_WARNING,
			     "release kernel socket\n");
		cancel_delayed_work(&rtlpriv->works.socket_rcv_wq);
		cancel_delayed_work(&rtlpriv->works.socket_send_wq);


		sock_release(pcoex_info->udpsock);
		pcoex_info->sock_open &= ~(KERNEL_SOCKET_OK);
		if (true == pcoex_info->bt_attend)
			pcoex_info->bt_attend = false;
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_WARNING,
			     "sock_open:%d, BT_attend:%d\n",
			     pcoex_info->sock_open, pcoex_info->bt_attend);
	}

}


u8 rtl_thread_sendmsgbysocket(struct rtl_priv *rtlpriv, u8 *msg, u8 msg_size)
{


	struct msghdr	udpmsg;
	struct iovec	iov;
	u8 error;
	struct bt_coex_info *pcoex_info = &rtlpriv->coex_info;
	mm_segment_t oldfs;


	iov.iov_base	 = (void *)msg;
	iov.iov_len	 = msg_size;
	udpmsg.msg_name	 = &pcoex_info->bt_addr;
	udpmsg.msg_namelen	= sizeof(struct sockaddr_in);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0))
	iov_iter_init(&udpmsg.msg_iter, WRITE, &iov, 1, msg_size);
#else
	udpmsg.msg_iov	 = &iov;
	udpmsg.msg_iovlen	= 1;
#endif
	udpmsg.msg_control	= NULL;
	udpmsg.msg_controllen = 0;
	udpmsg.msg_flags	= MSG_DONTWAIT | MSG_NOSIGNAL;
	udpmsg.msg_flags = 0;
	oldfs = get_fs();
	set_fs(KERNEL_DS);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0))
	error = sock_sendmsg(pcoex_info->udpsock, &udpmsg);
#else
	error = sock_sendmsg(pcoex_info->udpsock, &udpmsg, msg_size);
#endif
	set_fs(oldfs);
	if (error < 0) {
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_WARNING,
			     "Error when sending msg, error:%d\n",
			     error);
		return _FAIL;
	}

	return _SUCCESS;
}

static int rtl_btcoex_thread(void *context)
{

	struct rtl_priv *rtlpriv = (struct rtl_priv *)context;
	struct bt_coex_info *pcoex_info = &rtlpriv->coex_info;
	struct socket_msg_to_send *msg_to_send = NULL;
	u8 *msg;
	u8 msg_size;

	pcoex_info->socket_thread_started = true;

	while (!kthread_should_stop() && pcoex_info->is_exist) {

		msleep(1);
		msg_to_send = NULL;
		msg = NULL;
		msg_size = 0;

		spin_lock_bh(&rtlpriv->locks.socket_lock);
		if (!list_empty(&(pcoex_info->sock_msg_send_list))) {
			list_for_each_entry(msg_to_send,
				    &pcoex_info->sock_msg_send_list, list) {
				/* get the first one */
				break;
			}
		}
		if (NULL != msg_to_send) {
			list_del(&msg_to_send->list);
			msg = msg_to_send->msg;
			msg_size = msg_to_send->msg_size;
		}
		spin_unlock_bh(&rtlpriv->locks.socket_lock);

		if (NULL != msg) {
			rtl_thread_sendmsgbysocket(rtlpriv, msg, msg_size);

			kfree(msg_to_send);
			msg_to_send = NULL;
		}


	}

	pcoex_info->socket_thread_started = false;

	return true;

}




void rtl_btcoex_init_socket(struct rtl_priv *rtlpriv)
{

	u8 is_invite = false;
	struct bt_coex_info *pcoex_info = &rtlpriv->coex_info;
	void *thread_handle = NULL;

	pbtcoexadapter = rtlpriv;


	RT_TRACE_BTC(COMP_COEX_COMM, DBG_WARNING,
		     "8812AE:init socket with 8761AU\n");


	memset(pcoex_info, 0, sizeof(struct bt_coex_info));

	pcoex_info->is_exist = true;
	INIT_LIST_HEAD(&(pcoex_info->sock_msg_send_list));

	INIT_DELAYED_WORK(&rtlpriv->works.socket_rcv_wq,
			  (void *)rtl_btcoex_recvmsgbysocket);


	/* start to handle */
	thread_handle = kthread_run(rtl_btcoex_thread, rtlpriv,
				    "RTW_COEX_THREAD");
	if (IS_ERR(thread_handle))
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_WARNING,
			     "Create RTW_COEX_THREAD Fail !!!!!\n");



	rtl_btcoex_create_kernel_socket(rtlpriv, is_invite);
	RT_TRACE_BTC(COMP_COEX_COMM, DBG_WARNING,
		     "set coex_info->is_exist: %d\n",
		     pcoex_info->is_exist);

}

void rtl_btcoex_close_socket(struct rtl_priv *rtlpriv)
{
	struct bt_coex_info *pcoex_info = &rtlpriv->coex_info;

	RT_TRACE_BTC(COMP_COEX_COMM, DBG_WARNING,
		     "set coex_info->is_exist: %d\n",
		     pcoex_info->is_exist);
	if (true == pcoex_info->is_exist) {
		pcoex_info->is_exist = false;
		if (pcoex_info->bt_attend == true) { /*inform BT wifi leave*/
			rtl_btcoex_sendmsgbysocket(rtlpriv, wifi_leave,
						   sizeof(wifi_leave), false);
			msleep(50);
		}
		while (pcoex_info->socket_thread_started)
			msleep(1);
		rtl_btcoex_close_kernel_socket(rtlpriv);
		pbtcoexadapter = NULL;
	}
}



void rtl_btcoex_dump_tx_msg(u8 *tx_msg, u8 len, u8 *msg_name)
{

	u8 i = 0;

	return;

	RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD, "_%s\n", msg_name);
	for (i = 0; i < len; i++)
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD, "%02x ", tx_msg[i]);

	RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD, "\n");

}









void rtl_btcoex_SendEventExtBtCoexControl(struct rtl_priv *rtlpriv,
		u8 bNeedDbgRsp, u8 dataLen, void *pData)
{

	u8			len = 0, tx_event_length = 0;
	u8			local_buf[32] = "";
	u8			*ret_par;
	u8			opCode = 0;
	u8			*pInBuf = (u8 *)pData;
	struct rtl_hci_event *hci_event;

	RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
		     "#LEVEL_WIFI_ACTIVE,SendEventExtBtCoexControl\n");
	opCode = pInBuf[0];

	hci_event = (struct rtl_hci_event *)(&local_buf[0]);

	hci_event->event_code = HCI_EVENT_EXTENSION_RTK;
	hci_event->data[0] =
		HCI_EVENT_EXT_BT_COEX_CONTROL;	/* extension event code */
	len++;
	ret_par = hci_event->data + len;
	memcpy(&ret_par[0], pData, dataLen); /*maybe not safe here*/
	len += dataLen;
	hci_event->length = len;
	/* total tx event length + event_code length + sizeof(length) */
	tx_event_length = hci_event->length + 2;
	rtl_btcoex_dump_tx_msg((u8 *)hci_event, tx_event_length,
			       "BT COEX CONTROL");
	rtl_btcoex_sendmsgbysocket(rtlpriv, (u8 *)hci_event, tx_event_length,
				   false);

}


void rtl_btcoex_SendEventExtBtInfoControl(struct rtl_priv *rtlpriv, u8 dataLen,
		void *pData)
{

	struct rtl_hci_event *hci_event;
	u8			*ret_par;
	u8			len = 0, tx_event_length = 0;
	u8			local_buf[32] = "";

	struct bt_coex_info *pcoex_info = &rtlpriv->coex_info;
	struct BT_MGNT *bt_mgnt = &pcoex_info->bt_mgnt;

	RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
		     "#LEVEL_WIFI_ACTIVE,SendEventExtBtInfoControl\n");



	/* DBG_871X("%s\n",__func__); */
	if (bt_mgnt->ext_config.hci_extension_ver < 4) { /* not support */
		RT_TRACE_BTC(COMP_COEX_COMM, DBG_WARNING,
			"ERROR: hci_extension_ver = %d, hci_extension_ver<4 !!!!\n",
			     bt_mgnt->ext_config.hci_extension_ver);
		return;
	}

	hci_event = (struct rtl_hci_event *)(&local_buf[0]);
	hci_event->event_code = HCI_EVENT_EXTENSION_RTK;
	hci_event->data[0] = HCI_EVENT_EXT_BT_INFO_CONTROL;
	len++;
	ret_par = hci_event->data + len;

	memcpy(&ret_par[0], pData, dataLen);/*maybe not safe here*/
	len += dataLen;
	hci_event->length = len;
	/* total tx event length + event_code length + sizeof(length) */
	tx_event_length = hci_event->length + 2;
	rtl_btcoex_dump_tx_msg((u8 *)hci_event, tx_event_length,
			       "BT INFO CONTROL");
	rtl_btcoex_sendmsgbysocket(rtlpriv, (u8 *)hci_event, tx_event_length,
				   false);

}

void rtl_btcoex_SendScanNotify(struct rtl_priv *rtlpriv, u8 scanType)
{

	u8 tx_event_length = 0;
	u8 local_buf[7] = "";
	struct rtl_hci_event *hci_event;
	u8 *event_data = NULL;

	RT_TRACE_BTC(COMP_COEX_COMM, DBG_LOUD,
		     "#LEVEL_WIFI_ACTIVE,SendScanNotify\n");

	hci_event = (struct rtl_hci_event *)(&local_buf[0]);
	hci_event->event_code = HCI_EVENT_EXTENSION_RTK;
	event_data = hci_event->data;
	*(event_data) = HCI_EVENT_EXT_WIFI_SCAN_NOTIFY;
	*(event_data + 1) = scanType;
	hci_event->length = 2;
	/* total tx event length + event_code length + sizeof(length) */
	tx_event_length = hci_event->length + 2;
	rtl_btcoex_dump_tx_msg((u8 *)hci_event, tx_event_length,
			       "WIFI SCAN OPERATION");
	rtl_btcoex_sendmsgbysocket(rtlpriv, (u8 *)hci_event, tx_event_length,
				   false);
}
