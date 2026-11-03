// SPDX-License-Identifier: GPL-2.0-only
/* Qualcomm remote modem heap service, QMI service 0x34 version 1. */
#include <linux/dma-direct.h>
#include <linux/dma-mapping.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/remoteproc/qcom_rproc.h>
#include <linux/rpmsg/qcom_glink.h>
#include <dt-bindings/firmware/qcom,scm.h>
#include "heap_mem_ext_v01.h"

struct modem_heap {
	struct device *dev;
	u32 id, limit, used;
	bool guaranteed, persistent, guard, shared, requested, pool;
	size_t allocated, assigned;
	void *cpu;
	dma_addr_t dma;
	phys_addr_t phys;
	u64 owners;
	struct qcom_glink_window *window;
};

struct memshare {
	struct qmi_handle qmi;
	struct mutex lock;
	struct notifier_block ssr;
	void *ssr_handle;
	unsigned int count;
	struct modem_heap clients[];
};

static struct modem_heap *find_heap(struct memshare *ms, u32 id, u32 proc)
{
	unsigned int i;

	if (proc != DHMS_MEM_PROC_MPSS_V01)
		return NULL;
	for (i = 0; i < ms->count; i++)
		if (ms->clients[i].id == id)
			return &ms->clients[i];
	return NULL;
}

static int release_heap(struct modem_heap *heap)
{
	const struct qcom_scm_vmperm host = { QCOM_SCM_VMID_HLOS, QCOM_SCM_PERM_RWX };
	int ret;

	if (!heap->cpu)
		return 0;
	qcom_glink_unregister_modem_window(heap->window);
	heap->window = NULL;
	if (heap->owners != BIT_ULL(QCOM_SCM_VMID_HLOS)) {
		ret = qcom_scm_assign_mem(heap->phys, heap->assigned, &heap->owners, &host, 1);
		if (ret)
			return dev_err_probe(heap->dev, ret, "cannot reclaim modem heap %u\n", heap->id);
	}
	dma_free_attrs(heap->dev, heap->allocated, heap->cpu, heap->dma, DMA_ATTR_FORCE_CONTIGUOUS);
	heap->cpu = NULL;
	heap->used = 0;
	return 0;
}

static int allocate_heap(struct modem_heap *heap, u32 requested)
{
	const struct qcom_scm_vmperm perms[] = {
		{ QCOM_SCM_VMID_MSS_MSA, QCOM_SCM_PERM_RW },
		{ QCOM_SCM_VMID_HLOS, QCOM_SCM_PERM_RW },
	};
	u32 size = min(requested, heap->limit);
	int ret;

	if (!heap->cpu) {
		if (!size)
			return 0;
		heap->assigned = PAGE_ALIGN((size_t)size);
		heap->allocated = heap->assigned + (heap->guard ? PAGE_SIZE : 0);
		heap->cpu = dma_alloc_attrs(heap->dev, heap->allocated, &heap->dma,
					    GFP_KERNEL, DMA_ATTR_FORCE_CONTIGUOUS);
		if (!heap->cpu)
			return -ENOMEM;
		heap->owners = BIT_ULL(QCOM_SCM_VMID_HLOS);
		heap->phys = dma_to_phys(heap->dev, heap->dma);
		ret = qcom_scm_assign_mem(heap->phys, heap->assigned, &heap->owners,
					  perms, heap->shared ? 2 : 1);
		if (ret) {
			release_heap(heap);
			return ret;
		}
		heap->used = size;
	}
	if (heap->shared && !heap->window) {
		struct qcom_glink_window *window;

		window = qcom_glink_register_modem_window(heap->cpu, heap->phys, heap->used);
		if (IS_ERR(window)) {
			ret = PTR_ERR(window);
			release_heap(heap);
			return ret;
		}
		heap->window = window;
	}
	return 0;
}

static void alloc_request(struct qmi_handle *qh, struct sockaddr_qrtr *sq,
			  struct qmi_txn *txn, const void *data)
{
	struct memshare *ms = container_of(qh, struct memshare, qmi);
	const struct mem_alloc_generic_req_msg_v01 *req = data;
	struct mem_alloc_generic_resp_msg_v01 resp = {};
	struct modem_heap *heap;
	int ret;

	mutex_lock(&ms->lock);
	heap = find_heap(ms, req->client_id, req->proc_id);
	ret = heap ? allocate_heap(heap, req->num_bytes) : -ENOENT;
	if (ret) {
		resp.resp.result = QMI_RESULT_FAILURE_V01;
		resp.resp.error = ret == -ENOENT ? QMI_ERR_INVALID_ID_V01 :
				  ret == -ENOMEM ? QMI_ERR_NO_MEMORY_V01 : QMI_ERR_INTERNAL_V01;
	} else {
		heap->requested = true;
		resp.sequence_id_valid = 1;
		resp.sequence_id = req->sequence_id;
		resp.dhms_mem_alloc_addr_info_valid = 1;
		resp.dhms_mem_alloc_addr_info_len = 1;
		resp.dhms_mem_alloc_addr_info[0].phy_addr = heap->cpu ? heap->phys : 0;
		resp.dhms_mem_alloc_addr_info[0].num_bytes = heap->used;
	}
	mutex_unlock(&ms->lock);
	qmi_send_response(qh, sq, txn, MEM_ALLOC_GENERIC_RESP_MSG_V01,
			  MEM_ALLOC_REQ_MAX_MSG_LEN_V01, mem_alloc_generic_resp_msg_data_v01_ei, &resp);
}

static void free_request(struct qmi_handle *qh, struct sockaddr_qrtr *sq,
			 struct qmi_txn *txn, const void *data)
{
	struct memshare *ms = container_of(qh, struct memshare, qmi);
	const struct mem_free_generic_req_msg_v01 *req = data;
	struct mem_free_generic_resp_msg_v01 resp = {};
	struct modem_heap *heap = NULL;
	unsigned int i;

	mutex_lock(&ms->lock);
	if (req->dhms_mem_alloc_addr_info_len == 1 &&
	    (!req->proc_id_valid || req->proc_id == DHMS_MEM_PROC_MPSS_V01)) {
		for (i = 0; i < ms->count; i++) {
			struct modem_heap *candidate = &ms->clients[i];

			if (req->client_id_valid && req->client_id != candidate->id)
				continue;
			if (candidate->cpu &&
			    candidate->phys == req->dhms_mem_alloc_addr_info[0].phy_addr &&
			    candidate->used == req->dhms_mem_alloc_addr_info[0].num_bytes) {
				heap = candidate;
				break;
			}
		}
	}
	if (!heap) {
		resp.resp.result = QMI_RESULT_FAILURE_V01;
		resp.resp.error = QMI_ERR_INVALID_ID_V01;
	} else if (!heap->persistent && !heap->guaranteed && release_heap(heap)) {
		resp.resp.result = QMI_RESULT_FAILURE_V01;
		resp.resp.error = QMI_ERR_INTERNAL_V01;
	} else {
		qcom_glink_unregister_modem_window(heap->window);
		heap->window = NULL;
		heap->requested = false;
	}
	mutex_unlock(&ms->lock);
	qmi_send_response(qh, sq, txn, MEM_FREE_GENERIC_RESP_MSG_V01,
			  MEM_FREE_REQ_MAX_MSG_LEN_V01, mem_free_generic_resp_msg_data_v01_ei, &resp);
}

static void query_request(struct qmi_handle *qh, struct sockaddr_qrtr *sq,
			  struct qmi_txn *txn, const void *data)
{
	struct memshare *ms = container_of(qh, struct memshare, qmi);
	const struct mem_query_size_req_msg_v01 *req = data;
	struct mem_query_size_rsp_msg_v01 resp = {};
	struct modem_heap *heap;

	heap = find_heap(ms, req->client_id, req->proc_id_valid ? req->proc_id : DHMS_MEM_PROC_MPSS_V01);
	if (heap) {
		resp.size_valid = 1;
		resp.size = heap->limit;
	} else {
		resp.resp.result = QMI_RESULT_FAILURE_V01;
		resp.resp.error = QMI_ERR_INVALID_ID_V01;
	}
	qmi_send_response(qh, sq, txn, MEM_QUERY_SIZE_RESP_MSG_V01,
			  MEM_QUERY_MAX_MSG_LEN_V01, mem_query_size_resp_msg_data_v01_ei, &resp);
}

static const struct qmi_msg_handler handlers[] = {
	{ QMI_REQUEST, MEM_ALLOC_GENERIC_REQ_MSG_V01, mem_alloc_generic_req_msg_data_v01_ei,
	  sizeof(struct mem_alloc_generic_req_msg_v01), alloc_request },
	{ QMI_REQUEST, MEM_FREE_GENERIC_REQ_MSG_V01, mem_free_generic_req_msg_data_v01_ei,
	  sizeof(struct mem_free_generic_req_msg_v01), free_request },
	{ QMI_REQUEST, MEM_QUERY_SIZE_REQ_MSG_V01, mem_query_size_req_msg_data_v01_ei,
	  sizeof(struct mem_query_size_req_msg_v01), query_request },
	{}
};

static int modem_event(struct notifier_block *nb, unsigned long event, void *data)
{
	struct memshare *ms = container_of(nb, struct memshare, ssr);
	unsigned int i;

	mutex_lock(&ms->lock);
	for (i = 0; i < ms->count; i++) {
		struct modem_heap *heap = &ms->clients[i];

		if (event == QCOM_SSR_BEFORE_SHUTDOWN)
			heap->requested = false;
		if (event == QCOM_SSR_AFTER_POWERUP && !heap->requested &&
		    !heap->persistent && !heap->guaranteed)
			release_heap(heap);
	}
	mutex_unlock(&ms->lock);
	return NOTIFY_DONE;
}

static void cleanup_clients(void *data)
{
	struct memshare *ms = data;

	while (ms->count) {
		struct modem_heap *heap = &ms->clients[--ms->count];

		release_heap(heap);
		if (heap->pool)
			of_reserved_mem_device_release(heap->dev);
		put_device(heap->dev);
	}
}

static int add_heap(struct memshare *ms, struct device_node *np)
{
	struct modem_heap *heap = &ms->clients[ms->count];
	struct platform_device *child;
	const char *label;
	int ret;

	if (of_property_read_string(np, "label", &label) || strcmp(label, "modem") ||
	    of_property_read_u32(np, "qcom,client-id", &heap->id) ||
	    of_property_read_u32(np, "qcom,peripheral-size", &heap->limit) ||
	    find_heap(ms, heap->id, DHMS_MEM_PROC_MPSS_V01))
		return -EINVAL;
	child = of_find_device_by_node(np);
	if (!child)
		return -ENODEV;
	heap->dev = &child->dev;
	ms->count++;
	ret = of_dma_configure(heap->dev, np, true);
	if (ret)
		return ret;
	if (device_iommu_mapped(heap->dev))
		return -EINVAL;
	if (of_property_present(np, "memory-region")) {
		ret = of_reserved_mem_device_init(heap->dev);
		if (ret)
			return ret;
		heap->pool = true;
	}
	heap->shared = of_property_read_bool(np, "qcom,shared");
	heap->guard = of_property_read_bool(np, "qcom,guard-band");
	heap->guaranteed = of_property_read_bool(np, "qcom,allocate-boot-time");
	heap->persistent = of_property_read_bool(np, "qcom,allocate-on-request");
	return heap->guaranteed ? allocate_heap(heap, heap->limit) : 0;
}

static int memshare_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct memshare *ms;
	int ret;

	if (!qcom_scm_is_available())
		return -EPROBE_DEFER;
	ms = devm_kzalloc(dev, struct_size(ms, clients, of_get_available_child_count(dev->of_node)),
			  GFP_KERNEL);
	if (!ms)
		return -ENOMEM;
	mutex_init(&ms->lock);
	ret = devm_of_platform_populate(dev);
	if (ret)
		return ret;
	ret = devm_add_action_or_reset(dev, cleanup_clients, ms);
	if (ret)
		return ret;
	for_each_available_child_of_node_scoped(dev->of_node, np) {
		if (!of_device_is_compatible(np, "qcom,memshare-peripheral"))
			continue;
		ret = add_heap(ms, np);
		if (ret)
			return dev_err_probe(dev, ret, "cannot set up %pOF\n", np);
	}
	ms->ssr.notifier_call = modem_event;
	ms->ssr_handle = qcom_register_ssr_notifier("modem", &ms->ssr);
	if (IS_ERR(ms->ssr_handle))
		return PTR_ERR(ms->ssr_handle);
	/* A FREE request can describe 64 (64-bit address, 32-bit size) pairs. */
	ret = qmi_handle_init(&ms->qmi, 1 + 3 + MAX_ARR_CNT_V01 * 12 + 2 * 7, NULL, handlers);
	if (ret)
		goto unregister;
	ret = qmi_add_server(&ms->qmi, 0x34, 1, 1);
	if (ret) {
		qmi_handle_release(&ms->qmi);
		goto unregister;
	}
	platform_set_drvdata(pdev, ms);
	return 0;

unregister:
	qcom_unregister_ssr_notifier(ms->ssr_handle, &ms->ssr);
	return ret;
}

static void memshare_remove(struct platform_device *pdev)
{
	struct memshare *ms = platform_get_drvdata(pdev);

	qmi_handle_release(&ms->qmi);
	qcom_unregister_ssr_notifier(ms->ssr_handle, &ms->ssr);
}

static const struct of_device_id memshare_match[] = {
	{ .compatible = "qcom,memshare" },
	{}
};
MODULE_DEVICE_TABLE(of, memshare_match);

static struct platform_driver memshare_driver = {
	.probe = memshare_probe,
	.remove = memshare_remove,
	.driver = { .name = "msm_memshare", .of_match_table = memshare_match },
};
module_platform_driver(memshare_driver);

MODULE_DESCRIPTION("Qualcomm modem remote heap QMI service");
MODULE_LICENSE("GPL");
