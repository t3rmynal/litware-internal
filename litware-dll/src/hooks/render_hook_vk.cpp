#include "render_hook.h"
#include "../platform/compat.h"
#include "../platform/linux/input.h"
#include "../platform/linux/hook.h"
#include "../platform/linux/proc_maps.h"
#include "../debug.h"
#include "../Fonts.h"
#include <vulkan/vulkan.h>
#include <SDL2/SDL.h>
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <imgui_impl_sdl2.h>
#include <dlfcn.h>
#include <cstring>
#include <vector>
#include <atomic>

namespace {

// === Vulkan Hooks ===
using PFN_vkQueuePresentKHR = VkResult(VKAPI_PTR*)(VkQueue, const VkPresentInfoKHR*);
using PFN_vkCreateDevice = VkResult(VKAPI_PTR*)(VkPhysicalDevice, const VkDeviceCreateInfo*, const VkAllocationCallbacks*, VkDevice*);
using PFN_vkGetDeviceQueue = void(VKAPI_PTR*)(VkDevice, uint32_t, uint32_t, VkQueue*);
using PFN_vkGetSwapchainImagesKHR = VkResult(VKAPI_PTR*)(VkDevice, VkSwapchainKHR, uint32_t*, VkImage*);
using PFN_vkCreateRenderPass = VkResult(VKAPI_PTR*)(VkDevice, const VkRenderPassCreateInfo*, const VkAllocationCallbacks*, VkRenderPass*);
using PFN_vkCreateFramebuffer = VkResult(VKAPI_PTR*)(VkDevice, const VkFramebufferCreateInfo*, const VkAllocationCallbacks*, VkFramebuffer*);
using PFN_vkCreateCommandPool = VkResult(VKAPI_PTR*)(VkDevice, const VkCommandPoolCreateInfo*, const VkAllocationCallbacks*, VkCommandPool*);
using PFN_vkAllocateCommandBuffers = VkResult(VKAPI_PTR*)(VkDevice, const VkCommandBufferAllocateInfo*, VkCommandBuffer*);
using PFN_vkCreateDescriptorPool = VkResult(VKAPI_PTR*)(VkDevice, const VkDescriptorPoolCreateInfo*, const VkAllocationCallbacks*, VkDescriptorPool*);
using PFN_vkBeginCommandBuffer = VkResult(VKAPI_PTR*)(VkCommandBuffer, const VkCommandBufferBeginInfo*);
using PFN_vkCmdBeginRenderPass = void(VKAPI_PTR*)(VkCommandBuffer, const VkRenderPassBeginInfo*, VkSubpassContents);
using PFN_vkCmdEndRenderPass = void(VKAPI_PTR*)(VkCommandBuffer);
using PFN_vkEndCommandBuffer = VkResult(VKAPI_PTR*)(VkCommandBuffer);
using PFN_vkQueueSubmit = VkResult(VKAPI_PTR*)(VkQueue, uint32_t, const VkSubmitInfo*, VkFence);
using PFN_vkResetCommandBuffer = VkResult(VKAPI_PTR*)(VkCommandBuffer, VkCommandBufferResetFlags);
using PFN_vkCreateFence = VkResult(VKAPI_PTR*)(VkDevice, const VkFenceCreateInfo*, const VkAllocationCallbacks*, VkFence*);
using PFN_vkWaitForFences = VkResult(VKAPI_PTR*)(VkDevice, uint32_t, const VkFence*, VkBool32, uint64_t);
using PFN_vkResetFences = VkResult(VKAPI_PTR*)(VkDevice, uint32_t, const VkFence*);
using PFN_vkDeviceWaitIdle = VkResult(VKAPI_PTR*)(VkDevice);

// Function pointers
static PFN_vkQueuePresentKHR g_vkQueuePresentKHR = nullptr;
static PFN_vkCreateDevice g_vkCreateDevice = nullptr;
static PFN_vkGetDeviceQueue g_vkGetDeviceQueue = nullptr;
static PFN_vkGetSwapchainImagesKHR g_vkGetSwapchainImagesKHR = nullptr;
static PFN_vkCreateRenderPass g_vkCreateRenderPass = nullptr;
static PFN_vkCreateFramebuffer g_vkCreateFramebuffer = nullptr;
static PFN_vkCreateCommandPool g_vkCreateCommandPool = nullptr;
static PFN_vkAllocateCommandBuffers g_vkAllocateCommandBuffers = nullptr;
static PFN_vkCreateDescriptorPool g_vkCreateDescriptorPool = nullptr;
static PFN_vkBeginCommandBuffer g_vkBeginCommandBuffer = nullptr;
static PFN_vkCmdBeginRenderPass g_vkCmdBeginRenderPass = nullptr;
static PFN_vkCmdEndRenderPass g_vkCmdEndRenderPass = nullptr;
static PFN_vkEndCommandBuffer g_vkEndCommandBuffer = nullptr;
static PFN_vkQueueSubmit g_vkQueueSubmit = nullptr;
static PFN_vkResetCommandBuffer g_vkResetCommandBuffer = nullptr;
static PFN_vkCreateFence g_vkCreateFence = nullptr;
static PFN_vkWaitForFences g_vkWaitForFences = nullptr;
static PFN_vkResetFences g_vkResetFences = nullptr;
static PFN_vkDeviceWaitIdle g_vkDeviceWaitIdle = nullptr;

// Original function pointers
static PFN_vkQueuePresentKHR g_originalQueuePresent = nullptr;
static PFN_vkCreateDevice g_originalCreateDevice = nullptr;

// Vulkan state
static VkDevice g_vkDevice = VK_NULL_HANDLE;
static VkPhysicalDevice g_vkPhysDevice = VK_NULL_HANDLE;
static VkQueue g_vkQueue = VK_NULL_HANDLE;
static uint32_t g_vkQueueFamily = 0;
static VkSwapchainKHR g_vkSwapchain = VK_NULL_HANDLE;
static VkRenderPass g_renderPass = VK_NULL_HANDLE;
static VkDescriptorPool g_descriptorPool = VK_NULL_HANDLE;
static VkCommandPool g_cmdPool = VK_NULL_HANDLE;
static std::vector<VkImage> g_swapchainImages;
static std::vector<VkImageView> g_swapchainImageViews;
static std::vector<VkFramebuffer> g_framebuffers;
static std::vector<VkCommandBuffer> g_cmdBuffers;
static std::vector<VkFence> g_frameFences;

// ImGui state
static bool g_imguiInitialized = false;
static SDL_Window* g_sdlWindow = nullptr;
static bool g_menuOpen = false;
static bool g_showDebugConsole = false;

// Unload state
static std::atomic_bool g_unloading{false};
static std::atomic_bool g_pendingUnload{false};

// Forward declarations
void RenderFrameVk(uint32_t imageIndex);
void InitImGuiVk(VkQueue queue, VkSwapchainKHR swapchain);
void CleanupImGuiVk();

// === SDL2 and Input Integration ===
#ifdef SDL_ENABLE_SYSWM_WAYLAND
#define SDL_WAYLAND_HAVE_LIBDECOR 1
#endif

// Hook for SDL_CreateWindow
static void* (*g_origSDLCreateWindow)(const char*, int, int, int, int, uint32_t) = nullptr;

static void* HookSDLCreateWindow(const char* title, int x, int y, int w, int h, uint32_t flags) {
    SDL_Window* wnd = (SDL_Window*)g_origSDLCreateWindow(title, x, y, w, h, flags);
    if (wnd && !g_sdlWindow) {
        g_sdlWindow = wnd;
        BootstrapLog("[litware] SDL_CreateWindow hooked: %p", wnd);
    }
    return wnd;
}

// Hook for SDL_PollEvent
static int (*g_origSDLPollEvent)(SDL_Event*) = nullptr;

static int HookSDLPollEvent(SDL_Event* event) {
    int ret = g_origSDLPollEvent(event);
    if (ret && event) {
        linput::OnSdlEvent(*event);
        // Suppress menu events from reaching game
        if (g_menuOpen) {
            if (event->type == SDL_MOUSEMOTION || event->type == SDL_MOUSEBUTTONDOWN || event->type == SDL_MOUSEBUTTONUP) {
                ImGui_ImplSDL2_ProcessEvent(event);
                return 0;  // Consume
            }
        }
    }
    return ret;
}

// === Vulkan Hooks ===
static VkResult VKAPI_PTR HookCreateDevice(
    VkPhysicalDevice physicalDevice,
    const VkDeviceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDevice* pDevice) {

    VkResult result = g_originalCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);
    if (result == VK_SUCCESS && pDevice && *pDevice != VK_NULL_HANDLE) {
        g_vkPhysDevice = physicalDevice;
        g_vkDevice = *pDevice;

        // Find graphics queue family from create info
        for (uint32_t i = 0; i < pCreateInfo->queueCreateInfoCount; ++i) {
            g_vkQueueFamily = pCreateInfo->pQueueCreateInfos[i].queueFamilyIndex;
            // Get queue
            if (g_vkGetDeviceQueue) {
                g_vkGetDeviceQueue(g_vkDevice, g_vkQueueFamily, 0, &g_vkQueue);
            }
            break;
        }

        BootstrapLog("[litware] vkCreateDevice hooked: device=%p, physdev=%p, queue=%p", g_vkDevice, g_vkPhysDevice, g_vkQueue);
    }
    return result;
}

static VkResult VKAPI_PTR HookQueuePresent(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) {
    if (!g_unloading && pPresentInfo && pPresentInfo->swapchainCount > 0) {
        g_vkSwapchain = pPresentInfo->pSwapchains[0];

        if (!g_imguiInitialized) {
            InitImGuiVk(queue, g_vkSwapchain);
        }

        if (g_imguiInitialized && pPresentInfo->pImageIndices) {
            uint32_t imageIndex = pPresentInfo->pImageIndices[0];
            try {
                RenderFrameVk(imageIndex);
            } catch (...) {
                BootstrapLog("[litware] Exception in RenderFrameVk");
            }
        }
    }

    if (g_pendingUnload) {
        // Unload sequence triggered
        CleanupImGuiVk();
        return g_originalQueuePresent(queue, pPresentInfo);
    }

    return g_originalQueuePresent(queue, pPresentInfo);
}

// === Init/Shutdown ===
static void LoadVulkanFunctions() {
    void* vulkan = dlopen("libvulkan.so.1", RTLD_NOLOAD | RTLD_NOW);
    if (!vulkan) {
        BootstrapLog("[litware] Could not load libvulkan.so.1");
        return;
    }

#define LOAD_VK_FUNC(name) g_##name = (PFN_##name)dlsym(vulkan, #name); \
    if (!g_##name) BootstrapLog("[litware] Failed to load " #name);

    LOAD_VK_FUNC(vkQueuePresentKHR);
    LOAD_VK_FUNC(vkCreateDevice);
    LOAD_VK_FUNC(vkGetDeviceQueue);
    LOAD_VK_FUNC(vkGetSwapchainImagesKHR);
    LOAD_VK_FUNC(vkCreateRenderPass);
    LOAD_VK_FUNC(vkCreateFramebuffer);
    LOAD_VK_FUNC(vkCreateCommandPool);
    LOAD_VK_FUNC(vkAllocateCommandBuffers);
    LOAD_VK_FUNC(vkCreateDescriptorPool);
    LOAD_VK_FUNC(vkBeginCommandBuffer);
    LOAD_VK_FUNC(vkCmdBeginRenderPass);
    LOAD_VK_FUNC(vkCmdEndRenderPass);
    LOAD_VK_FUNC(vkEndCommandBuffer);
    LOAD_VK_FUNC(vkQueueSubmit);
    LOAD_VK_FUNC(vkResetCommandBuffer);
    LOAD_VK_FUNC(vkCreateFence);
    LOAD_VK_FUNC(vkWaitForFences);
    LOAD_VK_FUNC(vkResetFences);
    LOAD_VK_FUNC(vkDeviceWaitIdle);

#undef LOAD_VK_FUNC

    BootstrapLog("[litware] Vulkan functions loaded");
}

static void LoadSDL2Functions() {
    void* sdl = dlopen("libSDL2-2.0.so.0", RTLD_NOLOAD | RTLD_NOW);
    if (!sdl) {
        BootstrapLog("[litware] Could not load libSDL2-2.0.so.0");
        return;
    }

    void* create_window_addr = dlsym(sdl, "SDL_CreateWindow");
    void* poll_event_addr = dlsym(sdl, "SDL_PollEvent");

    if (create_window_addr && poll_event_addr) {
        lhook::create(create_window_addr, (void*)&HookSDLCreateWindow, (void**)&g_origSDLCreateWindow);
        lhook::create(poll_event_addr, (void*)&HookSDLPollEvent, (void**)&g_origSDLPollEvent);
        lhook::enable(create_window_addr);
        lhook::enable(poll_event_addr);
        BootstrapLog("[litware] SDL2 hooks installed");
    }
}

} // anonymous namespace

// === Public Render Hook Interface ===
namespace render_hook {

bool Initialize() {
    BootstrapLog("[litware] render_hook::Initialize (Vulkan)");

    LoadVulkanFunctions();
    LoadSDL2Functions();

    if (!g_vkQueuePresentKHR) {
        BootstrapLog("[litware] vkQueuePresentKHR not loaded");
        return false;
    }

    // Install Vulkan hook
    lhook::Status st = lhook::create(
        (void*)g_vkQueuePresentKHR,
        (void*)&HookQueuePresent,
        (void**)&g_originalQueuePresent
    );

    if (st != lhook::OK) {
        BootstrapLog("[litware] Failed to hook vkQueuePresentKHR: %d", (int)st);
        return false;
    }

    lhook::enable((void*)g_vkQueuePresentKHR);

    if (g_vkCreateDevice) {
        lhook::create(
            (void*)g_vkCreateDevice,
            (void*)&HookCreateDevice,
            (void**)&g_originalCreateDevice
        );
        lhook::enable((void*)g_vkCreateDevice);
    }

    BootstrapLog("[litware] Vulkan hooks installed successfully");
    return true;
}

void Shutdown() {
    BootstrapLog("[litware] render_hook::Shutdown");
    g_pendingUnload = true;
    CleanupImGuiVk();
    lhook::disable_all();
}

} // namespace render_hook

// === ImGui/Vulkan Initialization ===
static void InitImGuiVk(VkQueue queue, VkSwapchainKHR swapchain) {
    if (g_imguiInitialized) return;

    BootstrapLog("[litware] InitImGuiVk starting");

    if (!g_vkDevice || !g_vkGetSwapchainImagesKHR) {
        BootstrapLog("[litware] Vulkan not ready");
        return;
    }

    // Get swapchain images
    uint32_t imageCount = 0;
    g_vkGetSwapchainImagesKHR(g_vkDevice, swapchain, &imageCount, nullptr);
    if (imageCount == 0) {
        BootstrapLog("[litware] No swapchain images");
        return;
    }

    g_swapchainImages.resize(imageCount);
    if (g_vkGetSwapchainImagesKHR(g_vkDevice, swapchain, &imageCount, g_swapchainImages.data()) != VK_SUCCESS) {
        BootstrapLog("[litware] Failed to get swapchain images");
        return;
    }

    BootstrapLog("[litware] Got %u swapchain images", imageCount);

    // Create image views and framebuffers
    g_swapchainImageViews.resize(imageCount);
    g_framebuffers.resize(imageCount);

    // Create render pass
    VkAttachmentDescription attachment{};
    attachment.format = VK_FORMAT_B8G8R8A8_UNORM; // Common format for swapchain
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;  // Don't clear
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments = &attachment;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;

    if (g_vkCreateRenderPass(g_vkDevice, &rpInfo, nullptr, &g_renderPass) != VK_SUCCESS) {
        BootstrapLog("[litware] Failed to create render pass");
        return;
    }

    // Create descriptor pool for ImGui
    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}
    };
    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000;
    pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;

    if (g_vkCreateDescriptorPool(g_vkDevice, &pool_info, nullptr, &g_descriptorPool) != VK_SUCCESS) {
        BootstrapLog("[litware] Failed to create descriptor pool");
        return;
    }

    // Create command pool
    VkCommandPoolCreateInfo cmd_pool_info{};
    cmd_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmd_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cmd_pool_info.queueFamilyIndex = g_vkQueueFamily;

    if (g_vkCreateCommandPool(g_vkDevice, &cmd_pool_info, nullptr, &g_cmdPool) != VK_SUCCESS) {
        BootstrapLog("[litware] Failed to create command pool");
        return;
    }

    // Allocate command buffers and fences
    g_cmdBuffers.resize(imageCount);
    g_frameFences.resize(imageCount);

    VkCommandBufferAllocateInfo cmd_alloc{};
    cmd_alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_alloc.commandPool = g_cmdPool;
    cmd_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_alloc.commandBufferCount = imageCount;

    if (g_vkAllocateCommandBuffers(g_vkDevice, &cmd_alloc, g_cmdBuffers.data()) != VK_SUCCESS) {
        BootstrapLog("[litware] Failed to allocate command buffers");
        return;
    }

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (size_t i = 0; i < g_frameFences.size(); ++i) {
        if (g_vkCreateFence(g_vkDevice, &fence_info, nullptr, &g_frameFences[i]) != VK_SUCCESS) {
            BootstrapLog("[litware] Failed to create fence %zu", i);
            return;
        }
    }

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    if (!g_sdlWindow) {
        BootstrapLog("[litware] SDL window not captured");
        return;
    }

    ImGui_ImplSDL2_InitForVulkan(g_sdlWindow);

    ImGui_ImplVulkan_InitInfo init_info{};
    init_info.Instance = VK_NULL_HANDLE;  // Can be null
    init_info.PhysicalDevice = g_vkPhysDevice;
    init_info.Device = g_vkDevice;
    init_info.QueueFamily = g_vkQueueFamily;
    init_info.Queue = g_vkQueue;
    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.DescriptorPool = g_descriptorPool;
    init_info.Subpass = 0;
    init_info.MinImageCount = imageCount;
    init_info.ImageCount = imageCount;
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.Allocator = nullptr;
    init_info.CheckVkResultFn = nullptr;

    if (!ImGui_ImplVulkan_Init(&init_info, g_renderPass)) {
        BootstrapLog("[litware] ImGui_ImplVulkan_Init failed");
        return;
    }

    g_imguiInitialized = true;
    BootstrapLog("[litware] ImGui initialized successfully");
}

static void CleanupImGuiVk() {
    if (!g_imguiInitialized) return;

    if (g_vkDevice && g_vkDeviceWaitIdle) {
        g_vkDeviceWaitIdle(g_vkDevice);
    }

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    g_imguiInitialized = false;
    BootstrapLog("[litware] ImGui cleaned up");
}

static void RenderFrameVk(uint32_t imageIndex) {
    if (imageIndex >= g_cmdBuffers.size()) return;

    // Wait for fence
    if (g_vkWaitForFences && g_frameFences[imageIndex]) {
        g_vkWaitForFences(g_vkDevice, 1, &g_frameFences[imageIndex], VK_TRUE, UINT64_MAX);
        g_vkResetFences(g_vkDevice, 1, &g_frameFences[imageIndex]);
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    // Toggle menu on INSERT
    if ((GetAsyncKeyState(VK_INSERT) & 0x8000) && !g_menuOpen) {
        g_menuOpen = !g_menuOpen;
    }

    // Trigger unload on END
    if (GetAsyncKeyState(VK_END) & 0x8000) {
        g_pendingUnload = true;
    }

    // Simple menu placeholder
    if (g_menuOpen) {
        ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("LitWare Menu", &g_menuOpen)) {
            ImGui::Text("Vulkan ImGui Overlay - Linux");
            ImGui::Separator();
            ImGui::Checkbox("Debug Console", &g_showDebugConsole);
            if (ImGui::Button("Unload", ImVec2(120, 0))) {
                g_pendingUnload = true;
            }
            ImGui::End();
        }
    }

    ImGui::Render();

    // Record command buffer
    if (g_vkBeginCommandBuffer && g_vkResetCommandBuffer) {
        g_vkResetCommandBuffer(g_cmdBuffers[imageIndex], 0);

        VkCommandBufferBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        g_vkBeginCommandBuffer(g_cmdBuffers[imageIndex], &begin_info);

        VkClearValue clear_value = {{0.0f, 0.0f, 0.0f, 1.0f}};
        VkRenderPassBeginInfo rp_info{};
        rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp_info.renderPass = g_renderPass;
        rp_info.framebuffer = g_framebuffers[imageIndex];
        rp_info.renderArea.extent = {800, 600};  // Placeholder
        rp_info.clearValueCount = 1;
        rp_info.pClearValues = &clear_value;

        g_vkCmdBeginRenderPass(g_cmdBuffers[imageIndex], &rp_info, VK_SUBPASS_CONTENTS_INLINE);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), g_cmdBuffers[imageIndex]);
        g_vkCmdEndRenderPass(g_cmdBuffers[imageIndex]);

        g_vkEndCommandBuffer(g_cmdBuffers[imageIndex]);

        // Submit
        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &g_cmdBuffers[imageIndex];

        if (g_vkQueueSubmit) {
            g_vkQueueSubmit(g_vkQueue, 1, &submit_info, g_frameFences[imageIndex]);
        }
    }
}
