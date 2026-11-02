/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef DWC3_GADGET_GSI_H
#define DWC3_GADGET_GSI_H

struct dwc3;

#if IS_ENABLED(CONFIG_USB_DWC3_QCOM_GSI)
int dwc3_gsi_alloc_events(struct dwc3 *dwc, unsigned int length);
void dwc3_gsi_set_events(struct dwc3 *dwc, bool enable);
#else
static inline int dwc3_gsi_alloc_events(struct dwc3 *dwc, unsigned int length)
{
	return 0;
}
static inline void dwc3_gsi_set_events(struct dwc3 *dwc, bool enable) {}
#endif
#endif
