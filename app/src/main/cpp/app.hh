#pragma once
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>
#include <android/native_window.h>
#include <android/asset_manager.h>
#include <android/log.h>
#include <vector>
#include "renderer.hh"
#include "font.hh"
#include "ui.hh"

class App {
 private:
  VkInstance       instance        = VK_NULL_HANDLE;
  VkSurfaceKHR     surface         = VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice  = VK_NULL_HANDLE;
  uint32_t         graphicsFamily  = UINT32_MAX;
  uint32_t         presentFamily   = UINT32_MAX;
  VkDevice         logicalDevice   = VK_NULL_HANDLE;
  VkSwapchainKHR   swapchain       = VK_NULL_HANDLE;
  VkFormat         swapchainFormat;
  VkExtent2D       swapchainExtent;
  std::vector<VkImage>       swapchainImages;
  std::vector<VkImageView>   swapchainImageViews;
  VkRenderPass     renderPass      = VK_NULL_HANDLE;
  std::vector<VkFramebuffer> framebuffers;
  VkPipelineLayout pipelineLayout  = VK_NULL_HANDLE;
  VkPipeline       graphicsPipeline= VK_NULL_HANDLE;
  VkCommandPool    commandPool     = VK_NULL_HANDLE;
  VkCommandBuffer  commandBuffer   = VK_NULL_HANDLE;
  VkSemaphore      imageAvailableSemaphore = VK_NULL_HANDLE;
  VkSemaphore      renderFinishedSemaphore = VK_NULL_HANDLE;
  VkFence          inFlightFence   = VK_NULL_HANDLE;
  VkQueue          graphicsQueue   = VK_NULL_HANDLE;
  VkQueue          presentQueue    = VK_NULL_HANDLE;

  VkDescriptorSetLayout compositeSetLayout = VK_NULL_HANDLE;
  VkDescriptorPool      compositePool      = VK_NULL_HANDLE;
  VkDescriptorSet       compositeSet       = VK_NULL_HANDLE;

  Renderer renderer;
  Font     font;
  std::vector<float> scratchCurves;

  uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags required);

 public:
  Ui ui;

  void init(ANativeWindow* window, AAssetManager* mgr);
  void cleanup();
  void drawFrame();
  void onTouch(float px, float py);
  void setInsets(uint32_t top, uint32_t bottom, uint32_t left, uint32_t right);

  bool initialized = false;
  bool dirty       = false;
};
