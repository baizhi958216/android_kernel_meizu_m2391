/* SPDX-License-Identifier: GPL-2.0-only */
/* Kalama uses fabric-wide endpoint IDs. The application processor uses the
 * first (hlos) BCM voter. Display and camera have independent RSCs.
 */
#include <dt-bindings/interconnect/qcom,kalama.h>

static struct qcom_icc_node * const kalama_aggre1_noc_nodes[] = {
	[KALAMA_MASTER_QSPI_0] = &qhm_qspi,
	[KALAMA_MASTER_QUP_1] = &qhm_qup1,
	[KALAMA_MASTER_SDCC_4] = &xm_sdc4,
	[KALAMA_MASTER_UFS_MEM] = &xm_ufs_mem,
	[KALAMA_MASTER_USB3_0] = &xm_usb3_0,
	[KALAMA_SLAVE_A1NOC_SNOC] = &qns_a1noc_snoc,
};

static const struct qcom_icc_desc kalama_aggre1_noc = {
	.nodes = kalama_aggre1_noc_nodes,
	.num_nodes = ARRAY_SIZE(kalama_aggre1_noc_nodes),
	.bcms = aggre1_noc_bcms,
	.num_bcms = ARRAY_SIZE(aggre1_noc_bcms),
};

static struct qcom_icc_node * const kalama_aggre2_noc_nodes[] = {
	[KALAMA_MASTER_QDSS_BAM] = &qhm_qdss_bam,
	[KALAMA_MASTER_QUP_2] = &qhm_qup2,
	[KALAMA_MASTER_CRYPTO] = &qxm_crypto,
	[KALAMA_MASTER_IPA] = &qxm_ipa,
	[KALAMA_MASTER_SP] = &qxm_sp,
	[KALAMA_MASTER_QDSS_ETR] = &xm_qdss_etr_0,
	[KALAMA_MASTER_QDSS_ETR_1] = &xm_qdss_etr_1,
	[KALAMA_MASTER_SDCC_2] = &xm_sdc2,
	[KALAMA_SLAVE_A2NOC_SNOC] = &qns_a2noc_snoc,
};

static const struct qcom_icc_desc kalama_aggre2_noc = {
	.nodes = kalama_aggre2_noc_nodes,
	.num_nodes = ARRAY_SIZE(kalama_aggre2_noc_nodes),
	.bcms = aggre2_noc_bcms,
	.num_bcms = ARRAY_SIZE(aggre2_noc_bcms),
};

static struct qcom_icc_node * const kalama_clk_virt_nodes[] = {
	[KALAMA_MASTER_QUP_CORE_0] = &qup0_core_master,
	[KALAMA_MASTER_QUP_CORE_1] = &qup1_core_master,
	[KALAMA_MASTER_QUP_CORE_2] = &qup2_core_master,
	[KALAMA_SLAVE_QUP_CORE_0] = &qup0_core_slave,
	[KALAMA_SLAVE_QUP_CORE_1] = &qup1_core_slave,
	[KALAMA_SLAVE_QUP_CORE_2] = &qup2_core_slave,
};

static const struct qcom_icc_desc kalama_clk_virt = {
	.nodes = kalama_clk_virt_nodes,
	.num_nodes = ARRAY_SIZE(kalama_clk_virt_nodes),
	.bcms = clk_virt_bcms,
	.num_bcms = ARRAY_SIZE(clk_virt_bcms),
};

static struct qcom_icc_node * const kalama_cnoc_cfg_nodes[] = {
	[KALAMA_MASTER_CNOC_CFG] = &qsm_cfg,
	[KALAMA_SLAVE_AHB2PHY_SOUTH] = &qhs_ahb2phy0,
	[KALAMA_SLAVE_AHB2PHY_NORTH] = &qhs_ahb2phy1,
	[KALAMA_SLAVE_APPSS] = &qhs_apss,
	[KALAMA_SLAVE_CAMERA_CFG] = &qhs_camera_cfg,
	[KALAMA_SLAVE_CLK_CTL] = &qhs_clk_ctl,
	[KALAMA_SLAVE_RBCPR_CX_CFG] = &qhs_cpr_cx,
	[KALAMA_SLAVE_RBCPR_MMCX_CFG] = &qhs_cpr_mmcx,
	[KALAMA_SLAVE_RBCPR_MXA_CFG] = &qhs_cpr_mxa,
	[KALAMA_SLAVE_RBCPR_MXC_CFG] = &qhs_cpr_mxc,
	[KALAMA_SLAVE_CPR_NSPCX] = &qhs_cpr_nspcx,
	[KALAMA_SLAVE_CRYPTO_0_CFG] = &qhs_crypto0_cfg,
	[KALAMA_SLAVE_CX_RDPM] = &qhs_cx_rdpm,
	[KALAMA_SLAVE_DISPLAY_CFG] = &qhs_display_cfg,
	[KALAMA_SLAVE_GFX3D_CFG] = &qhs_gpuss_cfg,
	[KALAMA_SLAVE_I2C] = &qhs_i2c,
	[KALAMA_SLAVE_IMEM_CFG] = &qhs_imem_cfg,
	[KALAMA_SLAVE_IPA_CFG] = &qhs_ipa,
	[KALAMA_SLAVE_IPC_ROUTER_CFG] = &qhs_ipc_router,
	[KALAMA_SLAVE_CNOC_MSS] = &qhs_mss_cfg,
	[KALAMA_SLAVE_MX_RDPM] = &qhs_mx_rdpm,
	[KALAMA_SLAVE_PCIE_0_CFG] = &qhs_pcie0_cfg,
	[KALAMA_SLAVE_PCIE_1_CFG] = &qhs_pcie1_cfg,
	[KALAMA_SLAVE_PDM] = &qhs_pdm,
	[KALAMA_SLAVE_PIMEM_CFG] = &qhs_pimem_cfg,
	[KALAMA_SLAVE_PRNG] = &qhs_prng,
	[KALAMA_SLAVE_QDSS_CFG] = &qhs_qdss_cfg,
	[KALAMA_SLAVE_QSPI_0] = &qhs_qspi,
	[KALAMA_SLAVE_QUP_1] = &qhs_qup1,
	[KALAMA_SLAVE_QUP_2] = &qhs_qup2,
	[KALAMA_SLAVE_SDCC_2] = &qhs_sdc2,
	[KALAMA_SLAVE_SDCC_4] = &qhs_sdc4,
	[KALAMA_SLAVE_SPSS_CFG] = &qhs_spss_cfg,
	[KALAMA_SLAVE_TCSR] = &qhs_tcsr,
	[KALAMA_SLAVE_TLMM] = &qhs_tlmm,
	[KALAMA_SLAVE_UFS_MEM_CFG] = &qhs_ufs_mem_cfg,
	[KALAMA_SLAVE_USB3_0] = &qhs_usb3_0,
	[KALAMA_SLAVE_VENUS_CFG] = &qhs_venus_cfg,
	[KALAMA_SLAVE_VSENSE_CTRL_CFG] = &qhs_vsense_ctrl_cfg,
	[KALAMA_SLAVE_LPASS_QTB_CFG] = &qss_lpass_qtb_cfg,
	[KALAMA_SLAVE_CNOC_MNOC_CFG] = &qss_mnoc_cfg,
	[KALAMA_SLAVE_NSP_QTB_CFG] = &qss_nsp_qtb_cfg,
	[KALAMA_SLAVE_PCIE_ANOC_CFG] = &qss_pcie_anoc_cfg,
	[KALAMA_SLAVE_QDSS_STM] = &xs_qdss_stm,
	[KALAMA_SLAVE_TCU] = &xs_sys_tcu_cfg,
};

static const struct qcom_icc_desc kalama_cnoc_cfg = {
	.nodes = kalama_cnoc_cfg_nodes,
	.num_nodes = ARRAY_SIZE(kalama_cnoc_cfg_nodes),
	.bcms = config_noc_bcms,
	.num_bcms = ARRAY_SIZE(config_noc_bcms),
};

static struct qcom_icc_node * const kalama_cnoc_main_nodes[] = {
	[KALAMA_MASTER_GEM_NOC_CNOC] = &qnm_gemnoc_cnoc,
	[KALAMA_MASTER_GEM_NOC_PCIE_SNOC] = &qnm_gemnoc_pcie,
	[KALAMA_SLAVE_AOSS] = &qhs_aoss,
	[KALAMA_SLAVE_TME_CFG] = &qhs_tme_cfg,
	[KALAMA_SLAVE_CNOC_CFG] = &qss_cfg,
	[KALAMA_SLAVE_DDRSS_CFG] = &qss_ddrss_cfg,
	[KALAMA_SLAVE_BOOT_IMEM] = &qxs_boot_imem,
	[KALAMA_SLAVE_IMEM] = &qxs_imem,
	[KALAMA_SLAVE_PCIE_0] = &xs_pcie_0,
	[KALAMA_SLAVE_PCIE_1] = &xs_pcie_1,
};

static const struct qcom_icc_desc kalama_cnoc_main = {
	.nodes = kalama_cnoc_main_nodes,
	.num_nodes = ARRAY_SIZE(kalama_cnoc_main_nodes),
	.bcms = cnoc_main_bcms,
	.num_bcms = ARRAY_SIZE(cnoc_main_bcms),
};

static struct qcom_icc_node * const kalama_gem_noc_nodes[] = {
	[KALAMA_MASTER_GPU_TCU] = &alm_gpu_tcu,
	[KALAMA_MASTER_SYS_TCU] = &alm_sys_tcu,
	[KALAMA_MASTER_APPSS_PROC] = &chm_apps,
	[KALAMA_MASTER_GFX3D] = &qnm_gpu,
	[KALAMA_MASTER_LPASS_GEM_NOC] = &qnm_lpass_gemnoc,
	[KALAMA_MASTER_MSS_PROC] = &qnm_mdsp,
	[KALAMA_MASTER_MNOC_HF_MEM_NOC] = &qnm_mnoc_hf,
	[KALAMA_MASTER_MNOC_SF_MEM_NOC] = &qnm_mnoc_sf,
	[KALAMA_MASTER_COMPUTE_NOC] = &qnm_nsp_gemnoc,
	[KALAMA_MASTER_ANOC_PCIE_GEM_NOC] = &qnm_pcie,
	[KALAMA_MASTER_SNOC_GC_MEM_NOC] = &qnm_snoc_gc,
	[KALAMA_MASTER_SNOC_SF_MEM_NOC] = &qnm_snoc_sf,
	[KALAMA_SLAVE_GEM_NOC_CNOC] = &qns_gem_noc_cnoc,
	[KALAMA_SLAVE_LLCC] = &qns_llcc,
	[KALAMA_SLAVE_MEM_NOC_PCIE_SNOC] = &qns_pcie,
};

static const struct qcom_icc_desc kalama_gem_noc = {
	.nodes = kalama_gem_noc_nodes,
	.num_nodes = ARRAY_SIZE(kalama_gem_noc_nodes),
	.bcms = gem_noc_bcms,
	.num_bcms = ARRAY_SIZE(gem_noc_bcms),
};

static struct qcom_icc_node * const kalama_lpass_ag_noc_nodes[] = {
	[KALAMA_MASTER_LPIAON_NOC] = &qnm_lpiaon_noc,
	[KALAMA_SLAVE_LPASS_GEM_NOC] = &qns_lpass_ag_noc_gemnoc,
};

static const struct qcom_icc_desc kalama_lpass_ag_noc = {
	.nodes = kalama_lpass_ag_noc_nodes,
	.num_nodes = ARRAY_SIZE(kalama_lpass_ag_noc_nodes),
	.bcms = lpass_ag_noc_bcms,
	.num_bcms = ARRAY_SIZE(lpass_ag_noc_bcms),
};

static struct qcom_icc_node * const kalama_lpass_lpiaon_noc_nodes[] = {
	[KALAMA_MASTER_LPASS_LPINOC] = &qnm_lpass_lpinoc,
	[KALAMA_SLAVE_LPIAON_NOC_LPASS_AG_NOC] = &qns_lpass_aggnoc,
};

static const struct qcom_icc_desc kalama_lpass_lpiaon_noc = {
	.nodes = kalama_lpass_lpiaon_noc_nodes,
	.num_nodes = ARRAY_SIZE(kalama_lpass_lpiaon_noc_nodes),
	.bcms = lpass_lpiaon_noc_bcms,
	.num_bcms = ARRAY_SIZE(lpass_lpiaon_noc_bcms),
};

static struct qcom_icc_node * const kalama_lpass_lpicx_noc_nodes[] = {
	[KALAMA_MASTER_LPASS_PROC] = &qxm_lpinoc_dsp_axim,
	[KALAMA_SLAVE_LPICX_NOC_LPIAON_NOC] = &qns_lpi_aon_noc,
};

static const struct qcom_icc_desc kalama_lpass_lpicx_noc = {
	.nodes = kalama_lpass_lpicx_noc_nodes,
	.num_nodes = ARRAY_SIZE(kalama_lpass_lpicx_noc_nodes),
	.bcms = lpass_lpicx_noc_bcms,
	.num_bcms = ARRAY_SIZE(lpass_lpicx_noc_bcms),
};

static struct qcom_icc_node * const kalama_mc_virt_nodes[] = {
	[KALAMA_MASTER_LLCC] = &llcc_mc,
	[KALAMA_SLAVE_EBI1] = &ebi,
};

static const struct qcom_icc_desc kalama_mc_virt = {
	.nodes = kalama_mc_virt_nodes,
	.num_nodes = ARRAY_SIZE(kalama_mc_virt_nodes),
	.bcms = mc_virt_bcms,
	.num_bcms = ARRAY_SIZE(mc_virt_bcms),
};

static struct qcom_icc_node * const kalama_mmss_noc_nodes[] = {
	[KALAMA_MASTER_CAMNOC_HF] = &qnm_camnoc_hf,
	[KALAMA_MASTER_CAMNOC_ICP] = &qnm_camnoc_icp,
	[KALAMA_MASTER_CAMNOC_SF] = &qnm_camnoc_sf,
	[KALAMA_MASTER_MDP] = &qnm_mdp,
	[KALAMA_MASTER_CDSP_HCP] = &qnm_vapss_hcp,
	[KALAMA_MASTER_VIDEO] = &qnm_video,
	[KALAMA_MASTER_VIDEO_CV_PROC] = &qnm_video_cv_cpu,
	[KALAMA_MASTER_VIDEO_PROC] = &qnm_video_cvp,
	[KALAMA_MASTER_VIDEO_V_PROC] = &qnm_video_v_cpu,
	[KALAMA_MASTER_CNOC_MNOC_CFG] = &qsm_mnoc_cfg,
	[KALAMA_SLAVE_MNOC_HF_MEM_NOC] = &qns_mem_noc_hf,
	[KALAMA_SLAVE_MNOC_SF_MEM_NOC] = &qns_mem_noc_sf,
	[KALAMA_SLAVE_SERVICE_MNOC] = &srvc_mnoc,
};

static const struct qcom_icc_desc kalama_mmss_noc = {
	.nodes = kalama_mmss_noc_nodes,
	.num_nodes = ARRAY_SIZE(kalama_mmss_noc_nodes),
	.bcms = mmss_noc_bcms,
	.num_bcms = ARRAY_SIZE(mmss_noc_bcms),
};

static struct qcom_icc_node * const kalama_nsp_noc_nodes[] = {
	[KALAMA_MASTER_CDSP_PROC] = &qxm_nsp,
	[KALAMA_SLAVE_CDSP_MEM_NOC] = &qns_nsp_gemnoc,
};

static const struct qcom_icc_desc kalama_nsp_noc = {
	.nodes = kalama_nsp_noc_nodes,
	.num_nodes = ARRAY_SIZE(kalama_nsp_noc_nodes),
	.bcms = nsp_noc_bcms,
	.num_bcms = ARRAY_SIZE(nsp_noc_bcms),
};

static struct qcom_icc_node * const kalama_pcie_anoc_nodes[] = {
	[KALAMA_MASTER_PCIE_ANOC_CFG] = &qsm_pcie_anoc_cfg,
	[KALAMA_MASTER_PCIE_0] = &xm_pcie3_0,
	[KALAMA_MASTER_PCIE_1] = &xm_pcie3_1,
	[KALAMA_SLAVE_ANOC_PCIE_GEM_NOC] = &qns_pcie_mem_noc,
	[KALAMA_SLAVE_SERVICE_PCIE_ANOC] = &srvc_pcie_aggre_noc,
};

static const struct qcom_icc_desc kalama_pcie_anoc = {
	.nodes = kalama_pcie_anoc_nodes,
	.num_nodes = ARRAY_SIZE(kalama_pcie_anoc_nodes),
	.bcms = pcie_anoc_bcms,
	.num_bcms = ARRAY_SIZE(pcie_anoc_bcms),
};

static struct qcom_icc_node * const kalama_system_noc_nodes[] = {
	[KALAMA_MASTER_GIC_AHB] = &qhm_gic,
	[KALAMA_MASTER_A1NOC_SNOC] = &qnm_aggre1_noc,
	[KALAMA_MASTER_A2NOC_SNOC] = &qnm_aggre2_noc,
	[KALAMA_MASTER_GIC] = &xm_gic,
	[KALAMA_SLAVE_SNOC_GEM_NOC_GC] = &qns_gemnoc_gc,
	[KALAMA_SLAVE_SNOC_GEM_NOC_SF] = &qns_gemnoc_sf,
};

static const struct qcom_icc_desc kalama_system_noc = {
	.nodes = kalama_system_noc_nodes,
	.num_nodes = ARRAY_SIZE(kalama_system_noc_nodes),
	.bcms = system_noc_bcms,
	.num_bcms = ARRAY_SIZE(system_noc_bcms),
};

