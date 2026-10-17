// VK_LAYER_OPI_fakefeatures: врёт приложению (wined3d), что PowerVR BXM умеет
// geometryShader / multiViewport / pipelineStatisticsQuery / tessellationShader,
// чтобы wined3d выдал D3D feature level 11. При vkCreateDevice эти фичи вычищаются,
// драйвер их никогда не видит. Кроме того, команды, которые из-за фейка стали бы
// невалидными, санируются: viewport/scissor count -> 1 (wined3d при multiViewport ставит 16 —
// это и повесило GPU в первой версии), GS/tess-стадии выкидываются из пайплайнов,
// query pool'ы PIPELINE_STATISTICS не создаются.
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define EXPORT __attribute__((visibility("default")))

static PFN_vkGetInstanceProcAddr next_gipa;
static PFN_vkGetDeviceProcAddr next_gdpa;
static PFN_vkGetPhysicalDeviceFeatures next_features;
static PFN_vkGetPhysicalDeviceFeatures2 next_features2;
static PFN_vkGetPhysicalDeviceFeatures2 next_features2khr;
static PFN_vkDestroyInstance next_destroy_instance;
static int verbose = -1;
static PFN_vkCreateGraphicsPipelines next_create_gp;
static PFN_vkCmdSetViewport next_set_viewport;
static PFN_vkCmdSetScissor next_set_scissor;
static PFN_vkCmdSetViewportWithCount next_set_viewport_wc;
static PFN_vkCmdSetScissorWithCount next_set_scissor_wc;
static PFN_vkCreateQueryPool next_create_qp;

static void logf_(const char *msg)
{
    if (verbose < 0) verbose = getenv("OPI_FAKEFEATURES_VERBOSE") != NULL;
    if (verbose) fprintf(stderr, "[fakefeatures] %s\n", msg);
}

static void fake(VkPhysicalDeviceFeatures *f)
{
    f->geometryShader = VK_TRUE;
    f->multiViewport = VK_TRUE;
    f->pipelineStatisticsQuery = VK_TRUE;
    f->tessellationShader = VK_TRUE;
}

static void strip(VkPhysicalDeviceFeatures *f)
{
    f->geometryShader = VK_FALSE;
    f->multiViewport = VK_FALSE;
    f->pipelineStatisticsQuery = VK_FALSE;
    f->tessellationShader = VK_FALSE;
}

static VKAPI_ATTR void VKAPI_CALL layer_GetPhysicalDeviceFeatures(VkPhysicalDevice pd, VkPhysicalDeviceFeatures *f)
{
    next_features(pd, f);
    fake(f);
}

static VKAPI_ATTR void VKAPI_CALL layer_GetPhysicalDeviceFeatures2(VkPhysicalDevice pd, VkPhysicalDeviceFeatures2 *f)
{
    (next_features2 ? next_features2 : next_features2khr)(pd, f);
    fake(&f->features);
    logf_("features reported with fakes");
}

static VKAPI_ATTR VkResult VKAPI_CALL layer_CreateInstance(const VkInstanceCreateInfo *ci,
        const VkAllocationCallbacks *alloc, VkInstance *inst)
{
    VkLayerInstanceCreateInfo *lci = (VkLayerInstanceCreateInfo *)ci->pNext;
    while (lci && !(lci->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO && lci->function == VK_LAYER_LINK_INFO))
        lci = (VkLayerInstanceCreateInfo *)lci->pNext;
    if (!lci) return VK_ERROR_INITIALIZATION_FAILED;

    next_gipa = lci->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    lci->u.pLayerInfo = lci->u.pLayerInfo->pNext;

    PFN_vkCreateInstance create = (PFN_vkCreateInstance)next_gipa(VK_NULL_HANDLE, "vkCreateInstance");
    VkResult r = create(ci, alloc, inst);
    if (r != VK_SUCCESS) return r;

    next_features = (PFN_vkGetPhysicalDeviceFeatures)next_gipa(*inst, "vkGetPhysicalDeviceFeatures");
    next_features2 = (PFN_vkGetPhysicalDeviceFeatures2)next_gipa(*inst, "vkGetPhysicalDeviceFeatures2");
    next_features2khr = (PFN_vkGetPhysicalDeviceFeatures2)next_gipa(*inst, "vkGetPhysicalDeviceFeatures2KHR");
    next_destroy_instance = (PFN_vkDestroyInstance)next_gipa(*inst, "vkDestroyInstance");
    logf_("instance created");
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL layer_DestroyInstance(VkInstance inst, const VkAllocationCallbacks *alloc)
{
    next_destroy_instance(inst, alloc);
}

static VKAPI_ATTR VkResult VKAPI_CALL layer_CreateDevice(VkPhysicalDevice pd, const VkDeviceCreateInfo *ci,
        const VkAllocationCallbacks *alloc, VkDevice *dev)
{
    VkLayerDeviceCreateInfo *lci = (VkLayerDeviceCreateInfo *)ci->pNext;
    while (lci && !(lci->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO && lci->function == VK_LAYER_LINK_INFO))
        lci = (VkLayerDeviceCreateInfo *)lci->pNext;
    if (!lci) return VK_ERROR_INITIALIZATION_FAILED;

    PFN_vkGetInstanceProcAddr gipa = lci->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    next_gdpa = lci->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    lci->u.pLayerInfo = lci->u.pLayerInfo->pNext;

    // Вычищаем фейковые фичи (в копиях, чтобы не портить структуры приложения).
    VkDeviceCreateInfo copy = *ci;
    VkPhysicalDeviceFeatures feats;
    if (ci->pEnabledFeatures) {
        feats = *ci->pEnabledFeatures;
        strip(&feats);
        copy.pEnabledFeatures = &feats;
    }
    VkPhysicalDeviceFeatures saved;
    VkPhysicalDeviceFeatures2 *f2 = NULL;
    for (VkBaseOutStructure *s = (VkBaseOutStructure *)ci->pNext; s; s = s->pNext)
        if (s->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2) {
            f2 = (VkPhysicalDeviceFeatures2 *)s;
            saved = f2->features;
            strip(&f2->features);
        }

    PFN_vkCreateDevice create = (PFN_vkCreateDevice)gipa(VK_NULL_HANDLE, "vkCreateDevice");
    VkResult r = create(pd, &copy, alloc, dev);
    if (f2) f2->features = saved;
    if (r == VK_SUCCESS) {
        next_create_gp = (PFN_vkCreateGraphicsPipelines)next_gdpa(*dev, "vkCreateGraphicsPipelines");
        next_set_viewport = (PFN_vkCmdSetViewport)next_gdpa(*dev, "vkCmdSetViewport");
        next_set_scissor = (PFN_vkCmdSetScissor)next_gdpa(*dev, "vkCmdSetScissor");
        next_set_viewport_wc = (PFN_vkCmdSetViewportWithCount)next_gdpa(*dev, "vkCmdSetViewportWithCount");
        if (!next_set_viewport_wc) next_set_viewport_wc = (PFN_vkCmdSetViewportWithCount)next_gdpa(*dev, "vkCmdSetViewportWithCountEXT");
        next_set_scissor_wc = (PFN_vkCmdSetScissorWithCount)next_gdpa(*dev, "vkCmdSetScissorWithCount");
        if (!next_set_scissor_wc) next_set_scissor_wc = (PFN_vkCmdSetScissorWithCount)next_gdpa(*dev, "vkCmdSetScissorWithCountEXT");
        next_create_qp = (PFN_vkCreateQueryPool)next_gdpa(*dev, "vkCreateQueryPool");
    }
    logf_(r == VK_SUCCESS ? "device created (fakes stripped)" : "device creation FAILED");
    return r;
}


static VKAPI_ATTR VkResult VKAPI_CALL layer_CreateGraphicsPipelines(VkDevice dev, VkPipelineCache cache, uint32_t n,
        const VkGraphicsPipelineCreateInfo *infos, const VkAllocationCallbacks *alloc, VkPipeline *out)
{
    if (!n) return next_create_gp(dev, cache, n, infos, alloc, out);
    VkGraphicsPipelineCreateInfo *ci = malloc(sizeof(*ci) * n);
    VkPipelineViewportStateCreateInfo *vp = malloc(sizeof(*vp) * n);
    VkPipelineShaderStageCreateInfo **st = calloc(n, sizeof(*st));
    for (uint32_t i = 0; i < n; i++) {
        ci[i] = infos[i];
        if (infos[i].pViewportState && (infos[i].pViewportState->viewportCount > 1 || infos[i].pViewportState->scissorCount > 1)) {
            vp[i] = *infos[i].pViewportState;
            if (vp[i].viewportCount > 1) vp[i].viewportCount = 1;
            if (vp[i].scissorCount > 1) vp[i].scissorCount = 1;
            ci[i].pViewportState = &vp[i];
        }
        st[i] = malloc(sizeof(**st) * (infos[i].stageCount ? infos[i].stageCount : 1));
        uint32_t k = 0;
        for (uint32_t j = 0; j < infos[i].stageCount; j++) {
            VkShaderStageFlagBits s = infos[i].pStages[j].stage;
            if (s == VK_SHADER_STAGE_GEOMETRY_BIT || s == VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT
                    || s == VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT) {
                logf_("dropped GS/tess stage");
                continue;
            }
            st[i][k++] = infos[i].pStages[j];
        }
        ci[i].stageCount = k;
        ci[i].pStages = st[i];
        if (k != infos[i].stageCount) ci[i].pTessellationState = NULL;
    }
    VkResult r = next_create_gp(dev, cache, n, ci, alloc, out);
    for (uint32_t i = 0; i < n; i++) free(st[i]);
    free(st); free(vp); free(ci);
    return r;
}

static VKAPI_ATTR void VKAPI_CALL layer_CmdSetViewport(VkCommandBuffer cb, uint32_t first, uint32_t count, const VkViewport *v)
{
    if (first > 0 || !count) return;
    next_set_viewport(cb, 0, 1, v);
}

static VKAPI_ATTR void VKAPI_CALL layer_CmdSetScissor(VkCommandBuffer cb, uint32_t first, uint32_t count, const VkRect2D *r)
{
    if (first > 0 || !count) return;
    next_set_scissor(cb, 0, 1, r);
}

static VKAPI_ATTR void VKAPI_CALL layer_CmdSetViewportWithCount(VkCommandBuffer cb, uint32_t count, const VkViewport *v)
{
    next_set_viewport_wc(cb, count ? 1 : 0, v);
}

static VKAPI_ATTR void VKAPI_CALL layer_CmdSetScissorWithCount(VkCommandBuffer cb, uint32_t count, const VkRect2D *r)
{
    next_set_scissor_wc(cb, count ? 1 : 0, r);
}

static VKAPI_ATTR VkResult VKAPI_CALL layer_CreateQueryPool(VkDevice dev, const VkQueryPoolCreateInfo *ci,
        const VkAllocationCallbacks *alloc, VkQueryPool *qp)
{
    if (ci->queryType == VK_QUERY_TYPE_PIPELINE_STATISTICS) {
        logf_("refused PIPELINE_STATISTICS query pool");
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    return next_create_qp(dev, ci, alloc, qp);
}

static PFN_vkVoidFunction device_hook(const char *name)
{
#define DHOOK(n) if (!strcmp(name, "vk" #n)) return (PFN_vkVoidFunction)layer_##n
    DHOOK(CreateGraphicsPipelines);
    DHOOK(CmdSetViewport);
    DHOOK(CmdSetScissor);
    DHOOK(CreateQueryPool);
    if (!strcmp(name, "vkCmdSetViewportWithCount") || !strcmp(name, "vkCmdSetViewportWithCountEXT"))
        return (PFN_vkVoidFunction)layer_CmdSetViewportWithCount;
    if (!strcmp(name, "vkCmdSetScissorWithCount") || !strcmp(name, "vkCmdSetScissorWithCountEXT"))
        return (PFN_vkVoidFunction)layer_CmdSetScissorWithCount;
#undef DHOOK
    return NULL;
}

EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL OPI_GetDeviceProcAddr(VkDevice dev, const char *name)
{
    if (!strcmp(name, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)OPI_GetDeviceProcAddr;
    PFN_vkVoidFunction h = device_hook(name);
    if (h) {
        // своя функция — только если у драйвера есть соответствующая
        return next_gdpa(dev, name) ? h : NULL;
    }
    return next_gdpa(dev, name);
}

EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL OPI_GetInstanceProcAddr(VkInstance inst, const char *name)
{
#define HOOK(n) if (!strcmp(name, "vk" #n)) return (PFN_vkVoidFunction)layer_##n
    HOOK(CreateInstance);
    HOOK(DestroyInstance);
    HOOK(CreateDevice);
    HOOK(GetPhysicalDeviceFeatures);
    HOOK(GetPhysicalDeviceFeatures2);
    if (!strcmp(name, "vkGetPhysicalDeviceFeatures2KHR")) return (PFN_vkVoidFunction)layer_GetPhysicalDeviceFeatures2;
    if (!strcmp(name, "vkGetInstanceProcAddr")) return (PFN_vkVoidFunction)OPI_GetInstanceProcAddr;
    if (!strcmp(name, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)OPI_GetDeviceProcAddr;
#undef HOOK
    PFN_vkVoidFunction h = device_hook(name);
    if (h && next_gipa && next_gipa(inst, name)) return h;
    return next_gipa ? next_gipa(inst, name) : NULL;
}
