/****************************************************************************
 *  (C) Copyright 2008 Samsung Electronics Co., Ltd., All rights reserved
 *
 * @file   s3c-otg-hcdi-driver.c
 * @brief  It provides functions related with module for OTGHCD driver.
 * @version
 *  -# Jun 9,2008 v1.0 by SeungSoo Yang (ss1.yang@samsung.com)
 *	  : Creating the initial version of this code
 *  -# Jul 15,2008 v1.2 by SeungSoo Yang (ss1.yang@samsung.com)
 *	  : Optimizing for performance
 *  -# Aug 18,2008 v1.3 by SeungSoo Yang (ss1.yang@samsung.com)
 *	  : Modifying for successful rmmod & disconnecting
 * @see None
 *
 ****************************************************************************/
/****************************************************************************
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 ****************************************************************************/

#include <linux/module.h>
#include <linux/init.h>

#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/errno.h>
#include <linux/interrupt.h> /* for SA_SHIRQ */
#include <mach/map.h>	/* address for smdk */
#include <linux/dma-mapping.h> /* dma_alloc_coherent */
#include <linux/ioport.h>	/* request_mem_request ... */
#include <asm/irq.h>	/* for IRQ_OTG */
#include <linux/clk.h>


#include "shost.h"


static inline struct sec_otghost *hcd_to_sec_otghost(struct usb_hcd *hcd)
{
	return (struct sec_otghost *)(hcd->hcd_priv);
}
static inline struct usb_hcd *sec_otghost_to_hcd(struct sec_otghost *otghost)
{
	return container_of((void *) otghost, struct usb_hcd, hcd_priv);
}


#include "shost_oci.c"
#include "shost_transfer.c"
#include "shost_roothub.c"
#include "shost_hcd.c"

volatile	u8			*g_pUDCBase;
struct		usb_hcd		*g_pUsbHcd;

static const char	gHcdName[] = "EMSP_OTG_HCD";
static struct platform_device *g_pdev;


static void otg_power_work(struct work_struct *work)
{
	struct sec_otghost *otghost = container_of(work,
				struct sec_otghost, work);
	struct sec_otghost_data *hdata = otghost->otg_data;

	if (hdata && hdata->set_pwr_cb) {
		pr_info("otg power off - don't turn off the power\n");
		hdata->set_pwr_cb(0);
#ifdef CONFIG_USB_HOST_NOTIFY
		if (g_pUsbHcd)
			host_state_notify(&g_pUsbHcd->ndev,
					NOTIFY_HOST_OVERCURRENT);
#endif
	} else {
		otg_err(true, "invalid otghost data\n");
	}
}

static int s5pc110_start_otg(u32 regs)
{
	int ret_val = 0;
	u32 reg_val = 0;
	struct platform_device *pdev = g_pdev;
	struct sec_otghost *otghost = NULL;
	struct sec_otghost_data *otg_data = dev_get_platdata(&pdev->dev);

	otg_dbg(OTG_DBG_OTGHCDI_DRIVER, "s3c_otg_drv_probe\n");
	pr_info("otg probe start : 0x%x\n", regs);


	/*init for host mode*/
	/**
	Allocate memory for the base HCD &	Initialize the base HCD.
	*/
	g_pUsbHcd = usb_create_hcd(&s5pc110_otg_hc_driver, &pdev->dev,
					"s3cotg");/*pdev->dev.bus_id*/
	if (g_pUsbHcd == NULL) {
		ret_val = -ENOMEM;
		otg_err(OTG_DBG_OTGHCDI_DRIVER,
			"failed to usb_create_hcd\n");
		goto err_out_clk;
	}

#if 1
	pr_info("otg probe regs : 0x%p\n", otg_data->regs);

	if (!regs) {
		pr_info("otg mapping hcd resource\n");
		/* mapping hcd resource & device resource*/

		g_pUsbHcd->rsrc_start = pdev->resource[0].start;
		g_pUsbHcd->rsrc_len   = pdev->resource[0].end -
			pdev->resource[0].start + 1;

		if (!request_mem_region(g_pUsbHcd->rsrc_start,
					g_pUsbHcd->rsrc_len, gHcdName)) {
			otg_err(OTG_DBG_OTGHCDI_DRIVER,
					"failed to request_mem_region\n");
			ret_val = -EBUSY;
			goto err_out_create_hcd;
		}

		pr_info("otg rsrc_start %llu, ren %llu\n",
				g_pUsbHcd->rsrc_start,
				g_pUsbHcd->rsrc_len);

		pr_info("otg regs : %p\n", S3C_VA_HSOTG);

		/* Physical address => Virtual address */
		g_pUsbHcd->regs = S3C_VA_HSOTG;
		g_pUDCBase = (u8 *)g_pUsbHcd->regs;

	} else
		g_pUDCBase = (u8 *)regs;
#endif
	pr_info("otg g_pUDCBase 0x%p\n", g_pUDCBase);

	g_pUsbHcd->self.otg_port = 1;

	otghost = hcd_to_sec_otghost(g_pUsbHcd);

	if (otghost == NULL) {
		otg_err(true, "failed to get otghost hcd\n");
		ret_val = USB_ERR_FAIL;
		goto err_out_create_hcd;
	}
	otghost->otg_data = otg_data;

	INIT_WORK(&otghost->work, otg_power_work);
	otghost->wq = create_singlethread_workqueue("sec_otghostd");

	/* call others' init() */
	ret_val = otg_hcd_init_modules(otghost);
	if (ret_val != USB_ERR_SUCCESS) {
		otg_err(OTG_DBG_OTGHCDI_DRIVER,
			"failed to otg_hcd_init_modules\n");
		ret_val = USB_ERR_FAIL;
		goto err_out_create_hcd;
	}

	/**
	 * Attempt to ensure this device is really a s5pc110 USB-OTG Controller.
	 * Read and verify the SNPSID register contents. The value should be
	 * 0x45F42XXX, which corresponds to "OT2", as in "OTG version 2.XX".
	 */
	reg_val = read_reg_32(0x40);
	pr_info("otg reg 0x40 = %x\n", reg_val);
	if ((reg_val & 0xFFFFF000) != 0x4F542000) {
		otg_err(OTG_DBG_OTGHCDI_DRIVER,
			"Bad value for SNPSID: 0x%x\n", reg_val);
		ret_val = -EINVAL;
		goto err_out_create_hcd_init;
	}
#ifdef CONFIG_USB_HOST_NOTIFY
	if (otg_data->host_notify) {
		g_pUsbHcd->host_notify = otg_data->host_notify;
		g_pUsbHcd->ndev.name = dev_name(&pdev->dev);
		ret_val = host_notify_dev_register(&g_pUsbHcd->ndev);
		if (ret_val) {
			otg_err(OTG_DBG_OTGHCDI_DRIVER,
				"Failed to host_notify_dev_register\n");
			goto err_out_create_hcd_init;
		}
	}
#endif
#ifdef CONFIG_USB_SEC_WHITELIST
	if (otg_data->sec_whlist_table_num)
		g_pUsbHcd->sec_whlist_table_num =
			otg_data->sec_whlist_table_num;
#endif

	/*
	 * Finish generic HCD initialization and start the HCD. This function
	 * allocates the DMA buffer pool, registers the USB bus, requests the
	 * IRQ line, and calls s5pc110_otghcd_start method.
	 */
	ret_val = usb_add_hcd(g_pUsbHcd,
			pdev->resource[1].start, IRQF_DISABLED);
	if (ret_val < 0) {
		otg_err(OTG_DBG_OTGHCDI_DRIVER,
			"Failed to add hcd driver\n");
		goto err_out_host_notify_register;
	}

	otg_dbg(OTG_DBG_OTGHCDI_DRIVER,
		"OTG HCD Initialized HCD, bus=%s, usbbus=%d\n",
		"C110 OTG Controller", g_pUsbHcd->self.busnum);

	/* otg_print_registers(); */

	wake_lock_init(&otghost->wake_lock, WAKE_LOCK_SUSPEND, "usb_otg");
	wake_lock(&otghost->wake_lock);

	return USB_ERR_SUCCESS;

err_out_host_notify_register:
#ifdef CONFIG_USB_HOST_NOTIFY
	host_notify_dev_unregister(&g_pUsbHcd->ndev);
#endif

err_out_create_hcd_init:
	otg_hcd_deinit_modules(otghost);
	if (!regs)
		release_mem_region(g_pUsbHcd->rsrc_start, g_pUsbHcd->rsrc_len);

err_out_create_hcd:
	usb_put_hcd(g_pUsbHcd);

err_out_clk:

	return ret_val;
}

static int s5pc110_stop_otg(void)
{
	struct sec_otghost *otghost = NULL;
	struct sec_otghost_data *otgdata = NULL;

	pr_info("+++++ OTG STOP\n");

	otg_dbg(OTG_DBG_OTGHCDI_DRIVER, "s5pc110_stop_otg\n");

	otghost = hcd_to_sec_otghost(g_pUsbHcd);

#ifdef CONFIG_USB_HOST_NOTIFY
	host_notify_dev_unregister(&g_pUsbHcd->ndev);
#endif

	otg_hcd_deinit_modules(otghost);

	destroy_workqueue(otghost->wq);

	wake_unlock(&otghost->wake_lock);
	wake_lock_destroy(&otghost->wake_lock);

	usb_remove_hcd(g_pUsbHcd);

#if 1
	if (g_pUDCBase == S3C_VA_HSOTG) {
		pr_info("otg release_mem_region\n");
		release_mem_region(g_pUsbHcd->rsrc_start, g_pUsbHcd->rsrc_len);
	}
#endif

	usb_put_hcd(g_pUsbHcd);

	otgdata = otghost->otg_data;
	if (otgdata && otgdata->phy_exit && otgdata->pdev) {
		pr_info("otg phy_off\n");
		otgdata->phy_exit(0);
	}

	return 0;
}

/**
 * static int s5pc110_otg_drv_probe (struct platform_device *pdev)
 *
 * @brief probe function of OTG hcd platform_driver
 *
 * @param [in] pdev : pointer of platform_device of otg hcd platform_driver
 *
 * @return USB_ERR_SUCCESS : If success
 *         USB_ERR_FAIL : If fail
 * @remark
 * it allocates resources of it and call other modules' init function.
 * then call usb_create_hcd, usb_add_hcd, s5pc110_otghcd_start functions
 */

static int s5pc110_otg_drv_probe(struct platform_device *pdev)
{
	struct sec_otghost_data *otg_data = dev_get_platdata(&pdev->dev);
	g_pdev = pdev;

	pr_info("otg host_probe start %p\n", s5pc110_start_otg);
	otg_data->start = s5pc110_start_otg;
	otg_data->stop = s5pc110_stop_otg;
	otg_data->pdev = pdev;

	return 0;
}


/**
 * static int s5pc110_otg_drv_remove (struct platform_device *dev)
 *
 * @brief remove function of OTG hcd platform_driver
 *
 * @param [in] pdev : pointer of platform_device of otg hcd platform_driver
 *
 * @return USB_ERR_SUCCESS : If success
 *         USB_ERR_FAIL : If fail
 * @remark
 * This function is called when the otg device unregistered with the
 * s5pc110_otg_driver. This happens, for example, when the rmmod command is
 * executed. The device may or may not be electrically present. If it is
 * present, the driver stops device processing. Any resources used on behalf
 * of this device are freed.
 */

static int s5pc110_otg_drv_remove(struct platform_device *dev)
{
	return USB_ERR_SUCCESS;
}

/**
 * @struct s5pc110_otg_driver
 *
 * @brief
 * This structure defines the methods to be called by a bus driver
 * during the lifecycle of a device on that bus. Both drivers and
 * devices are registered with a bus driver. The bus driver matches
 * devices to drivers based on information in the device and driver
 * structures.
 *
 * The probe function is called when the bus driver matches a device
 * to this driver. The remove function is called when a device is
 * unregistered with the bus driver.
 */
struct platform_driver s5pc110_otg_driver = {
	.probe = s5pc110_otg_drv_probe,
	.remove = s5pc110_otg_drv_remove,
/*	.shutdown = usb_hcd_platform_shutdown, */
	.driver = {
		.name = "s3c_otghcd",
		.owner = THIS_MODULE,
	},
};

/**
 * static int __init s5pc110_otg_module_init(void)
 *
 * @brief module_init function
 *
 * @return it returns result of platform_driver_register
 * @remark
 * This function is called when the s5pc110_otg_driver is installed with the
 * insmod command. It registers the s5pc110_otg_driver structure with the
 * appropriate bus driver. This will cause the s5pc110_otg_driver_probe function
 * to be called. In addition, the bus driver will automatically expose
 * attributes defined for the device and driver in the special sysfs file
 * system.
 */
static int __init s5pc110_otg_module_init(void)
{
	int	ret_val = 0;

	otg_dbg(OTG_DBG_OTGHCDI_DRIVER,
		"s3c_otg_module_init\n");

	ret_val = platform_driver_register(&s5pc110_otg_driver);
	if (ret_val < 0) {
		otg_err(OTG_DBG_OTGHCDI_DRIVER,
			"platform_driver_register\n");
	}
	return ret_val;
}

/**
 * static void __exit s5pc110_otg_module_exit(void)
 *
 * @brief module_exit function
 *
 * @remark
 * This function is called when the driver is removed from the kernel
 * with the rmmod command. The driver unregisters itself with its bus
 * driver.
 */
static void __exit s5pc110_otg_module_exit(void)
{
	otg_dbg(OTG_DBG_OTGHCDI_DRIVER,
		"s3c_otg_module_exit\n");
	platform_driver_unregister(&s5pc110_otg_driver);
}

/* for debug */
void otg_print_registers(void)
{
	/* USB PHY Control Registers */

	pr_info("otg clock = %s\n",
			(readl(OTG_CLOCK) & (1<<13)) ? "ON" : "OFF");
	pr_info("otg USB_CONTROL = 0x%x.\n", readl(OTG_PHY_CONTROL));
	pr_info("otg UPHYPWR = 0x%x.\n", readl(OTG_PHYPWR));
	pr_info("otg UPHYCLK = 0x%x.\n", readl(OTG_PHYCLK));
	pr_info("otg URSTCON = 0x%x.\n", readl(OTG_RSTCON));

	/* OTG LINK Core registers (Core Global Registers) */
	pr_info("otg GOTGCTL = 0x%x.\n", read_reg_32(GOTGCTL));
	pr_info("otg GOTGINT = 0x%x.\n", read_reg_32(GOTGINT));
	pr_info("otg GAHBCFG = 0x%x.\n", read_reg_32(GAHBCFG));
	pr_info("otg GUSBCFG = 0x%x.\n", read_reg_32(GUSBCFG));
	pr_info("otg GINTSTS = 0x%x.\n", read_reg_32(GINTSTS));
	pr_info("otg GINTMSK = 0x%x.\n", read_reg_32(GINTMSK));

	/* Host Mode Registers */
	pr_info("otg HCFG = 0x%x.\n", read_reg_32(HCFG));
	pr_info("otg HPRT = 0x%x.\n", read_reg_32(HPRT));
	pr_info("otg HFIR = 0x%x.\n", read_reg_32(HFIR));

	/* Synopsys ID */
	pr_info("otg GSNPSID  = 0x%x.\n", read_reg_32(GSNPSID));

	/* HWCFG */
	pr_info("otg GHWCFG1  = 0x%x.\n", read_reg_32(GHWCFG1));
	pr_info("otg GHWCFG2  = 0x%x.\n", read_reg_32(GHWCFG2));
	pr_info("otg GHWCFG3  = 0x%x.\n", read_reg_32(GHWCFG3));
	pr_info("otg GHWCFG4  = 0x%x.\n", read_reg_32(GHWCFG4));

	/* PCGCCTL */
	pr_info("otg PCGCCTL  = 0x%x.\n", read_reg_32(PCGCCTL));
}

late_initcall(s5pc110_otg_module_init);
module_exit(s5pc110_otg_module_exit);

MODULE_DESCRIPTION("OTG USB HOST controller driver");
MODULE_AUTHOR("SAMSUNG / System LSI / EMSP");
MODULE_LICENSE("GPL");
