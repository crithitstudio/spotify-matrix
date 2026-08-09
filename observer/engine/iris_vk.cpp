// IRIS engine - Vulkan 1.3 backend. Dynamic rendering + synchronization2 only.
#include "iris_internal.h"

#include <algorithm>
#include <cstring>

namespace iris {

// ============================================================== lifecycle ==
Engine::Engine() : impl(new Impl) {}
Engine::~Engine() {
    if (impl && impl->device) shutdown();
}

bool Engine::init(const EngineConfig& cfg) {
    impl->cfg = cfg;
    impl->startTime = 0;
    if (!cfg.headless) {
        if (!platformInit(*impl)) return false;
    }
    impl->startTime = platformTime();
    if (!impl->initVulkan()) return false;
    IRIS_INFO("IRIS up: %s (%ux%u%s)", impl->props.deviceName, impl->rw, impl->rh,
              cfg.headless ? ", headless" : "");
    return true;
}

void Engine::shutdown() {
    if (!impl->device) return;
    vkDeviceWaitIdle(impl->device);
    impl->destroyVulkan();
    platformShutdown(*impl);
    impl->device = VK_NULL_HANDLE;
}

bool Engine::pumpEvents() {
    // clear edges
    std::memset(input_.pressed, 0, sizeof(input_.pressed));
    std::memset(input_.mousePressed, 0, sizeof(input_.mousePressed));
    input_.mouseDX = input_.mouseDY = input_.wheel = 0;
    if (impl->cfg.headless) return !impl->quitRequested;
    return platformPump(*this, *impl) && !impl->quitRequested;
}

void Engine::setMouseCaptured(bool captured) {
    impl->mouseCaptured = captured;
    if (!impl->cfg.headless) platformSetMouseCaptured(*impl, captured);
}
bool Engine::isMouseCaptured() const { return impl->mouseCaptured; }
double Engine::timeSeconds() const { return platformTime() - impl->startTime; }
uint32_t Engine::width() const { return impl->rw; }
uint32_t Engine::height() const { return impl->rh; }
bool Engine::headless() const { return impl->cfg.headless; }
void Engine::requestQuit() { impl->quitRequested = true; }

// ================================================================ context ==
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCb(VkDebugUtilsMessageSeverityFlagBitsEXT sev,
                                              VkDebugUtilsMessageTypeFlagsEXT,
                                              const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    if (sev >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        IRIS_WARN("vk: %s", data->pMessage);
    return VK_FALSE;
}

bool Engine::Impl::initVulkan() {
    if (volkInitialize() != VK_SUCCESS) {
        IRIS_ERROR("No Vulkan loader found. Install your GPU driver / vulkan-loader.");
        return false;
    }

    // ---- instance
    VkApplicationInfo appInfo{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = cfg.appName.c_str(),
        .pEngineName = "IRIS",
        .apiVersion = VK_API_VERSION_1_3,
    };
    std::vector<const char*> exts;
    if (!cfg.headless) exts = platformInstanceExtensions(*this);
    std::vector<const char*> layers;
#if IRIS_VALIDATION
    if (cfg.validation) {
        layers.push_back("VK_LAYER_KHRONOS_validation");
        exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
#endif
    VkInstanceCreateInfo instanceCI{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = (uint32_t)layers.size(),
        .ppEnabledLayerNames = layers.data(),
        .enabledExtensionCount = (uint32_t)exts.size(),
        .ppEnabledExtensionNames = exts.data(),
    };
    if (vkCreateInstance(&instanceCI, nullptr, &instance) != VK_SUCCESS) {
        IRIS_ERROR("vkCreateInstance failed");
        return false;
    }
    volkLoadInstance(instance);

#if IRIS_VALIDATION
    if (cfg.validation && vkCreateDebugUtilsMessengerEXT) {
        VkDebugUtilsMessengerCreateInfoEXT dbgCI{
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT,
            .pfnUserCallback = debugCb,
        };
        vkCreateDebugUtilsMessengerEXT(instance, &dbgCI, nullptr, &debugMessenger);
    }
#endif

    if (!cfg.headless && !platformCreateSurface(*this)) {
        IRIS_ERROR("Window surface creation failed");
        return false;
    }

    // ---- physical device: prefer discrete > integrated > anything (llvmpipe last but allowed)
    uint32_t devCount = 0;
    vkEnumeratePhysicalDevices(instance, &devCount, nullptr);
    if (!devCount) {
        IRIS_ERROR("No Vulkan devices");
        return false;
    }
    std::vector<VkPhysicalDevice> devs(devCount);
    vkEnumeratePhysicalDevices(instance, &devCount, devs.data());
    int bestScore = -1;
    for (auto d : devs) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(d, &p);
        if (p.apiVersion < VK_API_VERSION_1_3) continue;
        int score = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU     ? 300
                    : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 200
                    : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU    ? 100
                                                                             : 50;
        // need one graphics queue family (that can present, when windowed)
        uint32_t qfCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qfCount, nullptr);
        std::vector<VkQueueFamilyProperties> qfs(qfCount);
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qfCount, qfs.data());
        int family = -1;
        for (uint32_t i = 0; i < qfCount; ++i) {
            if (!(qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
            if (surface) {
                VkBool32 present = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(d, i, surface, &present);
                if (!present) continue;
            }
            family = (int)i;
            break;
        }
        if (family < 0) continue;
        if (score > bestScore) {
            bestScore = score;
            phys = d;
            queueFamily = (uint32_t)family;
            props = p;
        }
    }
    if (!phys) {
        IRIS_ERROR("No Vulkan 1.3 capable device with graphics queue");
        return false;
    }
    uboAlign = props.limits.minUniformBufferOffsetAlignment;
    if (uboAlign < 64) uboAlign = 64;

    // ---- logical device
    const float prio = 1.0f;
    VkDeviceQueueCreateInfo queueCI{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = queueFamily,
        .queueCount = 1,
        .pQueuePriorities = &prio,
    };
    VkPhysicalDeviceVulkan12Features feat12{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
        .descriptorBindingSampledImageUpdateAfterBind = VK_TRUE,
        .descriptorBindingPartiallyBound = VK_TRUE,
        .runtimeDescriptorArray = VK_TRUE,
    };
    VkPhysicalDeviceVulkan13Features feat13{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &feat12,
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE,
    };
    VkPhysicalDeviceFeatures feat10{.samplerAnisotropy = VK_TRUE};
    std::vector<const char*> devExts;
    if (surface) devExts.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    VkDeviceCreateInfo deviceCI{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &feat13,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueCI,
        .enabledExtensionCount = (uint32_t)devExts.size(),
        .ppEnabledExtensionNames = devExts.data(),
        .pEnabledFeatures = &feat10,
    };
    if (vkCreateDevice(phys, &deviceCI, nullptr, &device) != VK_SUCCESS) {
        IRIS_ERROR("vkCreateDevice failed on %s", props.deviceName);
        return false;
    }
    volkLoadDevice(device);
    vkGetDeviceQueue(device, queueFamily, 0, &queue);

    // ---- command pool + per-frame
    VkCommandPoolCreateInfo poolCI{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queueFamily,
    };
    VK_CHK(vkCreateCommandPool(device, &poolCI, nullptr, &cmdPool));

    // ---- samplers: [0] repeat+linear [1] clamp+linear [2] repeat+nearest [3] clamp+nearest
    for (int i = 0; i < 4; ++i) {
        bool repeat = (i & 1) == 0;
        bool linear = i < 2;
        VkSamplerCreateInfo sci{
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
            .minFilter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU = repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .anisotropyEnable = VK_TRUE,
            .maxAnisotropy = props.limits.maxSamplerAnisotropy < 8.f ? props.limits.maxSamplerAnisotropy : 8.f,
            .maxLod = VK_LOD_CLAMP_NONE,
        };
        VK_CHK(vkCreateSampler(device, &sci, nullptr, &samplers[i]));
    }

    // ---- bindless-lite descriptor layout
    VkDescriptorSetLayoutBinding bindings[2] = {
        {.binding = 0,
         .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT},
        {.binding = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         .descriptorCount = IRIS_MAX_TEXTURES,
         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT},
    };
    VkDescriptorBindingFlags bindFlags[2] = {
        0, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT};
    VkDescriptorSetLayoutBindingFlagsCreateInfo bindFlagsCI{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount = 2,
        .pBindingFlags = bindFlags,
    };
    VkDescriptorSetLayoutCreateInfo layoutCI{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = &bindFlagsCI,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        .bindingCount = 2,
        .pBindings = bindings,
    };
    VK_CHK(vkCreateDescriptorSetLayout(device, &layoutCI, nullptr, &setLayout));

    VkDescriptorPoolSize poolSizes[2] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, IRIS_FRAMES_IN_FLIGHT},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, IRIS_MAX_TEXTURES * IRIS_FRAMES_IN_FLIGHT},
    };
    VkDescriptorPoolCreateInfo dpoolCI{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .maxSets = IRIS_FRAMES_IN_FLIGHT,
        .poolSizeCount = 2,
        .pPoolSizes = poolSizes,
    };
    VK_CHK(vkCreateDescriptorPool(device, &dpoolCI, nullptr, &descPool));

    // ---- pipeline layout: one push-constant block shared by all pipelines
    VkPushConstantRange pcr{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = 112,
    };
    VkPipelineLayoutCreateInfo plCI{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &setLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pcr,
    };
    VK_CHK(vkCreatePipelineLayout(device, &plCI, nullptr, &pipeLayout));

    // ---- per-frame data
    const VkDeviceSize uboSlot = (sizeof(ViewUbo) + uboAlign - 1) & ~(uboAlign - 1);
    for (auto& f : frames) {
        VkCommandBufferAllocateInfo cbAI{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = cmdPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        VK_CHK(vkAllocateCommandBuffers(device, &cbAI, &f.cmd));
        VkSemaphoreCreateInfo semCI{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK_CHK(vkCreateSemaphore(device, &semCI, nullptr, &f.imageAvailable));
        VK_CHK(vkCreateSemaphore(device, &semCI, nullptr, &f.renderFinished));
        VkFenceCreateInfo fenceCI{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                                  .flags = VK_FENCE_CREATE_SIGNALED_BIT};
        VK_CHK(vkCreateFence(device, &fenceCI, nullptr, &f.inFlight));
        f.viewUbos = createBuffer(uboSlot * IRIS_MAX_VIEWS_PER_FRAME,
                                  VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  true);
        VkDescriptorSetAllocateInfo dsAI{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = descPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &setLayout,
        };
        VK_CHK(vkAllocateDescriptorSets(device, &dsAI, &f.set));
        VkDescriptorBufferInfo dbi{.buffer = f.viewUbos.buf, .offset = 0, .range = sizeof(ViewUbo)};
        VkWriteDescriptorSet w{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = f.set,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            .pBufferInfo = &dbi,
        };
        vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
    }

    // slot 0 MUST be the white texture (TEX_WHITE contract) - register it
    // before anything else can claim an index.
    uint32_t white = 0xffffffffu;
    GpuImage wi = createImage(1, 1, VK_FORMAT_R8G8B8A8_UNORM,
                              VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                              VK_IMAGE_ASPECT_COLOR_BIT);
    uploadToImage(wi, &white);
    registerTexture(wi, samplers[0]);

    // ---- swapchain + render images
    uint32_t w = cfg.width, h = cfg.height;
    if (!cfg.headless) {
        if (!createSwapchain()) return false;
        w = swapExtent.width;
        h = swapExtent.height;
    }
    if (!createRenderImages(w, h)) return false;
    if (!createPipelines()) return false;
    return true;
}

void Engine::Impl::destroyVulkan() {
    for (auto* f : fonts) delete f;
    fonts.clear();
    for (auto& t : targets) {
        // t.color is owned by the bindless textures list (registered copy);
        // destroying it there is enough. Depth is ours alone.
        destroyImage(t.depth);
    }
    targets.clear();
    for (auto& m : meshes) {
        destroyBuffer(m.vbuf);
        destroyBuffer(m.ibuf);
    }
    meshes.clear();
    for (auto& t : textures) destroyImage(t);
    textures.clear();
    destroyRenderImages();
    destroySwapchain();
    for (auto& f : frames) {
        destroyBuffer(f.viewUbos);
        destroyBuffer(f.uiVbuf);
        if (f.imageAvailable) vkDestroySemaphore(device, f.imageAvailable, nullptr);
        if (f.renderFinished) vkDestroySemaphore(device, f.renderFinished, nullptr);
        if (f.inFlight) vkDestroyFence(device, f.inFlight, nullptr);
    }
    if (pipeOpaque) vkDestroyPipeline(device, pipeOpaque, nullptr);
    if (pipeAlpha) vkDestroyPipeline(device, pipeAlpha, nullptr);
    if (pipeUi) vkDestroyPipeline(device, pipeUi, nullptr);
    if (pipePost) vkDestroyPipeline(device, pipePost, nullptr);
    if (pipeLayout) vkDestroyPipelineLayout(device, pipeLayout, nullptr);
    if (descPool) vkDestroyDescriptorPool(device, descPool, nullptr);
    if (setLayout) vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
    for (auto s : samplers)
        if (s) vkDestroySampler(device, s, nullptr);
    if (cmdPool) vkDestroyCommandPool(device, cmdPool, nullptr);
    vkDestroyDevice(device, nullptr);
    if (surface) vkDestroySurfaceKHR(instance, surface, nullptr);
#if IRIS_VALIDATION
    if (debugMessenger) vkDestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
#endif
    vkDestroyInstance(instance, nullptr);
}

// ============================================================== swapchain ==
bool Engine::Impl::createSwapchain() {
    VkSurfaceCapabilitiesKHR caps;
    VK_CHK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, surface, &caps));
    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &fmtCount, fmts.data());
    VkSurfaceFormatKHR pick = fmts[0];
    for (auto& f : fmts)
        if ((f.format == VK_FORMAT_B8G8R8A8_SRGB || f.format == VK_FORMAT_R8G8B8A8_SRGB) &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            pick = f;
            break;
        }
    swapFormat = pick.format;

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == 0xFFFFFFFFu) {
        platformGetFramebufferSize(*this, extent.width, extent.height);
    }
    if (extent.width == 0 || extent.height == 0) return false;   // minimized
    swapExtent = extent;

    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    if (!cfg.vsync) {
        uint32_t pmCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(phys, surface, &pmCount, nullptr);
        std::vector<VkPresentModeKHR> pms(pmCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(phys, surface, &pmCount, pms.data());
        for (auto m : pms)
            if (m == VK_PRESENT_MODE_MAILBOX_KHR) presentMode = m;
    }

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR scCI{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = imageCount,
        .imageFormat = swapFormat,
        .imageColorSpace = pick.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = presentMode,
        .clipped = VK_TRUE,
        .oldSwapchain = swapchain,
    };
    VkSwapchainKHR newSwap;
    VK_CHK(vkCreateSwapchainKHR(device, &scCI, nullptr, &newSwap));
    if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
    swapchain = newSwap;
    uint32_t n = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &n, nullptr);
    swapImages.resize(n);
    vkGetSwapchainImagesKHR(device, swapchain, &n, swapImages.data());
    swapchainDirty = false;
    return true;
}

void Engine::Impl::destroySwapchain() {
    if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
    swapchain = VK_NULL_HANDLE;
    swapImages.clear();
}

bool Engine::Impl::createRenderImages(uint32_t w, uint32_t h) {
    rw = w;
    rh = h;
    sceneColor = createImage(w, h, VK_FORMAT_R8G8B8A8_SRGB,
                             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                             VK_IMAGE_ASPECT_COLOR_BIT);
    sceneDepth = createImage(w, h, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                             VK_IMAGE_ASPECT_DEPTH_BIT);
    finalImage = createImage(w, h, VK_FORMAT_R8G8B8A8_SRGB,
                             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                             VK_IMAGE_ASPECT_COLOR_BIT);
    if (sceneColorTexId == TEX_INVALID) {
        sceneColorTexId = registerTexture(sceneColor, samplers[1]);
        // sceneColor lives in the texture list now; keep a copy of handles here
        // but ownership stays with `textures` entry for descriptor updates.
    } else {
        textures[sceneColorTexId] = sceneColor;
        writeTextureDescriptor(sceneColorTexId);
    }
    return true;
}

void Engine::Impl::destroyRenderImages() {
    // sceneColor is also referenced by textures[sceneColorTexId]; destroy once.
    if (sceneColorTexId != TEX_INVALID) {
        destroyImage(textures[sceneColorTexId]);
        sceneColor = {};
    } else {
        destroyImage(sceneColor);
    }
    destroyImage(sceneDepth);
    destroyImage(finalImage);
}

// ================================================================ shaders ==
VkShaderModule Engine::Impl::loadShader(const std::string& name) {
    std::string path = cfg.shaderDir + "/" + name;
    std::vector<uint8_t> code;
    if (!readFileBytes(path, code)) {
        IRIS_ERROR("Missing shader %s", path.c_str());
        return VK_NULL_HANDLE;
    }
    VkShaderModuleCreateInfo ci{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = code.size(),
        .pCode = (const uint32_t*)code.data(),
    };
    VkShaderModule mod;
    VK_CHK(vkCreateShaderModule(device, &ci, nullptr, &mod));
    return mod;
}

bool Engine::Impl::createPipelines() {
    VkShaderModule sceneVert = loadShader("scene.vert.spv");
    VkShaderModule sceneFrag = loadShader("scene.frag.spv");
    VkShaderModule uiVert = loadShader("ui.vert.spv");
    VkShaderModule uiFrag = loadShader("ui.frag.spv");
    VkShaderModule postVert = loadShader("post.vert.spv");
    VkShaderModule postFrag = loadShader("post.frag.spv");
    if (!sceneVert || !sceneFrag || !uiVert || !uiFrag || !postVert || !postFrag) return false;

    auto stages = [](VkShaderModule v, VkShaderModule f) {
        return std::array<VkPipelineShaderStageCreateInfo, 2>{
            VkPipelineShaderStageCreateInfo{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                            .stage = VK_SHADER_STAGE_VERTEX_BIT,
                                            .module = v,
                                            .pName = "main"},
            VkPipelineShaderStageCreateInfo{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                                            .module = f,
                                            .pName = "main"},
        };
    };

    // shared fixed state
    VkPipelineInputAssemblyStateCreateInfo ia{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkPipelineViewportStateCreateInfo vp{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    VkPipelineRasterizationStateCreateInfo rs{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo ms{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynCI{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dyn,
    };

    // scene vertex layout
    VkVertexInputBindingDescription sceneBind{.binding = 0, .stride = sizeof(Vertex),
                                              .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription sceneAttrs[] = {
        {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, pos)},
        {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, nrm)},
        {.location = 2, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Vertex, uv)},
        {.location = 3, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = offsetof(Vertex, col)},
    };
    VkPipelineVertexInputStateCreateInfo sceneVI{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &sceneBind,
        .vertexAttributeDescriptionCount = 4,
        .pVertexAttributeDescriptions = sceneAttrs,
    };

    // ui vertex layout
    VkVertexInputBindingDescription uiBind{.binding = 0, .stride = sizeof(UiVertex),
                                           .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription uiAttrs[] = {
        {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(UiVertex, pos)},
        {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(UiVertex, uv)},
        {.location = 2, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = offsetof(UiVertex, col)},
    };
    VkPipelineVertexInputStateCreateInfo uiVI{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &uiBind,
        .vertexAttributeDescriptionCount = 3,
        .pVertexAttributeDescriptions = uiAttrs,
    };
    VkPipelineVertexInputStateCreateInfo emptyVI{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };

    VkPipelineDepthStencilStateCreateInfo dsOn{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
    };
    VkPipelineDepthStencilStateCreateInfo dsRead{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
    };
    VkPipelineDepthStencilStateCreateInfo dsOff{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
    };

    VkPipelineColorBlendAttachmentState blendOff{.colorWriteMask = 0xF};
    VkPipelineColorBlendAttachmentState blendAlpha{
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = 0xF,
    };
    auto blendState = [](VkPipelineColorBlendAttachmentState* att) {
        return VkPipelineColorBlendStateCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments = att,
        };
    };
    VkPipelineColorBlendStateCreateInfo cbOff = blendState(&blendOff);
    VkPipelineColorBlendStateCreateInfo cbAlpha = blendState(&blendAlpha);

    VkFormat sceneFmt = VK_FORMAT_R8G8B8A8_SRGB;
    VkFormat depthFmt = VK_FORMAT_D32_SFLOAT;
    VkPipelineRenderingCreateInfo renderSceneCI{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &sceneFmt,
        .depthAttachmentFormat = depthFmt,
    };
    VkPipelineRenderingCreateInfo renderFinalCI{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &sceneFmt,   // final image same format
    };

    auto make = [&](VkShaderModule v, VkShaderModule f, VkPipelineVertexInputStateCreateInfo* vi,
                    VkPipelineDepthStencilStateCreateInfo* ds, VkPipelineColorBlendStateCreateInfo* cb,
                    VkPipelineRenderingCreateInfo* rend, VkPipeline* out) {
        auto st = stages(v, f);
        VkGraphicsPipelineCreateInfo ci{
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = rend,
            .stageCount = 2,
            .pStages = st.data(),
            .pVertexInputState = vi,
            .pInputAssemblyState = &ia,
            .pViewportState = &vp,
            .pRasterizationState = &rs,
            .pMultisampleState = &ms,
            .pDepthStencilState = ds,
            .pColorBlendState = cb,
            .pDynamicState = &dynCI,
            .layout = pipeLayout,
        };
        VK_CHK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr, out));
    };

    make(sceneVert, sceneFrag, &sceneVI, &dsOn, &cbOff, &renderSceneCI, &pipeOpaque);
    make(sceneVert, sceneFrag, &sceneVI, &dsRead, &cbAlpha, &renderSceneCI, &pipeAlpha);
    make(uiVert, uiFrag, &uiVI, &dsOff, &cbAlpha, &renderFinalCI, &pipeUi);
    make(postVert, postFrag, &emptyVI, &dsOff, &cbOff, &renderFinalCI, &pipePost);

    vkDestroyShaderModule(device, sceneVert, nullptr);
    vkDestroyShaderModule(device, sceneFrag, nullptr);
    vkDestroyShaderModule(device, uiVert, nullptr);
    vkDestroyShaderModule(device, uiFrag, nullptr);
    vkDestroyShaderModule(device, postVert, nullptr);
    vkDestroyShaderModule(device, postFrag, nullptr);
    return true;
}

// ============================================================== resources ==
uint32_t Engine::Impl::memType(uint32_t bits, VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & flags) == flags) return i;
    IRIS_FATAL("No suitable memory type");
    return 0;
}

GpuBuffer Engine::Impl::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                     VkMemoryPropertyFlags memFlags, bool map) {
    GpuBuffer b;
    b.size = size;
    VkBufferCreateInfo ci{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VK_CHK(vkCreateBuffer(device, &ci, nullptr, &b.buf));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device, b.buf, &req);
    VkMemoryAllocateInfo ai{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = memType(req.memoryTypeBits, memFlags),
    };
    VK_CHK(vkAllocateMemory(device, &ai, nullptr, &b.mem));
    VK_CHK(vkBindBufferMemory(device, b.buf, b.mem, 0));
    if (map) VK_CHK(vkMapMemory(device, b.mem, 0, size, 0, &b.mapped));
    return b;
}

void Engine::Impl::destroyBuffer(GpuBuffer& b) {
    if (b.buf) vkDestroyBuffer(device, b.buf, nullptr);
    if (b.mem) vkFreeMemory(device, b.mem, nullptr);
    b = {};
}

GpuImage Engine::Impl::createImage(uint32_t w, uint32_t h, VkFormat fmt, VkImageUsageFlags usage,
                                   VkImageAspectFlags aspect) {
    GpuImage img;
    img.format = fmt;
    img.w = w;
    img.h = h;
    VkImageCreateInfo ci{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = fmt,
        .extent = {w, h, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VK_CHK(vkCreateImage(device, &ci, nullptr, &img.img));
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device, img.img, &req);
    VkMemoryAllocateInfo ai{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = memType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };
    VK_CHK(vkAllocateMemory(device, &ai, nullptr, &img.mem));
    VK_CHK(vkBindImageMemory(device, img.img, img.mem, 0));
    VkImageViewCreateInfo vci{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = img.img,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = fmt,
        .subresourceRange = {aspect, 0, 1, 0, 1},
    };
    VK_CHK(vkCreateImageView(device, &vci, nullptr, &img.view));
    return img;
}

void Engine::Impl::destroyImage(GpuImage& img) {
    if (img.view) vkDestroyImageView(device, img.view, nullptr);
    if (img.img) vkDestroyImage(device, img.img, nullptr);
    if (img.mem) vkFreeMemory(device, img.mem, nullptr);
    img = {};
}

VkCommandBuffer Engine::Impl::beginOneTime() {
    VkCommandBufferAllocateInfo ai{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = cmdPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cmd;
    VK_CHK(vkAllocateCommandBuffers(device, &ai, &cmd));
    VkCommandBufferBeginInfo bi{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    VK_CHK(vkBeginCommandBuffer(cmd, &bi));
    return cmd;
}

void Engine::Impl::endOneTime(VkCommandBuffer cmd) {
    VK_CHK(vkEndCommandBuffer(cmd));
    VkCommandBufferSubmitInfo cbsi{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                                   .commandBuffer = cmd};
    VkSubmitInfo2 si{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                     .commandBufferInfoCount = 1,
                     .pCommandBufferInfos = &cbsi};
    VK_CHK(vkQueueSubmit2(queue, 1, &si, VK_NULL_HANDLE));
    VK_CHK(vkQueueWaitIdle(queue));
    vkFreeCommandBuffers(device, cmdPool, 1, &cmd);
}

void Engine::Impl::barrier(VkCommandBuffer cmd, GpuImage& img, VkImageLayout newLayout,
                           VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                           VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess,
                           VkImageAspectFlags aspect) {
    VkImageMemoryBarrier2 b{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = srcStage,
        .srcAccessMask = srcAccess,
        .dstStageMask = dstStage,
        .dstAccessMask = dstAccess,
        .oldLayout = img.layout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = img.img,
        .subresourceRange = {aspect, 0, 1, 0, 1},
    };
    VkDependencyInfo dep{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                         .imageMemoryBarrierCount = 1,
                         .pImageMemoryBarriers = &b};
    vkCmdPipelineBarrier2(cmd, &dep);
    img.layout = newLayout;
}

void Engine::Impl::uploadToImage(GpuImage& img, const void* rgba8) {
    VkDeviceSize size = (VkDeviceSize)img.w * img.h * 4;
    GpuBuffer staging = createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                     true);
    std::memcpy(staging.mapped, rgba8, size);
    VkCommandBuffer cmd = beginOneTime();
    barrier(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkBufferImageCopy region{
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .imageExtent = {img.w, img.h, 1},
    };
    vkCmdCopyBufferToImage(cmd, staging.buf, img.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    barrier(cmd, img, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    endOneTime(cmd);
    destroyBuffer(staging);
}

TexId Engine::Impl::registerTexture(GpuImage img, VkSampler sampler) {
    TexId id = (TexId)textures.size();
    if (id >= (TexId)IRIS_MAX_TEXTURES) IRIS_FATAL("Texture slots exhausted (%u)", IRIS_MAX_TEXTURES);
    textures.push_back(img);
    textureSamplers.push_back(sampler);
    writeTextureDescriptor(id);
    return id;
}

void Engine::Impl::writeTextureDescriptor(TexId id) {
    // Sampled state descriptor; render targets are transitioned to
    // SHADER_READ_ONLY before any sampling pass, matching this layout.
    VkDescriptorImageInfo dii{
        .sampler = textureSamplers[id],
        .imageView = textures[id].view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    for (auto& f : frames) {
        VkWriteDescriptorSet w{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = f.set,
            .dstBinding = 1,
            .dstArrayElement = (uint32_t)id,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &dii,
        };
        vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
    }
}

// ------------------------------------------------------- public resources --
TexId Engine::createTexture(const void* rgba8, int w, int h, bool srgb, bool repeat, bool filter) {
    VkFormat fmt = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    GpuImage img = impl->createImage((uint32_t)w, (uint32_t)h, fmt,
                                     VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                     VK_IMAGE_ASPECT_COLOR_BIT);
    impl->uploadToImage(img, rgba8);
    int samplerIdx = (repeat ? 0 : 1) + (filter ? 0 : 2);
    return impl->registerTexture(img, impl->samplers[samplerIdx]);
}

void Engine::updateTexture(TexId id, const void* rgba8, int w, int h) {
    GpuImage& img = impl->textures[id];
    if ((int)img.w != w || (int)img.h != h) {
        IRIS_WARN("updateTexture size mismatch");
        return;
    }
    vkQueueWaitIdle(impl->queue);
    img.layout = img.layout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_IMAGE_LAYOUT_UNDEFINED : img.layout;
    impl->uploadToImage(img, rgba8);
}

MeshId Engine::createMesh(const std::vector<Vertex>& verts, const std::vector<uint32_t>& indices) {
    MeshRes m;
    m.indexCount = (uint32_t)indices.size();
    for (auto& v : verts) m.bounds.expand(v.pos);
    VkDeviceSize vsize = verts.size() * sizeof(Vertex);
    VkDeviceSize isize = indices.size() * sizeof(uint32_t);
    GpuBuffer vstage = impl->createBuffer(vsize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                          true);
    GpuBuffer istage = impl->createBuffer(isize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                          true);
    std::memcpy(vstage.mapped, verts.data(), vsize);
    std::memcpy(istage.mapped, indices.data(), isize);
    m.vbuf = impl->createBuffer(vsize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    m.ibuf = impl->createBuffer(isize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkCommandBuffer cmd = impl->beginOneTime();
    VkBufferCopy vc{.size = vsize}, ic{.size = isize};
    vkCmdCopyBuffer(cmd, vstage.buf, m.vbuf.buf, 1, &vc);
    vkCmdCopyBuffer(cmd, istage.buf, m.ibuf.buf, 1, &ic);
    impl->endOneTime(cmd);
    impl->destroyBuffer(vstage);
    impl->destroyBuffer(istage);
    impl->meshes.push_back(std::move(m));
    return (MeshId)impl->meshes.size() - 1;
}

RtId Engine::createRenderTarget(uint32_t w, uint32_t h) {
    RenderTargetRes rt;
    rt.w = w;
    rt.h = h;
    rt.color = impl->createImage(w, h, VK_FORMAT_R8G8B8A8_SRGB,
                                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                     VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                 VK_IMAGE_ASPECT_COLOR_BIT);
    rt.depth = impl->createImage(w, h, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                 VK_IMAGE_ASPECT_DEPTH_BIT);
    rt.texId = impl->registerTexture(rt.color, impl->samplers[1]);
    impl->targets.push_back(rt);
    return (RtId)impl->targets.size() - 1;
}

TexId Engine::renderTargetTexture(RtId rt) const { return impl->targets[rt].texId; }

// ================================================================== frame ==
bool Engine::beginFrame() {
    Impl& im = *impl;
    FrameData& f = im.frames[im.frameIndex];
    VK_CHK(vkWaitForFences(im.device, 1, &f.inFlight, VK_TRUE, UINT64_MAX));

    if (!im.cfg.headless) {
        if (im.swapchainDirty) {
            vkDeviceWaitIdle(im.device);
            if (!im.createSwapchain()) return false;
            if (im.swapExtent.width != im.rw || im.swapExtent.height != im.rh) {
                im.destroyRenderImages();
                im.createRenderImages(im.swapExtent.width, im.swapExtent.height);
            }
        }
        VkResult res = vkAcquireNextImageKHR(im.device, im.swapchain, UINT64_MAX, f.imageAvailable,
                                             VK_NULL_HANDLE, &im.swapImageIndex);
        if (res == VK_ERROR_OUT_OF_DATE_KHR) {
            im.swapchainDirty = true;
            return false;
        }
        if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) VK_CHK(res);
    }

    VK_CHK(vkResetFences(im.device, 1, &f.inFlight));
    VK_CHK(vkResetCommandBuffer(f.cmd, 0));
    VkCommandBufferBeginInfo bi{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VK_CHK(vkBeginCommandBuffer(f.cmd, &bi));

    im.viewCursor = 0;
    im.uiVerts.clear();
    im.uiBatches.clear();
    im.inFrame = true;

    // fresh depth + scene image each frame; layouts reset lazily per pass
    return true;
}

void Engine::Impl::drawItems(VkCommandBuffer cmd, const SceneView& view,
                             const std::vector<DrawItem>& items, uint32_t uboOffset) {
    struct Push {
        float model[16];
        float tint[4];
        float misc[4];     // texIdx, emissive, flagsBits, unused
        float uvScale[4];
    } push;

    mat4 vp = view.proj * view.view;
    Frustum fr = Frustum::fromViewProj(vp);

    auto visible = [&](const DrawItem& it) {
        if (it.mesh < 0) return false;
        if (view.isReflection) {
            if (it.flags & DRAW_NO_REFLECT) return false;
        } else {
            if (it.flags & DRAW_ONLY_REFLECT) return false;
            if (it.flags & DRAW_NO_MAIN) return false;
        }
        const AABB& b = meshes[it.mesh].bounds;
        // transform 8 corners
        AABB wb;
        for (int i = 0; i < 8; ++i) {
            vec3 c{i & 1 ? b.mx.x : b.mn.x, i & 2 ? b.mx.y : b.mn.y, i & 4 ? b.mx.z : b.mn.z};
            wb.expand(it.model.transformPoint(c));
        }
        return fr.intersectsAABB(wb);
    };

    auto emit = [&](const DrawItem& it) {
        std::memcpy(push.model, it.model.m, 64);
        push.tint[0] = it.tint.x; push.tint[1] = it.tint.y; push.tint[2] = it.tint.z; push.tint[3] = it.tint.w;
        push.misc[0] = (float)it.tex;
        push.misc[1] = it.emissive;
        push.misc[2] = (float)(it.flags & 0xff);
        push.misc[3] = 0;
        push.uvScale[0] = it.uvScaleX; push.uvScale[1] = it.uvScaleY; push.uvScale[2] = 0; push.uvScale[3] = 0;
        vkCmdPushConstants(cmd, pipeLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(push), &push);
        MeshRes& m = meshes[it.mesh];
        VkDeviceSize zero = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &m.vbuf.buf, &zero);
        vkCmdBindIndexBuffer(cmd, m.ibuf.buf, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, m.indexCount, 1, 0, 0, 0);
    };

    // opaque
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeOpaque);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout, 0, 1,
                            &frames[frameIndex].set, 1, &uboOffset);
    for (auto& it : items)
        if (!(it.flags & DRAW_ALPHA) && visible(it)) emit(it);

    // alpha, back to front
    std::vector<const DrawItem*> alpha;
    for (auto& it : items)
        if ((it.flags & DRAW_ALPHA) && visible(it)) alpha.push_back(&it);
    std::sort(alpha.begin(), alpha.end(), [&](const DrawItem* a, const DrawItem* b) {
        vec3 pa{a->model.m[12], a->model.m[13], a->model.m[14]};
        vec3 pb{b->model.m[12], b->model.m[13], b->model.m[14]};
        return length2(pa - view.camPos) > length2(pb - view.camPos);
    });
    if (!alpha.empty()) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeAlpha);
        for (auto* it : alpha) emit(*it);
    }
}

void Engine::renderScene(const SceneView& view, const std::vector<DrawItem>& items,
                         const std::vector<Light>& lights) {
    Impl& im = *impl;
    FrameData& f = im.frames[im.frameIndex];
    VkCommandBuffer cmd = f.cmd;
    if (im.viewCursor >= IRIS_MAX_VIEWS_PER_FRAME) {
        IRIS_WARN("Too many views this frame");
        return;
    }

    // ---- fill UBO slot
    const VkDeviceSize slot = (sizeof(ViewUbo) + im.uboAlign - 1) & ~(im.uboAlign - 1);
    uint32_t uboOffset = (uint32_t)(slot * im.viewCursor);
    ViewUbo ubo{};
    std::memcpy(ubo.view, view.view.m, 64);
    std::memcpy(ubo.proj, view.proj.m, 64);
    ubo.camPosTime[0] = view.camPos.x; ubo.camPosTime[1] = view.camPos.y;
    ubo.camPosTime[2] = view.camPos.z; ubo.camPosTime[3] = view.time;
    ubo.ambientFog[0] = view.ambient.x; ubo.ambientFog[1] = view.ambient.y;
    ubo.ambientFog[2] = view.ambient.z; ubo.ambientFog[3] = view.fog.density;
    ubo.fogColor[0] = view.fog.color.x; ubo.fogColor[1] = view.fog.color.y;
    ubo.fogColor[2] = view.fog.color.z; ubo.fogColor[3] = 0;
    ubo.clipPlane[0] = view.clipPlane.x; ubo.clipPlane[1] = view.clipPlane.y;
    ubo.clipPlane[2] = view.clipPlane.z; ubo.clipPlane[3] = view.clipPlane.w;
    int nl = (int)std::min<size_t>(lights.size(), IRIS_MAX_LIGHTS);
    ubo.counts[0] = nl;
    ubo.counts[1] = view.isReflection ? 1 : 0;
    for (int i = 0; i < nl; ++i) {
        const Light& L = lights[i];
        ubo.lights[i] = {{L.pos.x, L.pos.y, L.pos.z, L.radius},
                         {L.color.x, L.color.y, L.color.z, L.intensity},
                         {L.dir.x, L.dir.y, L.dir.z, L.innerCos},
                         {L.outerCos, (float)L.type, 0, 0}};
    }
    std::memcpy((uint8_t*)f.viewUbos.mapped + uboOffset, &ubo, sizeof(ubo));
    im.viewCursor++;

    // ---- target images
    GpuImage* color;
    GpuImage* depth;
    uint32_t tw, th;
    if (view.target < 0) {
        color = &im.textures[im.sceneColorTexId];
        depth = &im.sceneDepth;
        tw = im.rw; th = im.rh;
    } else {
        color = &im.textures[im.targets[view.target].texId];
        depth = &im.targets[view.target].depth;
        tw = im.targets[view.target].w; th = im.targets[view.target].h;
    }

    im.barrier(cmd, *color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
               VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
               0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
               VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    im.barrier(cmd, *depth, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
               VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, 0,
               VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
               VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);

    VkRenderingAttachmentInfo colorAtt{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = color->view,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {.color = {{view.fog.color.x, view.fog.color.y, view.fog.color.z, 1}}},
    };
    VkRenderingAttachmentInfo depthAtt{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = depth->view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue = {.depthStencil = {1.0f, 0}},
    };
    VkRenderingInfo ri{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {{0, 0}, {tw, th}},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAtt,
        .pDepthAttachment = &depthAtt,
    };
    vkCmdBeginRendering(cmd, &ri);
    VkViewport vp{0, 0, (float)tw, (float)th, 0, 1};
    VkRect2D sc{{0, 0}, {tw, th}};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);

    im.drawItems(cmd, view, items, uboOffset);

    vkCmdEndRendering(cmd);
    im.barrier(cmd, *color, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
               VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
               VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

void Engine::renderPostAndUi(const PostParams& post) {
    Impl& im = *impl;
    FrameData& f = im.frames[im.frameIndex];
    VkCommandBuffer cmd = f.cmd;

    // upload UI verts
    if (!im.uiVerts.empty()) {
        size_t need = im.uiVerts.size() * sizeof(UiVertex);
        if (f.uiVbufCapacity < need) {
            vkQueueWaitIdle(im.queue);
            im.destroyBuffer(f.uiVbuf);
            size_t cap = need * 2;
            f.uiVbuf = im.createBuffer(cap, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                       true);
            f.uiVbufCapacity = cap;
        }
        std::memcpy(f.uiVbuf.mapped, im.uiVerts.data(), need);
    }

    im.barrier(cmd, im.finalImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
               VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
               VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    VkRenderingAttachmentInfo colorAtt{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = im.finalImage.view,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {.color = {{0, 0, 0, 1}}},
    };
    VkRenderingInfo ri{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {{0, 0}, {im.rw, im.rh}},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAtt,
    };
    vkCmdBeginRendering(cmd, &ri);
    VkViewport vp{0, 0, (float)im.rw, (float)im.rh, 0, 1};
    VkRect2D sc{{0, 0}, {im.rw, im.rh}};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);

    struct Push {
        float p0[4], p1[4], p2[4], p3[4];   // post params (model slot)
        float tint[4];
        float misc[4];
        float uvScale[4];
    } push{};
    push.p0[0] = post.grain; push.p0[1] = post.vignette; push.p0[2] = post.aberration; push.p0[3] = post.scanline;
    push.p1[0] = post.noiseBurst; push.p1[1] = post.desaturate; push.p1[2] = post.warp; push.p1[3] = post.fadeBlack;
    push.p2[0] = post.flashWhite; push.p2[1] = post.cctv; push.p2[2] = post.time; push.p2[3] = post.lowHealth;
    push.misc[0] = (float)im.sceneColorTexId;
    push.uvScale[0] = 1; push.uvScale[1] = 1;

    uint32_t zeroOffset = 0;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, im.pipePost);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, im.pipeLayout, 0, 1, &f.set, 1,
                            &zeroOffset);
    vkCmdPushConstants(cmd, im.pipeLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(push), &push);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    // ---- UI batches
    if (!im.uiVerts.empty()) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, im.pipeUi);
        VkDeviceSize zero = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &f.uiVbuf.buf, &zero);
        for (auto& b : im.uiBatches) {
            Push up{};
            up.p0[0] = (float)im.rw;   // screen size for vertex transform
            up.p0[1] = (float)im.rh;
            up.misc[0] = (float)b.tex;
            vkCmdPushConstants(cmd, im.pipeLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(up), &up);
            vkCmdDraw(cmd, b.vertexCount, 1, b.firstVertex, 0);
        }
    }
    vkCmdEndRendering(cmd);
}

void Engine::endFrame() {
    Impl& im = *impl;
    FrameData& f = im.frames[im.frameIndex];
    VkCommandBuffer cmd = f.cmd;

    if (!im.cfg.headless) {
        // blit final -> swapchain
        VkImage swapImg = im.swapImages[im.swapImageIndex];
        VkImageMemoryBarrier2 toDst{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = swapImg,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        };
        VkDependencyInfo dep1{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                              .imageMemoryBarrierCount = 1,
                              .pImageMemoryBarriers = &toDst};
        vkCmdPipelineBarrier2(cmd, &dep1);
        im.barrier(cmd, im.finalImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                   VK_ACCESS_2_TRANSFER_READ_BIT);
        VkImageBlit blit{
            .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        };
        blit.srcOffsets[1] = {(int32_t)im.rw, (int32_t)im.rh, 1};
        blit.dstOffsets[1] = {(int32_t)im.swapExtent.width, (int32_t)im.swapExtent.height, 1};
        vkCmdBlitImage(cmd, im.finalImage.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapImg,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        VkImageMemoryBarrier2 toPresent{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = swapImg,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        };
        VkDependencyInfo dep2{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                              .imageMemoryBarrierCount = 1,
                              .pImageMemoryBarriers = &toPresent};
        vkCmdPipelineBarrier2(cmd, &dep2);
    } else {
        im.barrier(cmd, im.finalImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                   VK_ACCESS_2_TRANSFER_READ_BIT);
    }

    VK_CHK(vkEndCommandBuffer(cmd));

    VkCommandBufferSubmitInfo cbsi{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                                   .commandBuffer = cmd};
    VkSemaphoreSubmitInfo waitSem{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                                  .semaphore = f.imageAvailable,
                                  .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphoreSubmitInfo sigSem{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                                 .semaphore = f.renderFinished,
                                 .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};
    VkSubmitInfo2 si{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = im.cfg.headless ? 0u : 1u,
        .pWaitSemaphoreInfos = &waitSem,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &cbsi,
        .signalSemaphoreInfoCount = im.cfg.headless ? 0u : 1u,
        .pSignalSemaphoreInfos = &sigSem,
    };
    VK_CHK(vkQueueSubmit2(im.queue, 1, &si, f.inFlight));

    if (!im.cfg.headless) {
        VkPresentInfoKHR pi{
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &f.renderFinished,
            .swapchainCount = 1,
            .pSwapchains = &im.swapchain,
            .pImageIndices = &im.swapImageIndex,
        };
        VkResult res = vkQueuePresentKHR(im.queue, &pi);
        if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR)
            im.swapchainDirty = true;
        else if (res != VK_SUCCESS)
            VK_CHK(res);
    } else {
        VK_CHK(vkWaitForFences(im.device, 1, &f.inFlight, VK_TRUE, UINT64_MAX));
    }

    im.inFrame = false;
    im.frameCounter++;
    im.frameIndex = (im.frameIndex + 1) % IRIS_FRAMES_IN_FLIGHT;
}

// =============================================================== readback ==
bool Engine::Impl::readImageRGBA(GpuImage& img, std::vector<uint8_t>& out) {
    vkQueueWaitIdle(queue);
    VkDeviceSize size = (VkDeviceSize)img.w * img.h * 4;
    GpuBuffer staging = createBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                     true);
    VkCommandBuffer cmd = beginOneTime();
    VkImageLayout prev = img.layout;
    if (prev != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
        barrier(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                VK_ACCESS_2_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_READ_BIT);
    VkBufferImageCopy region{
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .imageExtent = {img.w, img.h, 1},
    };
    vkCmdCopyImageToBuffer(cmd, img.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.buf, 1, &region);
    if (prev == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        barrier(cmd, img, prev, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    endOneTime(cmd);
    out.resize(size);
    std::memcpy(out.data(), staging.mapped, size);
    destroyBuffer(staging);
    return true;
}

bool Engine::captureSceneRGBA(std::vector<uint8_t>& out, int& w, int& h) {
    GpuImage& img = impl->textures[impl->sceneColorTexId];
    w = (int)img.w;
    h = (int)img.h;
    return impl->readImageRGBA(img, out);
}

} // namespace iris
