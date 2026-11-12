// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2016-2018 Linaro Ltd.
 * Copyright (C) 2014 Sony Mobile Communications AB
 * Copyright (c) 2012-2018, 2021 The Linux Foundation. All rights reserved.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/soc/qcom/mdt_loader.h>
#include <linux/soc/qcom/smem.h>
#include <linux/soc/qcom/smem_state.h>
#include <linux/qcom_scm.h>
#include <linux/interrupt.h>
#include "qcom_common.h"
#include "qcom_q6v5.h"

#include "remoteproc_internal.h"

#define WCSS_CRASH_REASON		421
#define WCSS_SMEM_HOST			1

#define WCNSS_PAS_ID			6
#define MPD_WCNSS_PAS_ID        0xD

#define BUF_SIZE			35

/**
 * enum state - state of a wcss (private)
 * @WCSS_NORMAL: subsystem is operating normally
 * @WCSS_CRASHED: subsystem has crashed and hasn't been shutdown
 * @WCSS_RESTARTING: subsystem has been shutdown and is now restarting
 * @WCSS_SHUTDOWN: subsystem has been shutdown
 *
 */
enum q6_wcss_state {
	WCSS_NORMAL,
	WCSS_CRASHED,
	WCSS_RESTARTING,
	WCSS_SHUTDOWN,
};

enum {
	Q6_IPQ,
	WCSS_AHB_IPQ,
	WCSS_PCIE_IPQ,
};

struct q6_wcss {
	struct device *dev;
	struct qcom_rproc_glink glink_subdev;
	struct qcom_rproc_ssr ssr_subdev;
	struct qcom_q6v5 q6;
	phys_addr_t mem_phys;
	phys_addr_t mem_reloc;
	void *mem_region;
	size_t mem_size;
	int crash_reason_smem;
	u32 version;
	s8 pd_asid;
	enum q6_wcss_state state;
};

struct wcss_data {
	int (*init_irq)(struct qcom_q6v5 *q6, struct platform_device *pdev,
			struct rproc *rproc, int crash_reason,
			void (*handover)(struct qcom_q6v5 *q6));
	const char *q6_firmware_name;
	int crash_reason_smem;
	u32 version;
	const char *ssr_name;
	const struct rproc_ops *ops;
	bool need_auto_boot;
	bool glink_subdev_required;
	s8 pd_asid;
	bool reset_seq;
	u32 pasid;
	int (*mdt_load_sec)(struct device *dev, const struct firmware *fw,
			    const char *fw_name, int pas_id, void *mem_region,
			    phys_addr_t mem_phys, size_t mem_size,
			    phys_addr_t *reloc_base);
};


/* RN07 diagnostic-only: read already allocated SMEM, never allocate/update it.
 * This is a bounded, non-atomic snapshot; remote writes may race the reads.
 * Do not interpret a logged value as proof that an interrupt was delivered.
 */
static void rn07_q6_smem_snapshot(struct q6_wcss *wcss, const char *stage)
{
	static const unsigned int ids[] = { 435, 428, 507 };
	unsigned int n, i, entries;
	size_t size;
	u8 *item;
	u32 header[5], value;
	char name[17];

	if (!of_machine_is_compatible("xiaomi,rn07"))
		return;

	for (n = 0; n < ARRAY_SIZE(ids); n++) {
		size = 0;
		item = qcom_smem_get(WCSS_SMEM_HOST, ids[n], &size);
		if (IS_ERR(item)) {
			dev_info(wcss->dev, "rn07-q6 %s smem=%u error=%ld\n",
				 stage, ids[n], PTR_ERR(item));
			continue;
		}
		if (ids[n] == 507) {
			/* Only the stock header and six boot-argument bytes. */
			dev_info(wcss->dev, "rn07-q6 %s smem=507 size=%zu prefix=%*ph\n",
				 stage, size, (int)min_t(size_t, size, 10), item);
			continue;
		}
		if (size < sizeof(header)) {
			dev_info(wcss->dev, "rn07-q6 %s smem=%u short-size=%zu\n",
				 stage, ids[n], size);
			continue;
		}
		memcpy(header, item, sizeof(header));
		dev_info(wcss->dev, "rn07-q6 %s smem=%u size=%zu header=%*ph\n",
			 stage, ids[n], size, (int)sizeof(header), header);
		if (header[0] != 0x504d5324 || (header[1] & 0xff) != 1)
			continue;
		entries = header[3] >> 16;
		/* At most four 20-byte entries; bound every read by item size. */
		entries = min_t(unsigned int, entries, 4);
		entries = min_t(size_t, entries, (size - sizeof(header)) / 20);
		for (i = 0; i < entries; i++) {
			memcpy(name, item + sizeof(header) + i * 20, 16);
			name[16] = '\0';
			memcpy(&value, item + sizeof(header) + i * 20 + 16, 4);
			dev_info(wcss->dev, "rn07-q6 %s smem=%u entry=%s value=%08x\n",
				 stage, ids[n], name, value);
		}
	}
}

#define rn07_q6_log(wcss, fmt, ...) do { \
	if (of_machine_is_compatible("xiaomi,rn07")) \
		dev_info((wcss)->dev, "rn07-q6 " fmt, ##__VA_ARGS__); \
} while (0)

static int q6_wcss_start(struct rproc *rproc)
{
	struct q6_wcss *wcss = rproc->priv;
	int ret;
	struct device_node *upd_np;
	struct platform_device *upd_pdev;
	struct rproc *upd_rproc;
	struct q6_wcss *upd_wcss;
	const struct wcss_data *desc;

	desc = of_device_get_match_data(wcss->dev);
	if (!desc)
		return -EINVAL;

	rn07_q6_log(wcss, "root start pasid=%u state=%d\n", desc->pasid, rproc->state);
	rn07_q6_smem_snapshot(wcss, "before-auth");
	qcom_q6v5_prepare(&wcss->q6);

	rn07_q6_log(wcss, "root auth enter\n");
	ret = qcom_scm_pas_auth_and_reset(desc->pasid);
	rn07_q6_log(wcss, "root auth return=%d\n", ret);
	if (ret) {
		dev_err(wcss->dev, "wcss_reset failed\n");
		return ret;
	}

	rn07_q6_log(wcss, "root ready-wait enter timeout-jiffies=%u\n", 5 * HZ);
	ret = qcom_q6v5_wait_for_start(&wcss->q6, 5 * HZ);
	rn07_q6_log(wcss, "root ready-wait return=%d\n", ret);
	rn07_q6_smem_snapshot(wcss, "after-ready-wait");
	if (ret == -ETIMEDOUT)
		dev_err(wcss->dev, "start timed out\n");

	/* Bring userpd wcss state to default value */
	for_each_available_child_of_node(wcss->dev->of_node, upd_np) {
		if (!strstr(upd_np->name, "pd"))
			continue;
		upd_pdev = of_find_device_by_node(upd_np);
		upd_rproc = platform_get_drvdata(upd_pdev);
		upd_wcss = upd_rproc->priv;
		upd_wcss->state = WCSS_NORMAL;
	}
	return ret;
}

static int q6_wcss_spawn_pd(struct rproc *rproc)
{
	int ret;
	struct q6_wcss *wcss = rproc->priv;

	ret = qcom_q6v5_request_spawn(&wcss->q6);
	if (ret == -ETIMEDOUT) {
		pr_err("%s spawn timedout\n", rproc->name);
		return ret;
	}

	ret = qcom_q6v5_wait_for_start(&wcss->q6, msecs_to_jiffies(10000));
	if (ret == -ETIMEDOUT) {
		pr_err("%s start timedout\n", rproc->name);
		wcss->q6.running = false;
		return ret;
	}
	wcss->q6.running = true;
	return ret;
}

static int wcss_ahb_pcie_pd_start(struct rproc *rproc)
{
	struct q6_wcss *wcss = rproc->priv;
	const struct wcss_data *desc = of_device_get_match_data(wcss->dev);
	int ret;

	if (desc->reset_seq) {
		ret = qcom_scm_pas_power_up(desc->pasid);
		if (ret) {
			dev_err(wcss->dev, "failed to power up ahb pd\n");
			return ret;
		}
	}

	if (wcss->q6.spawn_bit) {
		ret = q6_wcss_spawn_pd(rproc);
		if (ret)
			return ret;
	}

	wcss->state = WCSS_NORMAL;
	return 0;
}

static int q6_wcss_stop(struct rproc *rproc)
{
	struct q6_wcss *wcss = rproc->priv;
	int ret;
	const struct wcss_data *desc =
			of_device_get_match_data(wcss->dev);

	if (!desc)
		return -EINVAL;

	rn07_q6_log(wcss, "root stop power-down enter pasid=%u\n", desc->pasid);
	ret = qcom_scm_pas_power_down(desc->pasid);
	rn07_q6_log(wcss, "root stop power-down return=%d\n", ret);
	if (ret) {
		dev_err(wcss->dev, "not able to shutdown\n");
		return ret;
	}
	qcom_q6v5_unprepare(&wcss->q6);

	return 0;
}

static int wcss_ahb_pcie_pd_stop(struct rproc *rproc)
{
	struct q6_wcss *wcss = rproc->priv;
	struct rproc *rpd_rproc = dev_get_drvdata(wcss->dev->parent);
	const struct wcss_data *desc = of_device_get_match_data(wcss->dev);
	int ret;

	if (rproc->state != RPROC_CRASHED && wcss->q6.stop_bit) {
		rn07_q6_log(wcss, "userpd stop handshake enter asid=%d\n", wcss->pd_asid);
		ret = qcom_q6v5_request_stop(&wcss->q6, NULL);
		rn07_q6_log(wcss, "userpd stop handshake return=%d\n", ret);
		if (ret) {
			dev_err(&rproc->dev, "pd not stopped\n");
			return ret;
		}
	}

	if (desc->reset_seq) {
		rn07_q6_log(wcss, "userpd power-down enter pasid=%u\n", desc->pasid);
		ret = qcom_scm_pas_power_down(desc->pasid);
		rn07_q6_log(wcss, "userpd power-down return=%d\n", ret);
		if (ret) {
			dev_err(wcss->dev, "failed to power down pd\n");
			return ret;
		}
	}

	if (rproc->state != RPROC_CRASHED) {
		rn07_q6_log(wcss, "userpd parent-shutdown enter\n");
		rproc_shutdown(rpd_rproc);
		rn07_q6_log(wcss, "userpd parent-shutdown returned\n");
	}

	wcss->state = WCSS_SHUTDOWN;
	return 0;
}

static void *q6_wcss_da_to_va(struct rproc *rproc, u64 da, size_t len, bool *is_iomem)
{
	struct q6_wcss *wcss = rproc->priv;
	int offset;

	offset = da - wcss->mem_reloc;
	if (offset < 0 || offset + len > wcss->mem_size)
		return NULL;

	return wcss->mem_region + offset;
}

static int q6_wcss_load(struct rproc *rproc, const struct firmware *fw)
{
	struct q6_wcss *wcss = rproc->priv;
	const struct firmware *m3_fw;
	int ret;
	const char *m3_fw_name;
	struct device_node *upd_np;
	struct platform_device *upd_pdev;
	const struct wcss_data *desc =
				of_device_get_match_data(wcss->dev);

	if (!desc)
		return -EINVAL;

	/* load m3 firmware */
	for_each_available_child_of_node(wcss->dev->of_node, upd_np) {
		if (!strstr(upd_np->name, "pd"))
			continue;
		upd_pdev = of_find_device_by_node(upd_np);

		ret = of_property_read_string(upd_np, "m3_firmware",
					      &m3_fw_name);
		if (!ret && m3_fw_name) {
			ret = request_firmware(&m3_fw, m3_fw_name,
					       &upd_pdev->dev);
			if (ret)
				continue;

			ret = qcom_mdt_load_no_init(wcss->dev, m3_fw,
						    m3_fw_name, 0,
						    wcss->mem_region,
						    wcss->mem_phys,
						    wcss->mem_size,
						    &wcss->mem_reloc);

			release_firmware(m3_fw);

			if (ret) {
				dev_err(wcss->dev,
					"can't load m3_fw.bXX ret:%d\n", ret);
				return ret;
			}
		}
	}

	return qcom_mdt_load(wcss->dev, fw, rproc->firmware,
				desc->pasid, wcss->mem_region,
				wcss->mem_phys, wcss->mem_size,
				&wcss->mem_reloc);
}

static int wcss_ahb_pcie_pd_load(struct rproc *rproc, const struct firmware *fw)
{
	struct q6_wcss *wcss = rproc->priv, *wcss_rpd;
	struct rproc *rpd_rproc = dev_get_drvdata(wcss->dev->parent);
	int ret;
	const struct wcss_data *desc =
				of_device_get_match_data(wcss->dev);

	if (!desc)
		return -EINVAL;

	wcss_rpd = rpd_rproc->priv;

	/* Boot rootpd rproc */
	ret = rproc_boot(rpd_rproc);
	if (ret || wcss->state == WCSS_NORMAL)
		return ret;

	return desc->mdt_load_sec(wcss->dev, fw, rproc->firmware,
				desc->pasid, wcss->mem_region,
				wcss->mem_phys, wcss->mem_size,
				&wcss->mem_reloc);
}

static unsigned long q6_wcss_panic(struct rproc *rproc)
{
	struct q6_wcss *wcss = rproc->priv;

	return qcom_q6v5_panic(&wcss->q6);
}

static const struct rproc_ops wcss_ahb_pcie_ipq5018_ops = {
	.start = wcss_ahb_pcie_pd_start,
	.stop = wcss_ahb_pcie_pd_stop,
	.load = wcss_ahb_pcie_pd_load,
};

static const struct rproc_ops q6_wcss_ipq5018_ops = {
	.start = q6_wcss_start,
	.stop = q6_wcss_stop,
	.da_to_va = q6_wcss_da_to_va,
	.load = q6_wcss_load,
	.get_boot_addr = rproc_elf_get_boot_addr,
	.panic = q6_wcss_panic,
};

static int q6_alloc_memory_region(struct q6_wcss *wcss)
{
	struct reserved_mem *rmem = NULL;
	struct device_node *node;
	struct device *dev = wcss->dev;

	if (wcss->version == Q6_IPQ) {
		node = of_parse_phandle(dev->of_node, "memory-region", 0);
		if (node)
			rmem = of_reserved_mem_lookup(node);

		of_node_put(node);

		if (!rmem) {
			dev_err(dev, "unable to acquire memory-region\n");
			return -EINVAL;
		}
	} else {
		struct rproc *rpd_rproc = dev_get_drvdata(dev->parent);
		struct q6_wcss *rpd_wcss = rpd_rproc->priv;

		wcss->mem_phys = rpd_wcss->mem_phys;
		wcss->mem_reloc = rpd_wcss->mem_reloc;
		wcss->mem_size = rpd_wcss->mem_size;
		wcss->mem_region = rpd_wcss->mem_region;
		return 0;
	}

	wcss->mem_phys = rmem->base;
	wcss->mem_reloc = rmem->base;
	wcss->mem_size = rmem->size;
	wcss->mem_region = devm_ioremap_wc(dev, wcss->mem_phys, wcss->mem_size);
	if (!wcss->mem_region) {
		dev_err(dev, "unable to map memory region: %pa+%pa\n",
			&rmem->base, &rmem->size);
		return -EBUSY;
	}

	return 0;
}

static int q6_get_inbound_irq(struct qcom_q6v5 *q6,
			      struct platform_device *pdev,
			      const char *int_name,
			      irqreturn_t (*handler)(int irq, void *data))
{
	int ret, irq;
	char *interrupt, *tmp = (char *)int_name;
	struct q6_wcss *wcss = q6->rproc->priv;

	irq = platform_get_irq_byname(pdev, int_name);
	if (irq < 0) {
		if (irq != -EPROBE_DEFER)
			dev_err(&pdev->dev,
				"failed to retrieve %s IRQ: %d\n",
					int_name, irq);
		return irq;
	}

	if (!strcmp(int_name, "fatal")) {
		q6->fatal_irq = irq;
	} else if (!strcmp(int_name, "stop-ack")) {
		q6->stop_irq = irq;
		tmp = "stop_ack";
	} else if (!strcmp(int_name, "ready")) {
		q6->ready_irq = irq;
	} else if (!strcmp(int_name, "handover")) {
		q6->handover_irq  = irq;
	} else if (!strcmp(int_name, "spawn-ack")) {
		q6->spawn_irq = irq;
		tmp = "spawn_ack";
	} else {
		dev_err(&pdev->dev, "unknown interrupt\n");
		return -EINVAL;
	}

	interrupt = devm_kzalloc(&pdev->dev, BUF_SIZE, GFP_KERNEL);
	if (!interrupt)
		return -ENOMEM;

	snprintf(interrupt, BUF_SIZE, "q6v5_wcss_userpd%d", wcss->pd_asid);
	strlcat(interrupt, "_", BUF_SIZE);
	strlcat(interrupt, tmp, BUF_SIZE);

	ret = devm_request_threaded_irq(&pdev->dev, irq,
					NULL, handler,
					IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					interrupt, q6);
	if (ret) {
		dev_err(&pdev->dev, "failed to acquire %s irq\n", interrupt);
		return ret;
	}
	return 0;
}

static int q6_get_outbound_irq(struct qcom_q6v5 *q6,
			       struct platform_device *pdev,
			       const char *int_name)
{
	struct qcom_smem_state *tmp_state;
	unsigned  bit;

	tmp_state = qcom_smem_state_get(&pdev->dev, int_name, &bit);
	if (IS_ERR(tmp_state)) {
		dev_err(&pdev->dev, "failed to acquire %s state\n", int_name);
		return PTR_ERR(tmp_state);
	}

	if (!strcmp(int_name, "stop")) {
		q6->state = tmp_state;
		q6->stop_bit = bit;
	} else if (!strcmp(int_name, "spawn")) {
		q6->spawn_state = tmp_state;
		q6->spawn_bit = bit;
	}

	return 0;
}

static int init_irq(struct qcom_q6v5 *q6,
		    struct platform_device *pdev, struct rproc *rproc,
		    int crash_reason, void (*handover)(struct qcom_q6v5 *q6))
{
	int ret;

	q6->rproc = rproc;
	q6->dev = &pdev->dev;
	q6->crash_reason = crash_reason;
	q6->handover = handover;

	init_completion(&q6->start_done);
	init_completion(&q6->stop_done);
	init_completion(&q6->spawn_done);

	ret = q6_get_inbound_irq(q6, pdev, "fatal",
				 q6v5_fatal_interrupt);
	if (ret)
		return ret;

	ret = q6_get_inbound_irq(q6, pdev, "ready",
				 q6v5_ready_interrupt);
	if (ret)
		return ret;

	ret = q6_get_inbound_irq(q6, pdev, "stop-ack",
				 q6v5_stop_interrupt);
	if (ret)
		return ret;

	ret = q6_get_inbound_irq(q6, pdev, "spawn-ack",
				 q6v5_spawn_interrupt);
	if (ret)
		return ret;

	ret = q6_get_outbound_irq(q6, pdev, "stop");
	if (ret)
		return ret;

	ret = q6_get_outbound_irq(q6, pdev, "spawn");
	if (ret)
		return ret;

	return 0;
}

static int q6_wcss_probe(struct platform_device *pdev)
{
	const struct wcss_data *desc;
	struct q6_wcss *wcss;
	struct rproc *rproc;
	int ret;
	char *subdev_name;

	desc = of_device_get_match_data(&pdev->dev);
	if (!desc)
		return -EINVAL;

	rproc = rproc_alloc(&pdev->dev, pdev->name, desc->ops,
			    desc->q6_firmware_name, sizeof(*wcss));
	if (!rproc) {
		dev_err(&pdev->dev, "failed to allocate rproc\n");
		return -ENOMEM;
	}
	wcss = rproc->priv;
	wcss->dev = &pdev->dev;
	wcss->version = desc->version;

	ret = q6_alloc_memory_region(wcss);
	if (ret)
		goto free_rproc;

	wcss->pd_asid = qcom_get_pd_asid(wcss->dev->of_node);
	if (wcss->pd_asid < 0)
		goto free_rproc;

	if (desc->init_irq) {
		ret = desc->init_irq(&wcss->q6, pdev, rproc,
				desc->crash_reason_smem, NULL);
		if (ret) {
			if (wcss->version == Q6_IPQ)
				goto free_rproc;
			else
				dev_info(wcss->dev,
					 "userpd irq registration failed\n");
		}
	}

	if (desc->glink_subdev_required)
		qcom_add_glink_subdev(rproc, &wcss->glink_subdev, desc->ssr_name);

	subdev_name = (char *)(desc->ssr_name ? desc->ssr_name : pdev->name);
	qcom_add_ssr_subdev(rproc, &wcss->ssr_subdev, subdev_name);

	rproc->auto_boot = desc->need_auto_boot;
	ret = rproc_add(rproc);
	if (ret)
		goto free_rproc;

	platform_set_drvdata(pdev, rproc);

	ret = of_platform_populate(wcss->dev->of_node, NULL,
				   NULL, wcss->dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to populate wcss pd nodes\n");
		goto free_rproc;
	}
	return 0;

free_rproc:
	rproc_free(rproc);

	return ret;
}

static int q6_wcss_remove(struct platform_device *pdev)
{
	struct rproc *rproc = platform_get_drvdata(pdev);

	rproc_del(rproc);
	rproc_free(rproc);

	return 0;
}

static const struct wcss_data q6_ipq5018_res_init = {
	.init_irq = qcom_q6v5_init,
	.q6_firmware_name = "IPQ5018/q6_fw.mdt",
	.crash_reason_smem = WCSS_CRASH_REASON,
	.ssr_name = "q6wcss",
	.ops = &q6_wcss_ipq5018_ops,
	.version = Q6_IPQ,
	.glink_subdev_required = true,
	.pasid = MPD_WCNSS_PAS_ID,
};

static const struct wcss_data q6_ipq9574_res_init = {
	.init_irq = qcom_q6v5_init,
	.q6_firmware_name = "IPQ9574/q6_fw.mdt",
	.crash_reason_smem = WCSS_CRASH_REASON,
	.ssr_name = "q6wcss",
	.ops = &q6_wcss_ipq5018_ops,
	.version = Q6_IPQ,
	.glink_subdev_required = true,
	.pasid = WCNSS_PAS_ID,
};

static const struct wcss_data wcss_ahb_ipq5018_res_init = {
	.init_irq = init_irq,
	.q6_firmware_name = "IPQ5018/q6_fw.mdt",
	.crash_reason_smem = WCSS_CRASH_REASON,
	.ops = &wcss_ahb_pcie_ipq5018_ops,
	.version = WCSS_AHB_IPQ,
	.pasid = MPD_WCNSS_PAS_ID,
	.reset_seq = true,
	.mdt_load_sec = qcom_mdt_load_pd_seg,
};

static const struct wcss_data wcss_ahb_ipq9574_res_init = {
	.init_irq = init_irq,
	.q6_firmware_name = "IPQ9574/q6_fw.mdt",
	.crash_reason_smem = WCSS_CRASH_REASON,
	.ops = &wcss_ahb_pcie_ipq5018_ops,
	.version = WCSS_AHB_IPQ,
	.pasid = WCNSS_PAS_ID,
	.mdt_load_sec = qcom_mdt_load,
};

static const struct wcss_data wcss_pcie_ipq5018_res_init = {
	.init_irq = init_irq,
	.q6_firmware_name = "IPQ5018/q6_fw.mdt",
	.crash_reason_smem = WCSS_CRASH_REASON,
	.ops = &wcss_ahb_pcie_ipq5018_ops,
	.version = WCSS_PCIE_IPQ,
	.mdt_load_sec = qcom_mdt_load_pd_seg,
	.pasid = MPD_WCNSS_PAS_ID,
};

static const struct of_device_id q6_wcss_of_match[] = {
	{ .compatible = "qcom,ipq5018-q6-mpd", .data = &q6_ipq5018_res_init },
	{ .compatible = "qcom,ipq9574-q6-mpd", .data = &q6_ipq9574_res_init },
	{ .compatible = "qcom,ipq5018-wcss-ahb-mpd",
		.data = &wcss_ahb_ipq5018_res_init },
	{ .compatible = "qcom,ipq9574-wcss-ahb-mpd",
		.data = &wcss_ahb_ipq9574_res_init },
	{ .compatible = "qcom,ipq5018-wcss-pcie-mpd",
		.data = &wcss_pcie_ipq5018_res_init },
	{ },
};
MODULE_DEVICE_TABLE(of, q6_wcss_of_match);

static struct platform_driver q6_wcss_driver = {
	.probe = q6_wcss_probe,
	.remove = q6_wcss_remove,
	.driver = {
		.name = "qcom-q6-mpd",
		.of_match_table = q6_wcss_of_match,
	},
};
module_platform_driver(q6_wcss_driver);

MODULE_DESCRIPTION("Hexagon WCSS Multipd Peripheral Image Loader");
MODULE_LICENSE("GPL v2");
