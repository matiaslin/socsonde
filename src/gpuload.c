/*
 * Test the CPU load with dummy FMA
 *
 * We load vulkan compute with dlopen so no Vulkan loader package is
 * needed at link time. On Android, this picks up the system /system/lib64/libvulkan.so
 * (Adreno on Snapdragon, Xclipse on Exynos).
 */
#define VK_NO_PROTOTYPES
#include "common.h"
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "fma_spv.h"

#define FN(x) static PFN_##x x
FN(vkGetInstanceProcAddr); FN(vkCreateInstance); FN(vkDestroyInstance);
FN(vkEnumeratePhysicalDevices); FN(vkGetPhysicalDeviceProperties);
FN(vkGetPhysicalDeviceQueueFamilyProperties); FN(vkGetPhysicalDeviceMemoryProperties);
FN(vkCreateDevice); FN(vkDestroyDevice); FN(vkGetDeviceQueue); FN(vkDeviceWaitIdle);
FN(vkCreateBuffer); FN(vkDestroyBuffer); FN(vkGetBufferMemoryRequirements);
FN(vkAllocateMemory); FN(vkFreeMemory); FN(vkBindBufferMemory); FN(vkMapMemory); FN(vkUnmapMemory);
FN(vkCreateShaderModule); FN(vkDestroyShaderModule);
FN(vkCreateDescriptorSetLayout); FN(vkDestroyDescriptorSetLayout);
FN(vkCreatePipelineLayout); FN(vkDestroyPipelineLayout);
FN(vkCreateComputePipelines); FN(vkDestroyPipeline);
FN(vkCreateDescriptorPool); FN(vkDestroyDescriptorPool);
FN(vkAllocateDescriptorSets); FN(vkUpdateDescriptorSets);
FN(vkCreateCommandPool); FN(vkDestroyCommandPool); FN(vkAllocateCommandBuffers);
FN(vkBeginCommandBuffer); FN(vkEndCommandBuffer);
FN(vkCmdBindPipeline); FN(vkCmdBindDescriptorSets); FN(vkCmdPushConstants);
FN(vkCmdDispatch); FN(vkCmdPipelineBarrier);
FN(vkCreateFence); FN(vkDestroyFence); FN(vkQueueSubmit); FN(vkWaitForFences); FN(vkResetFences);

static VkInstance inst;

static int vk_init(void) {
  static const char *libs[] = { "libvulkan.so", "libvulkan.so.1", "/system/lib64/libvulkan.so" };
  void *h = NULL;
  for (size_t i = 0; i < sizeof libs / sizeof *libs && !h; i++) {
    h = dlopen(libs[i], RTLD_NOW | RTLD_LOCAL);
  }
  if (!h) {
    fprintf(stderr, "vulkan: cannot dlopen libvulkan: %s\n", dlerror());
    return -1;
  }

  vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)dlsym(h, "vkGetInstanceProcAddr");
  if (!vkGetInstanceProcAddr) return -1;

  vkCreateInstance = (PFN_vkCreateInstance)vkGetInstanceProcAddr(NULL, "vkCreateInstance");

  VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "socsonde", 1, "socsonde", 1, VK_API_VERSION_1_1 };
  VkInstanceCreateInfo ci = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &app, 0, NULL, 0, NULL };
  VkResult r = vkCreateInstance(&ci, NULL, &inst);
  if (r != VK_SUCCESS) {
    fprintf(stderr, "vulkan: vkCreateInstance failed (%d)\n", r);
    return -1;
  }

#define L(x) x = (PFN_##x)vkGetInstanceProcAddr(inst, #x)
  L(vkDestroyInstance); L(vkEnumeratePhysicalDevices); L(vkGetPhysicalDeviceProperties);
  L(vkGetPhysicalDeviceQueueFamilyProperties); L(vkGetPhysicalDeviceMemoryProperties);
  L(vkCreateDevice); L(vkDestroyDevice); L(vkGetDeviceQueue); L(vkDeviceWaitIdle);
  L(vkCreateBuffer); L(vkDestroyBuffer); L(vkGetBufferMemoryRequirements);
  L(vkAllocateMemory); L(vkFreeMemory); L(vkBindBufferMemory); L(vkMapMemory); L(vkUnmapMemory);
  L(vkCreateShaderModule); L(vkDestroyShaderModule);
  L(vkCreateDescriptorSetLayout); L(vkDestroyDescriptorSetLayout);
  L(vkCreatePipelineLayout); L(vkDestroyPipelineLayout);
  L(vkCreateComputePipelines); L(vkDestroyPipeline);
  L(vkCreateDescriptorPool); L(vkDestroyDescriptorPool);
  L(vkAllocateDescriptorSets); L(vkUpdateDescriptorSets);
  L(vkCreateCommandPool); L(vkDestroyCommandPool); L(vkAllocateCommandBuffers);
  L(vkBeginCommandBuffer); L(vkEndCommandBuffer);
  L(vkCmdBindPipeline); L(vkCmdBindDescriptorSets); L(vkCmdPushConstants);
  L(vkCmdDispatch); L(vkCmdPipelineBarrier);
  L(vkCreateFence); L(vkDestroyFence); L(vkQueueSubmit); L(vkWaitForFences); L(vkResetFences);
#undef L

  return 0;
}

static const char *dev_type(VkPhysicalDeviceType t) {
    switch (t) {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated-gpu";
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "discrete-gpu";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "virtual-gpu";
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return "cpu (software)";
    default: return "other";
    }
}

int vk_list(FILE *f) {
    if (vk_init()) return -1;
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(inst, &n, NULL);
    VkPhysicalDevice pd[8];
    if (n > 8) n = 8;
    vkEnumeratePhysicalDevices(inst, &n, pd);
    for (uint32_t i = 0; i < n; i++) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(pd[i], &p);
        fprintf(f, "  [%u] %s (%s) api %u.%u.%u driver 0x%x\n", i, p.deviceName, dev_type(p.deviceType),
                VK_VERSION_MAJOR(p.apiVersion), VK_VERSION_MINOR(p.apiVersion), VK_VERSION_PATCH(p.apiVersion),
                p.driverVersion);
    }
    vkDestroyInstance(inst, NULL);
    return (int)n;
}

#define CK(x) do { VkResult _r = (x); if (_r != VK_SUCCESS) { fprintf(stderr, "vulkan: %s failed (%d)\n", #x, _r); return 1; } } while (0)

int cmd_gpu(int argc, char **argv) {
  double secs = opt_d(argc, argv, "--seconds", 10);
  uint32_t iters = (uint32_t)opt_d(argc, argv, "--iters", 1024);
  uint32_t groups = (uint32_t)opt_d(argc, argv, "--groups", 4096);
  uint32_t per_submit = (uint32_t)opt_d(argc, argv, "--batch", 4);
  uint32_t want = (uint32_t)opt_d(argc, argv, "--device", 0);

  if (vk_init()) {
    printf("RESULT {\"ok\":false,\"error\":\"vulkan unavailable\"}\n");
    return 1;
  }

  uint32_t n = 8;
  VkPhysicalDevice pds[8];
  vkEnumeratePhysicalDevices(inst, &n, pds);
  if (!n || want >= n) {
    fprintf(stderr, "vulkan: no device %u (have %u)\n", want, n);
    return 1;
  }

  VkPhysicalDevice pd = pds[want];
  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(pd, &props);

  // The following sequence is inspired by:
  // https://github.com/SaschaWillems/Vulkan/blob/master/examples/computeheadless/computeheadless.cpp
  // https://vulkan-tutorial.com/Compute_Shader

  // Find queue that can run compute code
  uint32_t qn = 16, qf = UINT32_MAX;
  VkQueueFamilyProperties qp[16];
  vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, qp);
  for (uint32_t i = 0; i < qn; i++) {
    if (qp[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
      qf = i;
      break;
    }
  }
  if (qf == UINT32_MAX) {
    fprintf(stderr, "vulkan: no compute queue\n");
    return 1;
  }

  // Open the device
  float prio = 1.0f;
  VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, qf, 1, &prio };
  VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, NULL, 0, 1, &qci, 0, NULL, 0, NULL, NULL };
  VkDevice dev;
  CK(vkCreateDevice(pd, &dci, NULL, &dev));

  // Create the queue 
  VkQueue q;
  vkGetDeviceQueue(dev, qf, 0, &q);
  VkDeviceSize count = (VkDeviceSize)groups * 256, size = count * 16;
  VkBufferCreateInfo bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, size,
                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, NULL };

  // Create the buffer
  VkBuffer buf;
  CK(vkCreateBuffer(dev, &bci, NULL, &buf));
  VkMemoryRequirements mr;
  vkGetBufferMemoryRequirements(dev, buf, &mr);
  VkPhysicalDeviceMemoryProperties mp;
  vkGetPhysicalDeviceMemoryProperties(pd, &mp);
  uint32_t mt = UINT32_MAX, need = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
    if ((mr.memoryTypeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & need) == need) {
      mt = i;
      break;
    }
  }
  if (mt == UINT32_MAX) { fprintf(stderr, "vulkan: no host-visible memory\n"); return 1; }
  VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, mr.size, mt };
  VkDeviceMemory mem;
  CK(vkAllocateMemory(dev, &mai, NULL, &mem));
  CK(vkBindBufferMemory(dev, buf, mem, 0));
  float *ptr;
  CK(vkMapMemory(dev, mem, 0, size, 0, (void **)&ptr));
  memset(ptr, 0, size);

  // Load the shader
  VkShaderModuleCreateInfo smci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, sizeof fma_spv, fma_spv };
  VkShaderModule sm;
  CK(vkCreateShaderModule(dev, &smci, NULL, &sm));
  VkDescriptorSetLayoutBinding dslb = { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL };
  VkDescriptorSetLayoutCreateInfo dslci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, 0, 1, &dslb };
  VkDescriptorSetLayout dsl;
  CK(vkCreateDescriptorSetLayout(dev, &dslci, NULL, &dsl));
  VkPushConstantRange pcr = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 4 };
  VkPipelineLayoutCreateInfo plci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 1, &dsl, 1, &pcr };
  VkPipelineLayout pl;
  CK(vkCreatePipelineLayout(dev, &plci, NULL, &pl));
  VkComputePipelineCreateInfo cpci = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, NULL, 0,
      { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_COMPUTE_BIT, sm, "main", NULL },
      pl, VK_NULL_HANDLE, -1 };
  VkPipeline pipe;
  CK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpci, NULL, &pipe));

  VkDescriptorPoolSize ps = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 };
  VkDescriptorPoolCreateInfo dpci = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, NULL, 0, 1, 1, &ps };
  VkDescriptorPool dp;
  CK(vkCreateDescriptorPool(dev, &dpci, NULL, &dp));
  VkDescriptorSetAllocateInfo dsai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, dp, 1, &dsl };
  VkDescriptorSet ds;
  CK(vkAllocateDescriptorSets(dev, &dsai, &ds));
  VkDescriptorBufferInfo dbi = { buf, 0, VK_WHOLE_SIZE };
  VkWriteDescriptorSet wds = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, ds, 0, 0, 1,
                               VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &dbi, NULL };
  vkUpdateDescriptorSets(dev, 1, &wds, 0, NULL);

  VkCommandPoolCreateInfo cpi = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, NULL, 0, qf };
  VkCommandPool cp;
  CK(vkCreateCommandPool(dev, &cpi, NULL, &cp));
  VkCommandBufferAllocateInfo cbai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, cp, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1 };
  VkCommandBuffer cb;
  CK(vkAllocateCommandBuffers(dev, &cbai, &cb));
  VkCommandBufferBeginInfo cbbi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL, 0, NULL };
  CK(vkBeginCommandBuffer(cb, &cbbi));
  vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
  vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &ds, 0, NULL);
  vkCmdPushConstants(cb, pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &iters);
  VkMemoryBarrier mb = { VK_STRUCTURE_TYPE_MEMORY_BARRIER, NULL,
                         VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT };
  for (uint32_t i = 0; i < per_submit; i++) {
    if (i) vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                0, 1, &mb, 0, NULL, 0, NULL);
    vkCmdDispatch(cb, groups, 1, 1);
  }
  CK(vkEndCommandBuffer(cb));
  VkFenceCreateInfo fci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, NULL, 0 };
  VkFence fence;
  CK(vkCreateFence(dev, &fci, NULL, &fence));

  printf("gpu: %s (%s) | %u groups x 256 x %u iters, %u dispatch/submit, %.0fs\n",
         props.deviceName, dev_type(props.deviceType), groups, iters, per_submit, secs);
  fflush(stdout);

  // 16 FMAs per iteration
  double flop_per_dispatch = (double)count * iters * 16 * 2;
  unsigned long long dispatches = 0;
  double t0 = now_s(), worst = 0;
  VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 0, NULL, NULL, 1, &cb, 0, NULL };
  while (now_s() - t0 < secs) {
    double ts = now_s();
    CK(vkQueueSubmit(q, 1, &si, fence));
    CK(vkWaitForFences(dev, 1, &fence, VK_TRUE, 10ull * 1000 * 1000 * 1000));
    CK(vkResetFences(dev, 1, &fence));
    double dt = now_s() - ts;
    if (dt > worst) worst = dt;
    dispatches += per_submit;
  }
  double el = now_s() - t0;

  int ok = 1;
  double mn = 1e9, mx = -1e9;
  for (VkDeviceSize i = 0; i < count * 4; i += 997) {
    double v = ptr[i];
    if (!isfinite(v) || v <= 0 || v > 1.0001) ok = 0;
    if (v < mn) mn = v;
    if (v > mx) mx = v;
  }
  double gflops = dispatches * flop_per_dispatch / el / 1e9;
  printf("dispatches %llu in %.2fs, %.1f GFLOP/s, worst submit %.1f ms, result check %s (min %.4f max %.4f)\n",
         dispatches, el, gflops, worst * 1e3, ok ? "OK" : "FAILED", mn, mx);
  printf("RESULT {\"ok\":%s,\"device\":\"%s\",\"gflops\":%.1f,\"dispatches\":%llu,\"worst_submit_ms\":%.1f}\n",
         ok ? "true" : "false", props.deviceName, gflops, dispatches, worst * 1e3);

  // clean-up
  vkDeviceWaitIdle(dev);
  vkUnmapMemory(dev, mem);
  vkDestroyFence(dev, fence, NULL);
  vkDestroyCommandPool(dev, cp, NULL);
  vkDestroyDescriptorPool(dev, dp, NULL);
  vkDestroyPipeline(dev, pipe, NULL);
  vkDestroyPipelineLayout(dev, pl, NULL);
  vkDestroyDescriptorSetLayout(dev, dsl, NULL);
  vkDestroyShaderModule(dev, sm, NULL);
  vkDestroyBuffer(dev, buf, NULL);
  vkFreeMemory(dev, mem, NULL);
  vkDestroyDevice(dev, NULL);
  vkDestroyInstance(inst, NULL);
  return ok ? 0 : 1;
}
