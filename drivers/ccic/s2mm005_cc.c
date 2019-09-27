/*
 * driver/../s2mm005.c - S2MM005 USB CC function driver
 *
 * Copyright (C) 2015 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <linux/ccic/s2mm005.h>
#include <linux/ccic/s2mm005_ext.h>
#if defined(CONFIG_CCIC_NOTIFIER)
#include <linux/workqueue.h>
#endif
#if defined(CONFIG_CCIC_ALTERNATE_MODE)
#include <linux/ccic/ccic_alternate.h>
#endif
#if defined(CONFIG_CCIC_NOTIFIER)
#include <linux/ccic/ccic_core.h>
#endif
#include <linux/usb_notify.h>
#if defined(CONFIG_COMBO_REDRIVER_PTN36502)
#include <linux/combo_redriver/ptn36502.h>
#endif

#if defined(CONFIG_BATTERY_SAMSUNG)
extern unsigned int lpcharge;
#endif

#if defined(CONFIG_DUAL_ROLE_USB_INTF)
#include <linux/usb/class-dual-role.h>
#elif defined(CONFIG_TYPEC)
#include <linux/usb/typec.h>
#endif

int CC_DIR;
EXPORT_SYMBOL(CC_DIR);

extern struct pdic_notifier_struct pd_noti;
////////////////////////////////////////////////////////////////////////////////
// function definition
////////////////////////////////////////////////////////////////////////////////
void process_cc_water(void * data, LP_STATE_Type *Lp_DATA);
void process_cc_attach(void * data, u8 *plug_attach_done);
void process_cc_detach(void * data);
#if defined(CONFIG_TYPEC)
void process_message_role(void *data);
#endif
void process_cc_get_int_status(void *data, uint32_t *pPRT_MSG, MSG_IRQ_STATUS_Type *MSG_IRQ_State);
void process_cc_rid(void * data);
void ccic_event_work(void *data, int dest, int id, int attach, int event, int sub);
void process_cc_water_det(void * data);

#ifdef CONFIG_MUIC_SM5705_SWITCH_CONTROL_GPIO
extern int muic_GPIO_control(int gpio);
#endif
#if defined(CONFIG_MUIC_SUPPORT_KEYBOARDDOCK)
extern void muic_ADC_rescan(void);
int adc_rescan_done = 0;
#endif

#if defined(CONFIG_USB_DWC3)
extern void dwc3_set_selfpowered(u8 enable);
#endif

////////////////////////////////////////////////////////////////////////////////
// modified by khoonk 2015.05.18
////////////////////////////////////////////////////////////////////////////////
// s2mm005.c --> s2mm005_cc.h
////////////////////////////////////////////////////////////////////////////////
static char MSG_IRQ_Print[32][40] =
{
    {"bFlag_Ctrl_Reserved"},
    {"bFlag_Ctrl_GoodCRC"},
    {"bFlag_Ctrl_GotoMin"},
    {"bFlag_Ctrl_Accept"},
    {"bFlag_Ctrl_Reject"},
    {"bFlag_Ctrl_Ping"},
    {"bFlag_Ctrl_PS_RDY"},
    {"bFlag_Ctrl_Get_Source_Cap"},
    {"bFlag_Ctrl_Get_Sink_Cap"},
    {"bFlag_Ctrl_DR_Swap"},
    {"bFlag_Ctrl_PR_Swap"},
    {"bFlag_Ctrl_VCONN_Swap"},
    {"bFlag_Ctrl_Wait"},
    {"bFlag_Ctrl_Soft_Reset"},
    {"bFlag_Ctrl_Reserved_b14"},
    {"bFlag_Ctrl_Reserved_b15"},
    {"bFlag_Data_Reserved_b16"},
    {"bFlag_Data_SRC_Capability"},
    {"bFlag_Data_Request"},
    {"bFlag_Data_BIST"},
    {"bFlag_Data_SNK_Capability"},
    {"bFlag_Data_Reserved_05"},
    {"bFlag_Data_Reserved_06"},
    {"bFlag_Data_Reserved_07"},
    {"bFlag_Data_Reserved_08"},
    {"bFlag_Data_Reserved_09"},
    {"bFlag_Data_Reserved_10"},
    {"bFlag_Data_Reserved_11"},
    {"bFlag_Data_Reserved_12"},
    {"bFlag_Data_Reserved_13"},
    {"bFlag_Data_Reserved_14"},
    {"bFlag_Data_Vender_Defined"},
};

#if defined(CONFIG_CCIC_NOTIFIER)
static void ccic_event_notifier(struct work_struct *data)
{
	struct ccic_state_work *event_work =
		container_of(data, struct ccic_state_work, ccic_work);
	CC_NOTI_TYPEDEF ccic_noti;

	switch(event_work->dest){
		case CCIC_NOTIFY_DEV_USB :
			pr_info("usb:%s, dest=%s, id=%s, attach=%s, drp=%s\n", __func__,
				CCIC_NOTI_DEST_Print[event_work->dest],
				CCIC_NOTI_ID_Print[event_work->id],
				event_work->attach? "Attached": "Detached",
				CCIC_NOTI_USB_STATUS_Print[event_work->event]);
			break;
		default :
			pr_info("usb:%s, dest=%s, id=%s, attach=%d, event=%d, sub=%d\n", __func__,
				CCIC_NOTI_DEST_Print[event_work->dest],
				CCIC_NOTI_ID_Print[event_work->id],
				event_work->attach,
				event_work->event,
				event_work->sub);
			break;
	}

	ccic_noti.src = CCIC_NOTIFY_DEV_CCIC;
	ccic_noti.dest = event_work->dest;
	ccic_noti.id = event_work->id;
	ccic_noti.sub1 = event_work->attach;
	ccic_noti.sub2 = event_work->event;
	ccic_noti.sub3 = event_work->sub;
#ifdef CONFIG_USB_TYPEC_MANAGER_NOTIFIER
	ccic_noti.pd = &pd_noti;
#endif
	ccic_notifier_notify((CC_NOTI_TYPEDEF*)&ccic_noti, NULL, 0);

	kfree(event_work);
}

void ccic_event_work(void *data, int dest, int id, int attach, int event, int sub)
{
	struct s2mm005_data *usbpd_data = data;
	struct ccic_state_work * event_work;

#if defined(CONFIG_DUAL_ROLE_USB_INTF)
#if defined(CONFIG_USB_HOST_NOTIFY)
	struct otg_notify *o_notify = get_otg_notify();
#endif
#elif defined(CONFIG_TYPEC)
	struct typec_partner_desc desc;
	enum typec_pwr_opmode mode = TYPEC_PWR_MODE_USB;
#endif

	pr_info("usb: %s\n", __func__);
	event_work = kmalloc(sizeof(struct ccic_state_work), GFP_ATOMIC);
	INIT_WORK(&event_work->ccic_work, ccic_event_notifier);

	event_work->dest = dest;
	event_work->id = id;
	event_work->attach = attach;
	event_work->event = event;
	event_work->sub = sub;

#if defined(CONFIG_DUAL_ROLE_USB_INTF)
	if (id == CCIC_NOTIFY_ID_USB) {
		pr_info("usb: %s, dest=%d, sub2(event)=%d, usbpd_data->data_role=%d, usbpd_data->try_state_change=%d\n",
			__func__, dest, event, usbpd_data->data_role, usbpd_data->try_state_change);
#if defined(CONFIG_USB_HOST_NOTIFY)
		if(sub1 == CCIC_NOTIFY_ATTACH && event == USB_STATUS_NOTIFY_ATTACH_DFP && o_notify)
		{
			o_notify->host_super = 0;
			o_notify->host_high = 0;
			init_waitqueue_head(&o_notify->host_device_recognition_wait_q);
		}			
#endif
		usbpd_data->data_role = event;

		if (usbpd_data->dual_role != NULL)
			dual_role_instance_changed(usbpd_data->dual_role);

		if (usbpd_data->try_state_change &&
			(usbpd_data->data_role != USB_STATUS_NOTIFY_DETACH)) {
			// Role change try and new mode detected
			pr_info("usb: %s, reverse_completion\n", __func__);
			complete(&usbpd_data->reverse_completion);
		}
#if defined(CONFIG_CCIC_ALTERNATE_MODE)
		if(!event_work->sub) {
			if(usbpd_data->dp_is_connect)
				event_work->sub = 1;
		}
#endif
	}
	else if (id == CCIC_NOTIFY_ID_ROLE_SWAP ) {
		if (usbpd_data->dual_role != NULL)
			dual_role_instance_changed(usbpd_data->dual_role);
	}
#elif defined(CONFIG_TYPEC)
	if (id == CCIC_NOTIFY_ID_USB) {
		if (usbpd_data->partner == NULL) {
			pr_info("%s: typec_register_partner power_role=%d data_role=%d event=%d",
				__func__, usbpd_data->typec_power_role,usbpd_data->typec_data_role, event);
			if (event == USB_STATUS_NOTIFY_ATTACH_UFP) {
				mode = s2mm005_get_pd_support(usbpd_data);
				typec_set_pwr_opmode(usbpd_data->port, mode);
				desc.usb_pd = mode == TYPEC_PWR_MODE_PD;
				desc.accessory = TYPEC_ACCESSORY_NONE; /* XXX: handle accessories */
				desc.identity = NULL;
				usbpd_data->typec_data_role = TYPEC_DEVICE;
				typec_set_data_role(usbpd_data->port, TYPEC_DEVICE);
				usbpd_data->partner = typec_register_partner(usbpd_data->port, &desc);
			} else if (event == USB_STATUS_NOTIFY_ATTACH_DFP) {
				mode = s2mm005_get_pd_support(usbpd_data);
				typec_set_pwr_opmode(usbpd_data->port, mode);
				desc.usb_pd = mode == TYPEC_PWR_MODE_PD;
				desc.accessory = TYPEC_ACCESSORY_NONE; /* XXX: handle accessories */
				desc.identity = NULL;
				usbpd_data->typec_data_role = TYPEC_HOST;
				typec_set_data_role(usbpd_data->port, TYPEC_HOST);
				usbpd_data->partner = typec_register_partner(usbpd_data->port, &desc);
			} else
				pr_info("%s detach case\n", __func__);
		}else {
			pr_info("%s: data_role changed, power_role=%d data_role=%d, event=%d",
				__func__, usbpd_data->typec_power_role,usbpd_data->typec_data_role, event);
			if (event == USB_STATUS_NOTIFY_ATTACH_UFP) {
				usbpd_data->typec_data_role = TYPEC_DEVICE;
				typec_set_data_role(usbpd_data->port, usbpd_data->typec_data_role);
			} else if (event == USB_STATUS_NOTIFY_ATTACH_DFP) {
				usbpd_data->typec_data_role = TYPEC_HOST;
				typec_set_data_role(usbpd_data->port, usbpd_data->typec_data_role);
			} else
				pr_info("%s detach case\n", __func__);
		}
		if (usbpd_data->typec_try_state_change &&
			(event != USB_STATUS_NOTIFY_DETACH)) {
			// Role change try and new mode detected
			pr_info("usb: %s, typec_reverse_completion\n", __func__);
			complete(&usbpd_data->typec_reverse_completion);
		}
	}
#endif

	queue_work(usbpd_data->ccic_wq, &event_work->ccic_work);
}
#endif

#if defined(CONFIG_DUAL_ROLE_USB_INTF)
void role_swap_check(struct work_struct *wk)
{
	struct delayed_work *delay_work =
		container_of(wk, struct delayed_work, work);
	struct s2mm005_data *usbpd_data =
		container_of(delay_work, struct s2mm005_data, role_swap_work);
	int mode;

	pr_info("%s: ccic_set_dual_role check again usbpd_data->pd_state=%d\n",
		__func__, usbpd_data->pd_state);

	usbpd_data->try_state_change = 0;

	if (usbpd_data->pd_state == State_PE_Initial_detach) {
		pr_err("%s: ccic_set_dual_role reverse failed, set mode to DRP\n", __func__);
		disable_irq(usbpd_data->irq);
		/* exit from Disabled state and set mode to DRP */
		mode =  TYPE_C_ATTACH_DRP;
		s2mm005_rprd_mode_change(usbpd_data, mode);
		enable_irq(usbpd_data->irq);
	}
}

static int ccic_set_dual_role(struct dual_role_phy_instance *dual_role,
				   enum dual_role_property prop,
				   const unsigned int *val)
{
	struct s2mm005_data *usbpd_data = dual_role_get_drvdata(dual_role);
	struct i2c_client *i2c;

	USB_STATUS attached_state;
	int mode;
	int timeout = 0;
	int ret = 0;

	if (!usbpd_data) {
		pr_err("%s : usbpd_data is null \n", __func__);
		return -EINVAL;
	}

	i2c = usbpd_data->i2c;

	// Get Current Role //
	attached_state = usbpd_data->data_role;
	pr_info("%s : request prop = %d , attached_state = %d\n", __func__, prop, attached_state);

	if (attached_state != USB_STATUS_NOTIFY_ATTACH_DFP
	    && attached_state != USB_STATUS_NOTIFY_ATTACH_UFP) {
		pr_err("%s : current mode : %d - just return \n",__func__, attached_state);
		return 0;
	}

	if (attached_state == USB_STATUS_NOTIFY_ATTACH_DFP
	    && *val == DUAL_ROLE_PROP_MODE_DFP) {
		pr_err("%s : current mode : %d - request mode : %d just return \n",
			__func__, attached_state, *val);
		return 0;
	}

	if (attached_state == USB_STATUS_NOTIFY_ATTACH_UFP
	    && *val == DUAL_ROLE_PROP_MODE_UFP) {
		pr_err("%s : current mode : %d - request mode : %d just return \n",
			__func__, attached_state, *val);
		return 0;
	}

	if ( attached_state == USB_STATUS_NOTIFY_ATTACH_DFP) {
		/* Current mode DFP and Source  */
		pr_info("%s: try reversing, from Source to Sink\n", __func__);
		/* turns off VBUS first */
 		vbus_turn_on_ctrl(0);
#if defined(CONFIG_CCIC_NOTIFIER)
		/* muic */
		ccic_event_work(usbpd_data,
			CCIC_NOTIFY_DEV_MUIC, CCIC_NOTIFY_ID_ATTACH, 0/*sub1:attach*/, 0/*sub2:rprd*/, 0/*sub3:reserved*/);
#endif
		/* exit from Disabled state and set mode to UFP */
		mode =  TYPE_C_ATTACH_UFP;
		usbpd_data->try_state_change = TYPE_C_ATTACH_UFP;
		s2mm005_rprd_mode_change(usbpd_data, mode);
	} else {
		// Current mode UFP and Sink  //
		pr_info("%s: try reversing, from Sink to Source\n", __func__);
		/* exit from Disabled state and set mode to UFP */
		mode =  TYPE_C_ATTACH_DFP;
		usbpd_data->try_state_change = TYPE_C_ATTACH_DFP;
		s2mm005_rprd_mode_change(usbpd_data, mode);
	}

	reinit_completion(&usbpd_data->reverse_completion);
	timeout =
	    wait_for_completion_timeout(&usbpd_data->reverse_completion,
					msecs_to_jiffies
					(DUAL_ROLE_SET_MODE_WAIT_MS));

	if (!timeout) {
		usbpd_data->try_state_change = 0;
		pr_err("%s: reverse failed, set mode to DRP\n", __func__);
		disable_irq(usbpd_data->irq);
		/* exit from Disabled state and set mode to DRP */
		mode =  TYPE_C_ATTACH_DRP;
		s2mm005_rprd_mode_change(usbpd_data, mode);
		enable_irq(usbpd_data->irq);
		ret = -EIO;
	} else {
		pr_err("%s: reverse success, one more check\n", __func__);
		schedule_delayed_work(&usbpd_data->role_swap_work, msecs_to_jiffies(DUAL_ROLE_SET_MODE_WAIT_MS));
	}

	dev_info(&i2c->dev, "%s -> data role : %d\n", __func__, *val);
	return ret;
}

/* Decides whether userspace can change a specific property */
int dual_role_is_writeable(struct dual_role_phy_instance *drp,
				  enum dual_role_property prop)
{
	if (prop == DUAL_ROLE_PROP_MODE)
		return 1;
	else
		return 0;
}

/* Callback for "cat /sys/class/dual_role_usb/otg_default/<property>" */
int dual_role_get_local_prop(struct dual_role_phy_instance *dual_role,
				    enum dual_role_property prop,
				    unsigned int *val)
{
	struct s2mm005_data *usbpd_data = dual_role_get_drvdata(dual_role);

	USB_STATUS attached_state;
	int power_role;

	if (!usbpd_data) {
		pr_err("%s : usbpd_data is null : request prop = %d \n",__func__, prop);
		return -EINVAL;
	}
	attached_state = usbpd_data->data_role;
	power_role = usbpd_data->power_role;

	pr_info("%s : request prop = %d , attached_state = %d, power_role = %d\n",
		__func__, prop, attached_state, power_role);

	if (attached_state == USB_STATUS_NOTIFY_ATTACH_DFP) {
		if (prop == DUAL_ROLE_PROP_MODE)
			*val = DUAL_ROLE_PROP_MODE_DFP;
		else if (prop == DUAL_ROLE_PROP_PR)
			*val = power_role;
		else if (prop == DUAL_ROLE_PROP_DR)
			*val = DUAL_ROLE_PROP_DR_HOST;
		else
			return -EINVAL;
	} else if (attached_state == USB_STATUS_NOTIFY_ATTACH_UFP) {
		if (prop == DUAL_ROLE_PROP_MODE)
			*val = DUAL_ROLE_PROP_MODE_UFP;
		else if (prop == DUAL_ROLE_PROP_PR)
			*val = power_role;
		else if (prop == DUAL_ROLE_PROP_DR)
			*val = DUAL_ROLE_PROP_DR_DEVICE;
		else
			return -EINVAL;
	} else {
		if (prop == DUAL_ROLE_PROP_MODE)
			*val = DUAL_ROLE_PROP_MODE_NONE;
		else if (prop == DUAL_ROLE_PROP_PR)
			*val = DUAL_ROLE_PROP_PR_NONE;
		else if (prop == DUAL_ROLE_PROP_DR)
			*val = DUAL_ROLE_PROP_DR_NONE;
		else
			return -EINVAL;
	}

	return 0;
}

/* Callback for "echo <value> >
 *                      /sys/class/dual_role_usb/<name>/<property>"
 * Block until the entire final state is reached.
 * Blocking is one of the better ways to signal when the operation
 * is done.
 * This function tries to switch to Attached.SRC or Attached.SNK
 * by forcing the mode into SRC or SNK.
 * On failure, we fall back to Try.SNK state machine.
 */
int dual_role_set_prop(struct dual_role_phy_instance *dual_role,
			      enum dual_role_property prop,
			      const unsigned int *val)
{
	pr_info("%s : request prop = %d , *val = %d \n",__func__, prop, *val);
	if (prop == DUAL_ROLE_PROP_MODE)
		return ccic_set_dual_role(dual_role, prop, val);
	else
		return -EINVAL;
}
#elif defined(CONFIG_TYPEC)
int s2mm005_dr_set(const struct typec_capability *cap, enum typec_data_role role)
{
	struct s2mm005_data *usbpd_data = container_of(cap, struct s2mm005_data, typec_cap);
	if (!usbpd_data)
		return -EINVAL;

	pr_info("%s : typec_power_role=%d, typec_data_role=%d, role=%d\n", __func__,
		usbpd_data->typec_power_role, usbpd_data->typec_data_role, role);
	
	if (usbpd_data->typec_data_role != TYPEC_DEVICE
		&& usbpd_data->typec_data_role != TYPEC_HOST)
		return -EPERM;
	else if (usbpd_data->typec_data_role == role)
		return -EPERM;

	reinit_completion(&usbpd_data->typec_reverse_completion);
	if (role == TYPEC_DEVICE) {
		pr_info("%s :try reversing, from DFP to UFP\n", __func__);
		usbpd_data->typec_try_state_change = TRY_ROLE_SWAP_DR;
		usbpd_data->is_dr_swap++;
		send_role_swap_message(usbpd_data, 1);
	} else if (role == TYPEC_HOST) {
		pr_info("%s :try reversing, from UFP to DFP\n", __func__);
		usbpd_data->typec_try_state_change = TRY_ROLE_SWAP_DR;
		usbpd_data->is_dr_swap++;
		send_role_swap_message(usbpd_data, 1);
	} else {
		pr_info("%s :invalid typec_role\n", __func__);
		return -EIO;
	}
	
	if (!wait_for_completion_timeout(&usbpd_data->typec_reverse_completion, 
				msecs_to_jiffies(TRY_ROLE_SWAP_WAIT_MS))) {
		usbpd_data->typec_try_state_change = TRY_ROLE_SWAP_NONE;
		return -ETIMEDOUT;
	}

	return 0;
}

int s2mm005_pr_set(const struct typec_capability *cap, enum typec_role role)
{
	struct s2mm005_data *usbpd_data = container_of(cap, struct s2mm005_data, typec_cap);

	if (!usbpd_data)
		return -EINVAL;

	pr_info("%s : typec_power_role=%d, typec_data_role=%d, role=%d\n", __func__,
		usbpd_data->typec_power_role, usbpd_data->typec_data_role, role);

	if (usbpd_data->typec_power_role != TYPEC_SINK
	    && usbpd_data->typec_power_role != TYPEC_SOURCE)
		return -EPERM;
	else if (usbpd_data->typec_power_role == role)
		return -EPERM;

	reinit_completion(&usbpd_data->typec_reverse_completion);
	if (role == TYPEC_SINK) {
		pr_info("%s :try reversing, from Source to Sink\n", __func__);
		usbpd_data->typec_try_state_change = TRY_ROLE_SWAP_PR;
		usbpd_data->is_pr_swap++;
		send_role_swap_message(usbpd_data, 0);
	} else if (role == TYPEC_SOURCE) {
		pr_info("%s :try reversing, from Sink to Source\n", __func__);
		usbpd_data->typec_try_state_change = TRY_ROLE_SWAP_PR;
		usbpd_data->is_pr_swap++;
		send_role_swap_message(usbpd_data, 0);
	} else {
		pr_info("%s :invalid typec_role\n", __func__);
		return -EIO;
	}

	if (!wait_for_completion_timeout(&usbpd_data->typec_reverse_completion, 
				msecs_to_jiffies(TRY_ROLE_SWAP_WAIT_MS))) {
		usbpd_data->typec_try_state_change = TRY_ROLE_SWAP_NONE;
		if (usbpd_data->typec_power_role != role)
			return -ETIMEDOUT;
	}

	return 0;
}

void typec_role_swap_check(struct work_struct *wk)
{
	struct delayed_work *delay_work =
		container_of(wk, struct delayed_work, work);
	struct s2mm005_data *usbpd_data =
		container_of(delay_work, struct s2mm005_data, typec_role_swap_work);

	pr_info("%s: s2mm005_port_type_set check again usbpd_data->pd_state=%d\n",
		__func__, usbpd_data->pd_state);

	usbpd_data->typec_try_state_change = 0;

	if (usbpd_data->pd_state == State_PE_Initial_detach) {
		pr_err("%s: ccic_set_dual_role reverse failed, set mode to DRP\n", __func__);
		disable_irq(usbpd_data->irq);
		/* exit from Disabled state and set mode to DRP */
		s2mm005_rprd_mode_change(usbpd_data, TYPE_C_ATTACH_DRP);
		enable_irq(usbpd_data->irq);
	}
}

int s2mm005_port_type_set(const struct typec_capability *cap, enum typec_port_type port_type)
{
	struct s2mm005_data *usbpd_data = container_of(cap, struct s2mm005_data, typec_cap);
	int timeout = 0;

	if (!usbpd_data) {
		pr_err("%s : usbpd_data is null\n", __func__);
		return -EINVAL;
	}

	pr_info("%s : typec_power_role=%d, typec_data_role=%d, port_type=%d\n",
		__func__, usbpd_data->typec_power_role, usbpd_data->typec_data_role, port_type);

	switch (port_type) {
	case TYPEC_PORT_DFP:
		pr_info("%s : try reversing, from UFP(Sink) to DFP(Source)\n", __func__);
		usbpd_data->typec_try_state_change = TRY_ROLE_SWAP_TYPE;
		s2mm005_rprd_mode_change(usbpd_data, TYPE_C_ATTACH_DFP);
		break;
	case TYPEC_PORT_UFP:
		pr_info("%s : try reversing, from DFP(Source) to UFP(Sink)\n", __func__);
		/* turns off VBUS first */
 		vbus_turn_on_ctrl(0);
#if defined(CONFIG_CCIC_NOTIFIER)
		ccic_event_work(usbpd_data,
			CCIC_NOTIFY_DEV_MUIC, CCIC_NOTIFY_ID_ATTACH,
			0/*attach*/, 0/*rprd*/, 0);
#endif
		usbpd_data->typec_try_state_change = TRY_ROLE_SWAP_TYPE;
		s2mm005_rprd_mode_change(usbpd_data, TYPE_C_ATTACH_UFP);
		break;
	case TYPEC_PORT_DRP:
		pr_info("%s : set to DRP\n", __func__);
		return 0;
	default :
		pr_info("%s : invalid typec_role\n", __func__);
		return -EINVAL;
	}

	if (usbpd_data->typec_try_state_change) {
		reinit_completion(&usbpd_data->typec_reverse_completion);
		timeout =
		    wait_for_completion_timeout(&usbpd_data->typec_reverse_completion,
						msecs_to_jiffies
						(DUAL_ROLE_SET_MODE_WAIT_MS));

		if (!timeout) {
			pr_err("%s: reverse failed, set mode to DRP\n", __func__);
			disable_irq(usbpd_data->irq);
			/* exit from Disabled state and set mode to DRP */
			usbpd_data->typec_try_state_change = 0;
			s2mm005_rprd_mode_change(usbpd_data, TYPE_C_ATTACH_DRP);
			enable_irq(usbpd_data->irq);
			return -EIO;
		} else {
			pr_err("%s: reverse success, one more check\n", __func__);
			schedule_delayed_work(&usbpd_data->typec_role_swap_work, msecs_to_jiffies(DUAL_ROLE_SET_MODE_WAIT_MS));
		}
	}

	return 0;
}

int s2mm005_get_pd_support(struct s2mm005_data *usbpd_data)
{
	bool support_pd_role_swap = false;
	struct device_node *np = NULL;
	
	np = of_find_compatible_node(NULL, NULL, "sec-s2mm005,i2c");
	
	if (np)
		support_pd_role_swap = of_property_read_bool(np, "support_pd_role_swap");
	else
		pr_info("%s : np is null\n", __func__);

	pr_info("%s : TYPEC_CLASS: support_pd_role_swap is %d, usbc_data->pd_support : %d\n", __func__,
		support_pd_role_swap, usbpd_data->pd_support);
	
	if (support_pd_role_swap && usbpd_data->pd_support)
		return TYPEC_PWR_MODE_PD;
	
	return usbpd_data->pwr_opmode;
}
#endif

void process_cc_water_det(void * data)
{
	struct s2mm005_data *usbpd_data = data;

	pr_info("%s\n",__func__);
	s2mm005_int_clear(usbpd_data);	// interrupt clear
#if defined(CONFIG_SEC_FACTORY)
	if(!usbpd_data->fac_water_enable)
#endif
	{
		if(usbpd_data->water_det)
			s2mm005_manual_LPM(usbpd_data, 0x9);
	}
}

#if defined(CONFIG_CCIC_ALTERNATE_MODE)
void dp_detach(void *data)
{
	struct s2mm005_data *usbpd_data = data;

	pr_info("%s: dp_is_connect %d\n",__func__, usbpd_data->dp_is_connect);

	ccic_event_work(usbpd_data, CCIC_NOTIFY_DEV_USB_DP,
		CCIC_NOTIFY_ID_USB_DP, 0/*attach*/, usbpd_data->dp_hs_connect/*drp*/, 0);
	ccic_event_work(usbpd_data, CCIC_NOTIFY_DEV_DP,
		CCIC_NOTIFY_ID_DP_CONNECT, 0/*attach*/, 0/*drp*/, 0);

	usbpd_data->dp_is_connect = 0;
	usbpd_data->dp_hs_connect = 0;
	usbpd_data->is_sent_pin_configuration = 0;
	return;
}
#endif

//////////////////////////////////////////// ////////////////////////////////////
// Moisture detection processing
////////////////////////////////////////////////////////////////////////////////
void process_cc_water(void * data, LP_STATE_Type *Lp_DATA)
{
	struct s2mm005_data *usbpd_data = data;
	struct i2c_client *i2c = usbpd_data->i2c;
	uint32_t R_len;
	uint16_t REG_ADD;
	u8 R_DATA[1];
	int i;

	pr_info("%s\n",__func__);
	/* read reg for water and dry state */
	for(i=0; i<1; i++){
		R_DATA[0] = 0x00;
		REG_ADD = 0x8;
		s2mm005_read_byte(i2c, REG_ADD, R_DATA, 1);   //dummy read
	}
	REG_ADD = 0x60;
	R_len = 4;
	s2mm005_read_byte(i2c, REG_ADD, Lp_DATA->BYTE, R_len);
	dev_info(&i2c->dev, "%s: WATER reg:0x%02X WATER=%d DRY=%d\n", __func__,
		Lp_DATA->BYTE[0],
		Lp_DATA->BITS.WATER_DET,
		Lp_DATA->BITS.RUN_DRY);

#if defined(CONFIG_BATTERY_SAMSUNG)
		if (lpcharge) {
			dev_info(&i2c->dev, "%s: BOOTING_RUN_DRY=%d\n", __func__,
				Lp_DATA->BITS.BOOTING_RUN_DRY);
			usbpd_data->booting_run_dry  = Lp_DATA->BITS.BOOTING_RUN_DRY;
		}
#endif

#if defined(CONFIG_SEC_FACTORY)
	if (!Lp_DATA->BITS.WATER_DET) {
		Lp_DATA->BITS.RUN_DRY = 1;
	}
#endif

	/* check for dry case */
	if (Lp_DATA->BITS.RUN_DRY && !usbpd_data->run_dry) {
		dev_info(&i2c->dev, "== WATER RUN-DRY DETECT ==\n");
		ccic_event_work(usbpd_data,
			CCIC_NOTIFY_DEV_BATTERY, CCIC_NOTIFY_ID_WATER,
			0/*attach*/, 0, 0);
	}

		usbpd_data->run_dry = Lp_DATA->BITS.RUN_DRY;

	/* check for water case */
	if ((Lp_DATA->BITS.WATER_DET & !usbpd_data->water_det)) {
		dev_info(&i2c->dev, "== WATER DETECT ==\n");
		ccic_event_work(usbpd_data,
			CCIC_NOTIFY_DEV_BATTERY, CCIC_NOTIFY_ID_WATER,
			1/*attach*/, 0, 0);
	}

	usbpd_data->water_det = Lp_DATA->BITS.WATER_DET;
}
////////////////////////////////////////////////////////////////////////////////
// ATTACH processing
////////////////////////////////////////////////////////////////////////////////
void process_cc_attach(void * data,u8 *plug_attach_done)
{
	struct s2mm005_data *usbpd_data = data;
	struct i2c_client *i2c = usbpd_data->i2c;
	LP_STATE_Type Lp_DATA;
	FUNC_STATE_Type Func_DATA;
	static int prev_pd_state = State_PE_Initial_detach;
	uint32_t R_len;
	uint16_t REG_ADD;
	struct otg_notify *o_notify = get_otg_notify();
#if defined(CONFIG_CCIC_S2MM005_ANALOG_AUDIO)
	static int earphone_state = 0;
#endif
	int is_dfp = 0;
	int is_src = 0;

	pr_info("%s\n",__func__);

	// Check for moisture
	process_cc_water(usbpd_data, &Lp_DATA);

	if (usbpd_data->water_det || !usbpd_data->run_dry) {
		/* Moisture detection is only handled in the disconnected state(LPM). */
		return;
	} else if(!usbpd_data->booting_run_dry) {
		dev_info(&i2c->dev, " Water? No Dry\n");
		ccic_event_work(usbpd_data,
			CCIC_NOTIFY_DEV_BATTERY, CCIC_NOTIFY_ID_WATER, 1/*attach*/, 0, 0);

		REG_ADD = 0x20;
		R_len = 4;
		s2mm005_read_byte(i2c, REG_ADD, Func_DATA.BYTE, R_len);
		dev_info(&i2c->dev, "Rsvd_H:0x%02X   PD_Nxt_State:0x%02X   Rsvd_L:0x%02X   PD_State:%02d\n",
			Func_DATA.BYTES.RSP_BYTE2,
			Func_DATA.BYTES.PD_Next_State,
			Func_DATA.BYTES.RSP_BYTE1,
			Func_DATA.BYTES.PD_State);

		return;
	} else {
		REG_ADD = 0x20;
		R_len = 4;

		s2mm005_read_byte(i2c, REG_ADD, Func_DATA.BYTE, R_len);
		dev_info(&i2c->dev, "Rsvd_H:0x%02X   PD_Nxt_State:0x%02X   Rsvd_L:0x%02X   PD_State:%02d\n",
			Func_DATA.BYTES.RSP_BYTE2,
			Func_DATA.BYTES.PD_Next_State,
			Func_DATA.BYTES.RSP_BYTE1,
			Func_DATA.BYTES.PD_State);

		dev_info(&i2c->dev, "CC direction info: %d\n", Func_DATA.BYTES.RSP_BYTE1);
//		CC_DIR = Func_DATA.BYTES.RSP_BYTE1;
		if (Func_DATA.BYTES.RSP_BYTE1 == 66)
                    CC_DIR = 1;
                else CC_DIR = 0;
		dev_info(&i2c->dev, "CC_DIR: %d\n", CC_DIR);

#if defined(CONFIG_USB_HW_PARAM)
		if (!usbpd_data->pd_state && Func_DATA.BYTES.PD_State && Func_DATA.BITS.VBUS_CC_Short)
					inc_hw_param(o_notify, USB_CCIC_VBUS_CC_SHORT_COUNT);
#endif
		usbpd_data->pd_state = Func_DATA.BYTES.PD_State;
		usbpd_data->func_state = Func_DATA.DATA;

		is_dfp = usbpd_data->func_state & (0x1 << 26) ? 1 : 0;
		is_src = usbpd_data->func_state & (0x1 << 25) ? 1 : 0;
		dev_info(&i2c->dev, "func_state :0x%X, is_dfp : %d, is_src : %d\n", usbpd_data->func_state, \
			is_dfp, is_src);

		if (Func_DATA.BITS.RESET) {
			dev_info(&i2c->dev, "ccic reset detected\n");
			if (!Lp_DATA.BITS.AUTO_LP_ENABLE_BIT) {
				/* AUTO LPM Enable */
				s2mm005_manual_LPM(usbpd_data, 6);
			}

			set_enable_alternate_mode(ALTERNATE_MODE_START);
		}
		if (usbpd_data->pd_state == State_PE_SRC_Wait_New_Capabilities && Lp_DATA.BITS.Sleep_Cable_Detect) {
			s2mm005_manual_LPM(usbpd_data, 0x0D);
			return;
		}
#if defined(CONFIG_CCIC_S2MM005_ANALOG_AUDIO)
		dev_info(&i2c->dev, "%s : Lp_DATA.BITS.ACC_DETECTION:%d\n", __func__, Lp_DATA.BITS.ACC_DETECTION);

		if (usbpd_data->pd_state == State_PE_SRC_Wait_New_Capabilities && Lp_DATA.BITS.ACC_DETECTION) {
			dev_info(&i2c->dev, "Type-C Earjack detected\n");
			s2mm005_manual_ACC_LPM(usbpd_data);
			return;
		}
#endif
		if (!usbpd_data->support_analog_audio) {
			if (usbpd_data->pd_state == State_PE_SRC_Wait_New_Capabilities && Lp_DATA.BITS.ACC_DETECTION) {
				dev_info(&i2c->dev, "Type-C Earjack detected\n");
				usbpd_data->acc_type = CCIC_DOCK_TYPEC_ANALOG_EARPHONE;
				process_check_accessory(usbpd_data);
				return;
			}
		}
	}
#ifdef CONFIG_USB_NOTIFY_PROC_LOG
	store_usblog_notify(NOTIFY_FUNCSTATE, (void*)&usbpd_data->pd_state, NULL);
#endif

	if(usbpd_data->pd_state !=  State_PE_Initial_detach)
	{
		*plug_attach_done = 1;
		usbpd_data->plug_rprd_sel = 1;
		if (usbpd_data->pd_state == State_PE_PRS_SRC_SNK_Transition_to_off) {
			pr_info("%s State_PE_PRS_SRC_SNK_Transition_to_off! \n", __func__);
			vbus_turn_on_ctrl(0);
		} else if (usbpd_data->pd_state == State_PE_PRS_SNK_SRC_Source_on) {
			pr_info("%s State_PE_PRS_SNK_SRC_Source_on! \n", __func__);
			pd_noti.event = PDIC_NOTIFY_EVENT_PD_PRSWAP_SNKTOSRC;
			pd_noti.sink_status.selected_pdo_num = 0;
			pd_noti.sink_status.available_pdo_num = 0;
			pd_noti.sink_status.current_pdo_num = 0;
			ccic_event_work(usbpd_data, CCIC_NOTIFY_DEV_BATTERY,
					CCIC_NOTIFY_ID_POWER_STATUS, 0, 0, 0);
			vbus_turn_on_ctrl(1);
		}
		
#if defined(CONFIG_TYPEC)
		if (usbpd_data->pd_state == State_PE_SRC_Ready || usbpd_data->pd_state == State_PE_SNK_Ready)
		{
			usbpd_data->pd_support = true;
#if defined(CONFIG_USB_DWC3)
			dwc3_set_selfpowered(1);
#endif
			typec_set_pwr_opmode(usbpd_data->port, TYPEC_PWR_MODE_PD);
#ifdef CONFIG_MUIC_SM5705_SWITCH_CONTROL_GPIO
			pr_info("%s call muic_GPIO_control(0)\n", __func__);
			muic_GPIO_control(0);
#endif
		}
#endif		
		if (usbpd_data->is_dr_swap || usbpd_data->is_pr_swap) {
			dev_info(&i2c->dev, "%s - ignore all pd_state by %s\n",	__func__,(usbpd_data->is_dr_swap ? "dr_swap" : "pr_swap"));
			return;
		}

		if (usbpd_data->pd_state == State_PE_SNK_Wait_for_Capabilities
		|| usbpd_data->pd_state == State_PE_SNK_Select_Capability
		|| usbpd_data->pd_state == State_PE_SNK_Ready) {
			if (Func_DATA.BITS.VBUS_CC_Short) {
				dev_info(&i2c->dev, "%s PD TA CC-VBUS  short\n", __func__);
			}
		}

		switch (usbpd_data->pd_state) {
		//lse 0717 new	
		case State_PE_SRC_Startup:
		
		case State_PE_SRC_Send_Capabilities:
		case State_PE_SRC_Negotiate_Capability:
		case State_PE_SRC_Transition_Supply:
		case State_PE_SRC_Ready:
		case State_PE_SRC_Disabled:
			dev_info(&i2c->dev, "%s %d: pd_state:%02d, is_host = %d, is_client = %d\n",
							__func__, __LINE__, usbpd_data->pd_state, usbpd_data->is_host, usbpd_data->is_client);
			if (usbpd_data->is_client == CLIENT_ON) {
				dev_info(&i2c->dev, "%s %d: pd_state:%02d, turn off client\n",
							__func__, __LINE__, usbpd_data->pd_state);
				ccic_event_work(usbpd_data,
					CCIC_NOTIFY_DEV_MUIC, CCIC_NOTIFY_ID_ATTACH, 0/*attach*/, 0/*rprd*/, 0);
#if defined(CONFIG_DUAL_ROLE_USB_INTF)
				usbpd_data->power_role = DUAL_ROLE_PROP_PR_NONE;
#endif
				send_otg_notify(o_notify, NOTIFY_EVENT_POWER_SOURCE, 0);
				ccic_event_work(usbpd_data,
					CCIC_NOTIFY_DEV_USB, CCIC_NOTIFY_ID_USB, 0/*attach*/, USB_STATUS_NOTIFY_DETACH/*drp*/, 0);
				usbpd_data->is_client = CLIENT_OFF;
				msleep(300);
			}
			if (usbpd_data->is_host == HOST_OFF) {
				/* muic */
				ccic_event_work(usbpd_data,
					CCIC_NOTIFY_DEV_MUIC, CCIC_NOTIFY_ID_ATTACH, 1/*attach*/, 1/*rprd*/, 0);
				/* otg */
				usbpd_data->is_host = HOST_ON;
#if defined(CONFIG_DUAL_ROLE_USB_INTF)
				usbpd_data->power_role = DUAL_ROLE_PROP_PR_SRC;
#elif defined(CONFIG_TYPEC)
				usbpd_data->typec_power_role = TYPEC_SOURCE;
				typec_set_pwr_role(usbpd_data->port, TYPEC_SOURCE);
#endif
				send_otg_notify(o_notify, NOTIFY_EVENT_POWER_SOURCE, 1);
				/* add to turn on external 5V */
					vbus_turn_on_ctrl(1);

				if (is_blocked(o_notify, NOTIFY_BLOCK_TYPE_HOST))
					s2mm005_set_upsm_mode();

				ccic_event_work(usbpd_data,
					CCIC_NOTIFY_DEV_USB, CCIC_NOTIFY_ID_USB, 1/*attach*/, USB_STATUS_NOTIFY_ATTACH_DFP/*drp*/, 0);
#if defined(CONFIG_CCIC_ALTERNATE_MODE)
				// only start alternate mode at DFP state
//				send_alternate_message(usbpd_data, VDM_DISCOVER_ID);
				if (usbpd_data->acc_type != CCIC_DOCK_DETACHED) {
					pr_info("%s: cancel_delayed_work_sync - pd_state : %d\n", __func__, usbpd_data->pd_state);
					cancel_delayed_work_sync(&usbpd_data->acc_detach_work);
				}
#endif
			}
			break;
		case State_PE_SNK_Wait_for_Capabilities:
		case State_PE_SNK_Evaluate_Capability:
		case State_PE_SNK_Ready:
		case State_ErrorRecovery:
			dev_info(&i2c->dev, "%s %d: pd_state:%02d, is_host = %d, is_client = %d\n",
						__func__, __LINE__, usbpd_data->pd_state, usbpd_data->is_host, usbpd_data->is_client);

			if (usbpd_data->is_host == HOST_ON) {
				dev_info(&i2c->dev, "%s %d: pd_state:%02d,  turn off host\n",
						__func__, __LINE__, usbpd_data->pd_state);

#if defined(CONFIG_CCIC_ALTERNATE_MODE)
				if (usbpd_data->dp_is_connect == 1) {
					dp_detach(usbpd_data);
				}
#endif
				ccic_event_work(usbpd_data,
					CCIC_NOTIFY_DEV_MUIC, CCIC_NOTIFY_ID_ATTACH, 0/*attach*/, 1/*rprd*/, 0);
#if defined(CONFIG_DUAL_ROLE_USB_INTF)
				usbpd_data->power_role = DUAL_ROLE_PROP_PR_NONE;
#endif
				send_otg_notify(o_notify, NOTIFY_EVENT_POWER_SOURCE, 0);
				/* add to turn off external 5V */
				vbus_turn_on_ctrl(0);
				ccic_event_work(usbpd_data,
					CCIC_NOTIFY_DEV_USB, CCIC_NOTIFY_ID_USB, 0/*attach*/, USB_STATUS_NOTIFY_DETACH/*drp*/, 0);
				usbpd_data->is_host = HOST_OFF;
				msleep(300);
			}

			if (Lp_DATA.BITS.PDSTATE29_SBU_DONE) {
				dev_info(&i2c->dev, "%s SBU check done\n", __func__);
				ccic_event_work(usbpd_data,
					CCIC_NOTIFY_DEV_MUIC, CCIC_NOTIFY_ID_ATTACH,
					1/*attach*/, 0/*rprd*/,
					(Func_DATA.BITS.VBUS_CC_Short || Func_DATA.BITS.VBUS_SBU_Short) ? Rp_Abnormal:Func_DATA.BITS.RP_CurrentLvl);
			} else {
			/* muic */
			ccic_event_work(usbpd_data,
				CCIC_NOTIFY_DEV_MUIC, CCIC_NOTIFY_ID_ATTACH,
					1/*attach*/, 0/*rprd*/, Rp_Sbu_check);
			}

			if (usbpd_data->is_client == CLIENT_OFF && usbpd_data->is_host == HOST_OFF) {
				/* usb */
				usbpd_data->is_client = CLIENT_ON;
#if defined(CONFIG_DUAL_ROLE_USB_INTF)
				usbpd_data->power_role = DUAL_ROLE_PROP_PR_SNK;
#elif defined(CONFIG_TYPEC)
				usbpd_data->typec_power_role = TYPEC_SINK;
				typec_set_pwr_role(usbpd_data->port, TYPEC_SINK);
#endif
				send_otg_notify(o_notify, NOTIFY_EVENT_POWER_SOURCE, 0);
				ccic_event_work(usbpd_data,
					CCIC_NOTIFY_DEV_USB, CCIC_NOTIFY_ID_USB, 1/*attach*/, USB_STATUS_NOTIFY_ATTACH_UFP/*drp*/, 0);
			}
			break;
		default :
			break;
		}
	} else {
		*plug_attach_done = 0;
		usbpd_data->plug_rprd_sel = 0;
		usbpd_data->is_dr_swap = 0;
		usbpd_data->is_pr_swap = 0;
#if defined(CONFIG_CCIC_S2MM005_ANALOG_AUDIO)
		if(earphone_state == 1){
			printk("%s : Type-C Analog Headset detached\n",__func__);

			/* muic */
			ccic_event_work(usbpd_data, CCIC_NOTIFY_DEV_MUIC, CCIC_NOTIFY_ID_EARJACK, 0/*attach*/, 0/*rprd*/, 0);
			/* audio */
			ccic_event_work(usbpd_data, CCIC_NOTIFY_DEV_AUDIO, CCIC_NOTIFY_ID_EARJACK, 0/*attach*/, 0/*rprd*/, 0);
			earphone_state = 0;
			return;
		}
#endif
		if(prev_pd_state == State_PE_Initial_detach)	// if detach -> detach event is ignored
		{
			printk("%s : detach event is ignored\n",__func__);
			return;
		}
		/* muic */
		ccic_event_work(usbpd_data,
			CCIC_NOTIFY_DEV_MUIC, CCIC_NOTIFY_ID_ATTACH, 0/*attach*/, 0/*rprd*/, 0);
#if defined(CONFIG_COMBO_REDRIVER_PTN36502)
		ptn36502_config(SAFE_STATE, 0);
#endif
		if(usbpd_data->is_host > HOST_OFF || usbpd_data->is_client > CLIENT_OFF) {
#if defined(CONFIG_CCIC_ALTERNATE_MODE)
			if (usbpd_data->dp_is_connect == 1) {
				dp_detach(usbpd_data);
			}

			if (usbpd_data->acc_type != CCIC_DOCK_DETACHED) {
				pr_info("%s: schedule_delayed_work - pd_state : %d\n", __func__, usbpd_data->pd_state);
				if (usbpd_data->acc_type == CCIC_DOCK_HMT) {
					schedule_delayed_work(&usbpd_data->acc_detach_work, msecs_to_jiffies(GEAR_VR_DETACH_WAIT_MS));
//				}
//				else if(usbpd_data->acc_type == CCIC_DOCK_DP) {
//					acc_detach_process(usbpd_data);
				}else {
					// Changed the sequence of acc detach work before calling usb detach for DP drvier panic problem in case if qcom by jjuny79.kim
					schedule_delayed_work(&usbpd_data->acc_detach_work, msecs_to_jiffies(0));
				}
			}
#endif
#if defined(CONFIG_DUAL_ROLE_USB_INTF)
			if(usbpd_data->is_host > HOST_OFF || usbpd_data->power_role == DUAL_ROLE_PROP_PR_SRC)
				vbus_turn_on_ctrl(0);
#elif defined(CONFIG_TYPEC)
			if(usbpd_data->is_host > HOST_OFF || usbpd_data->typec_power_role == TYPEC_SOURCE)
				vbus_turn_on_ctrl(0);
#endif
			/* usb or otg */
			dev_info(&i2c->dev, "%s %d: pd_state:%02d, is_host = %d, is_client = %d\n",
					__func__, __LINE__, usbpd_data->pd_state, usbpd_data->is_host, usbpd_data->is_client);
			usbpd_data->is_host = HOST_OFF;
			usbpd_data->is_client = CLIENT_OFF;
#if defined(CONFIG_DUAL_ROLE_USB_INTF)
			usbpd_data->power_role = DUAL_ROLE_PROP_PR_NONE;
#elif defined(CONFIG_TYPEC)
			if (usbpd_data->partner) {
				pr_info("%s : typec_unregister_partner\n", __func__);
				if (!IS_ERR(usbpd_data->partner))
					typec_unregister_partner(usbpd_data->partner);
				usbpd_data->partner = NULL;
				usbpd_data->typec_power_role = TYPEC_SINK;
				usbpd_data->typec_data_role = TYPEC_DEVICE;
				usbpd_data->pwr_opmode = TYPEC_PWR_MODE_USB;
			}
			if (usbpd_data->typec_try_state_change == TRY_ROLE_SWAP_PR ||
				usbpd_data->typec_try_state_change == TRY_ROLE_SWAP_DR) {
				/* Role change try and new mode detected */
				pr_info("%s : typec_reverse_completion, detached while pd_swap\n", __func__);
				usbpd_data->typec_try_state_change = TRY_ROLE_SWAP_NONE;
				complete(&usbpd_data->typec_reverse_completion);
			}			
#endif
			usbpd_data->pd_support = false;
#if defined(CONFIG_USB_DWC3)
			dwc3_set_selfpowered(0);
#endif
			send_otg_notify(o_notify, NOTIFY_EVENT_POWER_SOURCE, 0);
			ccic_event_work(usbpd_data,
				CCIC_NOTIFY_DEV_USB, CCIC_NOTIFY_ID_USB, 0/*attach*/, USB_STATUS_NOTIFY_DETACH/*drp*/, 0);
#if defined(CONFIG_DUAL_ROLE_USB_INTF)
			if (!usbpd_data->try_state_change)
				s2mm005_rprd_mode_change(usbpd_data, TYPE_C_ATTACH_DRP);
#elif defined(CONFIG_TYPEC)
			if (!usbpd_data->typec_try_state_change)
				s2mm005_rprd_mode_change(usbpd_data, TYPE_C_ATTACH_DRP);			
#endif
		}
		usbpd_data->detach_done_wait = 1;
	}
	prev_pd_state = usbpd_data->pd_state;
}

//////////////////////////////////////////// ////////////////////////////////////
// Detach processing
// 1. Used when the s2mm005 unbind case
////////////////////////////////////////////////////////////////////////////////
void process_cc_detach(void * data)
{
	struct s2mm005_data *usbpd_data = data;
	struct otg_notify *o_notify = get_otg_notify();
	if (usbpd_data->pd_state) {
		usbpd_data->pd_state = State_PE_Initial_detach;
#if defined(CONFIG_CCIC_NOTIFIER)
		ccic_event_work(usbpd_data,
			CCIC_NOTIFY_DEV_MUIC, CCIC_NOTIFY_ID_ATTACH, 0/*attach*/, 0/*rprd*/, 0);
#endif
		if(usbpd_data->is_host > HOST_OFF)
				vbus_turn_on_ctrl(0);
		usbpd_data->is_host = HOST_OFF;
		usbpd_data->is_client = CLIENT_OFF;
#if defined(CONFIG_DUAL_ROLE_USB_INTF)
		usbpd_data->power_role = DUAL_ROLE_PROP_PR_NONE;
#elif defined(CONFIG_TYPEC)
		usbpd_data->typec_power_role = TYPEC_SINK;
		typec_set_pwr_role(usbpd_data->port, TYPEC_SINK);
		typec_set_data_role(usbpd_data->port, TYPEC_DEVICE);
		typec_set_pwr_opmode(usbpd_data->port, TYPEC_PWR_MODE_USB);
#endif
		send_otg_notify(o_notify, NOTIFY_EVENT_POWER_SOURCE, 0);
	}
}

////////////////////////////////////////////////////////////////////////////////
// Get staus interrupt register
////////////////////////////////////////////////////////////////////////////////
void process_cc_get_int_status(void *data, uint32_t *pPRT_MSG, MSG_IRQ_STATUS_Type *MSG_IRQ_State)
{
	struct s2mm005_data *usbpd_data = data;
	struct i2c_client *i2c = usbpd_data->i2c;
	uint8_t	R_INT_STATUS[32];
	uint16_t REG_ADD;
	uint32_t cnt;
	uint32_t IrqPrint;
	VDM_MSG_IRQ_STATUS_Type VDM_MSG_IRQ_State;
	SSM_MSG_IRQ_STATUS_Type SSM_MSG_IRQ_State;
	AP_REQ_GET_STATUS_Type AP_REQ_GET_State;

	pr_info("%s\n",__func__);
	for(cnt = 0;cnt < 32;cnt++)
	{
		R_INT_STATUS[cnt] = 0;
	}

	REG_ADD = 0x30;
	s2mm005_read_byte(i2c, REG_ADD, R_INT_STATUS, 32);	// sram :

	s2mm005_int_clear(usbpd_data);	// interrupt clear
	pPRT_MSG = (uint32_t *)&R_INT_STATUS[0];
	dev_info(&i2c->dev, "SYNC     Status = 0x%08X\n",pPRT_MSG[0]);
	dev_info(&i2c->dev, "CTRL MSG Status = 0x%08X\n",pPRT_MSG[1]);
	dev_info(&i2c->dev, "DATA MSG Status = 0x%08X\n",pPRT_MSG[2]);
	dev_info(&i2c->dev, "EXTD MSG Status = 0x%08X\n",pPRT_MSG[3]);
	dev_info(&i2c->dev, "MSG IRQ Status = 0x%08X\n",pPRT_MSG[4]);
	dev_info(&i2c->dev, "VDM IRQ Status = 0x%08X\n",pPRT_MSG[5]);
	dev_info(&i2c->dev, "SSM_MSG IRQ Status = 0x%08X\n",pPRT_MSG[6]);
	dev_info(&i2c->dev, "AP REQ GET Status = 0x%08X\n",pPRT_MSG[7]);
	MSG_IRQ_State->DATA = pPRT_MSG[4];
	VDM_MSG_IRQ_State.DATA = pPRT_MSG[5];
	SSM_MSG_IRQ_State.DATA = pPRT_MSG[6];
	AP_REQ_GET_State.DATA = pPRT_MSG[7];

#if defined(CONFIG_SEC_FACTORY)
	if((AP_REQ_GET_State.BYTES[0] >> 5) > 0) {
		dev_info(&i2c->dev, "FAC: Repeat_State:%d, Repeat_RID:%d, RID0:%d\n",
			AP_REQ_GET_State.BITS.FAC_Abnormal_Repeat_State,
			AP_REQ_GET_State.BITS.FAC_Abnormal_Repeat_RID,
			AP_REQ_GET_State.BITS.FAC_Abnormal_RID0);
		ccic_event_work(usbpd_data,
			CCIC_NOTIFY_DEV_CCIC, CCIC_NOTIFY_ID_FAC,
			AP_REQ_GET_State.BYTES[0] >> 5, 0, 0); // b5~b7
	}
#endif

	IrqPrint = 1;
	for(cnt=0;cnt<32;cnt++)
	{
		if((MSG_IRQ_State->DATA&IrqPrint) != 0)
		{
			dev_info(&i2c->dev, "    - IRQ %s \n",&MSG_IRQ_Print[cnt][0]);
		}
		IrqPrint = (IrqPrint<<1);
	}
	if (MSG_IRQ_State->BITS.Ctrl_Flag_DR_Swap)
	{
		usbpd_data->is_dr_swap++;
		dev_info(&i2c->dev, "is_dr_swap count : 0x%x\n", usbpd_data->is_dr_swap);
#if defined(CONFIG_CCIC_ALTERNATE_MODE)
		if (usbpd_data->dp_is_connect)
		{
			dev_info(&i2c->dev, "dr_swap is skiped, current status is dp mode !!\n");
		}
		else
#endif
		{
			if (usbpd_data->is_host == HOST_ON) {
				ccic_event_work(usbpd_data,
					CCIC_NOTIFY_DEV_USB, CCIC_NOTIFY_ID_USB, 0/*attach*/, USB_STATUS_NOTIFY_DETACH/*drp*/, 0);
				msleep(300);
				ccic_event_work(usbpd_data,
					CCIC_NOTIFY_DEV_MUIC, CCIC_NOTIFY_ID_ATTACH, 1/*attach*/, 0/*rprd*/,0);
				ccic_event_work(usbpd_data,
					CCIC_NOTIFY_DEV_USB, CCIC_NOTIFY_ID_USB, 1/*attach*/, USB_STATUS_NOTIFY_ATTACH_UFP/*drp*/, 0);
				usbpd_data->is_host = HOST_OFF;
				usbpd_data->is_client = CLIENT_ON;
			} else if (usbpd_data->is_client == CLIENT_ON) {
				ccic_event_work(usbpd_data,
					CCIC_NOTIFY_DEV_USB, CCIC_NOTIFY_ID_USB, 0/*attach*/, USB_STATUS_NOTIFY_DETACH/*drp*/, 0);
				msleep(300);
				ccic_event_work(usbpd_data,
					CCIC_NOTIFY_DEV_MUIC, CCIC_NOTIFY_ID_ATTACH, 1/*attach*/, 1/*rprd*/,0);
				ccic_event_work(usbpd_data,
					CCIC_NOTIFY_DEV_USB, CCIC_NOTIFY_ID_USB, 1/*attach*/, USB_STATUS_NOTIFY_ATTACH_DFP/*drp*/, 0);
				usbpd_data->is_host = HOST_ON;
				usbpd_data->is_client = CLIENT_OFF;
			}
		}
	}

#if defined(CONFIG_CCIC_ALTERNATE_MODE)
	if(VDM_MSG_IRQ_State.DATA)
		receive_alternate_message(usbpd_data, &VDM_MSG_IRQ_State);
	if(SSM_MSG_IRQ_State.BITS.Ssm_Flag_Unstructured_Data)
		receive_unstructured_vdm_message(usbpd_data, &SSM_MSG_IRQ_State);
	if(!AP_REQ_GET_State.BITS.Alt_Mode_By_I2C)
		set_enable_alternate_mode(ALTERNATE_MODE_RESET);
	if (!AP_REQ_GET_State.BITS.DPM_START_ON)
		set_enable_alternate_mode(ALTERNATE_MODE_RESET);
#endif
}

////////////////////////////////////////////////////////////////////////////////
// RID processing
////////////////////////////////////////////////////////////////////////////////
void process_cc_rid(void *data)
{
	struct s2mm005_data *usbpd_data = data;
	struct i2c_client *i2c = usbpd_data->i2c;
	static int prev_rid = RID_OPEN;
	u8 rid;
#if defined(CONFIG_DUAL_ROLE_USB_INTF)
	struct otg_notify *o_notify = get_otg_notify();
#endif

	pr_info("%s\n",__func__);
	s2mm005_read_byte_16(i2c, 0x50, &rid);	// fundtion read , 0x20 , 0x0:detach , not 0x0 attach :  source 3,6,7 / sink 16:17:21:29(decimanl)
	dev_info(&i2c->dev, "prev_rid:%x , RID:%x\n",prev_rid, rid);
	if(usbpd_data->pd_state == State_PE_Initial_detach && rid != RID_OPEN) {
		// workaround codes
		dev_info(&i2c->dev, "function_state mismatch with rid, forcely set it as rid open!\n");
		rid = RID_OPEN;
	}

	if(rid > 7)
		usbpd_data->cur_rid = RID_OPEN;
	else
		usbpd_data->cur_rid = rid;

	if(rid) {
#ifdef CONFIG_MUIC_SM5705_SWITCH_CONTROL_GPIO
		if ((rid == RID_000K) || (rid == RID_001K) || (rid == RID_523K) || (rid == RID_619K)
                    || (rid == RID_255K) || (rid == RID_301K)) {
			muic_GPIO_control(1);
                } else if ((rid == RID_UNDEFINED) || (rid == RID_OPEN)) {
			muic_GPIO_control(0);
#if defined(CONFIG_MUIC_SUPPORT_KEYBOARDDOCK)
			if (!adc_rescan_done) {
				muic_ADC_rescan();
				adc_rescan_done = 1;
			}
#endif
		}
#endif
		if(prev_rid != rid)
		{
#if defined(CONFIG_CCIC_NOTIFIER)
			/* rid */
			ccic_event_work(usbpd_data,
				CCIC_NOTIFY_DEV_MUIC, CCIC_NOTIFY_ID_RID, rid/*rid*/, 0, 0);

			if (rid == RID_000K) {
				/* otg */
				dev_info(&i2c->dev, "%s %d: RID_000K\n", __func__, __LINE__);
				if (usbpd_data->is_client) {
					/* usb or otg */
					ccic_event_work(usbpd_data,
						CCIC_NOTIFY_DEV_USB, CCIC_NOTIFY_ID_USB, 0/*attach*/, USB_STATUS_NOTIFY_DETACH/*drp*/, 0);
				}
				usbpd_data->is_host = HOST_ON_BY_RID000K;
				usbpd_data->is_client = CLIENT_OFF;
#if defined(CONFIG_DUAL_ROLE_USB_INTF)
				usbpd_data->power_role = DUAL_ROLE_PROP_PR_SRC;
				send_otg_notify(o_notify, NOTIFY_EVENT_POWER_SOURCE, 1);
#endif
				/* add to turn on external 5V */
				vbus_turn_on_ctrl(1);
				ccic_event_work(usbpd_data,
					CCIC_NOTIFY_DEV_USB, CCIC_NOTIFY_ID_USB, 1/*attach*/, USB_STATUS_NOTIFY_ATTACH_DFP/*drp*/, 0);
			} if(rid == RID_OPEN || rid == RID_UNDEFINED || rid == RID_523K || rid == RID_619K) {
				if (prev_rid == RID_000K) {
					/* add to turn off external 5V */
					vbus_turn_on_ctrl(0);
				}
				usbpd_data->is_host = HOST_OFF;
				usbpd_data->is_client = CLIENT_OFF;
#if defined(CONFIG_DUAL_ROLE_USB_INTF)
				usbpd_data->power_role = DUAL_ROLE_PROP_PR_NONE;
				send_otg_notify(o_notify, NOTIFY_EVENT_POWER_SOURCE, 0);
#endif
				/* usb or otg */
				ccic_event_work(usbpd_data,
					CCIC_NOTIFY_DEV_USB, CCIC_NOTIFY_ID_USB, 0/*attach*/, USB_STATUS_NOTIFY_DETACH/*drp*/, 0);
			}
#endif
		}
		prev_rid = rid;
	}
	return;
}
