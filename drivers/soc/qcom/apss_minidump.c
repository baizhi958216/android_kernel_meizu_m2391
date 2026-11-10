// SPDX-License-Identifier: GPL-2.0-only
/* Applications processor side of the Qualcomm SMEM minidump protocol. */
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/elf.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/overflow.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/soc/qcom/apss_minidump.h>
#include <linux/soc/qcom/minidump_smem.h>
#include <linux/soc/qcom/smem.h>

#define APSS_MAX_REGIONS	200
#define APSS_NAME_MAX		12 /* Firmware reserves the last four bytes. */
#define MD_DISABLED		0x4453424c /* DSBL */
#define MD_NOT_REQUIRED		0x4e52     /* NR */

struct apss_entry {
	struct list_head list;
	struct minidump_region region;
	unsigned long vaddr;
};

/* The bootloader concatenates KELF_HDR and the active regions in table order. */
struct apss_elf {
	Elf64_Ehdr hdr;
	Elf64_Shdr shdr[2 + APSS_MAX_REGIONS];
	Elf64_Phdr phdr[APSS_MAX_REGIONS];
	char strings[2 + APSS_MAX_REGIONS][MAX_REGION_NAME_LENGTH];
};

struct apss_image {
	struct minidump_region regions[1 + APSS_MAX_REGIONS];
	struct apss_elf elf;
};

struct apss_dump {
	struct minidump_subsystem *ss;
	struct apss_image *image;
	phys_addr_t base;
	__le32 old_enabled, old_required, old_status;
	bool set_encryption_defaults;
};

/* Registration may precede the SMEM platform device probe. */
static LIST_HEAD(entries);
static DEFINE_MUTEX(apss_lock);
static unsigned int num_entries;
static struct apss_dump *provider;

static void apss_elf_init(struct apss_elf *elf)
{
	Elf64_Ehdr *hdr = &elf->hdr;
	Elf64_Shdr *strings = &elf->shdr[1];

	memcpy(hdr->e_ident, ELFMAG, SELFMAG);
	hdr->e_ident[EI_CLASS] = ELFCLASS64;
	hdr->e_ident[EI_DATA] = ELFDATA2LSB;
	hdr->e_ident[EI_VERSION] = EV_CURRENT;
	hdr->e_type = ET_CORE;
	hdr->e_machine = EM_AARCH64;
	hdr->e_version = EV_CURRENT;
	hdr->e_ehsize = sizeof(*hdr);
	hdr->e_shoff = offsetof(struct apss_elf, shdr);
	hdr->e_shentsize = sizeof(*elf->shdr);
	hdr->e_shstrndx = 1;
	hdr->e_phoff = offsetof(struct apss_elf, phdr);
	hdr->e_phentsize = sizeof(*elf->phdr);
	strscpy(elf->strings[1], "STR_TBL");
	strings->sh_name = sizeof(elf->strings[0]);
	strings->sh_offset = offsetof(struct apss_elf, strings);
	strings->sh_type = SHT_STRTAB;
	strings->sh_size = sizeof(elf->strings);
}

/* Update both bootloader and ELF indices under one invalid/valid transition. */
static void apss_publish(struct apss_dump *dump)
{
	struct apss_image *image = dump->image;
	struct apss_elf *elf = &image->elf;
	struct minidump_subsystem *ss = dump->ss;
	size_t offset = sizeof(*elf);
	struct apss_entry *entry;
	unsigned int i = 0;

	WRITE_ONCE(ss->status, 0);
	wmb();
	memset(image, 0, sizeof(*image));
	apss_elf_init(elf);
	strscpy(image->regions[0].name, "KELF_HDR");
	image->regions[0].valid = cpu_to_le32(MINIDUMP_REGION_VALID);
	image->regions[0].address = cpu_to_le64(dump->base + offsetof(struct apss_image, elf));
	image->regions[0].size = cpu_to_le64(sizeof(*elf));

	list_for_each_entry(entry, &entries, list) {
		Elf64_Shdr *sh = &elf->shdr[i + 2];
		Elf64_Phdr *ph = &elf->phdr[i];
		u64 size = le64_to_cpu(entry->region.size);

		image->regions[i + 1] = entry->region;
		strscpy(elf->strings[i + 2], entry->region.name);
		sh->sh_name = (i + 2) * sizeof(elf->strings[0]);
		sh->sh_type = SHT_PROGBITS;
		sh->sh_flags = SHF_WRITE | SHF_ALLOC;
		sh->sh_addr = entry->vaddr;
		sh->sh_offset = offset;
		sh->sh_size = size;
		ph->p_type = PT_LOAD;
		ph->p_flags = PF_R | PF_W;
		ph->p_offset = offset;
		ph->p_vaddr = entry->vaddr;
		ph->p_paddr = le64_to_cpu(entry->region.address);
		ph->p_filesz = ph->p_memsz = size;
		offset += size;
		i++;
	}
	elf->hdr.e_phnum = i;
	elf->hdr.e_shnum = i + 2;
	WRITE_ONCE(ss->regions_baseptr, cpu_to_le64(dump->base));
	WRITE_ONCE(ss->region_count, cpu_to_le32(i + 1));
	wmb();
	WRITE_ONCE(ss->status, cpu_to_le32(1));
}

static void apss_release_region(struct device *dev, void *res)
{
	struct apss_entry *entry = res;

	guard(mutex)(&apss_lock);
	list_del(&entry->list);
	num_entries--;
	if (provider)
		apss_publish(provider);
}

int qcom_devm_minidump_add(struct device *dev, const char *name,
			  const void *data, size_t size)
{
	struct apss_entry *entry, *other;
	unsigned long start = (unsigned long)data, last;
	int ret;

	if (!dev || !name || !*name || strnlen(name, APSS_NAME_MAX + 1) > APSS_NAME_MAX ||
	    !strcmp(name, "KELF_HDR") || !data || !size || !IS_ALIGNED(size, 4) ||
	    check_add_overflow(start, size - 1, &last) ||
	    !virt_addr_valid(data) || !virt_addr_valid((const void *)last))
		return -EINVAL;
	entry = devres_alloc(apss_release_region, sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;
	guard(mutex)(&apss_lock);
	list_for_each_entry(other, &entries, list) {
		if (!strcmp(name, other->region.name)) {
			ret = -EEXIST;
			goto free;
		}
	}
	if (num_entries == APSS_MAX_REGIONS) {
		ret = -ENOSPC;
		goto free;
	}
	entry->vaddr = start;
	strscpy(entry->region.name, name);
	entry->region.valid = cpu_to_le32(MINIDUMP_REGION_VALID);
	entry->region.address = cpu_to_le64(virt_to_phys(data));
	entry->region.size = cpu_to_le64(size);
	list_add_tail(&entry->list, &entries);
	num_entries++;
	if (provider)
		apss_publish(provider);
	devres_add(dev, entry);
	return 0;
free:
	devres_free(entry);
	return ret;
}
EXPORT_SYMBOL_GPL(qcom_devm_minidump_add);

static int apss_dump_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct minidump_global_toc *toc;
	struct minidump_subsystem *ss;
	struct apss_dump *dump;
	struct sg_table sgt;
	dma_addr_t dma;
	size_t size;
	int ret;

	toc = qcom_smem_get(QCOM_SMEM_HOST_ANY, SBL_MINIDUMP_SMEM_ID, &size);
	if (IS_ERR(toc))
		return dev_err_probe(dev, PTR_ERR(toc), "cannot get minidump SMEM item\n");
	if (size < sizeof(*toc) || !READ_ONCE(toc->status) ||
	    READ_ONCE(toc->enabled) == cpu_to_le32(MD_DISABLED))
		return -ENODEV;
	ss = &toc->subsystems[0]; /* APSS */
	dump = devm_kzalloc(dev, sizeof(*dump), GFP_KERNEL);
	if (!dump)
		return -ENOMEM;
	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));
	if (ret)
		return ret;
	/* Firmware expects a physical address and does not use an APSS IOVA. */
	dump->image = dmam_alloc_attrs(dev, sizeof(*dump->image), &dma, GFP_KERNEL,
				       DMA_ATTR_FORCE_CONTIGUOUS);
	if (!dump->image)
		return -ENOMEM;
	ret = dma_get_sgtable_attrs(dev, &sgt, dump->image, dma, sizeof(*dump->image),
				    DMA_ATTR_FORCE_CONTIGUOUS);
	if (ret)
		return ret;
	if (sgt.orig_nents != 1 || sgt.sgl->length < sizeof(*dump->image)) {
		sg_free_table(&sgt);
		return -EINVAL;
	}
	dump->base = sg_phys(sgt.sgl);
	sg_free_table(&sgt);
	dump->ss = ss;
	guard(mutex)(&apss_lock);
	if (provider || READ_ONCE(ss->status))
		return -EBUSY;
	dump->old_enabled = READ_ONCE(ss->enabled);
	if (dump->old_enabled == cpu_to_le32(MD_DISABLED))
		return -ENODEV;
	dump->old_required = READ_ONCE(ss->encryption_required);
	dump->old_status = READ_ONCE(ss->encryption_status);
	/* Honor explicit firmware encryption policy. Kalama initializes these
	 * previously empty APSS-owned fields before publishing its first table.
	 */
	if (!dump->old_required && !dump->old_status) {
		WRITE_ONCE(ss->encryption_required, cpu_to_le32(MD_NOT_REQUIRED));
		WRITE_ONCE(ss->encryption_status, cpu_to_le32(MINIDUMP_SS_ENCR_DONE));
		dump->set_encryption_defaults = true;
	}
	WRITE_ONCE(ss->enabled, cpu_to_le32(MINIDUMP_SS_ENABLED));
	provider = dump;
	apss_publish(dump);
	return 0;
}

static void apss_dump_remove(struct platform_device *pdev)
{
	struct apss_dump *dump;
	struct minidump_subsystem *ss;

	guard(mutex)(&apss_lock);
	dump = provider;
	ss = dump->ss;
	WRITE_ONCE(ss->status, 0);
	wmb();
	if (READ_ONCE(ss->enabled) == cpu_to_le32(MINIDUMP_SS_ENABLED))
		WRITE_ONCE(ss->enabled, dump->old_enabled);
	WRITE_ONCE(ss->region_count, 0);
	WRITE_ONCE(ss->regions_baseptr, 0);
	if (dump->set_encryption_defaults &&
	    READ_ONCE(ss->encryption_required) == cpu_to_le32(MD_NOT_REQUIRED) &&
	    READ_ONCE(ss->encryption_status) == cpu_to_le32(MINIDUMP_SS_ENCR_DONE)) {
		WRITE_ONCE(ss->encryption_required, dump->old_required);
		WRITE_ONCE(ss->encryption_status, dump->old_status);
	}
	wmb();
	provider = NULL;
}

static const struct of_device_id apss_dump_match[] = {
	{ .compatible = "qcom,minidump" },
	{}
};
MODULE_DEVICE_TABLE(of, apss_dump_match);
static struct platform_driver apss_dump_driver = {
	.probe = apss_dump_probe,
	.remove = apss_dump_remove,
	.driver = {
		.name = "qcom-minidump",
		.of_match_table = apss_dump_match,
	},
};
module_platform_driver(apss_dump_driver);
MODULE_DESCRIPTION("Qualcomm APSS bootloader minidump region registry");
MODULE_LICENSE("GPL");
