// SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 OPlus. All rights reserved.
 */
#include <linux/uaccess.h>
#include <linux/of.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/soc/qcom/smem.h>
#include <linux/seq_file.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>
#include <soc/oplus/system/oplus_project.h>
#include <linux/io.h>
#include <stdbool.h>



#include "oplus_nfc.h"

#define NFC_CHIPSET_VERSION (0x1)
#define MIXED_CHIPSET    "mixed-chipset"
#define MAX_ID_COUNT     5

struct id_entry {
    u32 key;
    const char *chipset;
    const char *manifest_path;
    const char *feature_path;
};

static char current_chipset[32];
static bool support_nfc = false;

bool is_nfc_support(void)
{
    return support_nfc;
}

bool is_support_chip(chip_type chip)
{
    bool ret = false;
    const char* target_chipset;

    if (!support_nfc)
    {
        pr_err("%s, nfc not supported, or oplus_nfc has not started", __func__);
        return false;
    }

    switch(chip) {
        case NQ330:
            target_chipset = "NQ330";
            break;
        case SN100T:
            target_chipset = "SN100T|SN110T|SN220T|SN220U";
            break;
        case SN100F:
            target_chipset = "SN100F";
            break;
        case ST21H:
            target_chipset = "ST21H";
            break;
        case ST54H:
            target_chipset = "ST54H";
            break;
        case PN557:
            target_chipset = "PN557";
            break;
        default:
            target_chipset = "UNKNOWN";
            break;
    }

    if (strstr(target_chipset, current_chipset) != NULL) {
        ret = true;
    }

    pr_err("oplus_nfc target_chipset = %s, current_chipset = %s \n", target_chipset, current_chipset);
    return ret;
}
EXPORT_SYMBOL(is_support_chip);

static int nfc_read_func(struct seq_file *s, void *v)
{
    void *p = s->private;

    switch((uint64_t)(p)) {
    case NFC_CHIPSET_VERSION:
        seq_printf(s, "%s", current_chipset);
        break;
    default:
        seq_printf(s, "not support\n");
        break;
    }

    return 0;
}

static int nfc_open(struct inode *inode, struct file *file)
{
    return single_open(file, nfc_read_func, PDE_DATA(inode));
}

static const struct proc_ops  nfc_info_fops = {
    .proc_open  = nfc_open,
    .proc_read  = seq_read,
    .proc_release = single_release,
};

static int single_nfc_probe(struct platform_device *pdev)
{
    struct device_node *np;
    struct device* dev;
    unsigned int project;
    char prop_name[32];
    const char *chipset_node;
        const char *chipset_manifest;
    struct proc_dir_entry *p_entry;
    static struct proc_dir_entry *nfc_info = NULL;

    pr_err("enter %s", __func__);
    dev = &pdev->dev;
    if (!dev)
    {
        pr_err("%s, no device", __func__);
        goto error_init;
    }
    project = get_project();
    /*project name consists of 5-symbol
    **project contains letters is big then 0x10000 == 65536
    */
    if (project > 0x10000) {
        snprintf(prop_name, sizeof(prop_name), "chipset-%X", project);
    } else {
        snprintf(prop_name, sizeof(prop_name), "chipset-%u", project);
    }
    pr_err("%s, prop to be read = %s", __func__, prop_name);
    np = dev->of_node;

    if (of_property_read_string(dev->of_node, prop_name, &chipset_node))
    {
        snprintf(current_chipset, sizeof(current_chipset), "NULL");
    } else
    {
        pr_err("%s, get chipset_node content = %s", __func__, chipset_node);
        strncpy(current_chipset, chipset_node, sizeof(current_chipset) - 1);
        support_nfc = true;
    }

    nfc_info = proc_mkdir("oplus_nfc", NULL);
    if (!nfc_info)
    {
        pr_err("%s, make oplus_nfc dir fail", __func__);
        goto error_init;
    }

    p_entry = proc_create_data("chipset", S_IRUGO, nfc_info, &nfc_info_fops, (uint32_t *)(NFC_CHIPSET_VERSION));
    if (!p_entry)
    {
        pr_err("%s, make chipset node fail", __func__);
        goto error_init;
    }

        if (of_property_read_string(dev->of_node, "manifest", &chipset_manifest)) {
                pr_err("read manifest node fail");
        } else {
            pr_err("%s, get chipset_node content = %s", __func__, chipset_manifest);
                pr_err("read manifest node success");
                proc_symlink("manifest", nfc_info , chipset_manifest);
        }
    return 0;

error_init:
    pr_err("%s error_init", __func__);
    remove_proc_entry("oplus_nfc", NULL);
    return -ENOENT;
}

static int read_id_properties(struct device_node *np, u32 id_count, struct id_entry *id_entries)
{
    int err;
    u32 i;
    char propname[30];

    for (i = 0; i < id_count; i++) {
        snprintf(propname, sizeof(propname), "id-%u-key", i);
        err = of_property_read_u32(np, propname, &id_entries[i].key);
        if (err) {
          pr_err("Failed to read dts node:%s\n",propname);
          return err;
        }

        snprintf(propname, sizeof(propname), "id-%u-value-chipset", i);
        err = of_property_read_string(np, propname, &id_entries[i].chipset);
        if (err) {
          pr_err("Failed to read dts node:%s\n",propname);
          return err;
        }

        snprintf(propname, sizeof(propname), "id-%u-value-manifest-path", i);
        err = of_property_read_string(np, propname, &id_entries[i].manifest_path);
        if (err) {
          pr_err("Failed to read dts node:%s\n",propname);
          return err;
        }

        snprintf(propname, sizeof(propname), "id-%u-value-feature-path", i);
        err = of_property_read_string(np, propname, &id_entries[i].feature_path);
        if (err) {
          pr_err("Failed to read dts node:%s\n",propname);
          return err;
        }
    }

    return 0;
}

static int get_gpio_value(struct device_node *np, int *gpio_value)
{
    int gpio_num = of_get_named_gpio(np, "id-gpio", 0);
    if (!gpio_is_valid(gpio_num)) {
        pr_err("id-gpio is not valid\n");
        return -EINVAL;
    }

    *gpio_value = gpio_get_value(gpio_num);
    return 0;
}

static int create_chipset_file_and_symlinks(struct id_entry entry)
{
      struct proc_dir_entry *p_entry;
      static struct proc_dir_entry *nfc_info = NULL;

      strncpy(current_chipset, entry.chipset, sizeof(current_chipset) - 1);

      nfc_info = proc_mkdir("oplus_nfc", NULL);
      if (!nfc_info)
      {
        pr_err("%s, make oplus_nfc dir fail", __func__);
        goto error_process;
      }

      p_entry = proc_create_data("chipset", S_IRUGO, nfc_info, &nfc_info_fops, (uint32_t *)(NFC_CHIPSET_VERSION));
      if (!p_entry)
      {
        pr_err("%s, make chipset node fail", __func__);
        goto error_process;
      }
      proc_symlink("manifest", nfc_info , entry.manifest_path);
      proc_symlink("feature", nfc_info , entry.feature_path);

      return 0;
    error_process:
      remove_proc_entry("oplus_nfc", NULL);
      return -ENOENT;
}


static int mixed_nfc_probe(struct platform_device *pdev)
{
    struct device_node *np = pdev->dev.of_node;
    u32 id_count;
    int i, gpio_value, err;
    bool found = false;
    struct id_entry *id_entries = NULL;

    if (NULL == np)
    {
        pr_err("%s pdev->dev.of_node is NULL", __func__);
        return -ENOENT;
    }

    err = of_property_read_u32(np, "id_count", &id_count);
    if (err)
    {
      pr_err("%s read dts node id_count failed", __func__);
      return err;
    }

    if (id_count >= MAX_ID_COUNT)
    {
      pr_err("%s error: id_count more than %d", __func__, MAX_ID_COUNT);
      return -ENOENT;
    }

    id_entries = kzalloc(sizeof(struct id_entry) * id_count, GFP_DMA | GFP_KERNEL);
    if(NULL == id_entries)
    {
      pr_err("%s error:can not kzalloc memory for id_entry", __func__);
      return -ENOMEM;
    }

    err = read_id_properties(np, id_count,id_entries);
    if (err)
    {
      pr_err("%s error:read_id_properties failed", __func__);
      goto free_id_entries;
    }

    err = get_gpio_value(np, &gpio_value);
    if (err)
    {
      pr_err("%s error:get_gpio_value failed", __func__);
      goto free_id_entries;
    }

    for (i = 0; i < id_count; i++) {
        if (id_entries[i].key == gpio_value) {
                support_nfc = true;
            err = create_chipset_file_and_symlinks(id_entries[i]);
            if (err)
            {
              pr_err("%s error:create_chipset_file_and_symlinks failed", __func__);
              goto free_id_entries;
            }
            found = true;
            break;
        }
    }

    if (!found) {
        pr_err("No matching key found for GPIO value\n");
        err = -EINVAL;
        goto free_id_entries;
    }
    kfree(id_entries);
    return 0;

free_id_entries:
    kfree(id_entries);
    return err;
}


static int oplus_nfc_probe(struct platform_device *pdev)
{
    struct device* dev;
    uint32_t mixed_chipset;

    pr_err("enter %s", __func__);
    dev = &pdev->dev;
    if (!dev)
    {
        pr_err("%s, no device", __func__);
    return -ENOENT;
    }

    if (of_property_read_u32(dev->of_node, MIXED_CHIPSET, &mixed_chipset))
    {
        return single_nfc_probe(pdev);
    }
    else
    {
        if (1 == mixed_chipset)
        {
                pr_err("%s, the value of dts node:mixed-chipset is 1(true)", __func__);
                return mixed_nfc_probe(pdev);
        }
        else if(0 == mixed_chipset)
        {
            return single_nfc_probe(pdev);
        }
        else
        {
            pr_err("%s, mixed-chipset's value is wrong,it is neither 1 nor 0,exit probe", __func__);
            return -ENOENT;
        }
    }
    return 0;
}

static int oplus_nfc_remove(struct platform_device *pdev)
{
    remove_proc_entry("oplus_nfc", NULL);
    return 0;
}

static const struct of_device_id onc[] = {
    {.compatible = "oplus-nfc-chipset", },
    {},
};

MODULE_DEVICE_TABLE(of, onc);

static struct platform_driver oplus_nfc_driver = {
    .probe  = oplus_nfc_probe,
    .remove = oplus_nfc_remove,
    .driver = {
        .name = "oplus-nfc-chipset",
        .of_match_table = of_match_ptr(onc),
    },
};

static int __init oplus_nfc_init(void)
{
    pr_err("enter %s", __func__);
    return platform_driver_register(&oplus_nfc_driver);
}

subsys_initcall(oplus_nfc_init);

static void __exit oplus_nfc_exit(void)
{
    platform_driver_unregister(&oplus_nfc_driver);
}
module_exit(oplus_nfc_exit);

MODULE_DESCRIPTION("OPLUS nfc chipset version");
MODULE_LICENSE("GPL v2");
