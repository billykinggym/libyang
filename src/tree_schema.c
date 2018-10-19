/**
 * @file tree_schema.c
 * @author Radek Krejci <rkrejci@cesnet.cz>
 * @brief Schema tree implementation
 *
 * Copyright (c) 2015 - 2018 CESNET, z.s.p.o.
 *
 * This source code is licensed under BSD 3-Clause License (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://opensource.org/licenses/BSD-3-Clause
 */
#define _DEFAULT_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "libyang.h"
#include "common.h"
#include "context.h"
#include "tree_schema_internal.h"

#define FREE_ARRAY(CTX, ARRAY, FUNC) {uint64_t c__; LY_ARRAY_FOR(ARRAY, c__){FUNC(CTX, &ARRAY[c__], dict);}LY_ARRAY_FREE(ARRAY);}
#define FREE_MEMBER(CTX, MEMBER, FUNC) if (MEMBER) {FUNC(CTX, MEMBER, dict);free(MEMBER);}
#define FREE_STRING(CTX, STRING, DICT) if (DICT && STRING) {lydict_remove(CTX, STRING);}
#define FREE_STRINGS(CTX, ARRAY, DICT) {uint64_t c__; LY_ARRAY_FOR(ARRAY, c__){FREE_STRING(CTX, ARRAY[c__], DICT);}LY_ARRAY_FREE(ARRAY);}

static void lysp_grp_free(struct ly_ctx *ctx, struct lysp_grp *grp, int dict);
static void lysp_node_free(struct ly_ctx *ctx, struct lysp_node *node, int dict);

#define LYSC_CTX_BUFSIZE 4086
struct lysc_ctx {
    struct lysc_module *mod;
    uint16_t path_len;
    char path[LYSC_CTX_BUFSIZE];
};

static void
lysp_stmt_free(struct ly_ctx *ctx, struct lysp_stmt *stmt, int dict)
{
    struct lysp_stmt *child, *next;

    FREE_STRING(ctx, stmt->stmt, dict);
    FREE_STRING(ctx, stmt->arg, dict);

    LY_LIST_FOR_SAFE(stmt->child, next, child) {
        lysp_stmt_free(ctx, child, dict);
    }

    free(stmt);
}

static void
lysp_ext_instance_free(struct ly_ctx *ctx, struct lysp_ext_instance *ext, int dict)
{
    struct lysp_stmt *stmt, *next;

    FREE_STRING(ctx, ext->name, dict);
    FREE_STRING(ctx, ext->argument, dict);

    LY_LIST_FOR_SAFE(ext->child, next, stmt) {
        lysp_stmt_free(ctx, stmt, dict);
    }
}

static void
lysp_import_free(struct ly_ctx *ctx, struct lysp_import *import, int dict)
{
    FREE_STRING(ctx, import->name, dict);
    FREE_STRING(ctx, import->prefix, dict);
    FREE_STRING(ctx, import->dsc, dict);
    FREE_STRING(ctx, import->ref, dict);
    FREE_ARRAY(ctx, import->exts, lysp_ext_instance_free);
}

static void
lysp_include_free(struct ly_ctx *ctx, struct lysp_include *include, int dict)
{
    FREE_STRING(ctx, include->name, dict);
    FREE_STRING(ctx, include->dsc, dict);
    FREE_STRING(ctx, include->ref, dict);
    FREE_ARRAY(ctx, include->exts, lysp_ext_instance_free);
}

static void
lysp_revision_free(struct ly_ctx *ctx, struct lysp_revision *rev, int dict)
{
    FREE_STRING(ctx, rev->dsc, dict);
    FREE_STRING(ctx, rev->ref, dict);
    FREE_ARRAY(ctx, rev->exts, lysp_ext_instance_free);
}

static void
lysp_ext_free(struct ly_ctx *ctx, struct lysp_ext *ext, int dict)
{
    FREE_STRING(ctx, ext->name, dict);
    FREE_STRING(ctx, ext->argument, dict);
    FREE_STRING(ctx, ext->dsc, dict);
    FREE_STRING(ctx, ext->ref, dict);
    FREE_ARRAY(ctx, ext->exts, lysp_ext_instance_free);
}

static void
lysp_feature_free(struct ly_ctx *ctx, struct lysp_feature *feat, int dict)
{
    FREE_STRING(ctx, feat->name, dict);
    FREE_STRINGS(ctx, feat->iffeatures, 1);
    FREE_STRING(ctx, feat->dsc, dict);
    FREE_STRING(ctx, feat->ref, dict);
    FREE_ARRAY(ctx, feat->exts, lysp_ext_instance_free);
}

static void
lysp_ident_free(struct ly_ctx *ctx, struct lysp_ident *ident, int dict)
{
    FREE_STRING(ctx, ident->name, dict);
    FREE_STRINGS(ctx, ident->iffeatures, 1);
    FREE_STRINGS(ctx, ident->bases, dict);
    FREE_STRING(ctx, ident->dsc, dict);
    FREE_STRING(ctx, ident->ref, dict);
    FREE_ARRAY(ctx, ident->exts, lysp_ext_instance_free);
}

static void
lysp_restr_free(struct ly_ctx *ctx, struct lysp_restr *restr, int dict)
{
    FREE_STRING(ctx, restr->arg, dict);
    FREE_STRING(ctx, restr->emsg, dict);
    FREE_STRING(ctx, restr->eapptag, dict);
    FREE_STRING(ctx, restr->dsc, dict);
    FREE_STRING(ctx, restr->ref, dict);
    FREE_ARRAY(ctx, restr->exts, lysp_ext_instance_free);
}

static void
lysp_type_enum_free(struct ly_ctx *ctx, struct lysp_type_enum *item, int dict)
{
    FREE_STRING(ctx, item->name, dict);
    FREE_STRING(ctx, item->dsc, dict);
    FREE_STRING(ctx, item->ref, dict);
    FREE_STRINGS(ctx, item->iffeatures, 1);
    FREE_ARRAY(ctx, item->exts, lysp_ext_instance_free);
}

static void
lysp_type_free(struct ly_ctx *ctx, struct lysp_type *type, int dict)
{
    FREE_STRING(ctx, type->name, dict);
    FREE_MEMBER(ctx, type->range, lysp_restr_free);
    FREE_MEMBER(ctx, type->length, lysp_restr_free);
    FREE_ARRAY(ctx, type->patterns, lysp_restr_free);
    FREE_ARRAY(ctx, type->enums, lysp_type_enum_free);
    FREE_ARRAY(ctx, type->bits, lysp_type_enum_free);
    FREE_STRING(ctx, type->path, dict);
    FREE_STRINGS(ctx, type->bases, dict);
    FREE_ARRAY(ctx, type->types, lysp_type_free);
    FREE_ARRAY(ctx, type->exts, lysp_ext_instance_free);
}

static void
lysp_tpdf_free(struct ly_ctx *ctx, struct lysp_tpdf *tpdf, int dict)
{
    FREE_STRING(ctx, tpdf->name, dict);
    FREE_STRING(ctx, tpdf->units, dict);
    FREE_STRING(ctx, tpdf->dflt, dict);
    FREE_STRING(ctx, tpdf->dsc, dict);
    FREE_STRING(ctx, tpdf->ref, dict);
    FREE_ARRAY(ctx, tpdf->exts, lysp_ext_instance_free);
    lysp_type_free(ctx, &tpdf->type, dict);
}

static void
lysp_action_inout_free(struct ly_ctx *ctx, struct lysp_action_inout *inout, int dict)
{
    struct lysp_node *node, *next;

    FREE_ARRAY(ctx, inout->musts, lysp_restr_free);
    FREE_ARRAY(ctx, inout->typedefs, lysp_tpdf_free);
    FREE_ARRAY(ctx, inout->groupings, lysp_grp_free);
    LY_LIST_FOR_SAFE(inout->data, next, node) {
        lysp_node_free(ctx, node, dict);
    }
    FREE_ARRAY(ctx, inout->exts, lysp_ext_instance_free);

}

static void
lysp_action_free(struct ly_ctx *ctx, struct lysp_action *action, int dict)
{
    FREE_STRING(ctx, action->name, dict);
    FREE_STRING(ctx, action->dsc, dict);
    FREE_STRING(ctx, action->ref, dict);
    FREE_STRINGS(ctx, action->iffeatures, 1);
    FREE_ARRAY(ctx, action->typedefs, lysp_tpdf_free);
    FREE_ARRAY(ctx, action->groupings, lysp_grp_free);
    FREE_MEMBER(ctx, action->input, lysp_action_inout_free);
    FREE_MEMBER(ctx, action->output, lysp_action_inout_free);
    FREE_ARRAY(ctx, action->exts, lysp_ext_instance_free);
}

static void
lysp_notif_free(struct ly_ctx *ctx, struct lysp_notif *notif, int dict)
{
    struct lysp_node *node, *next;

    FREE_STRING(ctx, notif->name, dict);
    FREE_STRING(ctx, notif->dsc, dict);
    FREE_STRING(ctx, notif->ref, dict);
    FREE_STRINGS(ctx, notif->iffeatures, 1);
    FREE_ARRAY(ctx, notif->musts, lysp_restr_free);
    FREE_ARRAY(ctx, notif->typedefs, lysp_tpdf_free);
    FREE_ARRAY(ctx, notif->groupings, lysp_grp_free);
    LY_LIST_FOR_SAFE(notif->data, next, node) {
        lysp_node_free(ctx, node, dict);
    }
    FREE_ARRAY(ctx, notif->exts, lysp_ext_instance_free);
}

static void
lysp_grp_free(struct ly_ctx *ctx, struct lysp_grp *grp, int dict)
{
    struct lysp_node *node, *next;

    FREE_STRING(ctx, grp->name, dict);
    FREE_STRING(ctx, grp->dsc, dict);
    FREE_STRING(ctx, grp->ref, dict);
    FREE_ARRAY(ctx, grp->typedefs, lysp_tpdf_free);
    FREE_ARRAY(ctx, grp->groupings, lysp_grp_free);
    LY_LIST_FOR_SAFE(grp->data, next, node) {
        lysp_node_free(ctx, node, dict);
    }
    FREE_ARRAY(ctx, grp->actions, lysp_action_free);
    FREE_ARRAY(ctx, grp->notifs, lysp_notif_free);
    FREE_ARRAY(ctx, grp->exts, lysp_ext_instance_free);
}

static void
lysp_when_free(struct ly_ctx *ctx, struct lysp_when *when, int dict)
{
    FREE_STRING(ctx, when->cond, dict);
    FREE_STRING(ctx, when->dsc, dict);
    FREE_STRING(ctx, when->ref, dict);
    FREE_ARRAY(ctx, when->exts, lysp_ext_instance_free);
}

static void
lysp_augment_free(struct ly_ctx *ctx, struct lysp_augment *augment, int dict)
{
    struct lysp_node *node, *next;

    FREE_STRING(ctx, augment->nodeid, dict);
    FREE_STRING(ctx, augment->dsc, dict);
    FREE_STRING(ctx, augment->ref, dict);
    FREE_MEMBER(ctx, augment->when, lysp_when_free);
    FREE_STRINGS(ctx, augment->iffeatures, 1);
    LY_LIST_FOR_SAFE(augment->child, next, node) {
        lysp_node_free(ctx, node, dict);
    }
    FREE_ARRAY(ctx, augment->actions, lysp_action_free);
    FREE_ARRAY(ctx, augment->notifs, lysp_notif_free);
    FREE_ARRAY(ctx, augment->exts, lysp_ext_instance_free);
}

static void
lysp_deviate_free(struct ly_ctx *ctx, struct lysp_deviate *d, int dict)
{
    struct lysp_deviate_add *add = (struct lysp_deviate_add*)d;
    struct lysp_deviate_rpl *rpl = (struct lysp_deviate_rpl*)d;

    FREE_ARRAY(ctx, d->exts, lysp_ext_instance_free);
    switch(d->mod) {
    case LYS_DEV_NOT_SUPPORTED:
        /* nothing to do */
        break;
    case LYS_DEV_ADD:
    case LYS_DEV_DELETE: /* compatible for dynamically allocated data */
        FREE_STRING(ctx, add->units, dict);
        FREE_ARRAY(ctx, add->musts, lysp_restr_free);
        FREE_STRINGS(ctx, add->uniques, dict);
        FREE_STRINGS(ctx, add->dflts, dict);
        break;
    case LYS_DEV_REPLACE:
        FREE_MEMBER(ctx, rpl->type, lysp_type_free);
        FREE_STRING(ctx, rpl->units, dict);
        FREE_STRING(ctx, rpl->dflt, dict);
        break;
    default:
        LOGINT(ctx);
        break;
    }
}

static void
lysp_deviation_free(struct ly_ctx *ctx, struct lysp_deviation *dev, int dict)
{
    struct lysp_deviate *next, *iter;

    FREE_STRING(ctx, dev->nodeid, dict);
    FREE_STRING(ctx, dev->dsc, dict);
    FREE_STRING(ctx, dev->ref, dict);
    LY_LIST_FOR_SAFE(dev->deviates, next, iter) {
        lysp_deviate_free(ctx, iter, dict);
        free(iter);
    }
    FREE_ARRAY(ctx, dev->exts, lysp_ext_instance_free);
}

static void
lysp_refine_free(struct ly_ctx *ctx, struct lysp_refine *ref, int dict)
{
    FREE_STRING(ctx, ref->nodeid, dict);
    FREE_STRING(ctx, ref->dsc, dict);
    FREE_STRING(ctx, ref->ref, dict);
    FREE_STRINGS(ctx, ref->iffeatures, 1);
    FREE_ARRAY(ctx, ref->musts, lysp_restr_free);
    FREE_STRING(ctx, ref->presence, dict);
    FREE_STRINGS(ctx, ref->dflts, dict);
    FREE_ARRAY(ctx, ref->exts, lysp_ext_instance_free);
}

static void
lysp_node_free(struct ly_ctx *ctx, struct lysp_node *node, int dict)
{
    struct lysp_node *child, *next;

    FREE_STRING(ctx, node->name, dict);
    FREE_STRING(ctx, node->dsc, dict);
    FREE_STRING(ctx, node->ref, dict);
    FREE_MEMBER(ctx, node->when, lysp_when_free);
    FREE_STRINGS(ctx, node->iffeatures, dict);
    FREE_ARRAY(ctx, node->exts, lysp_ext_instance_free);

    switch(node->nodetype) {
    case LYS_CONTAINER:
        FREE_ARRAY(ctx, ((struct lysp_node_container*)node)->musts, lysp_restr_free);
        FREE_STRING(ctx, ((struct lysp_node_container*)node)->presence, dict);
        FREE_ARRAY(ctx, ((struct lysp_node_container*)node)->typedefs, lysp_tpdf_free);
        FREE_ARRAY(ctx, ((struct lysp_node_container*)node)->groupings, lysp_grp_free);
        LY_LIST_FOR_SAFE(((struct lysp_node_container*)node)->child, next, child) {
            lysp_node_free(ctx, child, dict);
        }
        FREE_ARRAY(ctx, ((struct lysp_node_container*)node)->actions, lysp_action_free);
        FREE_ARRAY(ctx, ((struct lysp_node_container*)node)->notifs, lysp_notif_free);
        break;
    case LYS_LEAF:
        FREE_ARRAY(ctx, ((struct lysp_node_leaf*)node)->musts, lysp_restr_free);
        lysp_type_free(ctx, &((struct lysp_node_leaf*)node)->type, dict);
        FREE_STRING(ctx, ((struct lysp_node_leaf*)node)->units, dict);
        FREE_STRING(ctx, ((struct lysp_node_leaf*)node)->dflt, dict);
        break;
    case LYS_LEAFLIST:
        FREE_ARRAY(ctx, ((struct lysp_node_leaflist*)node)->musts, lysp_restr_free);
        lysp_type_free(ctx, &((struct lysp_node_leaflist*)node)->type, dict);
        FREE_STRING(ctx, ((struct lysp_node_leaflist*)node)->units, dict);
        FREE_STRINGS(ctx, ((struct lysp_node_leaflist*)node)->dflts, dict);
        break;
    case LYS_LIST:
        FREE_ARRAY(ctx, ((struct lysp_node_list*)node)->musts, lysp_restr_free);
        FREE_STRING(ctx, ((struct lysp_node_list*)node)->key, dict);
        FREE_ARRAY(ctx, ((struct lysp_node_list*)node)->typedefs, lysp_tpdf_free);
        FREE_ARRAY(ctx, ((struct lysp_node_list*)node)->groupings,  lysp_grp_free);
        LY_LIST_FOR_SAFE(((struct lysp_node_list*)node)->child, next, child) {
            lysp_node_free(ctx, child, dict);
        }
        FREE_ARRAY(ctx, ((struct lysp_node_list*)node)->actions, lysp_action_free);
        FREE_ARRAY(ctx, ((struct lysp_node_list*)node)->notifs, lysp_notif_free);
        FREE_STRINGS(ctx, ((struct lysp_node_list*)node)->uniques, dict);
        break;
    case LYS_CHOICE:
        LY_LIST_FOR_SAFE(((struct lysp_node_choice*)node)->child, next, child) {
            lysp_node_free(ctx, child, dict);
        }
        FREE_STRING(ctx, ((struct lysp_node_choice*)node)->dflt, dict);
        break;
    case LYS_CASE:
        LY_LIST_FOR_SAFE(((struct lysp_node_case*)node)->child, next, child) {
            lysp_node_free(ctx, child, dict);
        }
        break;
    case LYS_ANYDATA:
    case LYS_ANYXML:
        FREE_ARRAY(ctx, ((struct lysp_node_anydata*)node)->musts, lysp_restr_free);
        break;
    case LYS_USES:
        FREE_ARRAY(ctx, ((struct lysp_node_uses*)node)->refines, lysp_refine_free);
        FREE_ARRAY(ctx, ((struct lysp_node_uses*)node)->augments, lysp_augment_free);
        break;
    default:
        LOGINT(ctx);
    }

    free(node);
}

static void
lysp_module_free_(struct lysp_module *module, int dict)
{
    struct ly_ctx *ctx;
    struct lysp_node *node, *next;

    LY_CHECK_ARG_RET(NULL, module,);
    ctx = module->ctx;

    FREE_STRING(ctx, module->name, dict);
    FREE_STRING(ctx, module->filepath, dict);
    FREE_STRING(ctx, module->ns, dict);  /* or belongs-to */
    FREE_STRING(ctx, module->prefix, dict);

    FREE_ARRAY(ctx, module->imports, lysp_import_free);
    FREE_ARRAY(ctx, module->includes, lysp_include_free);

    FREE_STRING(ctx, module->org, dict);
    FREE_STRING(ctx, module->contact, dict);
    FREE_STRING(ctx, module->dsc, dict);
    FREE_STRING(ctx, module->ref, dict);

    FREE_ARRAY(ctx, module->revs, lysp_revision_free);
    FREE_ARRAY(ctx, module->extensions, lysp_ext_free);
    FREE_ARRAY(ctx, module->features, lysp_feature_free);
    FREE_ARRAY(ctx, module->identities, lysp_ident_free);
    FREE_ARRAY(ctx, module->typedefs, lysp_tpdf_free);
    FREE_ARRAY(ctx, module->groupings, lysp_grp_free);
    LY_LIST_FOR_SAFE(module->data, next, node) {
        lysp_node_free(ctx, node, dict);
    }
    FREE_ARRAY(ctx, module->augments, lysp_augment_free);
    FREE_ARRAY(ctx, module->rpcs, lysp_action_free);
    FREE_ARRAY(ctx, module->notifs, lysp_notif_free);
    FREE_ARRAY(ctx, module->deviations, lysp_deviation_free);
    FREE_ARRAY(ctx, module->exts, lysp_ext_instance_free);

    free(module);
}

API void
lysp_module_free(struct lysp_module *module)
{
    if (module) {
        lysp_module_free_(module, 1);
    }
}

static void
lysc_iffeature_free(struct ly_ctx *UNUSED(ctx), struct lysc_iffeature *iff, int UNUSED(dict))
{
    LY_ARRAY_FREE(iff->features);
    free(iff->expr);
}

static void
lysc_feature_free(struct ly_ctx *ctx, struct lysc_feature *feat, int dict)
{
    FREE_STRING(ctx, feat->name, dict);
    FREE_ARRAY(ctx, feat->iffeatures, lysc_iffeature_free);
    LY_ARRAY_FREE(feat->depfeatures);
}

static void
lysc_module_free_(struct lysc_module *module, int dict)
{
    struct ly_ctx *ctx;

    LY_CHECK_ARG_RET(NULL, module,);
    ctx = module->ctx;

    FREE_STRING(ctx, module->name, dict);
    FREE_STRING(ctx, module->ns, dict);
    FREE_STRING(ctx, module->prefix, dict);


    FREE_ARRAY(ctx, module->features, lysc_feature_free);


    free(module);
}

API void
lysc_module_free(struct lysc_module *module, void (*private_destructor)(const struct lysc_node *node, void *priv))
{
    if (module) {
        lysc_module_free_(module, 1);
    }
}

void
lys_module_free(struct lys_module *module, void (*private_destructor)(const struct lysc_node *node, void *priv))
{
    if (!module) {
        return;
    }

    lysc_module_free(module->compiled, private_destructor);
    lysp_module_free(module->parsed);
    free(module);
}

struct iff_stack {
    int size;
    int index;     /* first empty item */
    uint8_t *stack;
};

static LY_ERR
iff_stack_push(struct iff_stack *stack, uint8_t value)
{
    if (stack->index == stack->size) {
        stack->size += 4;
        stack->stack = ly_realloc(stack->stack, stack->size * sizeof *stack->stack);
        LY_CHECK_ERR_RET(!stack->stack, LOGMEM(NULL); stack->size = 0, LY_EMEM);
    }
    stack->stack[stack->index++] = value;
    return LY_SUCCESS;
}

static uint8_t
iff_stack_pop(struct iff_stack *stack)
{
    stack->index--;
    return stack->stack[stack->index];
}

static void
iff_stack_clean(struct iff_stack *stack)
{
    stack->size = 0;
    free(stack->stack);
}

static void
iff_setop(uint8_t *list, uint8_t op, int pos)
{
    uint8_t *item;
    uint8_t mask = 3;

    assert(pos >= 0);
    assert(op <= 3); /* max 2 bits */

    item = &list[pos / 4];
    mask = mask << 2 * (pos % 4);
    *item = (*item) & ~mask;
    *item = (*item) | (op << 2 * (pos % 4));
}

static uint8_t
iff_getop(uint8_t *list, int pos)
{
    uint8_t *item;
    uint8_t mask = 3, result;

    assert(pos >= 0);

    item = &list[pos / 4];
    result = (*item) & (mask << 2 * (pos % 4));
    return result >> 2 * (pos % 4);
}

#define LYS_IFF_LP 0x04 /* ( */
#define LYS_IFF_RP 0x08 /* ) */

API int
lysc_feature_value(const struct lysc_feature *feature)
{
    LY_CHECK_ARG_RET(NULL, feature, -1);
    return feature->flags & LYS_FENABLED ? 1 : 0;
}

static struct lysc_feature *
lysc_feature_find(struct lysc_module *mod, const char *name, size_t len)
{
    size_t i;
    struct lysc_feature *f;

    for (i = 0; i < len; ++i) {
        if (name[i] == ':') {
            /* we have a prefixed feature */
            mod = lysc_module_find_prefix(mod, name, i);
            LY_CHECK_RET(!mod, NULL);

            name = &name[i + 1];
            len = len - i - 1;
        }
    }

    /* we have the correct module, get the feature */
    LY_ARRAY_FOR(mod->features, i) {
        f = &mod->features[i];
        if (!strncmp(f->name, name, len) && f->name[len] == '\0') {
            return f;
        }
    }

    return NULL;
}

static int
lysc_iffeature_value_(const struct lysc_iffeature *iff, int *index_e, int *index_f)
{
    uint8_t op;
    int a, b;

    op = iff_getop(iff->expr, *index_e);
    (*index_e)++;

    switch (op) {
    case LYS_IFF_F:
        /* resolve feature */
        return lysc_feature_value(iff->features[(*index_f)++]);
    case LYS_IFF_NOT:
        /* invert result */
        return lysc_iffeature_value_(iff, index_e, index_f) ? 0 : 1;
    case LYS_IFF_AND:
    case LYS_IFF_OR:
        a = lysc_iffeature_value_(iff, index_e, index_f);
        b = lysc_iffeature_value_(iff, index_e, index_f);
        if (op == LYS_IFF_AND) {
            return a && b;
        } else { /* LYS_IFF_OR */
            return a || b;
        }
    }

    return 0;
}

API int
lysc_iffeature_value(const struct lysc_iffeature *iff)
{
    int index_e = 0, index_f = 0;

    LY_CHECK_ARG_RET(NULL, iff, -1);

    if (iff->expr) {
        return lysc_iffeature_value_(iff, &index_e, &index_f);
    }
    return 0;
}

/*
 * op: 1 - enable, 0 - disable
 */
/**
 * @brief Enable/Disable the specified feature in the module.
 *
 * If the feature is already set to the desired value, LY_SUCCESS is returned.
 * By changing the feature, also all the feature which depends on it via their
 * if-feature statements are again evaluated (disabled if a if-feature statemen
 * evaluates to false).
 *
 * @param[in] mod Compiled module where to set (search for) the feature.
 * @param[in] name Name of the feature to set. Asterisk ('*') can be used to
 * set all the features in the module.
 * @param[in] value Desired value of the feature: 1 (enable) or 0 (disable).
 * @return LY_ERR value.
 */
static LY_ERR
lys_feature_change(const struct lysc_module *mod, const char *name, int value)
{
    int all = 0;
    unsigned int u;
    struct lysc_feature *f, **df;
    struct lysc_iffeature *iff;
    struct ly_set *changed;

    if (!mod->features) {
        LOGERR(mod->ctx, LY_EINVAL, "Unable to switch feature since the module \"%s\" has no features.", mod->name);
        return LY_EINVAL;
    }

    if (!strcmp(name, "*")) {
        /* enable all */
        all = 1;
    }
    changed = ly_set_new();

    for (u = 0; u < LY_ARRAY_SIZE(mod->features); ++u) {
        f = &mod->features[u];
        if (all || !strcmp(f->name, name)) {
            if ((value && (f->flags & LYS_FENABLED)) || (!value && !(f->flags & LYS_FENABLED))) {
                if (all) {
                    /* skip already set features */
                    continue;
                } else {
                    /* feature already set correctly */
                    ly_set_free(changed, NULL);
                    return LY_SUCCESS;
                }
            }

            if (value) { /* enable */
                /* check referenced features if they are enabled */
                LY_ARRAY_FOR(f->iffeatures, struct lysc_iffeature, iff) {
                    if (!lysc_iffeature_value(iff)) {
                        if (all) {
                            LOGWRN(mod->ctx,
                                   "Feature \"%s\" cannot be enabled since it is disabled by its if-feature condition(s).",
                                   f->name);
                            goto next;
                        } else {
                            LOGERR(mod->ctx, LY_EDENIED,
                                   "Feature \"%s\" cannot be enabled since it is disabled by its if-feature condition(s).",
                                   f->name);
                            ly_set_free(changed, NULL);
                            return LY_EDENIED;
                        }
                    }
                }
                /* enable the feature */
                f->flags |= LYS_FENABLED;
            } else { /* disable */
                /* disable the feature */
                f->flags &= ~LYS_FENABLED;
            }

            /* remember the changed feature */
            ly_set_add(changed, f, LY_SET_OPT_USEASLIST);

            if (!all) {
                /* stop in case changing a single feature */
                break;
            }
        }
next:
        ;
    }

    if (!all && !changed->count) {
        LOGERR(mod->ctx, LY_EINVAL, "Feature \"%s\" not found in module \"%s\".", name, mod->name);
        ly_set_free(changed, NULL);
        return LY_EINVAL;
    }

    /* reflect change(s) in the dependent features */
    for (u = 0; u < changed->count; ++u) {
        /* If a dependent feature is enabled, it can be now changed by the change (to false) of the value of
         * its if-feature statements. The reverse logic, automatically enable feature when its feature is enabled
         * is not done - by default, features are disabled and must be explicitely enabled. */
        f = changed->objs[u];
        LY_ARRAY_FOR(f->depfeatures, struct lysc_feature*, df) {
            if (!((*df)->flags & LYS_FENABLED)) {
                /* not enabled, nothing to do */
                continue;
            }
            /* check the feature's if-features which could change by the previous change of our feature */
            LY_ARRAY_FOR((*df)->iffeatures, struct lysc_iffeature, iff) {
                if (!lysc_iffeature_value(iff)) {
                    /* the feature must be disabled now */
                    (*df)->flags &= ~LYS_FENABLED;
                    /* add the feature into the list of changed features */
                    ly_set_add(changed, *df, LY_SET_OPT_USEASLIST);
                    break;
                }
            }
        }
    }

    ly_set_free(changed, NULL);
    return LY_SUCCESS;
}

API LY_ERR
lys_feature_enable(struct lys_module *module, const char *feature)
{
    LY_CHECK_ARG_RET(NULL, module, module->compiled, feature, LY_EINVAL);

    return lys_feature_change(module->compiled, feature, 1);
}

API LY_ERR
lys_feature_disable(struct lys_module *module, const char *feature)
{
    LY_CHECK_ARG_RET(NULL, module, module->compiled, feature, LY_EINVAL);

    return lys_feature_change(module->compiled, feature, 0);
}

API int
lys_feature_value(const struct lys_module *module, const char *feature)
{
    struct lysc_feature *f;
    struct lysc_module *mod;
    unsigned int u;

    LY_CHECK_ARG_RET(NULL, module, module->compiled, feature, -1);
    mod = module->compiled;

    /* search for the specified feature */
    for (u = 0; u < LY_ARRAY_SIZE(mod->features); ++u) {
        f = &mod->features[u];
        if (!strcmp(f->name, feature)) {
            if (f->flags & LYS_FENABLED) {
                return 1;
            } else {
                return 0;
            }
        }
    }

    /* feature definition not found */
    return -1;
}

static LY_ERR
lys_compile_iffeature(struct lysc_ctx *ctx, const char *value, int UNUSED(options), struct lysc_iffeature *iff, struct lysc_feature *parent)
{
    const char *c = value;
    int r, rc = EXIT_FAILURE;
    int i, j, last_not, checkversion = 0;
    unsigned int f_size = 0, expr_size = 0, f_exp = 1;
    uint8_t op;
    struct iff_stack stack = {0, 0, NULL};
    struct lysc_feature *f, **df;

    assert(c);

    /* pre-parse the expression to get sizes for arrays, also do some syntax checks of the expression */
    for (i = j = last_not = 0; c[i]; i++) {
        if (c[i] == '(') {
            j++;
            checkversion = 1;
            continue;
        } else if (c[i] == ')') {
            j--;
            continue;
        } else if (isspace(c[i])) {
            checkversion = 1;
            continue;
        }

        if (!strncmp(&c[i], "not", r = 3) || !strncmp(&c[i], "and", r = 3) || !strncmp(&c[i], "or", r = 2)) {
            if (c[i + r] == '\0') {
                LOGVAL(ctx->mod->ctx, LY_VLOG_STR, ctx->path, LYVE_SYNTAX_YANG,
                       "Invalid value \"%s\" of if-feature - unexpected end of expression.", value);
                return LY_EVALID;
            } else if (!isspace(c[i + r])) {
                /* feature name starting with the not/and/or */
                last_not = 0;
                f_size++;
            } else if (c[i] == 'n') { /* not operation */
                if (last_not) {
                    /* double not */
                    expr_size = expr_size - 2;
                    last_not = 0;
                } else {
                    last_not = 1;
                }
            } else { /* and, or */
                f_exp++;
                /* not a not operation */
                last_not = 0;
            }
            i += r;
        } else {
            f_size++;
            last_not = 0;
        }
        expr_size++;

        while (!isspace(c[i])) {
            if (!c[i] || c[i] == ')') {
                i--;
                break;
            }
            i++;
        }
    }
    if (j || f_exp != f_size) {
        /* not matching count of ( and ) */
        LOGVAL(ctx->mod->ctx, LY_VLOG_STR, ctx->path, LYVE_SYNTAX_YANG,
               "Invalid value \"%s\" of if-feature - non-matching opening and closing parentheses.", value);
        return LY_EVALID;
    }

    if (checkversion || expr_size > 1) {
        /* check that we have 1.1 module */
        if (ctx->mod->version != LYS_VERSION_1_1) {
            LOGVAL(ctx->mod->ctx, LY_VLOG_STR, ctx->path, LYVE_SYNTAX_YANG,
                   "Invalid value \"%s\" of if-feature - YANG 1.1 expression in YANG 1.0 module.", value);
            return LY_EVALID;
        }
    }

    /* allocate the memory */
    LY_ARRAY_CREATE_RET(ctx->mod->ctx, iff->features, f_size, LY_EMEM);
    iff->expr = calloc((j = (expr_size / 4) + ((expr_size % 4) ? 1 : 0)), sizeof *iff->expr);
    stack.stack = malloc(expr_size * sizeof *stack.stack);
    LY_CHECK_ERR_GOTO(!stack.stack || !iff->expr, LOGMEM(ctx->mod->ctx), error);

    stack.size = expr_size;
    f_size--; expr_size--; /* used as indexes from now */

    for (i--; i >= 0; i--) {
        if (c[i] == ')') {
            /* push it on stack */
            iff_stack_push(&stack, LYS_IFF_RP);
            continue;
        } else if (c[i] == '(') {
            /* pop from the stack into result all operators until ) */
            while((op = iff_stack_pop(&stack)) != LYS_IFF_RP) {
                iff_setop(iff->expr, op, expr_size--);
            }
            continue;
        } else if (isspace(c[i])) {
            continue;
        }

        /* end of operator or operand -> find beginning and get what is it */
        j = i + 1;
        while (i >= 0 && !isspace(c[i]) && c[i] != '(') {
            i--;
        }
        i++; /* go back by one step */

        if (!strncmp(&c[i], "not", 3) && isspace(c[i + 3])) {
            if (stack.index && stack.stack[stack.index - 1] == LYS_IFF_NOT) {
                /* double not */
                iff_stack_pop(&stack);
            } else {
                /* not has the highest priority, so do not pop from the stack
                 * as in case of AND and OR */
                iff_stack_push(&stack, LYS_IFF_NOT);
            }
        } else if (!strncmp(&c[i], "and", 3) && isspace(c[i + 3])) {
            /* as for OR - pop from the stack all operators with the same or higher
             * priority and store them to the result, then push the AND to the stack */
            while (stack.index && stack.stack[stack.index - 1] <= LYS_IFF_AND) {
                op = iff_stack_pop(&stack);
                iff_setop(iff->expr, op, expr_size--);
            }
            iff_stack_push(&stack, LYS_IFF_AND);
        } else if (!strncmp(&c[i], "or", 2) && isspace(c[i + 2])) {
            while (stack.index && stack.stack[stack.index - 1] <= LYS_IFF_OR) {
                op = iff_stack_pop(&stack);
                iff_setop(iff->expr, op, expr_size--);
            }
            iff_stack_push(&stack, LYS_IFF_OR);
        } else {
            /* feature name, length is j - i */

            /* add it to the expression */
            iff_setop(iff->expr, LYS_IFF_F, expr_size--);

            /* now get the link to the feature definition */
            f = lysc_feature_find(ctx->mod, &c[i], j - i);
            LY_CHECK_ERR_GOTO(!f,
                              LOGVAL(ctx->mod->ctx, LY_VLOG_STR, ctx->path, LYVE_SYNTAX_YANG,
                                     "Invalid value \"%s\" of if-feature - unable to find feature \"%.*s\".", value, j - i, &c[i]);
                              rc = LY_EINVAL,
                              error)
            iff->features[f_size] = f;
            LY_ARRAY_INCREMENT(iff->features);
            if (parent) {
                /* and add itself into the dependants list */
                LY_ARRAY_NEW_RET(ctx->mod->ctx, f->depfeatures, df, LY_EMEM);
                *df = parent;

                /* TODO check for circular dependency */
            }
            f_size--;
        }
    }
    while (stack.index) {
        op = iff_stack_pop(&stack);
        iff_setop(iff->expr, op, expr_size--);
    }

    if (++expr_size || ++f_size) {
        /* not all expected operators and operands found */
        LOGVAL(ctx->mod->ctx, LY_VLOG_STR, ctx->path, LYVE_SYNTAX_YANG,
               "Invalid value \"%s\" of if-feature - processing error.", value);
        rc = LY_EINT;
    } else {
        rc = LY_SUCCESS;
    }

error:
    /* cleanup */
    iff_stack_clean(&stack);

    return rc;
}

static LY_ERR
lys_compile_feature(struct lysc_ctx *ctx, struct lysp_feature *feature_p, int options, struct lysc_feature *feature)
{
    unsigned int u;
    LY_ERR ret;

    if (options & LYSC_OPT_FREE_SP) {
        /* just switch the pointers */
        feature->name = feature_p->name;
    } else {
        /* keep refcounts correct for lysp_module_free() */
        feature->name = lydict_insert(ctx->mod->ctx, feature_p->name, 0);
    }
    feature->flags = feature_p->flags;

    if (feature_p->iffeatures) {
        /* allocate everything now */
        LY_ARRAY_CREATE_RET(ctx->mod->ctx, feature->iffeatures, LY_ARRAY_SIZE(feature_p->iffeatures), LY_EMEM);

        for (u = 0; u < LY_ARRAY_SIZE(feature_p->iffeatures); ++u) {
            ret = lys_compile_iffeature(ctx, feature_p->iffeatures[u], options, &feature->iffeatures[u], feature);
            LY_CHECK_RET(ret);
            LY_ARRAY_INCREMENT(feature->iffeatures);
        }
    }

    return LY_SUCCESS;
}

LY_ERR
lys_compile(struct lysp_module *sp, int options, struct lysc_module **sc)
{
    struct lysc_ctx ctx = {0};
    struct lysc_module *mod_c;
    unsigned int u;
    LY_ERR ret;

    LY_CHECK_ARG_RET(NULL, sc, sp, sp->ctx, LY_EINVAL);

    if (sp->submodule) {
        LOGERR(sp->ctx, LY_EINVAL, "Submodules (%s) are not supposed to be compiled, compile only the main modules.", sp->name);
        return LY_EINVAL;
    }

    ctx.mod = mod_c = calloc(1, sizeof *mod_c);
    LY_CHECK_ERR_RET(!mod_c, LOGMEM(sp->ctx), LY_EMEM);
    mod_c->ctx = sp->ctx;
    mod_c->version = sp->version;

    if (options & LYSC_OPT_FREE_SP) {
        /* just switch the pointers */
        mod_c->name = sp->name;
        mod_c->ns = sp->ns;
        mod_c->prefix = sp->prefix;
    } else {
        /* keep refcounts correct for lysp_module_free() */
        mod_c->name = lydict_insert(sp->ctx, sp->name, 0);
        mod_c->ns = lydict_insert(sp->ctx, sp->ns, 0);
        mod_c->prefix = lydict_insert(sp->ctx, sp->prefix, 0);
    }

    if (sp->features) {
        /* allocate everything now */
        LY_ARRAY_CREATE_RET(ctx.mod->ctx, mod_c->features, LY_ARRAY_SIZE(sp->features), LY_EMEM);

        for (u = 0; u < LY_ARRAY_SIZE(sp->features); ++u) {
            ret = lys_compile_feature(&ctx, &sp->features[u], options, &mod_c->features[u]);
            LY_CHECK_GOTO(ret != LY_SUCCESS, error);
            LY_ARRAY_INCREMENT(mod_c->features);
        }
    }

    if (options & LYSC_OPT_FREE_SP) {
        lysp_module_free_(sp, 0);
    }

    (*sc) = mod_c;
    return LY_SUCCESS;

error:

    if (options & LYSC_OPT_FREE_SP) {
        lysc_module_free_(mod_c, 0);
    } else {
        lysc_module_free_(mod_c, 1);
    }
    return ret;
}

const struct lys_module *
lys_parse_mem_(struct ly_ctx *ctx, const char *data, LYS_INFORMAT format, const char *revision, int implement)
{
    struct lys_module *mod = NULL;
    LY_ERR ret;

    LY_CHECK_ARG_RET(ctx, ctx, data, NULL);

    mod = calloc(1, sizeof *mod);
    LY_CHECK_ERR_RET(!mod, LOGMEM(ctx), NULL);

    switch (format) {
    case LYS_IN_YIN:
        /* TODO not yet supported
        mod = yin_read_module(ctx, data, revision, implement);
        */
        break;
    case LYS_IN_YANG:
        ret = yang_parse(ctx, data, &mod->parsed);
        LY_CHECK_RET(ret, NULL);
        break;
    default:
        LOGERR(ctx, LY_EINVAL, "Invalid schema input format.");
        break;
    }

    if (implement) {
        mod->parsed->implemented = 1;
    }

    if (revision) {
        /* check revision of the parsed model */
        if (!mod->parsed->revs || strcmp(revision, mod->parsed->revs[0].rev)) {
            LOGERR(ctx, LY_EINVAL, "Module \"%s\" parsed with the wrong revision (\"%s\" instead \"%s\").",
                   mod->parsed->name, mod->parsed->revs[0].rev, revision);
            lysp_module_free(mod->parsed);
            free(mod);
            return NULL;
        }
    }

    /* check for duplicity in the context */

    /* add into context */
    ly_set_add(&ctx->list, mod, LY_SET_OPT_USEASLIST);

#if 0
    /* hack for NETCONF's edit-config's operation attribute. It is not defined in the schema, but since libyang
     * implements YANG metadata (annotations), we need its definition. Because the ietf-netconf schema is not the
     * internal part of libyang, we cannot add the annotation into the schema source, but we do it here to have
     * the anotation definitions available in the internal schema structure. There is another hack in schema
     * printers to do not print this internally added annotation. */
    if (mod && ly_strequal(mod->name, "ietf-netconf", 0)) {
        if (lyp_add_ietf_netconf_annotations(mod)) {
            lys_free(mod, NULL, 1, 1);
            return NULL;
        }
    }
#endif

    return mod;
}

API const struct lys_module *
lys_parse_mem(struct ly_ctx *ctx, const char *data, LYS_INFORMAT format)
{
    return lys_parse_mem_(ctx, data, format, NULL, 1);
}

static void
lys_parse_set_filename(struct ly_ctx *ctx, const char **filename, int fd)
{
#ifdef __APPLE__
    char path[MAXPATHLEN];
#else
    int len;
    char path[PATH_MAX], proc_path[32];
#endif

#ifdef __APPLE__
    if (fcntl(fd, F_GETPATH, path) != -1) {
        *filename = lydict_insert(ctx, path, 0);
    }
#else
    /* get URI if there is /proc */
    sprintf(proc_path, "/proc/self/fd/%d", fd);
    if ((len = readlink(proc_path, path, PATH_MAX - 1)) > 0) {
        *filename = lydict_insert(ctx, path, len);
    }
#endif
}

const struct lys_module *
lys_parse_fd_(struct ly_ctx *ctx, int fd, LYS_INFORMAT format, const char *revision, int implement)
{
    const struct lys_module *mod;
    size_t length;
    char *addr;

    LY_CHECK_ARG_RET(ctx, ctx, NULL);
    if (fd < 0) {
        LOGARG(ctx, fd);
        return NULL;
    }

    LY_CHECK_RET(ly_mmap(ctx, fd, &length, (void **)&addr), NULL);
    if (!addr) {
        LOGERR(ctx, LY_EINVAL, "Empty schema file.");
        return NULL;
    }

    mod = lys_parse_mem_(ctx, addr, format, revision, implement);
    ly_munmap(addr, length);

    if (mod && !mod->parsed->filepath) {
        lys_parse_set_filename(ctx, &mod->parsed->filepath, fd);
    }

    return mod;
}

API const struct lys_module *
lys_parse_fd(struct ly_ctx *ctx, int fd, LYS_INFORMAT format)
{
    return lys_parse_fd_(ctx, fd, format, NULL, 1);
}

API const struct lys_module *
lys_parse_path(struct ly_ctx *ctx, const char *path, LYS_INFORMAT format)
{
    int fd;
    const struct lys_module *mod;
    const char *rev, *dot, *filename;
    size_t len;

    LY_CHECK_ARG_RET(ctx, ctx, path, NULL);

    fd = open(path, O_RDONLY);
    LY_CHECK_ERR_RET(fd == -1, LOGERR(ctx, LY_ESYS, "Opening file \"%s\" failed (%s).", path, strerror(errno)), NULL);

    mod = lys_parse_fd(ctx, fd, format);
    close(fd);
    LY_CHECK_RET(!mod, NULL);

    /* check that name and revision match filename */
    filename = strrchr(path, '/');
    if (!filename) {
        filename = path;
    } else {
        filename++;
    }
    rev = strchr(filename, '@');
    dot = strrchr(filename, '.');

    /* name */
    len = strlen(mod->parsed->name);
    if (strncmp(filename, mod->parsed->name, len) ||
            ((rev && rev != &filename[len]) || (!rev && dot != &filename[len]))) {
        LOGWRN(ctx, "File name \"%s\" does not match module name \"%s\".", filename, mod->parsed->name);
    }
    if (rev) {
        len = dot - ++rev;
        if (!mod->parsed->revs || len != 10 || strncmp(mod->parsed->revs[0].rev, rev, len)) {
            LOGWRN(ctx, "File name \"%s\" does not match module revision \"%s\".", filename,
                   mod->parsed->revs ? mod->parsed->revs[0].rev : "none");
        }
    }

    if (!mod->parsed->filepath) {
        /* store URI */
        char rpath[PATH_MAX];
        if (realpath(path, rpath) != NULL) {
            mod->parsed->filepath = lydict_insert(ctx, rpath, 0);
        } else {
            mod->parsed->filepath = lydict_insert(ctx, path, 0);
        }
    }

    return mod;
}

